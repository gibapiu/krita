/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KOCOMPOSITEOPCOLOROKLCH_H
#define KOCOMPOSITEOPCOLOROKLCH_H

#include <QByteArray>
#include <QVector>

#include <KoColorSpace.h>
#include <KoColorSpaceMaths.h>
#include <KoCompositeOp.h>
#include <KoCompositeOpRegistry.h>
#include <KoGamutMappingUtils.h>
#include <KoXyzColorSpaceTraits.h>

#include "KoCompositeOpOklchUtils.h"
#include "KoCompositeOpOver.h"

/**
 * A non-separable color composite operation using OKLCH components.
 *
 * In the overlapping region, destination lightness is combined with source
 * chroma and hue. The target color is then composited with the same
 * Porter-Duff source-over equation as Krita's other Color modes.
 */
template<class Traits>
class KoCompositeOpColorOklch : public KoCompositeOp
{
    using channels_type = typename Traits::channels_type;

public:
    explicit KoCompositeOpColorOklch(const KoColorSpace *cs)
        : KoCompositeOp(cs, COMPOSITE_COLOR_OKLCH, KoCompositeOp::categoryOKLCH())
        , m_conversionContexts(cs)
    {
    }

    using KoCompositeOp::composite;

    void composite(const KoCompositeOp::ParameterInfo &params) const override
    {
        if (params.rows <= 0 || params.cols <= 0) {
            return;
        }

        const bool allChannelFlags = params.channelFlags.isEmpty();
        const bool alphaLocked = !allChannelFlags && !params.channelFlags.testBit(Traits::alpha_pos);

        bool hasSelectedColorChannel = allChannelFlags;
        if (!hasSelectedColorChannel) {
            for (int channel = 0; channel < Traits::channels_nb; ++channel) {
                if (channel != Traits::alpha_pos && params.channelFlags.testBit(channel)) {
                    hasSelectedColorChannel = true;
                    break;
                }
            }
        }

        if (!hasSelectedColorChannel) {
            KoCompositeOpOver<Traits> fallback(colorSpace());
            fallback.composite(params);
            return;
        }

        const channels_type opacity =
            KoColorSpaceMaths<float, channels_type>::scaleToA(params.opacity);
        const channels_type zero = KoColorSpaceMathsTraits<channels_type>::zeroValue;
        const bool constantSource = params.srcRowStride == 0;

        if (opacity == zero
            || (constantSource
                && reinterpret_cast<const channels_type *>(params.srcRowStart)[Traits::alpha_pos] == zero)) {
            return;
        }

        if (!hasOverlappingPixels(params, opacity)) {
            compositeFallback(params);
            return;
        }

        typename ConversionContextStore::ConversionContext *conversionContext =
            m_conversionContexts.contextForCurrentThread();
        if (!conversionContext || !conversionContext->isValid()) {
            compositeFallback(params);
            return;
        }

        const qint32 pixelCount = params.cols;
        conversionContext->ensureCapacity(pixelCount, !constantSource);
        QVector<KoXyzF32Traits::Pixel> &srcXyz = conversionContext->srcXyz;
        QVector<KoXyzF32Traits::Pixel> &dstXyz = conversionContext->dstXyz;
        QVector<channels_type> &sourceAlphas = conversionContext->perPixelValues;
        QByteArray &blendColorRow = conversionContext->convertedPixels;

        QVector4D constantSourceOkLab;
        bool constantSourceConverted = false;
        const qint32 srcPixelIncrement = constantSource ? 0 : Traits::channels_nb;

        const quint8 *srcRow = params.srcRowStart;
        quint8 *dstRow = params.dstRowStart;
        const quint8 *maskRow = params.maskRowStart;

        for (int row = 0; row < params.rows; ++row) {
            const channels_type *src = reinterpret_cast<const channels_type *>(srcRow);
            const channels_type *srcPixel = src;
            channels_type *dst = reinterpret_cast<channels_type *>(dstRow);
            bool needsBlendColor = false;

            for (qint32 column = 0; column < pixelCount; ++column) {
                const channels_type *dstPixel = dst + column * Traits::channels_nb;
                const channels_type srcAlpha = effectiveSourceAlpha(srcPixel, maskRow, column, opacity);

                sourceAlphas[column] = srcAlpha;
                needsBlendColor |= srcAlpha != zero && dstPixel[Traits::alpha_pos] != zero;
                srcPixel += srcPixelIncrement;
            }

            if (needsBlendColor) {
                if (constantSource) {
                    if (!constantSourceConverted) {
                        KoXyzF32Traits::Pixel constantSourceXyz;
                        conversionContext->toXyz->transform(params.srcRowStart,
                                                            reinterpret_cast<quint8 *>(&constantSourceXyz),
                                                            1);
                        constantSourceOkLab = normalizedSourceOkLab(
                            KoGamutMappingUtils::XYZToOkLab(toVector(constantSourceXyz)));
                        constantSourceConverted = true;
                    }
                } else {
                    conversionContext->toXyz->transform(
                        srcRow, reinterpret_cast<quint8 *>(srcXyz.data()), pixelCount);
                }
                conversionContext->toXyz->transform(
                    dstRow, reinterpret_cast<quint8 *>(dstXyz.data()), pixelCount);

                for (qint32 column = 0; column < pixelCount; ++column) {
                    if (sourceAlphas[column] == zero
                        || dst[column * Traits::channels_nb + Traits::alpha_pos] == zero) {
                        continue;
                    }

                    const QVector4D destinationOkLab =
                        KoGamutMappingUtils::XYZToOkLab(toVector(dstXyz[column]));
                    QVector4D blendColor = constantSource
                        ? constantSourceOkLab
                        : normalizedSourceOkLab(
                            KoGamutMappingUtils::XYZToOkLab(toVector(srcXyz[column])));
                    // (L_d, C_s, h_s) is exactly (L_d, a_s, b_s), so retain
                    // the Cartesian chromatic components and avoid a polar round trip.
                    blendColor.setX(destinationOkLab.x());
                    dstXyz[column] = fromVector(KoGamutMappingUtils::OkLabToXYZ(
                        blendColor));
                }

                conversionContext->fromXyz->transform(reinterpret_cast<const quint8 *>(dstXyz.constData()),
                                                       reinterpret_cast<quint8 *>(blendColorRow.data()),
                                                       pixelCount);
            }

            const channels_type *blendColors =
                reinterpret_cast<const channels_type *>(blendColorRow.constData());
            srcPixel = src;
            for (qint32 column = 0; column < pixelCount; ++column) {
                channels_type *dstPixel = dst + column * Traits::channels_nb;
                const channels_type *blendPixel = blendColors + column * Traits::channels_nb;
                const channels_type srcAlpha = sourceAlphas[column];
                const channels_type dstAlpha = dstPixel[Traits::alpha_pos];

                if (!allChannelFlags && dstAlpha == zero) {
                    for (int channel = 0; channel < Traits::channels_nb; ++channel) {
                        if (channel != Traits::alpha_pos) {
                            dstPixel[channel] = zero;
                        }
                    }
                }

                if (srcAlpha != zero) {
                    if (alphaLocked) {
                        if (dstAlpha != zero) {
                            compositeAlphaLocked(srcAlpha, blendPixel, dstPixel, allChannelFlags, params.channelFlags);
                        }
                    } else if (dstAlpha == zero) {
                        copySourceColor(srcPixel, srcAlpha, dstPixel, allChannelFlags, params.channelFlags);
                    } else {
                        const channels_type newAlpha = Arithmetic::unionShapeOpacity(srcAlpha, dstAlpha);
                        compositeColor(srcPixel,
                                       srcAlpha,
                                       dstPixel,
                                       dstAlpha,
                                       newAlpha,
                                       blendPixel,
                                       allChannelFlags,
                                       params.channelFlags);
                        dstPixel[Traits::alpha_pos] = newAlpha;
                    }
                }

                srcPixel += srcPixelIncrement;
            }

            srcRow += params.srcRowStride;
            dstRow += params.dstRowStride;
            if (maskRow) {
                maskRow += params.maskRowStride;
            }
        }
    }

private:
    using ConversionContextStore = KoOklchConversionContextStore<Traits, channels_type>;

