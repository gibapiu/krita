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
#include <KoXyzColorSpaceTraits.h>

template<class Traits, class AuxiliaryType>
class KoOklchThreadDataStore
{
public:
    struct Data {
        Data(const KoColorSpace *colorSpace, const KoColorSpace *xyzColorSpace)
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
            if (auxiliary.size() < pixelCount) {
                auxiliary.resize(pixelCount);
            }

            const int outputBytes = pixelCount * int(Traits::pixelSize);
            if (output.size() < outputBytes) {
                output.resize(outputBytes);
            }
        }

        QScopedPointer<KoColorConversionTransformation> toXyz;
        QScopedPointer<KoColorConversionTransformation> fromXyz;
        QVector<KoXyzF32Traits::Pixel> srcXyz;
        QVector<KoXyzF32Traits::Pixel> dstXyz;
        QVector<AuxiliaryType> auxiliary;
        QByteArray output;
    };

    explicit KoOklchThreadDataStore(const KoColorSpace *colorSpace)
        : m_colorSpace(colorSpace)
    {
    }

    Data *localData() const
    {
        std::call_once(m_xyzColorSpaceOnce, [this]() {
            m_xyzColorSpace = KoColorSpaceRegistry::instance()->colorSpace(
                XYZAColorModelID.id(), Float32BitsColorDepthID.id(), QString());
        });

        if (!m_xyzColorSpace) {
            return nullptr;
        }

        if (!m_threadData.hasLocalData()) {
            m_threadData.setLocalData(new Data(m_colorSpace, m_xyzColorSpace));
        }
        return m_threadData.localData();
    }

private:
    const KoColorSpace *m_colorSpace;
    mutable std::once_flag m_xyzColorSpaceOnce;
    mutable const KoColorSpace *m_xyzColorSpace {nullptr};
    mutable QThreadStorage<Data *> m_threadData;
};

#endif
