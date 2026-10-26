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
        const channels_type unit = KoColorSpaceMathsTraits<channels_type>::unitValue;
        const bool constantSource = params.srcRowStride == 0;

        if (opacity == zero
            || (constantSource
                && reinterpret_cast<const channels_type *>(params.srcRowStart)[Traits::alpha_pos] == zero)) {
            return;
        }

        if (!hasColorBlendingOverlap(params, opacity)) {
            KoCompositeOpOver<Traits> sourceOver(colorSpace());
            sourceOver.composite(params);
            return;
        }

        typename ConversionContextStore::ConversionContext *conversionContext =
            m_conversionContexts.contextForCurrentThread();
        if (!conversionContext || !conversionContext->isValid()) {
            compositeFallback(params);
            return;
        }

        QVector4D constantSourceOkLab;
        bool constantSourceConverted = false;
        const qint32 srcPixelIncrement = constantSource ? 0 : Traits::channels_nb;

        const auto compositeBatch = [&](qint32 firstRow,
                                        qint32 firstColumn,
                                        qint32 batchRows,
                                        qint32 batchColumns) {
            conversionContext->beginBatch(batchRows * batchColumns, !constantSource);

            for (qint32 row = firstRow; row < firstRow + batchRows; ++row) {
                const quint8 *srcRow = params.srcRowStart
                    + (constantSource ? 0 : row * params.srcRowStride);
                quint8 *dstRow = params.dstRowStart + row * params.dstRowStride;
                const quint8 *maskRow = params.maskRowStart
                    ? params.maskRowStart + row * params.maskRowStride
                    : nullptr;
                const channels_type *srcPixel = reinterpret_cast<const channels_type *>(srcRow)
                    + (constantSource ? 0 : firstColumn * Traits::channels_nb);
                channels_type *dstPixel = reinterpret_cast<channels_type *>(dstRow)
                    + firstColumn * Traits::channels_nb;

                for (qint32 column = firstColumn; column < firstColumn + batchColumns; ++column) {
                    const channels_type srcAlpha = effectiveSourceAlpha(srcPixel, maskRow, column, opacity);
                    const channels_type dstAlpha = dstPixel[Traits::alpha_pos];

                    if (!allChannelFlags && dstAlpha == zero) {
                        clearColorChannels(dstPixel);
                    }

                    if (srcAlpha != zero) {
                        if (alphaLocked) {
                            if (dstAlpha != zero) {
                                conversionContext->addActivePixel(row, column, srcAlpha);
                            }
                        } else if (dstAlpha == zero) {
                            copySourceColor(
                                srcPixel, srcAlpha, dstPixel, allChannelFlags, params.channelFlags);
                        } else {
                            conversionContext->addActivePixel(row, column, srcAlpha);
                        }
                    }

                    srcPixel += srcPixelIncrement;
                    dstPixel += Traits::channels_nb;
                }
            }

            const qint32 activePixelCount = conversionContext->activePixelCount;
            if (activePixelCount == 0) {
                return;
            }

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
                conversionContext->packPixels(params.srcRowStart, params.srcRowStride);
                conversionContext->toXyz->transform(
                    reinterpret_cast<const quint8 *>(conversionContext->packedPixels.constData()),
                    reinterpret_cast<quint8 *>(conversionContext->srcXyz.data()),
                    activePixelCount);
            }

            conversionContext->packPixels(params.dstRowStart, params.dstRowStride);
            conversionContext->toXyz->transform(
                reinterpret_cast<const quint8 *>(conversionContext->packedPixels.constData()),
                reinterpret_cast<quint8 *>(conversionContext->dstXyz.data()),
                activePixelCount);

            for (qint32 pixel = 0; pixel < activePixelCount; ++pixel) {
                const QVector4D destinationOkLab = KoGamutMappingUtils::XYZToOkLab(
                    toVector(conversionContext->dstXyz[pixel]));
                QVector4D blendColor = constantSource
                    ? constantSourceOkLab
                    : normalizedSourceOkLab(KoGamutMappingUtils::XYZToOkLab(
                        toVector(conversionContext->srcXyz[pixel])));
                // (L_d, C_s, h_s) is exactly (L_d, a_s, b_s), so retain
                // the Cartesian chromatic components and avoid a polar round trip.
                blendColor.setX(destinationOkLab.x());
                conversionContext->dstXyz[pixel] = fromVector(
                    KoGamutMappingUtils::OkLabToXYZ(blendColor));
            }

            conversionContext->fromXyz->transform(
                reinterpret_cast<const quint8 *>(conversionContext->dstXyz.constData()),
                reinterpret_cast<quint8 *>(conversionContext->convertedPixels.data()),
                activePixelCount);

            const channels_type *blendColors =
                reinterpret_cast<const channels_type *>(conversionContext->convertedPixels.constData());
            for (const typename ConversionContextStore::ActiveSpan &span :
                 std::as_const(conversionContext->activeSpans)) {
                const channels_type *srcPixel = reinterpret_cast<const channels_type *>(
                    params.srcRowStart
                    + (constantSource ? 0 : span.row * params.srcRowStride))
                    + (constantSource ? 0 : span.column * Traits::channels_nb);
                channels_type *dstPixel = reinterpret_cast<channels_type *>(
                    params.dstRowStart + span.row * params.dstRowStride)
                    + span.column * Traits::channels_nb;
                const channels_type *blendPixel = blendColors
                    + span.packedOffset * Traits::channels_nb;

                for (qint32 pixel = 0; pixel < span.length; ++pixel) {
                    const channels_type srcAlpha =
                        conversionContext->perPixelValues[span.packedOffset + pixel];
                    const channels_type dstAlpha = dstPixel[Traits::alpha_pos];

                    if (alphaLocked) {
                        compositeAlphaLocked(
                            srcAlpha, blendPixel, dstPixel, allChannelFlags, params.channelFlags);
                    } else {
                        const channels_type newAlpha = Arithmetic::unionShapeOpacity(srcAlpha, dstAlpha);
                        if (newAlpha == unit) {
                            compositeFullAlphaResult(srcPixel,
                                                     srcAlpha,
                                                     dstPixel,
                                                     dstAlpha,
                                                     blendPixel,
                                                     allChannelFlags,
                                                     params.channelFlags);
                        } else {
                            compositeColor(srcPixel,
                                           srcAlpha,
                                           dstPixel,
                                           dstAlpha,
                                           newAlpha,
                                           blendPixel,
                                           allChannelFlags,
                                           params.channelFlags);
                        }
                        dstPixel[Traits::alpha_pos] = newAlpha;
                    }

                    srcPixel += srcPixelIncrement;
                    dstPixel += Traits::channels_nb;
                    blendPixel += Traits::channels_nb;
                }
            }
        };

        if (params.cols <= ConversionContextStore::MaxBatchPixels) {
            const qint32 rowsPerBatch = qMax<qint32>(
                1, ConversionContextStore::MaxBatchPixels / params.cols);
            for (qint32 row = 0; row < params.rows; row += rowsPerBatch) {
                compositeBatch(row, 0, qMin(rowsPerBatch, params.rows - row), params.cols);
            }
        } else {
            for (qint32 row = 0; row < params.rows; ++row) {
                for (qint32 column = 0; column < params.cols;
                     column += ConversionContextStore::MaxBatchPixels) {
                    compositeBatch(row,
                                   column,
                                   1,
                                   qMin(ConversionContextStore::MaxBatchPixels, params.cols - column));
                }
            }
        }
    }

