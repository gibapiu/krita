/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KOCOMPOSITEOPOVEROKLCHSRGBU8FASTPATH_H
#define KOCOMPOSITEOPOVEROKLCHSRGBU8FASTPATH_H

#include <QBitArray>
#include <QString>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

#include <KoColorModelStandardIds.h>
#include <KoColorProfile.h>
#include <KoColorSpace.h>
#include <KoColorSpaceMaths.h>
#include <KoCompositeOp.h>
#include <KoGamutMappingUtils.h>

#include "KoCompositeOpOklchUtils.h"

template<class Traits, bool UseAntipodalHybrid = false>
class KoCompositeOpOverOklchSrgbU8FastPath
{
    using channels_type = typename Traits::channels_type;
    using Endpoint = std::conditional_t<UseAntipodalHybrid,
                                        KoOklchCompositeUtils::AntipodalHybridEndpoint,
                                        KoGamutMappingUtils::OkLch>;

    struct EncodedSrgbU8 {
        quint8 red;
        quint8 green;
        quint8 blue;
    };

    struct LinearSrgb {
        float red;
        float green;
        float blue;
    };

    struct Lms {
        float l;
        float m;
        float s;
    };

public:
    static bool supportsColorSpace(const KoColorSpace *colorSpace)
    {
        if constexpr (!std::is_same_v<channels_type, quint8>) {
            return false;
        } else {
            if (!colorSpace) {
                return false;
            }
            const KoColorProfile *profile = colorSpace->profile();
            return colorSpace->colorModelId() == RGBAColorModelID
                && colorSpace->colorDepthId() == Integer8BitsColorDepthID
                && profile
                && profile->name() == QStringLiteral("sRGB-elle-V2-srgbtrc.icc")
                && profile->getColorPrimaries() == PRIMARIES_ITU_R_BT_709_5
                && profile->getTransferCharacteristics() == TRC_IEC_61966_2_1;
        }
    }

    static void composite(const KoCompositeOp::ParameterInfo &params,
                          channels_type opacity,
                          bool allChannelsEnabled,
                          bool alphaLocked,
                          bool constantSource)
    {
        const channels_type zeroChannelValue = KoColorSpaceMathsTraits<channels_type>::zeroValue;
        const channels_type opaqueChannelValue = KoColorSpaceMathsTraits<channels_type>::unitValue;
        const qint32 sourceChannelIncrement = constantSource ? 0 : Traits::channels_nb;
        Endpoint constantSourceEndpoint;
        bool constantSourceEndpointInitialized = false;

        const quint8 *sourceRow = params.srcRowStart;
        quint8 *destinationRow = params.dstRowStart;
        const quint8 *maskRow = params.maskRowStart;

        for (qint32 row = 0; row < params.rows; ++row) {
            const channels_type *sourcePixel = reinterpret_cast<const channels_type *>(sourceRow);
            channels_type *destinationPixel = reinterpret_cast<channels_type *>(destinationRow);

            for (qint32 column = 0; column < params.cols; ++column) {
                const channels_type sourceAlpha = effectiveSourceAlpha(
                    sourcePixel, maskRow, column, opacity);
                const channels_type destinationAlpha = destinationPixel[Traits::alpha_pos];
                channels_type sourceColorWeight = zeroChannelValue;

                if (sourceAlpha != zeroChannelValue) {
                    if (alphaLocked || destinationAlpha == opaqueChannelValue) {
                        sourceColorWeight = sourceAlpha;
                    } else if (destinationAlpha == zeroChannelValue) {
                        if (!allChannelsEnabled) {
                            clearColorChannels(destinationPixel);
                        }
                        destinationPixel[Traits::alpha_pos] = sourceAlpha;
                        sourceColorWeight = opaqueChannelValue;
                    } else {
                        const channels_type compositeAlpha = destinationAlpha
                            + KoColorSpaceMaths<channels_type>::multiply(
                                opaqueChannelValue - destinationAlpha, sourceAlpha);
                        destinationPixel[Traits::alpha_pos] = compositeAlpha;
                        sourceColorWeight = KoColorSpaceMaths<channels_type>::divide(
                            sourceAlpha, compositeAlpha);
                    }
                }

                if (sourceColorWeight == opaqueChannelValue) {
                    copyColorChannels(
                        sourcePixel, destinationPixel, allChannelsEnabled, params.channelFlags);
                } else if (sourceColorWeight != zeroChannelValue) {
                    if (constantSource && !constantSourceEndpointInitialized) {
                        constantSourceEndpoint = pixelToEndpoint(sourcePixel);
                        constantSourceEndpointInitialized = true;
                    }
                    const Endpoint sourceEndpoint = constantSource
                        ? constantSourceEndpoint
                        : pixelToEndpoint(sourcePixel);
                    const Endpoint destinationEndpoint = pixelToEndpoint(destinationPixel);
                    const QVector4D blendedOkLab = interpolateEndpoints(
                        destinationEndpoint,
                        sourceEndpoint,
                        KoColorSpaceMaths<channels_type, float>::scaleToA(sourceColorWeight));
                    writeOkLabToPixel(
                        blendedOkLab, destinationPixel, allChannelsEnabled, params.channelFlags);
                }

                sourcePixel += sourceChannelIncrement;
                destinationPixel += Traits::channels_nb;
            }

            sourceRow += params.srcRowStride;
            destinationRow += params.dstRowStride;
            if (maskRow) {
                maskRow += params.maskRowStride;
            }
        }
    }

private:
    static float decodeSrgbTransferFunction(float encodedValue)
    {
        return encodedValue <= 0.04045f
            ? encodedValue / 12.92f
            : std::pow((encodedValue + 0.055f) / 1.055f, 2.4f);
    }

