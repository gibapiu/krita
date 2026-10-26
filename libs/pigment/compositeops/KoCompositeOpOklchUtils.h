/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KOCOMPOSITEOPOKLCHUTILS_H
#define KOCOMPOSITEOPOKLCHUTILS_H

#include <QByteArray>
#include <QScopedPointer>
#include <QThreadStorage>
#include <QVector>
#include <QVector4D>

#include <cstring>
#include <mutex>
#include <utility>

#include <KoColorConversionTransformation.h>
#include <KoColorModelStandardIds.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorSpaceTraits.h>
#include <KoGamutMappingUtils.h>
#include <KoXyzColorSpaceTraits.h>

namespace KoOklchCompositeUtils
{
inline KoGamutMappingUtils::OkLch okLabToOkLchIgnoringProfileNoise(const QVector4D &okLab)
{
    KoGamutMappingUtils::OkLch okLch = KoGamutMappingUtils::OkLabToOkLch(okLab);
    constexpr float maximumProfileNoiseChroma = 0.0001f;
    if (okLch.chroma <= maximumProfileNoiseChroma) {
        okLch.chroma = 0.0f;
        okLch.hue = 0.0f;
        okLch.hueIsPowerless = true;
    }
    return okLch;
}
}

template<class Traits, class PerPixelValue>
class KoOklchConversionContextStore
{
public:
    static constexpr qint32 MaxBatchPixels = 4096;

    struct ActiveSpan {
        qint32 row;
        qint32 column;
        qint32 length;
        qint32 packedOffset;
    };

    struct ConversionContext {
        ConversionContext(const KoColorSpace *colorSpace, const KoColorSpace *xyzColorSpace)
        {
            const auto intent = KoColorConversionTransformation::IntentRelativeColorimetric;
            const auto flags = KoColorConversionTransformation::adjustmentConversionFlags();
            toXyz.reset(colorSpace->createColorConverter(xyzColorSpace, intent, flags));
            fromXyz.reset(xyzColorSpace->createColorConverter(colorSpace, intent, flags));
        }

        bool isValid() const
        {
            return toXyz && fromXyz;
        }

        void beginBatch(qint32 pixelCapacity, bool needsSourceBuffer)
        {
            if (needsSourceBuffer && srcXyz.size() < pixelCapacity) {
                srcXyz.resize(pixelCapacity);
            }
            if (dstXyz.size() < pixelCapacity) {
                dstXyz.resize(pixelCapacity);
            }
            if (perPixelValues.size() < pixelCapacity) {
                perPixelValues.resize(pixelCapacity);
            }

            const int nativeBytes = pixelCapacity * int(Traits::pixelSize);
            if (packedPixels.size() < nativeBytes) {
                packedPixels.resize(nativeBytes);
            }
            if (convertedPixels.size() < nativeBytes) {
                convertedPixels.resize(nativeBytes);
            }
            if (activeSpans.capacity() < pixelCapacity) {
                activeSpans.reserve(pixelCapacity);
            }

            activeSpans.clear();
            activePixelCount = 0;
        }

        void addActivePixel(qint32 row, qint32 column, PerPixelValue value)
        {
            if (!activeSpans.isEmpty()) {
                ActiveSpan &lastSpan = activeSpans.last();
                if (lastSpan.row == row && lastSpan.column + lastSpan.length == column) {
                    ++lastSpan.length;
                } else {
                    activeSpans.append({row, column, 1, activePixelCount});
                }
            } else {
                activeSpans.append({row, column, 1, activePixelCount});
            }

            perPixelValues[activePixelCount] = value;
            ++activePixelCount;
        }

        void packPixels(const quint8 *rowStart, qint32 rowStride)
        {
            for (const ActiveSpan &span : std::as_const(activeSpans)) {
                const quint8 *source = rowStart
                    + span.row * rowStride
                    + span.column * Traits::pixelSize;
                quint8 *destination = reinterpret_cast<quint8 *>(packedPixels.data())
                    + span.packedOffset * Traits::pixelSize;
                std::memcpy(destination, source, span.length * Traits::pixelSize);
            }
        }

        QScopedPointer<KoColorConversionTransformation> toXyz;
        QScopedPointer<KoColorConversionTransformation> fromXyz;
        QVector<KoXyzF32Traits::Pixel> srcXyz;
        QVector<KoXyzF32Traits::Pixel> dstXyz;
        QVector<PerPixelValue> perPixelValues;
        QByteArray packedPixels;
        QByteArray convertedPixels;
        QVector<ActiveSpan> activeSpans;
        qint32 activePixelCount {0};
    };

    explicit KoOklchConversionContextStore(const KoColorSpace *colorSpace)
        : m_colorSpace(colorSpace)
    {
    }

    ConversionContext *contextForCurrentThread() const
    {
        std::call_once(m_xyzColorSpaceOnce, [this]() {
            m_xyzColorSpace = KoColorSpaceRegistry::instance()->colorSpace(
                XYZAColorModelID.id(), Float32BitsColorDepthID.id(), QString());
        });

        if (!m_xyzColorSpace) {
            return nullptr;
        }

        if (!m_threadContexts.hasLocalData()) {
            m_threadContexts.setLocalData(new ConversionContext(m_colorSpace, m_xyzColorSpace));
        }
        return m_threadContexts.localData();
    }

private:
    const KoColorSpace *m_colorSpace;
    mutable std::once_flag m_xyzColorSpaceOnce;
    mutable const KoColorSpace *m_xyzColorSpace {nullptr};
    mutable QThreadStorage<ConversionContext *> m_threadContexts;
};

#endif
