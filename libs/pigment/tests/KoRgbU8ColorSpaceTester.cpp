/*
 *  SPDX-FileCopyrightText: 2005 Adrian Page <adrian@pagenet.plus.com>
 *  SPDX-FileCopyrightText: 2008 Bart Coppens <kde@bartcoppens.be>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KoRgbU8ColorSpaceTester.h"

#include "KoColorModelStandardIds.h"

#include <QScopedPointer>
#include <QStringList>

#include <simpletest.h>
#include <DebugPigment.h>
#include <cmath>
#include <string.h>

#include "KoColor.h"
#include "KoColorConversionTransformation.h"
#include "KoColorSpace.h"
#include "KoColorSpaceRegistry.h"
#include "KoCompositeOp.h"
#include "KoGamutMappingUtils.h"
#include "KoMixColorsOp.h"
#include "KoXyzColorSpaceTraits.h"
#include "../compositeops/KoCompositeOpOklchUtils.h"
#include <KoCompositeOpRegistry.h>
#include <kistest.h>

#define NUM_CHANNELS 4

#define RED_CHANNEL 0
#define GREEN_CHANNEL 1
#define BLUE_CHANNEL 2
#define ALPHA_CHANNEL 3
#include <KoColorProfile.h>

void KoRgbU8ColorSpaceTester::testBasics()
{
}

#define PIXEL_RED 0
#define PIXEL_GREEN 1
#define PIXEL_BLUE 2
#define PIXEL_ALPHA 3

void KoRgbU8ColorSpaceTester::testMixColors()
{
    const KoColorSpace* cs = KoColorSpaceRegistry::instance()->rgb8();
    KoMixColorsOp * mixOp = cs->mixColorsOp();

    // Test mixColors.
    quint8 pixel1[4];
    quint8 pixel2[4];
    quint8 outputPixel[4];

    pixel1[PIXEL_RED] = 255;
    pixel1[PIXEL_GREEN] = 255;
    pixel1[PIXEL_BLUE] = 255;
    pixel1[PIXEL_ALPHA] = 255;

    pixel2[PIXEL_RED] = 0;
    pixel2[PIXEL_GREEN] = 0;
    pixel2[PIXEL_BLUE] = 0;
    pixel2[PIXEL_ALPHA] = 0;

    const quint8 *pixelPtrs[2];
    qint16 weights[2];

    pixelPtrs[0] = pixel1;
    pixelPtrs[1] = pixel2;

    weights[0] = 255;
    weights[1] = 0;

    mixOp->mixColors(pixelPtrs, weights, 2, outputPixel);

    QVERIFY((int)outputPixel[PIXEL_RED] == 255);
    QVERIFY((int)outputPixel[PIXEL_GREEN] == 255);
    QVERIFY((int)outputPixel[PIXEL_BLUE] == 255);
    QVERIFY((int)outputPixel[PIXEL_ALPHA] == 255);

    weights[0] = 0;
    weights[1] = 255;

    mixOp->mixColors(pixelPtrs, weights, 2, outputPixel);

    QVERIFY((int)outputPixel[PIXEL_RED] == 0);
    QVERIFY((int)outputPixel[PIXEL_GREEN] == 0);
    QVERIFY((int)outputPixel[PIXEL_BLUE] == 0);
    QVERIFY((int)outputPixel[PIXEL_ALPHA] == 0);

    weights[0] = 128;
    weights[1] = 127;

    mixOp->mixColors(pixelPtrs, weights, 2, outputPixel);

    QVERIFY((int)outputPixel[PIXEL_RED] == 255);
    QVERIFY((int)outputPixel[PIXEL_GREEN] == 255);
    QVERIFY((int)outputPixel[PIXEL_BLUE] == 255);
    QVERIFY((int)outputPixel[PIXEL_ALPHA] == 128);

    pixel1[PIXEL_RED] = 200;
    pixel1[PIXEL_GREEN] = 100;
    pixel1[PIXEL_BLUE] = 50;
    pixel1[PIXEL_ALPHA] = 255;

    pixel2[PIXEL_RED] = 100;
    pixel2[PIXEL_GREEN] = 200;
    pixel2[PIXEL_BLUE] = 20;
    pixel2[PIXEL_ALPHA] = 255;

    mixOp->mixColors(pixelPtrs, weights, 2, outputPixel);

    QVERIFY((int)outputPixel[PIXEL_RED] == 150);
    QCOMPARE((int)outputPixel[PIXEL_GREEN], 150);
    QVERIFY((int)outputPixel[PIXEL_BLUE] == 35);
    QVERIFY((int)outputPixel[PIXEL_ALPHA] == 255);

    pixel1[PIXEL_RED] = 0;
    pixel1[PIXEL_GREEN] = 0;
    pixel1[PIXEL_BLUE] = 0;
    pixel1[PIXEL_ALPHA] = 0;

    pixel2[PIXEL_RED] = 255;
    pixel2[PIXEL_GREEN] = 255;
    pixel2[PIXEL_BLUE] = 255;
    pixel2[PIXEL_ALPHA] = 254;

    weights[0] = 89;
    weights[1] = 166;

    mixOp->mixColors(pixelPtrs, weights, 2, outputPixel);

    QVERIFY((int)outputPixel[PIXEL_RED] == 255);
    QVERIFY((int)outputPixel[PIXEL_GREEN] == 255);
    QVERIFY((int)outputPixel[PIXEL_BLUE] == 255);
    QVERIFY((int)outputPixel[PIXEL_ALPHA] == 165);
}
void KoRgbU8ColorSpaceTester::testMixColorsAverage()
{
    const KoColorSpace* cs = KoColorSpaceRegistry::instance()->rgb8();
    KoMixColorsOp * mixOp = cs->mixColorsOp();

    // Test mixColors.
    quint8 pixel1[4];
    quint8 pixel2[4];
    quint8 outputPixel[4];

    pixel1[PIXEL_RED] = 255;
    pixel1[PIXEL_GREEN] = 255;
    pixel1[PIXEL_BLUE] = 255;
    pixel1[PIXEL_ALPHA] = 255;

    pixel2[PIXEL_RED] = 0;
    pixel2[PIXEL_GREEN] = 0;
    pixel2[PIXEL_BLUE] = 0;
    pixel2[PIXEL_ALPHA] = 0;

    const quint8 *pixelPtrs[2];

    pixelPtrs[0] = pixel1;
    pixelPtrs[1] = pixel2;

    mixOp->mixColors(pixelPtrs, 2, outputPixel);

    QCOMPARE((int)outputPixel[PIXEL_RED], 255);
    QCOMPARE((int)outputPixel[PIXEL_GREEN], 255);
    QCOMPARE((int)outputPixel[PIXEL_BLUE], 255);
    QCOMPARE((int)outputPixel[PIXEL_ALPHA], 128);

    pixel2[PIXEL_ALPHA] = 255;
    mixOp->mixColors(pixelPtrs, 2, outputPixel);

    QCOMPARE((int)outputPixel[PIXEL_RED], 128);
    QCOMPARE((int)outputPixel[PIXEL_GREEN], 128);
    QCOMPARE((int)outputPixel[PIXEL_BLUE], 128);
    QCOMPARE((int)outputPixel[PIXEL_ALPHA], 255);
}

void KoRgbU8ColorSpaceTester::testCompositeOps()
{
    // Just COMPOSITE_COPY for now

    QList<KoID> depthIDs = KoColorSpaceRegistry::instance()->colorDepthList(RGBAColorModelID.id(),
                           KoColorSpaceRegistry::AllColorSpaces);

    Q_FOREACH (const KoID& depthId, depthIDs) {

        if (depthId.id().contains("Float")) continue;

        qDebug() << depthId.id();
        const KoColorSpace* cs = KoColorSpaceRegistry::instance()->colorSpace(
                                     RGBAColorModelID.id(), depthId.id(), "");
        
        const KoCompositeOp* copyOp = cs->compositeOp(COMPOSITE_COPY);
        KoColor src(cs), dst(cs);

        QColor red(255, 0, 0);
        QColor blue(0, 0, 255);
        QColor transparentRed(255, 0, 0, 0);

        // Copying a color over another color should replace the original color
        src.fromQColor(red);
        dst.fromQColor(blue);
        
        qDebug() << src.toQColor() << dst.toQColor();

        QVERIFY(memcmp(dst.data(), src.data(), cs->pixelSize()) != 0);

        copyOp->composite(dst.data(), cs->pixelSize(), src.data(), cs->pixelSize(),
                          0, 0, 1, 1, OPACITY_OPAQUE_F);

        src.fromQColor(red);
        QVERIFY(memcmp(dst.data(), src.data(), cs->pixelSize()) == 0);

        // Copying something transparent over something non-transparent should, of course, make the dst transparent
        src.fromQColor(transparentRed);
        dst.fromQColor(blue);

        QVERIFY(memcmp(dst.data(), src.data(), cs->pixelSize()) != 0);

        copyOp->composite(dst.data(), cs->pixelSize(), src.data(), cs->pixelSize(),
                          0, 0, 1, 1, OPACITY_OPAQUE_F);

        src.fromQColor(transparentRed);
        QVERIFY(memcmp(dst.data(), src.data(), cs->pixelSize()) == 0);

        // Copying something solid over something transparent
        src.fromQColor(blue);
        dst.fromQColor(transparentRed);

        QVERIFY(memcmp(dst.data(), src.data(), cs->pixelSize()) != 0);

        copyOp->composite(dst.data(), cs->pixelSize(), src.data(), cs->pixelSize(),
                          0, 0, 1, 1, OPACITY_OPAQUE_F);

        src.fromQColor(blue);
        QVERIFY(memcmp(dst.data(), src.data(), cs->pixelSize()) == 0);

    }
}

void KoRgbU8ColorSpaceTester::testOklchCompositeOp()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    const KoCompositeOp *op = cs->compositeOp(COMPOSITE_OVER_OKLCH);
    QVERIFY(op);

    KoColor src(cs);
    KoColor dst(cs);
    src.fromQColor(Qt::red);
    dst.fromQColor(Qt::blue);

    op->composite(dst.data(), cs->pixelSize(), src.data(), cs->pixelSize(),
                  nullptr, 0, 1, 1, 0.5f);

    const QColor result = dst.toQColor();
    QVERIFY(result.red() > 160);
    QVERIFY(result.green() < 80);
    QVERIFY(result.blue() > 160);
    QCOMPARE(result.alpha(), 255);

    dst.fromQColor(Qt::blue);
    op->composite(dst.data(), cs->pixelSize(), src.data(), cs->pixelSize(),
                  nullptr, 0, 1, 1, 1.0f);
    QCOMPARE(dst.toQColor(), QColor(Qt::red));
}

void KoRgbU8ColorSpaceTester::testOklchHybridCompositeOp()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    const KoCompositeOp *op = cs->compositeOp(COMPOSITE_OVER_OKLCH_HYBRID);
    QVERIFY(op);

    constexpr float lightness = 0.65f;
    constexpr float chroma = 0.2f;
    constexpr float pi = 3.14159265358979323846f;
    const auto endpointAtHue = [=](float hue) {
        return KoOklchCompositeUtils::prepareAntipodalHybridEndpoint(
            QVector4D(lightness,
                      chroma * std::cos(hue),
                      chroma * std::sin(hue),
                      0.0f));
    };

    const auto from = endpointAtHue(0.0f);
    const QVector4D antipodalMidpoint = KoOklchCompositeUtils::interpolateAntipodalHybrid(
        from, endpointAtHue(pi), 0.5f);
    QVERIFY(qAbs(antipodalMidpoint.x() - lightness) < 0.000001f);
    QVERIFY(std::hypot(antipodalMidpoint.y(), antipodalMidpoint.z()) < 0.000001f);

    const QVector4D belowMidpoint = KoOklchCompositeUtils::interpolateAntipodalHybrid(
        from, endpointAtHue(pi - 0.00001f), 0.5f);
    const QVector4D aboveMidpoint = KoOklchCompositeUtils::interpolateAntipodalHybrid(
        from, endpointAtHue(-pi + 0.00001f), 0.5f);
    QVERIFY((belowMidpoint - aboveMidpoint).length() < 0.00001f);

    KoColor source(QColor(Qt::red), cs);
    KoColor destination(QColor(Qt::blue), cs);
    op->composite(destination.data(), cs->pixelSize(), source.data(), cs->pixelSize(),
                  nullptr, 0, 1, 1, 1.0f);
    QCOMPARE(destination.toQColor(), QColor(Qt::red));
}

void KoRgbU8ColorSpaceTester::testOklchColorCompositeOp()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    const KoCompositeOp *op = cs->compositeOp(COMPOSITE_COLOR_OKLCH);
    QVERIFY(op);

    const KoColorSpace *xyzColorSpace = KoColorSpaceRegistry::instance()->colorSpace(
        XYZAColorModelID.id(), Float32BitsColorDepthID.id(), QString());
    QVERIFY(xyzColorSpace);

    QScopedPointer<KoColorConversionTransformation> toXyz(
        cs->createColorConverter(xyzColorSpace,
                                 KoColorConversionTransformation::IntentRelativeColorimetric,
                                 KoColorConversionTransformation::adjustmentConversionFlags()));
    QVERIFY(toXyz);

    const auto toOkLab = [&toXyz](const KoColor &color) {
        KoXyzF32Traits::Pixel xyz;
        toXyz->transform(color.data(), reinterpret_cast<quint8 *>(&xyz), 1);
        return KoGamutMappingUtils::XYZToOkLab(QVector4D(xyz.x, xyz.y, xyz.z, 0.0f));
    };

    KoColor src(cs);
    KoColor dst(cs);
    src.fromQColor(QColor(180, 100, 80));
    dst.fromQColor(QColor(80, 80, 80));

    const QVector4D sourceOkLab = toOkLab(src);
    const QVector4D destinationOkLab = toOkLab(dst);

    op->composite(dst.data(), cs->pixelSize(), src.data(), cs->pixelSize(),
                  nullptr, 0, 1, 1, 1.0f);

    const QVector4D resultOkLab = toOkLab(dst);
    QVERIFY(qAbs(resultOkLab.x() - destinationOkLab.x()) < 0.01f);
    QVERIFY(qAbs(resultOkLab.y() - sourceOkLab.y()) < 0.015f);
    QVERIFY(qAbs(resultOkLab.z() - sourceOkLab.z()) < 0.015f);

    const QColor translucentSource(180, 100, 80, 128);
    src.fromQColor(translucentSource);
    dst.fromQColor(QColor(20, 30, 40, 0));
    op->composite(dst.data(), cs->pixelSize(), src.data(), cs->pixelSize(),
                  nullptr, 0, 1, 1, 1.0f);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(dst.data()), cs->pixelSize()),
             QByteArray(reinterpret_cast<const char *>(src.data()), cs->pixelSize()));
}

void KoRgbU8ColorSpaceTester::testOklchCompositeOpBatches()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    constexpr qint32 rows = 5;
    constexpr qint32 columns = 7;
    constexpr qint32 pixelSize = 4;
    constexpr qint32 sourceStride = columns * pixelSize + 8;
    constexpr qint32 destinationStride = columns * pixelSize + 12;
    constexpr qint32 maskStride = columns + 3;

    QByteArray source(sourceStride * rows, char(0x5a));
    QByteArray initialDestination(destinationStride * rows, char(0x6b));
    QByteArray mask(maskStride * rows, char(0));

    for (qint32 row = 0; row < rows; ++row) {
        for (qint32 column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            quint8 *sourcePixel = reinterpret_cast<quint8 *>(source.data())
                + row * sourceStride + column * pixelSize;
            quint8 *destinationPixel = reinterpret_cast<quint8 *>(initialDestination.data())
                + row * destinationStride + column * pixelSize;

            sourcePixel[0] = quint8(19 * index + 23);
            sourcePixel[1] = quint8(43 * index + 71);
            sourcePixel[2] = quint8(67 * index + 11);
            sourcePixel[3] = index % 6 == 0 ? 0 : (index % 5 == 0 ? 255 : quint8(41 + 5 * index));

            destinationPixel[0] = quint8(29 * index + 101);
            destinationPixel[1] = quint8(53 * index + 17);
            destinationPixel[2] = quint8(31 * index + 197);
            destinationPixel[3] = index % 7 == 0 ? 0 : (index % 4 == 0 ? 255 : quint8(53 + 4 * index));

            reinterpret_cast<quint8 *>(mask.data())[row * maskStride + column] =
                index % 5 == 0 ? 0 : (index % 3 == 0 ? 255 : quint8(37 + 6 * index));
        }
    }

    QBitArray partialChannels(pixelSize, true);
    partialChannels.setBit(1, false);
    QBitArray alphaLockedChannels(pixelSize, true);
    alphaLockedChannels.setBit(ALPHA_CHANNEL, false);

    const QList<QString> operationIds {
        COMPOSITE_OVER_OKLCH,
        COMPOSITE_OVER_OKLCH_HYBRID,
        COMPOSITE_COLOR_OKLCH,
    };
    const QList<QBitArray> channelSelections {
        QBitArray(),
        partialChannels,
        alphaLockedChannels,
    };
    const QStringList channelSelectionNames {
        QStringLiteral("all channels"),
        QStringLiteral("partial channels"),
        QStringLiteral("alpha locked"),
    };

    for (const QString &operationId : operationIds) {
        const KoCompositeOp *op = cs->compositeOp(operationId);
        QVERIFY2(op, qPrintable(operationId));

        for (int channelSelection = 0;
             channelSelection < channelSelections.size();
             ++channelSelection) {
            const QBitArray &channelFlags = channelSelections[channelSelection];
            for (const bool constantSource : {false, true}) {
                QByteArray batchedDestination = initialDestination;
                QByteArray rowwiseDestination = initialDestination;

                KoCompositeOp::ParameterInfo params;
                params.dstRowStart = reinterpret_cast<quint8 *>(batchedDestination.data());
                params.dstRowStride = destinationStride;
                params.srcRowStart = reinterpret_cast<const quint8 *>(source.constData());
                params.srcRowStride = constantSource ? 0 : sourceStride;
                params.maskRowStart = reinterpret_cast<const quint8 *>(mask.constData());
                params.maskRowStride = maskStride;
                params.rows = rows;
                params.cols = columns;
                params.opacity = 0.73f;
                params.flow = 1.0f;
                params.channelFlags = channelFlags;
                op->composite(params);

                params.rows = 1;
                for (qint32 row = 0; row < rows; ++row) {
                    params.dstRowStart = reinterpret_cast<quint8 *>(rowwiseDestination.data())
                        + row * destinationStride;
                    params.srcRowStart = reinterpret_cast<const quint8 *>(source.constData())
                        + (constantSource ? 0 : row * sourceStride);
                    params.maskRowStart = reinterpret_cast<const quint8 *>(mask.constData())
                        + row * maskStride;
                    op->composite(params);
                }

                int firstMismatch = -1;
                for (int byte = 0; byte < batchedDestination.size(); ++byte) {
                    if (batchedDestination[byte] != rowwiseDestination[byte]) {
                        firstMismatch = byte;
                        break;
                    }
                }
                if (firstMismatch >= 0) {
                    QFAIL(qPrintable(QStringLiteral(
                                         "%1, %2, constant source: %3, byte %4: %5 != %6")
                                         .arg(operationId)
                                         .arg(channelSelectionNames[channelSelection])
                                         .arg(constantSource ? QStringLiteral("yes")
                                                             : QStringLiteral("no"))
                                         .arg(firstMismatch)
                                         .arg(quint8(batchedDestination[firstMismatch]))
                                         .arg(quint8(rowwiseDestination[firstMismatch]))));
                }
            }
        }
    }
}

void KoRgbU8ColorSpaceTester::testOklchSrgbFastPath()
{
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    QCOMPARE(cs->profile()->name(), QStringLiteral("sRGB-elle-V2-srgbtrc.icc"));
    QCOMPARE(cs->profile()->getColorPrimaries(), PRIMARIES_ITU_R_BT_709_5);
    QCOMPARE(cs->profile()->getTransferCharacteristics(), TRC_IEC_61966_2_1);

    const KoColorSpace *xyzColorSpace = KoColorSpaceRegistry::instance()->colorSpace(
        XYZAColorModelID.id(), Float32BitsColorDepthID.id(), QString());
    QVERIFY(xyzColorSpace);

    const auto intent = KoColorConversionTransformation::IntentRelativeColorimetric;
    const auto flags = KoColorConversionTransformation::adjustmentConversionFlags();
    QScopedPointer<KoColorConversionTransformation> toXyz(
        cs->createColorConverter(xyzColorSpace, intent, flags));
    QScopedPointer<KoColorConversionTransformation> fromXyz(
        xyzColorSpace->createColorConverter(cs, intent, flags));
    QVERIFY(toXyz);
    QVERIFY(fromXyz);

    const QList<QColor> colors {
        QColor(0, 0, 0),
        QColor(255, 255, 255),
        QColor(255, 0, 0),
        QColor(0, 255, 0),
        QColor(0, 0, 255),
        QColor(127, 127, 127),
        QColor(31, 113, 227),
        QColor(219, 83, 146),
    };
    const QList<float> opacities {0.17f, 0.5f, 0.83f};

    const QList<QPair<QString, bool>> modes {
        {COMPOSITE_OVER_OKLCH, false},
        {COMPOSITE_OVER_OKLCH_HYBRID, true},
    };

    for (const auto &mode : modes) {
        const KoCompositeOp *op = cs->compositeOp(mode.first);
        QVERIFY2(op, qPrintable(mode.first));
        for (const QColor &sourceColor : colors) {
            for (const QColor &destinationColor : colors) {
                for (const float opacity : opacities) {
                    KoColor source(sourceColor, cs);
                    KoColor actual(destinationColor, cs);
                    KoColor expected(destinationColor, cs);

                    op->composite(actual.data(), cs->pixelSize(), source.data(), cs->pixelSize(),
                                  nullptr, 0, 1, 1, opacity);

                    KoXyzF32Traits::Pixel sourceXyz;
                    KoXyzF32Traits::Pixel destinationXyz;
                    toXyz->transform(source.data(), reinterpret_cast<quint8 *>(&sourceXyz), 1);
                    toXyz->transform(expected.data(), reinterpret_cast<quint8 *>(&destinationXyz), 1);

                    const auto toVector = [](const KoXyzF32Traits::Pixel &pixel) {
                        return QVector4D(pixel.x, pixel.y, pixel.z, 0.0f);
                    };
                    const QVector4D sourceOkLab = KoGamutMappingUtils::XYZToOkLab(
                        toVector(sourceXyz));
                    const QVector4D destinationOkLab = KoGamutMappingUtils::XYZToOkLab(
                        toVector(destinationXyz));
                    const quint8 integerOpacity = KoColorSpaceMaths<float, quint8>::scaleToA(opacity);
                    const float amount = KoColorSpaceMaths<quint8, float>::scaleToA(integerOpacity);
                    const QVector4D resultOkLab = mode.second
                        ? KoOklchCompositeUtils::interpolateAntipodalHybrid(
                            KoOklchCompositeUtils::prepareAntipodalHybridEndpoint(destinationOkLab),
                            KoOklchCompositeUtils::prepareAntipodalHybridEndpoint(sourceOkLab),
                            amount)
                        : KoGamutMappingUtils::interpolateOkLch(
                            KoOklchCompositeUtils::okLabToOkLchIgnoringProfileNoise(destinationOkLab),
                            KoOklchCompositeUtils::okLabToOkLchIgnoringProfileNoise(sourceOkLab),
                            amount);
                    const QVector4D resultXyz = KoGamutMappingUtils::OkLabToXYZ(resultOkLab);
                    const KoXyzF32Traits::Pixel referenceXyz {
                        resultXyz.x(), resultXyz.y(), resultXyz.z(), 1.0f};
                    fromXyz->transform(
                        reinterpret_cast<const quint8 *>(&referenceXyz), expected.data(), 1);

                    for (int channel = 0; channel < ALPHA_CHANNEL; ++channel) {
                        const int difference = qAbs(
                            int(actual.data()[channel]) - int(expected.data()[channel]));
                        QVERIFY2(
                            difference <= 2,
                            qPrintable(QStringLiteral(
                                           "%1: %2 -> %3 at %4, channel %5 differs by %6")
                                           .arg(mode.first)
                                           .arg(destinationColor.name(), sourceColor.name())
                                           .arg(opacity)
                                           .arg(channel)
                                           .arg(difference)));
                    }
                    QCOMPARE(actual.data()[ALPHA_CHANNEL], expected.data()[ALPHA_CHANNEL]);
                }
            }
        }
    }
}

void KoRgbU8ColorSpaceTester::testCompositeOpsWithChannelFlags()
{
    const KoColorSpace* cs = KoColorSpaceRegistry::instance()->rgb8();
    QList<KoCompositeOp*> ops = cs->compositeOps();

    Q_FOREACH (const KoCompositeOp *op, ops) {
        /**
         * ALPHA_DARKEN composite op doesn't take channel
         * flags into account, so just skip it
         */
        if (op->id() == COMPOSITE_ALPHA_DARKEN) continue;
        if (op->id() == COMPOSITE_DISSOLVE) continue;

        quint8 src[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badDst[] = {12,12,12,0};

        KoCompositeOp::ParameterInfo params;
        params.maskRowStart  = 0;
        params.dstRowStride  = 0;
        params.srcRowStride  = 0;
        params.maskRowStride = 0;
        params.rows          = 1;
        params.cols          = 1;
        params.opacity       = 1.0f;
        params.flow          = 1.0f;

        QBitArray channelFlags(4, true);
        channelFlags[2] = false;
        params.channelFlags  = channelFlags;

        params.srcRowStart   = src;

        params.dstRowStart   = goodDst;
        op->composite(params);

        params.dstRowStart   = badDst;
        op->composite(params);

        /**
         * The badDst has zero alpha, so the channels should be zeroed
         * before increasing alpha of the pixel
         */
        if (badDst[3] != 0 && badDst[2] != 0) {
            qDebug() << op->id()
                     << "easy case:" << goodDst[2]
                     << "difficult case:" << badDst[2];

            qDebug() << "The composite op has failed to erase the color "
                "channel which was hidden by zero alpha.";
            qDebug() << "Expected Blue channel:" << 0;
            qDebug() << "Actual Blue channel:  " << badDst[2];

            QFAIL("Failed to erase color channel");
        }
    }
}

