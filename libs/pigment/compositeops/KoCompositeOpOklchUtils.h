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

#include <algorithm>
#include <cmath>
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
inline constexpr float maximumProfileNoiseChroma = 0.0001f;

inline KoGamutMappingUtils::OkLch okLabToOkLchIgnoringProfileNoise(const QVector4D &okLab)
{
    KoGamutMappingUtils::OkLch okLch = KoGamutMappingUtils::OkLabToOkLch(okLab);
    if (okLch.chroma <= maximumProfileNoiseChroma) {
        okLch.chroma = 0.0f;
        okLch.hue = 0.0f;
        okLch.hueIsPowerless = true;
    }
    return okLch;
}

struct AntipodalHybridEndpoint
{
    QVector4D okLab;
    QVector4D radialOkLab;
    KoGamutMappingUtils::OkLch radialOkLch;
};

inline constexpr float radialLightnessBoundary = 6.0f / 29.0f;
inline constexpr float radialLightnessOutputBoundary = 0.08f;
inline constexpr float radialLightnessCubicScale = 24389.0f / 2700.0f;
inline constexpr float continuousFallbackSigma = 0.19f;
inline constexpr float endpointChromaSigma = 0.02f;
inline constexpr float antipodalFadeStart = 160.0f * 3.14159265358979323846f / 180.0f;

// This is a radial coordinate transform: L, a and b are scaled together.
// It must not be applied as a lightness-only correction.
inline float applyRadialLightnessTransform(float lightness)
{
    return lightness <= radialLightnessBoundary
        ? radialLightnessCubicScale * lightness * lightness * lightness
        : 1.16f * lightness - 0.16f;
}

inline float removeRadialLightnessTransform(float lightness)
{
    return lightness <= radialLightnessOutputBoundary
        ? std::cbrt(lightness / radialLightnessCubicScale)
        : (lightness + 0.16f) / 1.16f;
}

inline AntipodalHybridEndpoint prepareAntipodalHybridEndpoint(const QVector4D &okLab)
{
    const KoGamutMappingUtils::OkLch okLch = okLabToOkLchIgnoringProfileNoise(okLab);
    const QVector4D cleanedOkLab(okLab.x(),
                                okLch.hueIsPowerless ? 0.0f : okLab.y(),
                                okLch.hueIsPowerless ? 0.0f : okLab.z(),
                                okLab.w());
    const float radialLightness = applyRadialLightnessTransform(okLab.x());
    const float scale = std::abs(okLab.x()) > 1.0e-12f
        ? radialLightness / okLab.x()
        : 0.0f;
    const QVector4D radialOkLab(radialLightness,
                               cleanedOkLab.y() * scale,
                               cleanedOkLab.z() * scale,
                               okLab.w());
    KoGamutMappingUtils::OkLch radialOkLch = okLch;
    radialOkLch.lightness = radialLightness;
    radialOkLch.chroma *= scale;
    radialOkLch.hueIsPowerless = radialOkLch.chroma <= maximumProfileNoiseChroma;
    return {cleanedOkLab, radialOkLab, radialOkLch};
}

inline float smootherStep(float amount)
{
    return amount * amount * amount * (amount * (amount * 6.0f - 15.0f) + 10.0f);
}

inline QVector4D interpolateAntipodalHybrid(const AntipodalHybridEndpoint &from,
                                            const AntipodalHybridEndpoint &to,
                                            float amount)
{
    if (amount <= 0.0f) {
        return from.okLab;
    }
    if (amount >= 1.0f) {
        return to.okLab;
    }

    constexpr float pi = 3.14159265358979323846f;
    constexpr float twoPi = 2.0f * pi;
    float fromHue = from.radialOkLch.hue;
    float toHue = to.radialOkLch.hue;
    if (from.radialOkLch.hueIsPowerless) {
        fromHue = to.radialOkLch.hueIsPowerless ? 0.0f : toHue;
    }
    if (to.radialOkLch.hueIsPowerless) {
        toHue = fromHue;
    }
    if (toHue - fromHue > pi) {
        fromHue += twoPi;
    } else if (toHue - fromHue < -pi) {
        toHue += twoPi;
    }

    const float radialLightness = from.radialOkLch.lightness
        + amount * (to.radialOkLch.lightness - from.radialOkLch.lightness);
    const float radialChroma = from.radialOkLch.chroma
        + amount * (to.radialOkLch.chroma - from.radialOkLch.chroma);
    const float hue = fromHue + amount * (toHue - fromHue);
    const float polarA = radialChroma * std::cos(hue);
    const float polarB = radialChroma * std::sin(hue);
    const float cartesianA = from.radialOkLab.y()
        + amount * (to.radialOkLab.y() - from.radialOkLab.y());
    const float cartesianB = from.radialOkLab.z()
        + amount * (to.radialOkLab.z() - from.radialOkLab.z());

    float polarWeight = radialChroma / (radialChroma + continuousFallbackSigma);
    const float minimumEndpointChroma = std::min(from.radialOkLch.chroma,
                                                 to.radialOkLch.chroma);
    polarWeight *= minimumEndpointChroma
        / (minimumEndpointChroma + endpointChromaSigma);

    if (!from.radialOkLch.hueIsPowerless && !to.radialOkLch.hueIsPowerless) {
        float separation = std::abs(to.radialOkLch.hue - from.radialOkLch.hue);
        if (separation > pi) {
            separation = twoPi - separation;
        }
        if (separation > antipodalFadeStart) {
            const float fadeAmount = (separation - antipodalFadeStart)
                / (pi - antipodalFadeStart);
            polarWeight *= 1.0f - smootherStep(fadeAmount);
        }
    }

    const float lightness = removeRadialLightnessTransform(radialLightness);
    const float scale = std::abs(radialLightness) > 1.0e-12f
        ? lightness / radialLightness
        : 0.0f;
    return QVector4D(lightness,
                     (cartesianA + polarWeight * (polarA - cartesianA)) * scale,
                     (cartesianB + polarWeight * (polarB - cartesianB)) * scale,
                     from.okLab.w() + amount * (to.okLab.w() - from.okLab.w()));
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
