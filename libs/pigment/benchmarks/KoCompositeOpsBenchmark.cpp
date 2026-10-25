/*
 *  SPDX-FileCopyrightText: 2010 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "KoCompositeOpsBenchmark.h"

#include "../compositeops/KoCompositeOpAlphaDarken.h"
#include "../compositeops/KoCompositeOpColorOklch.h"
#include "../compositeops/KoCompositeOpOver.h"
#include "../compositeops/KoCompositeOpOverOklch.h"
#include <KoOptimizedCompositeOpFactory.h>

#include <KoColorSpaceTraits.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <simpletest.h>

const int TILE_WIDTH = 64;
const int TILE_HEIGHT = 64;

const int IMG_WIDTH = 2048;
const int IMG_HEIGHT = 2048;

const qreal OPACITY_HALF = 0.5;

const int TILES_IN_WIDTH = IMG_WIDTH / TILE_WIDTH;
const int TILES_IN_HEIGHT = IMG_HEIGHT / TILE_HEIGHT;

enum MaskPattern {
    RandomMask,
    OpaqueMask,
    ZeroMask,
    DistributedActivePixels,
    ClusteredActivePixels,
};

void KoCompositeOpsBenchmark::initTestCase()
{
    const int bufLen = IMG_HEIGHT * IMG_WIDTH * KoBgrU8Traits::pixelSize;

    m_dstBuffer = new quint8[bufLen];
    m_srcBuffer = new quint8[bufLen];
    m_mskBuffer = new quint8[IMG_HEIGHT * IMG_WIDTH];
}

// this is called before every benchmark
void KoCompositeOpsBenchmark::init()
{
    QRandomGenerator rng(42);

    for (int i = 0; i < int(IMG_WIDTH * IMG_HEIGHT * KoBgrU8Traits::pixelSize); i++) {
        const int randVal = rng.bounded(RAND_MAX);

        m_srcBuffer[i] = randVal & 0x0000FF;
        m_dstBuffer[i] = (randVal & 0x00FF000) >> 8;
    }

    for (int i = 0; i < IMG_WIDTH * IMG_HEIGHT; ++i) {
        m_mskBuffer[i] = rng.bounded(256);
    }
}


void KoCompositeOpsBenchmark::cleanupTestCase()
{
    delete [] m_dstBuffer;
    delete [] m_srcBuffer;
    delete [] m_mskBuffer;
}

void KoCompositeOpsBenchmark::benchmarkCompositeOver()
{
    KoCompositeOp *compositeOp = KoOptimizedCompositeOpFactory::createOverOp32(KoColorSpaceRegistry::instance()->rgb8());
    QBENCHMARK{
        compositeImage(compositeOp, false);
    }
}

void KoCompositeOpsBenchmark::benchmarkCompositeColor()
{
    const KoCompositeOp *compositeOp =
        KoColorSpaceRegistry::instance()->rgb8()->compositeOp(COMPOSITE_COLOR);
    QVERIFY(compositeOp);
    QBENCHMARK{
        compositeImage(compositeOp, false);
    }
}

void KoCompositeOpsBenchmark::benchmarkCompositeOverOklch()
{
    KoCompositeOpOverOklch<KoBgrU8Traits> op(KoColorSpaceRegistry::instance()->rgb8());
    KoCompositeOp *compositeOp = &op;
    QBENCHMARK{
        compositeImage(compositeOp, false);
    }
}

void KoCompositeOpsBenchmark::benchmarkCompositeColorOklch()
{
    KoCompositeOpColorOklch<KoBgrU8Traits> op(KoColorSpaceRegistry::instance()->rgb8());
    KoCompositeOp *compositeOp = &op;
    QBENCHMARK{
        compositeImage(compositeOp, false);
    }
}

void KoCompositeOpsBenchmark::benchmarkCompositeAlphaDarkenHard()
{
    KoCompositeOp *compositeOp = KoOptimizedCompositeOpFactory::createAlphaDarkenOpHard32(KoColorSpaceRegistry::instance()->rgb8());
    QBENCHMARK{
        compositeImage(compositeOp, false);
    }
}


void KoCompositeOpsBenchmark::benchmarkCompositeAlphaDarkenCreamy()
{
    KoCompositeOp *compositeOp = KoOptimizedCompositeOpFactory::createAlphaDarkenOpCreamy32(KoColorSpaceRegistry::instance()->rgb8());
    QBENCHMARK{
        compositeImage(compositeOp, false);
    }
}

static void addOklchWorkloadColumns()
{
    QTest::addColumn<QString>("compositeId");
    QTest::addColumn<int>("maskPattern");
    QTest::addColumn<int>("activePixelPeriod");
    QTest::addColumn<bool>("opaqueDestination");
    QTest::addColumn<bool>("constantSource");
}

void KoCompositeOpsBenchmark::benchmarkNormalOklchWorkloads_data()
{
    addOklchWorkloadColumns();

    QTest::newRow("random-normal")
        << COMPOSITE_OVER << int(RandomMask) << 0 << false << false;
    QTest::newRow("random-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(RandomMask) << 0 << false << false;

    QTest::newRow("opaque-mask-normal")
        << COMPOSITE_OVER << int(OpaqueMask) << 0 << false << false;
    QTest::newRow("opaque-mask-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(OpaqueMask) << 0 << false << false;

    QTest::newRow("zero-mask-normal")
        << COMPOSITE_OVER << int(ZeroMask) << 0 << false << false;
    QTest::newRow("zero-mask-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(ZeroMask) << 0 << false << false;

    QTest::newRow("distributed01-normal")
        << COMPOSITE_OVER << int(DistributedActivePixels) << 100 << false << false;
    QTest::newRow("distributed01-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(DistributedActivePixels) << 100 << false << false;

    QTest::newRow("clustered01-normal")
        << COMPOSITE_OVER << int(ClusteredActivePixels) << 100 << false << false;
    QTest::newRow("clustered01-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(ClusteredActivePixels) << 100 << false << false;

    QTest::newRow("distributed10-normal")
        << COMPOSITE_OVER << int(DistributedActivePixels) << 10 << false << false;
    QTest::newRow("distributed10-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(DistributedActivePixels) << 10 << false << false;

    QTest::newRow("distributed25-normal")
        << COMPOSITE_OVER << int(DistributedActivePixels) << 4 << false << false;
    QTest::newRow("distributed25-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(DistributedActivePixels) << 4 << false << false;

    QTest::newRow("opaque-destination-normal")
        << COMPOSITE_OVER << int(RandomMask) << 0 << true << false;
    QTest::newRow("opaque-destination-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(RandomMask) << 0 << true << false;

    QTest::newRow("constant-source-normal")
        << COMPOSITE_OVER << int(RandomMask) << 0 << false << true;
    QTest::newRow("constant-source-normal-oklch")
        << COMPOSITE_OVER_OKLCH << int(RandomMask) << 0 << false << true;
}

void KoCompositeOpsBenchmark::benchmarkNormalOklchWorkloads()
{
    benchmarkCompositeWorkload();
}

void KoCompositeOpsBenchmark::benchmarkColorOklchWorkloads_data()
{
    addOklchWorkloadColumns();

    QTest::newRow("random-color")
        << COMPOSITE_COLOR << int(RandomMask) << 0 << false << false;
    QTest::newRow("random-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(RandomMask) << 0 << false << false;

    QTest::newRow("opaque-mask-color")
        << COMPOSITE_COLOR << int(OpaqueMask) << 0 << false << false;
    QTest::newRow("opaque-mask-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(OpaqueMask) << 0 << false << false;

    QTest::newRow("zero-mask-color")
        << COMPOSITE_COLOR << int(ZeroMask) << 0 << false << false;
    QTest::newRow("zero-mask-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(ZeroMask) << 0 << false << false;

    QTest::newRow("distributed01-color")
        << COMPOSITE_COLOR << int(DistributedActivePixels) << 100 << false << false;
    QTest::newRow("distributed01-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(DistributedActivePixels) << 100 << false << false;

    QTest::newRow("clustered01-color")
        << COMPOSITE_COLOR << int(ClusteredActivePixels) << 100 << false << false;
    QTest::newRow("clustered01-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(ClusteredActivePixels) << 100 << false << false;

    QTest::newRow("distributed10-color")
        << COMPOSITE_COLOR << int(DistributedActivePixels) << 10 << false << false;
    QTest::newRow("distributed10-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(DistributedActivePixels) << 10 << false << false;

    QTest::newRow("distributed25-color")
        << COMPOSITE_COLOR << int(DistributedActivePixels) << 4 << false << false;
    QTest::newRow("distributed25-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(DistributedActivePixels) << 4 << false << false;

    QTest::newRow("opaque-destination-color")
        << COMPOSITE_COLOR << int(RandomMask) << 0 << true << false;
    QTest::newRow("opaque-destination-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(RandomMask) << 0 << true << false;

    QTest::newRow("constant-source-color")
        << COMPOSITE_COLOR << int(RandomMask) << 0 << false << true;
    QTest::newRow("constant-source-color-oklch")
        << COMPOSITE_COLOR_OKLCH << int(RandomMask) << 0 << false << true;
}

void KoCompositeOpsBenchmark::benchmarkColorOklchWorkloads()
{
    benchmarkCompositeWorkload();
}

void KoCompositeOpsBenchmark::benchmarkCompositeWorkload()
{
    QFETCH(QString, compositeId);
    QFETCH(int, maskPattern);
    QFETCH(int, activePixelPeriod);
    QFETCH(bool, opaqueDestination);
    QFETCH(bool, constantSource);

    prepareCompositeWorkload(
        maskPattern, activePixelPeriod, opaqueDestination, constantSource);

    const KoCompositeOp *compositeOp =
        KoColorSpaceRegistry::instance()->rgb8()->compositeOp(compositeId);
    QVERIFY(compositeOp);

    QBENCHMARK{
        compositeImage(compositeOp, constantSource);
    }
}

void KoCompositeOpsBenchmark::prepareCompositeWorkload(int maskPattern,
                                                       int activePixelPeriod,
                                                       bool opaqueDestination,
                                                       bool constantSource)
{
    const int pixelCount = IMG_WIDTH * IMG_HEIGHT;
    const quint8 opaque = KoColorSpaceMathsTraits<quint8>::unitValue;

    if (maskPattern == OpaqueMask) {
        std::memset(m_mskBuffer, opaque, pixelCount);
    } else if (maskPattern == ZeroMask) {
        std::memset(m_mskBuffer, 0, pixelCount);
    } else if (maskPattern == DistributedActivePixels) {
        std::memset(m_mskBuffer, 0, pixelCount);
        for (int pixel = 0; pixel < pixelCount; pixel += activePixelPeriod) {
            m_mskBuffer[pixel] = opaque;
        }
    } else if (maskPattern == ClusteredActivePixels) {
        std::memset(m_mskBuffer, 0, pixelCount);
        const int activePixelCount = pixelCount / activePixelPeriod;
        const int clusterWidth = int(std::ceil(std::sqrt(double(activePixelCount))));
        for (int row = 0, remainingPixels = activePixelCount;
             remainingPixels > 0;
             ++row) {
            const int activeColumns = std::min(clusterWidth, remainingPixels);
            std::memset(m_mskBuffer + row * IMG_WIDTH, opaque, activeColumns);
            remainingPixels -= activeColumns;
        }
    }

    if (opaqueDestination) {
        for (int pixel = 0; pixel < pixelCount; ++pixel) {
            m_dstBuffer[pixel * KoBgrU8Traits::channels_nb + KoBgrU8Traits::alpha_pos] = opaque;
        }
    }

    if (constantSource) {
        m_srcBuffer[KoBgrU8Traits::alpha_pos] = opaque;
    }
}

void KoCompositeOpsBenchmark::compositeImage(const KoCompositeOp *compositeOp, bool constantSource)
{
    const int rowStride = IMG_WIDTH * KoBgrU8Traits::pixelSize;
    const int maskRowStride = IMG_WIDTH;
    for (int y = 0; y < TILES_IN_HEIGHT; ++y) {
        for (int x = 0; x < TILES_IN_WIDTH; ++x) {
            const int bufferOffset = y * TILE_HEIGHT * rowStride
                + x * TILE_WIDTH * KoBgrU8Traits::pixelSize;
            const int maskOffset = y * TILE_HEIGHT * maskRowStride + x * TILE_WIDTH;
            const int sourceOffset = constantSource ? 0 : bufferOffset;
            const int sourceStride = constantSource ? 0 : rowStride;
            compositeOp->composite(m_dstBuffer + bufferOffset,
                                   rowStride,
                                   m_srcBuffer + sourceOffset,
                                   sourceStride,
                                   m_mskBuffer + maskOffset,
                                   maskRowStride,
                                   TILE_HEIGHT,
                                   TILE_WIDTH,
                                   OPACITY_HALF);
        }
    }
}


QTEST_GUILESS_MAIN(KoCompositeOpsBenchmark)