// for posix_memalign()
#include <stdlib.h>

#if defined Q_OS_WIN
#define MEMALIGN_ALLOC(p, a, s) ((*(p)) = _aligned_malloc((s), (a)), *(p) ? 0 : errno)
#define MEMALIGN_FREE(p) _aligned_free((p))
#else
#define MEMALIGN_ALLOC(p, a, s) posix_memalign((p), (a), (s))
#define MEMALIGN_FREE(p) free((p))
#endif

void KoRgbU8ColorSpaceTester::testCompositeCopyDivisionByZero()
{
    const KoColorSpace* cs = KoColorSpaceRegistry::instance()->rgb8();
    const KoCompositeOp *op = cs->compositeOp(COMPOSITE_COPY);

    const int pixelAlignment = sizeof(float) * 16; // 512-bit alignment should be enough for everyone! (c)
    const int numPixels = 256;
    void *src = 0;
    void *dst = 0;

    int result = 0;

    result = MEMALIGN_ALLOC(&src, pixelAlignment, cs->pixelSize() * numPixels);
    KIS_ASSERT(!result);

    result = MEMALIGN_ALLOC(&dst, pixelAlignment, cs->pixelSize() * numPixels);
    KIS_ASSERT(!result);

    // generate buffers in misaligned manner
    int numTestablePixels = numPixels - 1;
    quint8 * srcPtr = reinterpret_cast<quint8*>(src) + cs->pixelSize();
    quint8 * dstPtr = reinterpret_cast<quint8*>(dst) + cs->pixelSize();


    auto testBadPixel = [cs, op, srcPtr, dstPtr, numTestablePixels] (int badPixelPos,
            const quint8 *goodSrc,
            const quint8 *goodDst,
            const quint8 *badSrc,
            const quint8 *badDst,
            quint8 opacity,
            quint8 *expectedDst) {

        quint8 *badPixelDstPtr = dstPtr + badPixelPos * cs->pixelSize();

        for (int i = 0; i < numTestablePixels; i++) {
            if (i != badPixelPos) {
                memcpy(srcPtr + cs->pixelSize() * i, goodSrc, cs->pixelSize());
                memcpy(dstPtr + cs->pixelSize() * i, goodDst, cs->pixelSize());
            } else {
                memcpy(srcPtr + cs->pixelSize() * i, badSrc, cs->pixelSize());
                memcpy(dstPtr + cs->pixelSize() * i, badDst, cs->pixelSize());
            }
        }

        const qreal opacityF = opacity / 255.0;
        op->composite(dstPtr, numTestablePixels * cs->pixelSize(),
                      srcPtr, numTestablePixels * cs->pixelSize(),
                      0, 0,
                      1, numTestablePixels, opacityF);

        if (memcmp(badPixelDstPtr, expectedDst, cs->pixelSize()) != 0) {
            qDebug() << "badPixelPos" << badPixelPos;
            qDebug() << "opacity" << opacity;
            qDebug() << "oriS" << badSrc[0] << badSrc[1] << badSrc[2] << badSrc[3];
            qDebug() << "oriD" << badDst[0] << badDst[1] << badDst[2] << badDst[3];
            qDebug() << "expD" << expectedDst[0] << expectedDst[1] << expectedDst[2] << expectedDst[3];
            qDebug() << "dst1" << badPixelDstPtr[0] << badPixelDstPtr[1] << badPixelDstPtr[2] << badPixelDstPtr[3];
#if !defined(XSIMD_NO_SUPPORTED_ARCHITECTURE)
            QFAIL("Failed to compose pixels");
#else
            qWarning() << "Skipping failed test when xsimd library is not used";
#endif
        }
    };

    /**
     * This test is supposed to catch irregularities between vector and scalar versions
     * of the composite op. In vector version we handle division be zero in a relaxed
     * way, so it some cases the content of the color channels may be different from
     * the one of the scalar versions of the algorithm. It is only allowed when the
     * pixel's alpha is null. In such case Krita considers pixel state as "undefined",
     * so any value is considered okay.
     */

    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,129};
        quint8 badDst[] = {12,12,12,0};
        quint8 *expectedDst = badSrc;
        testBadPixel(1, goodSrc, goodDst, badSrc, badDst, 255, expectedDst);
    }

    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,129};
        quint8 badDst[] = {12,12,12,0};
        quint8 *expectedDst = badSrc;
        testBadPixel(10, goodSrc, goodDst, badSrc, badDst, 255, expectedDst);
    }


    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,129};
        quint8 badDst[] = {12,12,12,0};
        quint8 expectedDst[] = {128,128,128,65};
        testBadPixel(1, goodSrc, goodDst, badSrc, badDst, 128, expectedDst);
    }

    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,129};
        quint8 badDst[] = {12,12,12,0};
        quint8 expectedDst[] = {128,128,128,65};
        testBadPixel(10, goodSrc, goodDst, badSrc, badDst, 128, expectedDst);
    }

    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,0};
        quint8 badDst[] = {12,12,12,0};
        quint8 expectedDst[] = {0,0,0,0}; // NOTE: the pixel has been changed, even though visually it hasn't (due to zero alpha)
        testBadPixel(1, goodSrc, goodDst, badSrc, badDst, 128, expectedDst);
    }

    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,0};
        quint8 badDst[] = {12,12,12,0};
        quint8 expectedDst[] = {255,255,255,0}; // NOTE: the result is different from the scalar version, but we don't care, since alpha is still zero
        testBadPixel(10, goodSrc, goodDst, badSrc, badDst, 128, expectedDst);
    }

    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,0};
        quint8 badDst[] = {12,12,12,127};
        quint8 expectedDst[] = {12,12,12,63};
        testBadPixel(1, goodSrc, goodDst, badSrc, badDst, 128, expectedDst);
    }

    {
        quint8 goodSrc[] = {128,128,128,129};
        quint8 goodDst[] = {10,10,10,11};
        quint8 badSrc[] = {128,128,128,0};
        quint8 badDst[] = {12,12,12,127};
        quint8 expectedDst[] = {12,12,12,63};
        testBadPixel(10, goodSrc, goodDst, badSrc, badDst, 128, expectedDst);
    }
}

KISTEST_MAIN(KoRgbU8ColorSpaceTester)
