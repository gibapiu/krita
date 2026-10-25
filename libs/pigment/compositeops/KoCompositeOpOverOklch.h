/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KOCOMPOSITEOPOVEROKLCH_H
#define KOCOMPOSITEOPOVEROKLCH_H

#include <QByteArray>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

#include <KoColorProfile.h>
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
        , m_useDefaultSrgbU8FastPath(supportsDefaultSrgbU8FastPath(cs))
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

        if constexpr (std::is_same_v<channels_type, quint8>) {
            if (m_useDefaultSrgbU8FastPath) {
                compositeDefaultSrgbU8(
                    params, opacity, allChannelFlags, alphaLocked, constantSource);
                return;
            }
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

    static bool supportsDefaultSrgbU8FastPath(const KoColorSpace *colorSpace)
    {
        if constexpr (!std::is_same_v<channels_type, quint8>) {
            return false;
        }

        // KoColorProfile does not expose whether an ICC profile is a matrix-shaper.
        // Keep this path restricted to Krita's known built-in profile so a CLUT
        // profile with otherwise similar metadata cannot be treated as a matrix.
        const KoColorProfile *profile = colorSpace->profile();
        return profile
            && profile->name() == QStringLiteral("sRGB-elle-V2-srgbtrc.icc")
            && profile->getColorPrimaries() == PRIMARIES_ITU_R_BT_709_5
            && profile->getTransferCharacteristics() == TRC_IEC_61966_2_1;
    }

    static float srgbToLinear(float value)
    {
        return value <= 0.04045f
            ? value / 12.92f
            : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    static const std::array<float, 256> &srgbDecodeTable()
    {
        static const std::array<float, 256> table = []() {
            std::array<float, 256> result {};
            for (int value = 0; value < 256; ++value) {
                result[value] = srgbToLinear(float(value) / 255.0f);
            }
            return result;
        }();
        return table;
    }

    static const std::array<float, 255> &linearEncodingThresholds()
    {
        // Each boundary is the linear-light value halfway between adjacent
        // encoded U8 values. Binary search therefore gives exact U8 rounding
        // without evaluating the inverse transfer function for every pixel.
        static const std::array<float, 255> thresholds = []() {
            std::array<float, 255> result {};
            for (int value = 0; value < 255; ++value) {
                result[value] = srgbToLinear((float(value) + 0.5f) / 255.0f);
            }
            return result;
        }();
        return thresholds;
    }

    static quint8 linearToSrgbU8(float value)
    {
        if (!(value > 0.0f)) {
            return 0;
        }
        if (value >= 1.0f) {
            return 255;
        }

        const auto &thresholds = linearEncodingThresholds();
        return quint8(std::upper_bound(thresholds.cbegin(), thresholds.cend(), value)
                      - thresholds.cbegin());
    }

    static KoGamutMappingUtils::OkLch defaultSrgbU8ToOkLch(const channels_type *pixel)
    {
        const auto &decode = srgbDecodeTable();
        const float red = decode[pixel[Traits::red_pos]];
        const float green = decode[pixel[Traits::green_pos]];
        const float blue = decode[pixel[Traits::blue_pos]];

        const float l = 0.4122214708f * red + 0.5363325363f * green + 0.0514459929f * blue;
        const float m = 0.2119034982f * red + 0.6806995451f * green + 0.1073969566f * blue;
        const float s = 0.0883024619f * red + 0.2817188376f * green + 0.6299787005f * blue;

        const float lRoot = std::cbrt(l);
        const float mRoot = std::cbrt(m);
        const float sRoot = std::cbrt(s);

        return KoGamutMappingUtils::OkLabToOkLch(QVector4D(
            0.2104542553f * lRoot + 0.7936177850f * mRoot - 0.0040720468f * sRoot,
            1.9779984951f * lRoot - 2.4285922050f * mRoot + 0.4505937099f * sRoot,
            0.0259040371f * lRoot + 0.7827717662f * mRoot - 0.8086757660f * sRoot,
            0.0f));
    }

    static void defaultSrgbU8FromOkLab(const QVector4D &okLab,
                                       channels_type *pixel,
                                       bool allChannelFlags,
                                       const QBitArray &channelFlags)
    {
        const float lRoot = okLab.x() + 0.3963377774f * okLab.y() + 0.2158037573f * okLab.z();
        const float mRoot = okLab.x() - 0.1055613458f * okLab.y() - 0.0638541728f * okLab.z();
        const float sRoot = okLab.x() - 0.0894841775f * okLab.y() - 1.2914855480f * okLab.z();
        const float l = lRoot * lRoot * lRoot;
        const float m = mRoot * mRoot * mRoot;
        const float s = sRoot * sRoot * sRoot;

        const quint8 red = linearToSrgbU8(
            4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
        const quint8 green = linearToSrgbU8(
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
        const quint8 blue = linearToSrgbU8(
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);

        if (allChannelFlags || channelFlags.testBit(Traits::red_pos)) {
            pixel[Traits::red_pos] = red;
        }
        if (allChannelFlags || channelFlags.testBit(Traits::green_pos)) {
            pixel[Traits::green_pos] = green;
        }
        if (allChannelFlags || channelFlags.testBit(Traits::blue_pos)) {
            pixel[Traits::blue_pos] = blue;
        }
    }

    static void compositeDefaultSrgbU8(const KoCompositeOp::ParameterInfo &params,
                                       channels_type opacity,
                                       bool allChannelFlags,
                                       bool alphaLocked,
                                       bool constantSource)
    {
        const channels_type zero = KoColorSpaceMathsTraits<channels_type>::zeroValue;
        const channels_type unit = KoColorSpaceMathsTraits<channels_type>::unitValue;
        const qint32 srcPixelIncrement = constantSource ? 0 : Traits::channels_nb;
        KoGamutMappingUtils::OkLch constantSourceOkLch;
        bool constantSourceConverted = false;

        const quint8 *srcRow = params.srcRowStart;
        quint8 *dstRow = params.dstRowStart;
        const quint8 *maskRow = params.maskRowStart;

        for (qint32 row = 0; row < params.rows; ++row) {
            const channels_type *srcPixel = reinterpret_cast<const channels_type *>(srcRow);
            channels_type *dstPixel = reinterpret_cast<channels_type *>(dstRow);

            for (qint32 column = 0; column < params.cols; ++column) {
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
                    if (constantSource && !constantSourceConverted) {
                        constantSourceOkLch = defaultSrgbU8ToOkLch(srcPixel);
                        constantSourceConverted = true;
                    }
                    const KoGamutMappingUtils::OkLch sourceOkLch = constantSource
                        ? constantSourceOkLch
                        : defaultSrgbU8ToOkLch(srcPixel);
                    const KoGamutMappingUtils::OkLch destinationOkLch =
                        defaultSrgbU8ToOkLch(dstPixel);
                    const QVector4D result = KoGamutMappingUtils::interpolateOkLch(
                        destinationOkLch,
                        sourceOkLch,
                        KoColorSpaceMaths<channels_type, float>::scaleToA(srcBlend));
                    defaultSrgbU8FromOkLab(
                        result, dstPixel, allChannelFlags, params.channelFlags);
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

    const bool m_useDefaultSrgbU8FastPath;
    mutable ConversionContextStore m_conversionContexts;
};

#endif
