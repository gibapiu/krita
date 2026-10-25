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

#include <cstring>

#include <simpletest.h>

const int TILE_WIDTH = 64;
const int TILE_HEIGHT = 64;

const int IMG_WIDTH = 2048;
const int IMG_HEIGHT = 2048;

const qreal OPACITY_HALF = 0.5;

const int TILES_IN_WIDTH = IMG_WIDTH / TILE_WIDTH;
const int TILES_IN_HEIGHT = IMG_HEIGHT / TILE_HEIGHT;

enum CompositeScenario {
    OpaqueDestination,
    SparseMask,
    ConstantSource,
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

void KoCompositeOpsBenchmark::benchmarkCompositeScenarios_data()
{
    QTest::addColumn<QString>("compositeId");
    QTest::addColumn<int>("scenario");
    QTest::addColumn<int>("activePixelPeriod");

    QTest::newRow("opaque-color") << COMPOSITE_COLOR << int(OpaqueDestination) << 0;
    QTest::newRow("opaque-color-oklch") << COMPOSITE_COLOR_OKLCH << int(OpaqueDestination) << 0;

    QTest::newRow("sparse01-normal") << COMPOSITE_OVER << int(SparseMask) << 100;
    QTest::newRow("sparse01-normal-oklch") << COMPOSITE_OVER_OKLCH << int(SparseMask) << 100;
    QTest::newRow("sparse01-color") << COMPOSITE_COLOR << int(SparseMask) << 100;
    QTest::newRow("sparse01-color-oklch") << COMPOSITE_COLOR_OKLCH << int(SparseMask) << 100;

    QTest::newRow("sparse10-normal") << COMPOSITE_OVER << int(SparseMask) << 10;
    QTest::newRow("sparse10-normal-oklch") << COMPOSITE_OVER_OKLCH << int(SparseMask) << 10;
    QTest::newRow("sparse10-color") << COMPOSITE_COLOR << int(SparseMask) << 10;
    QTest::newRow("sparse10-color-oklch") << COMPOSITE_COLOR_OKLCH << int(SparseMask) << 10;

    QTest::newRow("sparse25-normal") << COMPOSITE_OVER << int(SparseMask) << 4;
    QTest::newRow("sparse25-normal-oklch") << COMPOSITE_OVER_OKLCH << int(SparseMask) << 4;
    QTest::newRow("sparse25-color") << COMPOSITE_COLOR << int(SparseMask) << 4;
    QTest::newRow("sparse25-color-oklch") << COMPOSITE_COLOR_OKLCH << int(SparseMask) << 4;

    QTest::newRow("constant-normal") << COMPOSITE_OVER << int(ConstantSource) << 0;
    QTest::newRow("constant-normal-oklch") << COMPOSITE_OVER_OKLCH << int(ConstantSource) << 0;
    QTest::newRow("constant-color") << COMPOSITE_COLOR << int(ConstantSource) << 0;
    QTest::newRow("constant-color-oklch") << COMPOSITE_COLOR_OKLCH << int(ConstantSource) << 0;
}

void KoCompositeOpsBenchmark::benchmarkCompositeScenarios()
{
    QFETCH(QString, compositeId);
    QFETCH(int, scenario);
    QFETCH(int, activePixelPeriod);

    const KoColorSpace *colorSpace = KoColorSpaceRegistry::instance()->rgb8();
    const KoCompositeOp *compositeOp = colorSpace->compositeOp(compositeId);
    QVERIFY(compositeOp);

    if (scenario == OpaqueDestination) {
        for (int pixel = 0; pixel < IMG_WIDTH * IMG_HEIGHT; ++pixel) {
            m_dstBuffer[pixel * KoBgrU8Traits::channels_nb + KoBgrU8Traits::alpha_pos] =
                KoColorSpaceMathsTraits<quint8>::unitValue;
        }
    } else if (scenario == SparseMask) {
        const int maskSize = IMG_WIDTH * IMG_HEIGHT;
        std::memset(m_mskBuffer, 0, maskSize);
        for (int pixel = 0; pixel < maskSize; pixel += activePixelPeriod) {
            m_mskBuffer[pixel] = KoColorSpaceMathsTraits<quint8>::unitValue;
        }
    } else if (scenario == ConstantSource) {
        m_srcBuffer[KoBgrU8Traits::alpha_pos] = KoColorSpaceMathsTraits<quint8>::unitValue;
    }

    const bool constantSource = scenario == ConstantSource;
    QBENCHMARK{
        compositeImage(compositeOp, constantSource);
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