    static const std::array<float, 256> &encodedSrgbU8ToLinearTable()
    {
        static const std::array<float, 256> table = []() {
            std::array<float, 256> result {};
            for (int encodedValue = 0; encodedValue < 256; ++encodedValue) {
                result[encodedValue] = decodeSrgbTransferFunction(float(encodedValue) / 255.0f);
            }
            return result;
        }();
        return table;
    }

    static const std::array<float, 255> &linearSrgbToEncodedU8RoundingThresholds()
    {
        static const std::array<float, 255> thresholds = []() {
            std::array<float, 255> result {};
            for (int encodedValue = 0; encodedValue < 255; ++encodedValue) {
                result[encodedValue] = decodeSrgbTransferFunction(
                    (float(encodedValue) + 0.5f) / 255.0f);
            }
            return result;
        }();
        return thresholds;
    }

    static quint8 encodeLinearSrgbToU8(float linearValue)
    {
        if (!(linearValue > 0.0f)) {
            return 0;
        }
        if (linearValue >= 1.0f) {
            return 255;
        }

        const auto &roundingThresholds = linearSrgbToEncodedU8RoundingThresholds();
        return quint8(std::upper_bound(
                          roundingThresholds.cbegin(), roundingThresholds.cend(), linearValue)
                      - roundingThresholds.cbegin());
    }

    static EncodedSrgbU8 readEncodedSrgbU8(const channels_type *pixel)
    {
        return {
            pixel[Traits::red_pos],
            pixel[Traits::green_pos],
            pixel[Traits::blue_pos],
        };
    }

    static LinearSrgb decodeToLinearSrgb(const EncodedSrgbU8 &encodedSrgb)
    {
        const auto &decodeTable = encodedSrgbU8ToLinearTable();
        return {
            decodeTable[encodedSrgb.red],
            decodeTable[encodedSrgb.green],
            decodeTable[encodedSrgb.blue],
        };
    }

    static Lms linearSrgbToLms(const LinearSrgb &linearSrgb)
    {
        return {
            0.4122214708f * linearSrgb.red
                + 0.5363325363f * linearSrgb.green
                + 0.0514459929f * linearSrgb.blue,
            0.2119034982f * linearSrgb.red
                + 0.6806995451f * linearSrgb.green
                + 0.1073969566f * linearSrgb.blue,
            0.0883024619f * linearSrgb.red
                + 0.2817188376f * linearSrgb.green
                + 0.6299787005f * linearSrgb.blue,
        };
    }

    static QVector4D cubeRootLmsToOkLab(const Lms &lms)
    {
        const float lRoot = std::cbrt(lms.l);
        const float mRoot = std::cbrt(lms.m);
        const float sRoot = std::cbrt(lms.s);

        return QVector4D(
            0.2104542553f * lRoot + 0.7936177850f * mRoot - 0.0040720468f * sRoot,
            1.9779984951f * lRoot - 2.4285922050f * mRoot + 0.4505937099f * sRoot,
            0.0259040371f * lRoot + 0.7827717662f * mRoot - 0.8086757660f * sRoot,
            0.0f);
    }

