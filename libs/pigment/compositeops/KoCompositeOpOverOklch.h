/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KOCOMPOSITEOPOVEROKLCH_H
#define KOCOMPOSITEOPOVEROKLCH_H

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
 * A source-over composite operation that interpolates colors in OKLCH.
 *
 * Conversion through XYZ keeps the operation aware of the image's RGB profile.
 * The resulting color is converted back through Krita's color-management path.
 */
template<class Traits>
class KoCompositeOpOverOklch : public KoCompositeOp
{
    using channels_type = typename Traits::channels_type;

public:
    explicit KoCompositeOpOverOklch(const KoColorSpace *cs)
        : KoCompositeOp(cs, COMPOSITE_OVER_OKLCH, KoCompositeOp::categoryOKLCH())
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

        typename ConversionContextStore::ConversionContext *conversionContext =
            m_conversionContexts.contextForCurrentThread();
        if (!conversionContext || !conversionContext->isValid()) {
            KoCompositeOpOver<Traits> fallback(colorSpace());
            fallback.composite(params);
            return;
        }

        KoGamutMappingUtils::OkLch constantSourceOkLch;
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
                    channels_type srcBlend = zero;

                    if (srcAlpha != zero) {
                        if (alphaLocked || dstAlpha == unit) {
                            srcBlend = srcAlpha;
                        } else if (dstAlpha == zero) {
                            if (!allChannelFlags) {
                                clearColorChannels(dstPixel);
                            }
                            dstPixel[Traits::alpha_pos] = srcAlpha;
                            srcBlend = unit;
                        } else {
                            const channels_type newAlpha = dstAlpha
                                + KoColorSpaceMaths<channels_type>::multiply(unit - dstAlpha, srcAlpha);
                            dstPixel[Traits::alpha_pos] = newAlpha;
                            srcBlend = KoColorSpaceMaths<channels_type>::divide(srcAlpha, newAlpha);
                        }
                    }

                    if (srcBlend == unit) {
                        copyColorChannels(srcPixel, dstPixel, allChannelFlags, params.channelFlags);
                    } else if (srcBlend != zero) {
                        conversionContext->addActivePixel(
                            row,
                            column,
                            KoColorSpaceMaths<channels_type, float>::scaleToA(srcBlend));
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
                    constantSourceOkLch = KoGamutMappingUtils::OkLabToOkLch(
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
                const KoGamutMappingUtils::OkLch from = KoGamutMappingUtils::OkLabToOkLch(
                    KoGamutMappingUtils::XYZToOkLab(toVector(conversionContext->dstXyz[pixel])));
                const KoGamutMappingUtils::OkLch to = constantSource
                    ? constantSourceOkLch
                    : KoGamutMappingUtils::OkLabToOkLch(
                        KoGamutMappingUtils::XYZToOkLab(toVector(conversionContext->srcXyz[pixel])));
                conversionContext->dstXyz[pixel] = fromVector(KoGamutMappingUtils::OkLabToXYZ(
                    KoGamutMappingUtils::interpolateOkLch(
                        from, to, conversionContext->perPixelValues[pixel])));
            }

            conversionContext->fromXyz->transform(
                reinterpret_cast<const quint8 *>(conversionContext->dstXyz.constData()),
                reinterpret_cast<quint8 *>(conversionContext->convertedPixels.data()),
                activePixelCount);

            const channels_type *converted =
                reinterpret_cast<const channels_type *>(conversionContext->convertedPixels.constData());
            for (const typename ConversionContextStore::ActiveSpan &span :
                 std::as_const(conversionContext->activeSpans)) {
                channels_type *dstPixel = reinterpret_cast<channels_type *>(
                    params.dstRowStart + span.row * params.dstRowStride)
                    + span.column * Traits::channels_nb;
                const channels_type *convertedPixel = converted
                    + span.packedOffset * Traits::channels_nb;

                for (qint32 pixel = 0; pixel < span.length; ++pixel) {
                    copyColorChannels(convertedPixel, dstPixel, allChannelFlags, params.channelFlags);
                    dstPixel += Traits::channels_nb;
                    convertedPixel += Traits::channels_nb;
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
    using ConversionContextStore = KoOklchConversionContextStore<Traits, float>;

    static void clearColorChannels(channels_type *pixel)
    {
        const channels_type zero = KoColorSpaceMathsTraits<channels_type>::zeroValue;
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos) {
                pixel[channel] = zero;
            }
        }
    }

    static void copyColorChannels(const channels_type *source,
                                  channels_type *destination,
                                  bool allChannelFlags,
                                  const QBitArray &channelFlags)
    {
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos
                && (allChannelFlags || channelFlags.testBit(channel))) {
                destination[channel] = source[channel];
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

    static QVector4D toVector(const KoXyzF32Traits::Pixel &pixel)
    {
        return QVector4D(pixel.x, pixel.y, pixel.z, 0.0f);
    }

    static KoXyzF32Traits::Pixel fromVector(const QVector4D &value)
    {
        return {value.x(), value.y(), value.z(), 1.0f};
    }

    mutable ConversionContextStore m_conversionContexts;
};

#endif