    static bool hasOverlappingPixels(const KoCompositeOp::ParameterInfo &params, channels_type opacity)
    {
        const bool constantSource = params.srcRowStride == 0;
        const qint32 srcPixelIncrement = constantSource ? 0 : Traits::channels_nb;
        const channels_type zero = KoColorSpaceMathsTraits<channels_type>::zeroValue;
        const quint8 *srcRow = params.srcRowStart;
        const quint8 *dstRow = params.dstRowStart;
        const quint8 *maskRow = params.maskRowStart;

        for (int row = 0; row < params.rows; ++row) {
            const channels_type *srcPixel = reinterpret_cast<const channels_type *>(srcRow);
            const channels_type *dstPixel = reinterpret_cast<const channels_type *>(dstRow);

            for (qint32 column = 0; column < params.cols; ++column) {
                if (effectiveSourceAlpha(srcPixel, maskRow, column, opacity) != zero
                    && dstPixel[Traits::alpha_pos] != zero) {
                    return true;
                }
                srcPixel += srcPixelIncrement;
                dstPixel += Traits::channels_nb;
            }

            srcRow += params.srcRowStride;
            dstRow += params.dstRowStride;
            if (maskRow) {
                maskRow += params.maskRowStride;
            }
        }
        return false;
    }

    static channels_type effectiveSourceAlpha(const channels_type *src,
                                              const quint8 *maskRow,
                                              qint32 column,
                                              channels_type opacity)
    {
        channels_type alpha = src[Traits::alpha_pos];
        if (maskRow) {
            alpha = KoColorSpaceMaths<quint8, channels_type>::multiply(maskRow[column], alpha, opacity);
        } else if (opacity != KoColorSpaceMathsTraits<channels_type>::unitValue) {
            alpha = KoColorSpaceMaths<channels_type>::multiply(alpha, opacity);
        }
        return alpha;
    }