    static Lms okLabToCubeRootLms(const QVector4D &okLab)
    {
        return {
            okLab.x() + 0.3963377774f * okLab.y() + 0.2158037573f * okLab.z(),
            okLab.x() - 0.1055613458f * okLab.y() - 0.0638541728f * okLab.z(),
            okLab.x() - 0.0894841775f * okLab.y() - 1.2914855480f * okLab.z(),
        };
    }

    static Lms cubeLms(const Lms &cubeRootLms)
    {
        return {
            cubeRootLms.l * cubeRootLms.l * cubeRootLms.l,
            cubeRootLms.m * cubeRootLms.m * cubeRootLms.m,
            cubeRootLms.s * cubeRootLms.s * cubeRootLms.s,
        };
    }

    static LinearSrgb lmsToLinearSrgb(const Lms &lms)
    {
        return {
            4.0767416621f * lms.l - 3.3077115913f * lms.m + 0.2309699292f * lms.s,
            -1.2684380046f * lms.l + 2.6097574011f * lms.m - 0.3413193965f * lms.s,
            -0.0041960863f * lms.l - 0.7034186147f * lms.m + 1.7076147010f * lms.s,
        };
    }

    static EncodedSrgbU8 encodeToSrgbU8(const LinearSrgb &linearSrgb)
    {
        return {
            encodeLinearSrgbToU8(linearSrgb.red),
            encodeLinearSrgbToU8(linearSrgb.green),
            encodeLinearSrgbToU8(linearSrgb.blue),
        };
    }

    static Endpoint pixelToEndpoint(const channels_type *pixel)
    {
        const LinearSrgb linearSrgb = decodeToLinearSrgb(readEncodedSrgbU8(pixel));
        const QVector4D okLab = cubeRootLmsToOkLab(linearSrgbToLms(linearSrgb));
        if constexpr (UseAntipodalHybrid) {
            return KoOklchCompositeUtils::prepareAntipodalHybridEndpoint(okLab);
        } else {
            return KoOklchCompositeUtils::okLabToOkLchIgnoringProfileNoise(okLab);
        }
    }

    static QVector4D interpolateEndpoints(const Endpoint &from,
                                          const Endpoint &to,
                                          float amount)
    {
        if constexpr (UseAntipodalHybrid) {
            return KoOklchCompositeUtils::interpolateAntipodalHybrid(from, to, amount);
        } else {
            return KoGamutMappingUtils::interpolateOkLch(from, to, amount);
        }
    }

    static void writeOkLabToPixel(const QVector4D &okLab,
                                  channels_type *pixel,
                                  bool allChannelsEnabled,
                                  const QBitArray &channelFlags)
    {
        const EncodedSrgbU8 encodedSrgb = encodeToSrgbU8(
            lmsToLinearSrgb(cubeLms(okLabToCubeRootLms(okLab))));

        if (allChannelsEnabled || channelFlags.testBit(Traits::red_pos)) {
            pixel[Traits::red_pos] = encodedSrgb.red;
        }
        if (allChannelsEnabled || channelFlags.testBit(Traits::green_pos)) {
            pixel[Traits::green_pos] = encodedSrgb.green;
        }
        if (allChannelsEnabled || channelFlags.testBit(Traits::blue_pos)) {
            pixel[Traits::blue_pos] = encodedSrgb.blue;
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
                                  bool allChannelsEnabled,
                                  const QBitArray &channelFlags)
    {
        for (int channel = 0; channel < Traits::channels_nb; ++channel) {
            if (channel != Traits::alpha_pos
                && (allChannelsEnabled || channelFlags.testBit(channel))) {
                destination[channel] = source[channel];
            }
        }
    }

    static channels_type effectiveSourceAlpha(const channels_type *source,
                                              const quint8 *maskRow,
                                              qint32 column,
                                              channels_type opacity)
    {
        channels_type alpha = source[Traits::alpha_pos];
        if (maskRow) {
            alpha = KoColorSpaceMaths<quint8, channels_type>::multiply(
                maskRow[column], alpha, opacity);
        } else if (opacity != KoColorSpaceMathsTraits<channels_type>::unitValue) {
            alpha = KoColorSpaceMaths<channels_type>::multiply(alpha, opacity);
        }
        return alpha;
    }
};

#endif
