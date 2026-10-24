/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KOCOMPOSITEOPOKLCHUTILS_H
#define KOCOMPOSITEOPOKLCHUTILS_H

#include <QByteArray>
#include <QScopedPointer>
#include <QThreadStorage>
#include <QVector>

#include <mutex>

#include <KoColorConversionTransformation.h>
#include <KoColorModelStandardIds.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorSpaceTraits.h>
#include <KoXyzColorSpaceTraits.h>

template<class Traits, class PerPixelValue>
class KoOklchConversionContextStore
{
public:
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

        void ensureCapacity(qint32 pixelCount, bool needsSourceBuffer)
        {
            if (needsSourceBuffer && srcXyz.size() < pixelCount) {
                srcXyz.resize(pixelCount);
            }
            if (dstXyz.size() < pixelCount) {
                dstXyz.resize(pixelCount);
            }
            if (perPixelValues.size() < pixelCount) {
                perPixelValues.resize(pixelCount);
            }

            const int outputBytes = pixelCount * int(Traits::pixelSize);
            if (convertedPixels.size() < outputBytes) {
                convertedPixels.resize(outputBytes);
            }
        }

        QScopedPointer<KoColorConversionTransformation> toXyz;
        QScopedPointer<KoColorConversionTransformation> fromXyz;
        QVector<KoXyzF32Traits::Pixel> srcXyz;
        QVector<KoXyzF32Traits::Pixel> dstXyz;
        QVector<PerPixelValue> perPixelValues;
        QByteArray convertedPixels;
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
