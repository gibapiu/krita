# Normal OKLCH blending lab

A buildless browser diagnostic for the dark-gradient Normal OKLCH artifacts.

Serve `dist/` over HTTP, for example:

```sh
python3 -m http.server 4173 --directory dev-tools/oklch-blending-lab/dist
```

Then open:

- `http://127.0.0.1:4173/` for the supplied 2D blend cases.
- `http://127.0.0.1:4173/space.html` for the rotatable 3D sRGB-gamut and path explorer.

The page compares browser source-over, Cartesian Oklab, raw polar OKLCH,
Krita's current lightness-only toe, continuous Oklab fallback, a combined
radial-toe/fallback path with the CSS Color 4 Local MINDE gamut mapper, and an
experimental endpoint-aware continuous gate for the paper's residual
achromatic-endpoint case.

The 3D page samples the boundary of the linear-sRGB cube into Cartesian Oklab,
supports free endpoint colors and camera rotation, and reports pre/post-map
gamut containment, mapping frequency, mapping delta-E, reference deviation,
and path stretch for each selected algorithm.

The path explorer also includes an antipodal ambiguity gate. Its quintic fade
collapses the polar contribution into the Cartesian Oklab chord as endpoint hue
separation approaches 180 degrees, then allows it to reopen on the other side
without a positional or first-derivative jump.

For comparison, the explorer includes a clockwise bow-rotation variant. It
keeps the polar bow and rotates its displacement around the oriented A-to-B
Cartesian Oklab chord while the endpoint pair crosses the antipodal window.
This preserves a continuous direction choice at the cost of a visible excursion
through the lightness dimension.

This is an exploratory visual harness, not production pigment code. The radial
toe + COFb combination and its parameters require regression tests and profiling
before translation to C++.
