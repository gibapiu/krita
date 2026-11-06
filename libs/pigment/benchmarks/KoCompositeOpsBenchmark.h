/*
 *  SPDX-FileCopyrightText: 2010 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KO_COMPOSITEOPS_BENCHMARK_H_
#define KO_COMPOSITEOPS_BENCHMARK_H_

#include <QObject>

class KoCompositeOp;

class KoCompositeOpsBenchmark : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init();
    
    void initTestCase();
    void cleanupTestCase();
    
    void benchmarkCompositeOver();
    void benchmarkCompositeColor();
    void benchmarkCompositeOverOklch();
    void benchmarkCompositeOverOklchHybrid();
    void benchmarkCompositeColorOklch();
    void benchmarkCompositeAlphaDarkenHard();
    void benchmarkCompositeAlphaDarkenCreamy();
    void benchmarkNormalOklchWorkloads_data();
    void benchmarkNormalOklchWorkloads();
    void benchmarkColorOklchWorkloads_data();
    void benchmarkColorOklchWorkloads();

private:
    void compositeImage(const KoCompositeOp *compositeOp, bool constantSource);
    void benchmarkCompositeWorkload();
    void prepareCompositeWorkload(int maskPattern,
                                  int activePixelPeriod,
                                  bool opaqueDestination,
                                  bool constantSource);

    quint8 * m_dstBuffer;
    quint8 * m_srcBuffer;
    quint8 * m_mskBuffer;
        

};

#endif