private:
    using ConversionContextStore = KoOklchConversionContextStore<Traits, channels_type>;

    static bool hasColorBlendingOverlap(const KoCompositeOp::ParameterInfo &params,
                                        channels_type opacity)
    {
        const bool constantSource = params.srcRowStride == 0;
        const qint32 sourcePixelIncrement = constantSource ? 0 : Traits::channels_nb;
        const channels_type zero = KoColorSpaceMathsTraits<channels_type>::zeroValue;
        const quint8 *sourceRow = params.srcRowStart;
        const quint8 *destinationRow = params.dstRowStart;
        const quint8 *maskRow = params.maskRowStart;

        for (qint32 row = 0; row < params.rows; ++row) {
            const channels_type *sourcePixel =
                reinterpret_cast<const channels_type *>(sourceRow);
            const channels_type *destinationPixel =
                reinterpret_cast<const channels_type *>(destinationRow);

            for (qint32 column = 0; column < params.cols; ++column) {
                if (effectiveSourceAlpha(sourcePixel, maskRow, column, opacity) != zero
                    && destinationPixel[Traits::alpha_pos] != zero) {
                    return true;
                }
                sourcePixel += sourcePixelIncrement;
                destinationPixel += Traits::channels_nb;
            }

            sourceRow += params.srcRowStride;
            destinationRow += params.dstRowStride;
            if (maskRow) {
                maskRow += params.maskRowStride;
            }
        }

        return false;
    }

    static void clearColorChannels(channels_type *pixel)
    {
        const channels_type zero = KoColorSpaceMathsTraits<channels_type>::zeroValue;
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos) {
                pixel[channel] = zero;
            }
        }
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

    static void compositeFullAlphaResult(const channels_type *src,
                                         channels_type srcAlpha,
                                         channels_type *dst,
                                         channels_type dstAlpha,
                                         const channels_type *blend,
                                         bool allChannelFlags,
                                         const QBitArray &channelFlags)
    {
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos && (allChannelFlags || channelFlags.testBit(channel))) {
                dst[channel] = Arithmetic::blend(
                    src[channel], srcAlpha, dst[channel], dstAlpha, blend[channel]);
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
