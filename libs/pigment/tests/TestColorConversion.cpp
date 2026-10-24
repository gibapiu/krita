/*
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "TestColorConversion.h"
#include "KoColorConversions.h"
#include "KoGamutMappingUtils.h"

#include <simpletest.h>

#include <cmath>

void TestColorConversion::testRGBHSV()
{
    float r, g, b, h, s, v;

    RGBToHSV(1, 0, 0, &h, &s, &v);
    QCOMPARE(h, 0.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(v, 1.0f);

    RGBToHSV(1, 1, 0, &h, &s, &v);
    QCOMPARE(h, 60.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(v, 1.0f);

    RGBToHSV(0, 1, 0, &h, &s, &v);
    QCOMPARE(h, 120.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(v, 1.0f);

    RGBToHSV(0, 1, 1, &h, &s, &v);
    QCOMPARE(h, 180.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(v, 1.0f);

    RGBToHSV(0, 0, 1, &h, &s, &v);
    QCOMPARE(h, 240.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(v, 1.0f);

    RGBToHSV(1, 0, 1, &h, &s, &v);
    QCOMPARE(h, 300.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(v, 1.0f);

    RGBToHSV(0, 0, 0, &h, &s, &v);
    QCOMPARE(h, -1.0f);
    QCOMPARE(s, 0.0f);
    QCOMPARE(v, 0.0f);

    RGBToHSV(1, 1, 1, &h, &s, &v);
    QCOMPARE(h, -1.0f);
    QCOMPARE(s, 0.0f);
    QCOMPARE(v, 1.0f);

    RGBToHSV(0.5, 0.25, 0.75, &h, &s, &v);
    QCOMPARE(h, 270.0f);
    QCOMPARE(s, 0.666667f);
    QCOMPARE(v, 0.75f);

    HSVToRGB(0, 1, 1, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 0.0f);

    HSVToRGB(60, 1, 1, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 0.0f);

    HSVToRGB(120, 1, 1, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 0.0f);

    HSVToRGB(180, 1, 1, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 1.0f);

    HSVToRGB(240, 1, 1, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 1.0f);

    HSVToRGB(300, 1, 1, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 1.0f);

    HSVToRGB(-1, 0, 0, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 0.0f);

    HSVToRGB(-1, 0, 1, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 1.0f);

    HSVToRGB(270, 0.666667f, 0.75f, &r, &g, &b);
    QCOMPARE(r, 0.5f);
    QCOMPARE(g, 0.25f);
    QCOMPARE(b, 0.75f);
}

void TestColorConversion::testRGBHSL()
{
    float r, g, b, h, s, l;

    RGBToHSL(1, 0, 0, &h, &s, &l);
    QCOMPARE(h, 0.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(l, 0.5f);

    RGBToHSL(1, 1, 0, &h, &s, &l);
    QCOMPARE(h, 60.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(l, 0.5f);

    RGBToHSL(0, 1, 0, &h, &s, &l);
    QCOMPARE(h, 120.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(l, 0.5f);

    RGBToHSL(0, 1, 1, &h, &s, &l);
    QCOMPARE(h, 180.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(l, 0.5f);

    RGBToHSL(0, 0, 1, &h, &s, &l);
    QCOMPARE(h, 240.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(l, 0.5f);

    RGBToHSL(1, 0, 1, &h, &s, &l);
    QCOMPARE(h, 300.0f);
    QCOMPARE(s, 1.0f);
    QCOMPARE(l, 0.5f);

    RGBToHSL(0, 0, 0, &h, &s, &l);
    QCOMPARE(h, -1.0f);
    QCOMPARE(s, 0.0f);
    QCOMPARE(l, 0.0f);

    RGBToHSL(1, 1, 1, &h, &s, &l);
    QCOMPARE(h, -1.0f);
    QCOMPARE(s, 0.0f);
    QCOMPARE(l, 1.0f);

    RGBToHSL(0.5, 0.25, 0.75, &h, &s, &l);
    QCOMPARE(h, 270.0f);
    QCOMPARE(s, 0.5f);
    QCOMPARE(l, 0.5f);

    HSLToRGB(0, 1, 0.5, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 0.0f);

    HSLToRGB(60, 1, 0.5, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 0.0f);

    HSLToRGB(120, 1, 0.5, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 0.0f);

    HSLToRGB(180, 1, 0.5, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 1.0f);

    HSLToRGB(240, 1, 0.5, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 1.0f);

    HSLToRGB(300, 1, 0.5, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 1.0f);

    HSLToRGB(-1, 0, 0, &r, &g, &b);
    QCOMPARE(r, 0.0f);
    QCOMPARE(g, 0.0f);
    QCOMPARE(b, 0.0f);

    HSLToRGB(-1, 0, 1, &r, &g, &b);
    QCOMPARE(r, 1.0f);
    QCOMPARE(g, 1.0f);
    QCOMPARE(b, 1.0f);

    HSLToRGB(270, 0.5, 0.5, &r, &g, &b);
    QCOMPARE(r, 0.5f);
    QCOMPARE(g, 0.25f);
    QCOMPARE(b, 0.75f);
}

void TestColorConversion::testOkLchInterpolation()
{
    constexpr float pi = 3.14159265358979323846f;

    const auto fromLch = [pi](float lightness, float chroma, float hueDegrees) {
        const float hue = hueDegrees * pi / 180.0f;
        return QVector4D(lightness, chroma * std::cos(hue), chroma * std::sin(hue), 0.0f);
    };

    const QVector4D from = fromLch(0.4f, 0.1f, 350.0f);
    const QVector4D to = fromLch(0.8f, 0.3f, 10.0f);
    const QVector4D midpoint = KoGamutMappingUtils::interpolateOkLch(from, to, 0.5f);

    QVERIFY(qAbs(midpoint.x() - 0.6f) < 1e-6f);
    QVERIFY(qAbs(midpoint.y() - 0.2f) < 1e-6f);
    QVERIFY(qAbs(midpoint.z()) < 1e-6f);

    const QVector4D precomputedMidpoint = KoGamutMappingUtils::interpolateOkLch(
        KoGamutMappingUtils::OkLabToOkLch(from),
        KoGamutMappingUtils::OkLabToOkLch(to),
        0.5f);
    QVERIFY((precomputedMidpoint - midpoint).length() < 1e-6f);

    const QVector4D gray(0.4f, 0.0f, 0.0f, 0.0f);
    const QVector4D blue = fromLch(0.8f, 0.2f, 240.0f);
    const QVector4D grayToBlue = KoGamutMappingUtils::interpolateOkLch(gray, blue, 0.5f);
    const QVector4D expectedGrayToBlue = fromLch(0.6f, 0.1f, 240.0f);

    QVERIFY((grayToBlue - expectedGrayToBlue).length() < 1e-6f);
    QCOMPARE(KoGamutMappingUtils::interpolateOkLch(from, to, 0.0f), from);
    QCOMPARE(KoGamutMappingUtils::interpolateOkLch(from, to, 1.0f), to);

    const QVector4D xyz(0.25f, 0.4f, 0.1f, 0.0f);
    const QVector4D xyzRoundTrip = KoGamutMappingUtils::OkLabToXYZ(
        KoGamutMappingUtils::XYZToOkLab(xyz));
    QVERIFY((xyzRoundTrip - xyz).length() < 2e-6f);
}

QTEST_GUILESS_MAIN(TestColorConversion)
