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
#include <KoXyzColorSpaceTraits.h>

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
        , m_threadData(cs)
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

        typename ThreadDataStore::Data *data = m_threadData.localData();
        if (!data || !data->isValid()) {
            KoCompositeOpOver<Traits> fallback(colorSpace());
            fallback.composite(params);
            return;
        }

        const qint32 pixelCount = params.cols;
        data->ensureCapacity(pixelCount, !constantSource);
        QVector<KoXyzF32Traits::Pixel> &srcXyz = data->srcXyz;
        QVector<KoXyzF32Traits::Pixel> &dstXyz = data->dstXyz;
        QVector<float> &blendAmounts = data->auxiliary;
        QByteArray &convertedRow = data->output;

        KoGamutMappingUtils::OkLch constantSourceOkLch;
        bool constantSourceConverted = false;
        const qint32 srcPixelIncrement = constantSource ? 0 : Traits::channels_nb;

        const quint8 *srcRow = params.srcRowStart;
        quint8 *dstRow = params.dstRowStart;
        const quint8 *maskRow = params.maskRowStart;

        for (int row = 0; row < params.rows; ++row) {
            const channels_type *src = reinterpret_cast<const channels_type *>(srcRow);
            const channels_type *srcPixel = src;
            channels_type *dst = reinterpret_cast<channels_type *>(dstRow);
            bool needsColorConversion = false;

            for (qint32 column = 0; column < pixelCount; ++column) {
                channels_type *dstPixel = dst + column * Traits::channels_nb;

                const channels_type srcAlpha = effectiveSourceAlpha(srcPixel, maskRow, column, opacity);
                const channels_type dstAlpha = dstPixel[Traits::alpha_pos];
                channels_type srcBlend = zero;

                if (srcAlpha != zero) {
                    if (alphaLocked || dstAlpha == unit) {
                        srcBlend = srcAlpha;
                    } else if (dstAlpha == zero) {
                        if (!allChannelFlags) {
                            for (int channel = 0; channel < Traits::channels_nb; ++channel) {
                                if (channel != Traits::alpha_pos) {
                                    dstPixel[channel] = zero;
                                }
                            }
                        }

                        dstPixel[Traits::alpha_pos] = srcAlpha;
                        srcBlend = unit;
                    } else {
                        const channels_type newAlpha = dstAlpha
                            + KoColorSpaceMaths<channels_type>::multiply(
                                unit - dstAlpha,
                                srcAlpha);
                        dstPixel[Traits::alpha_pos] = newAlpha;
                        srcBlend = KoColorSpaceMaths<channels_type>::divide(srcAlpha, newAlpha);
                    }
                }

                blendAmounts[column] =
                    KoColorSpaceMaths<channels_type, float>::scaleToA(srcBlend);
                needsColorConversion |= blendAmounts[column] > 0.0f && blendAmounts[column] < 1.0f;
                srcPixel += srcPixelIncrement;
            }

            if (needsColorConversion) {
                if (constantSource) {
                    if (!constantSourceConverted) {
                        KoXyzF32Traits::Pixel constantSourceXyz;
                        data->toXyz->transform(params.srcRowStart,
                                               reinterpret_cast<quint8 *>(&constantSourceXyz),
                                               1);
                        constantSourceOkLch = KoGamutMappingUtils::OkLabToOkLch(
                            KoGamutMappingUtils::XYZToOkLab(toVector(constantSourceXyz)));
                        constantSourceConverted = true;
                    }
                } else {
                    data->toXyz->transform(srcRow, reinterpret_cast<quint8 *>(srcXyz.data()), pixelCount);
                }
                data->toXyz->transform(dstRow, reinterpret_cast<quint8 *>(dstXyz.data()), pixelCount);

                for (qint32 column = 0; column < pixelCount; ++column) {
                    const float amount = blendAmounts[column];
                    if (amount <= 0.0f || amount >= 1.0f) {
                        continue;
                    }

                    const KoGamutMappingUtils::OkLch from = KoGamutMappingUtils::OkLabToOkLch(
                        KoGamutMappingUtils::XYZToOkLab(toVector(dstXyz[column])));
                    const KoGamutMappingUtils::OkLch to = constantSource
                        ? constantSourceOkLch
                        : KoGamutMappingUtils::OkLabToOkLch(
                            KoGamutMappingUtils::XYZToOkLab(toVector(srcXyz[column])));
                    dstXyz[column] = fromVector(KoGamutMappingUtils::OkLabToXYZ(
                        KoGamutMappingUtils::interpolateOkLch(from, to, amount)));
                }

                data->fromXyz->transform(reinterpret_cast<const quint8 *>(dstXyz.constData()),
                                         reinterpret_cast<quint8 *>(convertedRow.data()),
                                         pixelCount);
            }

            const channels_type *converted = reinterpret_cast<const channels_type *>(convertedRow.constData());
            srcPixel = src;
            for (qint32 column = 0; column < pixelCount; ++column) {
                channels_type *dstPixel = dst + column * Traits::channels_nb;
                const channels_type *convertedPixel = converted + column * Traits::channels_nb;

                if (blendAmounts[column] > 0.0f) {
                    for (int channel = 0; channel < Traits::channels_nb; ++channel) {
                        if (channel == Traits::alpha_pos
                            || (!allChannelFlags && !params.channelFlags.testBit(channel))) {
                            continue;
                        }
                        dstPixel[channel] = blendAmounts[column] >= 1.0f
                            ? srcPixel[channel]
                            : convertedPixel[channel];
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
    using ThreadDataStore = KoOklchThreadDataStore<Traits, float>;

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

    mutable ThreadDataStore m_threadData;
};

#endif