    static QVector4D normalizedSourceOkLab(QVector4D source)
    {
        constexpr float powerlessHueEpsilonSquared = 0.000004f * 0.000004f;
        const float chromaSquared = source.y() * source.y() + source.z() * source.z();
        if (chromaSquared <= powerlessHueEpsilonSquared) {
            source.setY(0.0f);
            source.setZ(0.0f);
        }
        return source;
    }

    static void compositeAlphaLocked(channels_type srcAlpha,
                                     const channels_type *blend,
                                     channels_type *dst,
                                     bool allChannelFlags,
                                     const QBitArray &channelFlags)
    {
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos && (allChannelFlags || channelFlags.testBit(channel))) {
                dst[channel] = KoColorSpaceMaths<channels_type>::blend(blend[channel], dst[channel], srcAlpha);
            }
        }
    }

    static void compositeColor(const channels_type *src,
                               channels_type srcAlpha,
                               channels_type *dst,
                               channels_type dstAlpha,
                               channels_type newAlpha,
                               const channels_type *blend,
                               bool allChannelFlags,
                               const QBitArray &channelFlags)
    {
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos && (allChannelFlags || channelFlags.testBit(channel))) {
                dst[channel] = Arithmetic::div(
                    Arithmetic::blend(src[channel], srcAlpha, dst[channel], dstAlpha, blend[channel]),
                    newAlpha);
            }
        }
    }

    static void copySourceColor(const channels_type *src,
                                channels_type srcAlpha,
                                channels_type *dst,
                                bool allChannelFlags,
                                const QBitArray &channelFlags)
    {
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos && (allChannelFlags || channelFlags.testBit(channel))) {
                dst[channel] = src[channel];
            }
        }
        dst[Traits::alpha_pos] = srcAlpha;
    }

    static QVector4D toVector(const KoXyzF32Traits::Pixel &pixel)
    {
        return QVector4D(pixel.x, pixel.y, pixel.z, 0.0f);
    }

    static KoXyzF32Traits::Pixel fromVector(const QVector4D &value)
    {
        return {value.x(), value.y(), value.z(), 1.0f};
    }

    void compositeFallback(const KoCompositeOp::ParameterInfo &params) const
    {
        const KoCompositeOp *fallback = colorSpace()->compositeOp(COMPOSITE_COLOR);
        if (fallback) {
            fallback->composite(params);
        } else {
            KoCompositeOpOver<Traits> normalFallback(colorSpace());
            normalFallback.composite(params);
        }
    }

    mutable ConversionContextStore m_conversionContexts;
};

#endif
