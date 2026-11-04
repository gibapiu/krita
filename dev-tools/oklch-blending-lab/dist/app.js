const BASE_WIDTH = 128;
const BASE_HEIGHT = 88;
const POWERLESS_CHROMA = 0.0001;
const TWO_PI = 2 * Math.PI;
const PI = Math.PI;

const swatches = [
  { id: "A", kind: "linear", color: [0, 255, 250], label: "cyan → black" },
  { id: "B", kind: "linear", color: [255, 140, 0], label: "orange → black" },
  { id: "C", kind: "linear", color: [195, 0, 255], label: "purple → black" },
  { id: "D", kind: "linear", color: [255, 255, 255], label: "white → black" },
  { id: "E", kind: "radial", color: [255, 0, 0], label: "red center → black" },
  { id: "F", kind: "radial", color: [0, 255, 0], label: "green center → black" },
  { id: "G", kind: "radial", color: [0, 0, 255], label: "blue center → black" },
  { id: "J", kind: "radial", color: [255, 255, 255], label: "white center → black" },
];

const methodInfo = {
  browser: { title: "Browser normal", detail: "Encoded-sRGB source-over; the actual CSS normal-compositing baseline." },
  cssOklab: { title: "CSS / Oklab", detail: "Cartesian Oklab interpolation with CSS Color 4 Local MINDE gamut mapping." },
  rawOklch: { title: "Raw OKLCH", detail: "Shorter hue arc with component clipping at display conversion." },
  kritaCurrent: { title: "Krita current patch", detail: "Raw polar OKLCH, then lightness-only CIELAB cubic toe." },
  cofb: { title: "Continuous Oklab fallback", detail: "Raw OKLCH blended toward Cartesian Oklab using a chroma gate." },
  hybrid: { title: "Radial toe + COFb", detail: "Black-aware radial coordinates and continuous Cartesian fallback; hard display clip." },
  hybridMinde: { title: "Radial toe + COFb + Local MINDE", detail: "The hybrid followed by CSS Color 4 perceptual chroma-reduction mapping." },
  endpointHybridMinde: { title: "Endpoint-aware hybrid + Local MINDE", detail: "The hybrid with an extra continuous endpoint-neutral gate (σe=0.02)." },
};

const state = {
  testCase: "case1",
  opacity: 0.5,
  sigma: 0.19,
  scale: 1,
  activeMethods: new Set(["browser", "cssOklab", "kritaCurrent", "hybrid", "hybridMinde", "endpointHybridMinde"]),
  token: 0,
};

const query = new URLSearchParams(location.search);
if (["case1", "case2", "stacked"].includes(query.get("case"))) state.testCase = query.get("case");
if (query.has("opacity")) state.opacity = Math.max(0, Math.min(1, Number(query.get("opacity"))));
if (query.has("sigma")) state.sigma = Math.max(0.02, Math.min(0.5, Number(query.get("sigma"))));
if ([1, 2].includes(Number(query.get("scale")))) state.scale = Number(query.get("scale"));

const rows = document.querySelector("#methodRows");
const workspace = document.querySelector(".workspace");
const caseSelect = document.querySelector("#caseSelect");
const opacityInput = document.querySelector("#opacityInput");
const opacityOutput = document.querySelector("#opacityOutput");
const sigmaInput = document.querySelector("#sigmaInput");
const sigmaOutput = document.querySelector("#sigmaOutput");
const resolutionSelect = document.querySelector("#resolutionSelect");

caseSelect.value = state.testCase;
opacityInput.value = String(Math.round(state.opacity * 100));
opacityOutput.value = `${opacityInput.value}%`;
sigmaInput.value = state.sigma.toFixed(2);
sigmaOutput.value = state.sigma.toFixed(2);
resolutionSelect.value = String(state.scale);

const clamp01 = value => Math.max(0, Math.min(1, value));
const mix = (a, b, t) => a + (b - a) * t;
const mix3 = (a, b, t) => [mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)];

function decodeSrgb(value) {
  value /= 255;
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
}

function encodeSrgb(value) {
  value = clamp01(value);
  const encoded = value <= 0.0031308 ? 12.92 * value : 1.055 * value ** (1 / 2.4) - 0.055;
  return Math.round(255 * encoded);
}

function rgb8ToOklab(rgb) {
  return linearRgbToOklab(rgb.map(decodeSrgb));
}

function linearRgbToOklab([red, green, blue]) {
  const l = Math.cbrt(0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue);
  const m = Math.cbrt(0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue);
  const s = Math.cbrt(0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue);
  return [
    0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s,
  ];
}

function oklabToLinearRgb([lightness, a, b]) {
  let l = lightness + 0.3963377774 * a + 0.2158037573 * b;
  let m = lightness - 0.1055613458 * a - 0.0638541728 * b;
  let s = lightness - 0.0894841775 * a - 1.291485548 * b;
  l *= l * l;
  m *= m * m;
  s *= s * s;
  return [
    4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
    -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s,
  ];
}

function labToLch([lightness, a, b]) {
  const chroma = Math.hypot(a, b);
  return { lightness, chroma, hue: Math.atan2(b, a), powerless: chroma <= POWERLESS_CHROMA };
}

function lchToLab({ lightness, chroma, hue, powerless }) {
  if (powerless || chroma === 0) return [lightness, 0, 0];
  return [lightness, chroma * Math.cos(hue), chroma * Math.sin(hue)];
}

function hueEndpoints(from, to) {
  let fromHue = from.hue;
  let toHue = to.hue;
  if (from.powerless) fromHue = to.powerless ? 0 : toHue;
  if (to.powerless) toHue = fromHue;
  if (toHue - fromHue > PI) fromHue += TWO_PI;
  else if (toHue - fromHue < -PI) toHue += TWO_PI;
  return [fromHue, toHue];
}

function polarInterpolate(fromLab, toLab, amount) {
  const from = labToLch(fromLab);
  const to = labToLch(toLab);
  const [fromHue, toHue] = hueEndpoints(from, to);
  const lightness = mix(from.lightness, to.lightness, amount);
  const chroma = mix(from.chroma, to.chroma, amount);
  const hue = mix(fromHue, toHue, amount);
  return { lab: [lightness, chroma * Math.cos(hue), chroma * Math.sin(hue)], chroma };
}

const TOE_BOUNDARY = 6 / 29;
const TOE_OUTPUT_BOUNDARY = 0.08;
const TOE_SCALE = 24389 / 2700;

function applyToe(lightness) {
  return lightness <= TOE_BOUNDARY ? TOE_SCALE * lightness ** 3 : 1.16 * lightness - 0.16;
}

function removeToe(lightness) {
  return lightness <= TOE_OUTPUT_BOUNDARY ? Math.cbrt(lightness / TOE_SCALE) : (lightness + 0.16) / 1.16;
}

function radialize(lab) {
  const lightness = lab[0];
  const transformed = applyToe(lightness);
  if (Math.abs(lightness) < 1e-12) return [transformed, 0, 0];
  const scale = transformed / lightness;
  return [transformed, lab[1] * scale, lab[2] * scale];
}

function unradialize(lab) {
  const transformed = lab[0];
  const lightness = removeToe(transformed);
  if (Math.abs(transformed) < 1e-12) return [lightness, 0, 0];
  const scale = lightness / transformed;
  return [lightness, lab[1] * scale, lab[2] * scale];
}

function cofb(fromLab, toLab, amount, sigma, endpointSigma = 0) {
  const polar = polarInterpolate(fromLab, toLab, amount);
  const cartesian = mix3(fromLab, toLab, amount);
  let weight = polar.chroma / (polar.chroma + sigma);
  if (endpointSigma > 0) {
    const endpointChroma = Math.min(Math.hypot(fromLab[1], fromLab[2]), Math.hypot(toLab[1], toLab[2]));
    weight *= endpointChroma / (endpointChroma + endpointSigma);
  }
  return [polar.lab[0], mix(cartesian[1], polar.lab[1], weight), mix(cartesian[2], polar.lab[2], weight)];
}

function currentKrita(fromLab, toLab, amount) {
  const result = polarInterpolate(fromLab, toLab, amount).lab;
  if (fromLab[0] < TOE_BOUNDARY || toLab[0] < TOE_BOUNDARY) {
    result[0] = removeToe(mix(applyToe(fromLab[0]), applyToe(toLab[0]), amount));
  }
  return result;
}

function hybrid(fromLab, toLab, amount, sigma) {
  return unradialize(cofb(radialize(fromLab), radialize(toLab), amount, sigma));
}

function endpointAwareHybrid(fromLab, toLab, amount, sigma) {
  return unradialize(cofb(radialize(fromLab), radialize(toLab), amount, sigma, 0.02));
}

function inGamut(linearRgb) {
  return linearRgb.every(channel => channel >= -1e-7 && channel <= 1.0000001);
}

function clippedLinear(linearRgb) {
  return linearRgb.map(clamp01);
}

function deltaEOK(oneLab, twoLab) {
  return Math.hypot(oneLab[0] - twoLab[0], oneLab[1] - twoLab[1], oneLab[2] - twoLab[2]);
}

function localMindeToLinearRgb(lab) {
  const originalLinear = oklabToLinearRgb(lab);
  if (inGamut(originalLinear)) return originalLinear;
  const origin = labToLch(lab);
  if (origin.lightness >= 1) return [1, 1, 1];
  if (origin.lightness <= 0) return [0, 0, 0];

  const JND = 0.02;
  const epsilon = 0.0001;
  let current = { ...origin };
  let clipped = clippedLinear(originalLinear);
  let difference = deltaEOK(linearRgbToOklab(clipped), lab);
  if (difference < JND) return clipped;

  let min = 0;
  let max = origin.chroma;
  let minInGamut = true;

  while (max - min > epsilon) {
    current.chroma = (min + max) / 2;
    current.powerless = current.chroma <= POWERLESS_CHROMA;
    const currentLab = lchToLab(current);
    const linear = oklabToLinearRgb(currentLab);

    if (minInGamut && inGamut(linear)) {
      min = current.chroma;
      continue;
    }

    clipped = clippedLinear(linear);
    difference = deltaEOK(linearRgbToOklab(clipped), currentLab);
    if (difference < JND) {
      if (JND - difference < epsilon) return clipped;
      minInGamut = false;
      min = current.chroma;
    } else {
      max = current.chroma;
    }
  }
  return clipped;
}

function sampleBackdrop(swatch, u, v) {
  let amount;
  if (swatch.kind === "linear") {
    amount = u;
  } else {
    amount = Math.min(1, Math.hypot(u - 0.5, v - 0.5) / Math.SQRT1_2);
  }
  return swatch.color.map(channel => Math.round(mix(channel, 0, amount)));
}

function sampleSource(testCase, u, v) {
  if (testCase === "case1") return [231, 235, 0];
  const amount = Math.min(1, Math.hypot(u, v) / Math.SQRT2);
  return [
    Math.round(mix(0, 255, amount)),
    Math.round(mix(0, 225, amount)),
    Math.round(mix(255, 0, amount)),
  ];
}

function evaluate(method, backdropRgb, sourceRgb, amount) {
  if (method === "browser") {
    const rgb = mix3(backdropRgb, sourceRgb, amount).map(Math.round);
    return { rgb, linear: rgb.map(decodeSrgb), gamut: "in gamut", excursion: 0 };
  }

  const fromLab = rgb8ToOklab(backdropRgb);
  const toLab = rgb8ToOklab(sourceRgb);
  let lab;

  switch (method) {
    case "cssOklab": lab = mix3(fromLab, toLab, amount); break;
    case "rawOklch": lab = polarInterpolate(fromLab, toLab, amount).lab; break;
    case "kritaCurrent": lab = currentKrita(fromLab, toLab, amount); break;
    case "cofb": lab = cofb(fromLab, toLab, amount, state.sigma); break;
    case "hybrid":
    case "hybridMinde": lab = hybrid(fromLab, toLab, amount, state.sigma); break;
    case "endpointHybridMinde": lab = endpointAwareHybrid(fromLab, toLab, amount, state.sigma); break;
    default: throw new Error(`Unknown method: ${method}`);
  }

  const preMap = oklabToLinearRgb(lab);
  const excursion = Math.max(0, ...preMap.map(channel => Math.max(-channel, channel - 1)));
  const mapped = method === "cssOklab" || method === "hybridMinde" || method === "endpointHybridMinde"
    ? localMindeToLinearRgb(lab)
    : clippedLinear(preMap);
  return {
    rgb: mapped.map(encodeSrgb),
    linear: mapped,
    gamut: excursion > 1e-7 ? `out by ${excursion.toFixed(4)}` : "in gamut",
    excursion,
  };
}

function evaluateStacked(method, backdrop, u, v) {
  const first = evaluate(method, backdrop, sampleSource("case1", u, v), state.opacity).rgb;
  return { result: evaluate(method, first, sampleSource("case2", u, v), state.opacity), source: sampleSource("case2", u, v), intermediate: first };
}

function renderSwatch(canvas, method, swatch) {
  const width = BASE_WIDTH * state.scale;
  const height = BASE_HEIGHT * state.scale;
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d", { alpha: false });
  const image = context.createImageData(width, height);
  const metadata = new Array(width * height);
  let outOfGamutPixels = 0;
  let maximumExcursion = 0;

  for (let y = 0; y < height; y++) {
    const v = height === 1 ? 0 : y / (height - 1);
    for (let x = 0; x < width; x++) {
      const u = width === 1 ? 0 : x / (width - 1);
      const backdrop = sampleBackdrop(swatch, u, v);
      let source;
      let result;
      let intermediate = null;

      if (state.testCase === "stacked") {
        const stacked = evaluateStacked(method, backdrop, u, v);
        source = stacked.source;
        result = stacked.result;
        intermediate = stacked.intermediate;
      } else {
        source = sampleSource(state.testCase, u, v);
        result = evaluate(method, backdrop, source, state.opacity);
      }

      const index = y * width + x;
      const offset = index * 4;
      image.data[offset] = result.rgb[0];
      image.data[offset + 1] = result.rgb[1];
      image.data[offset + 2] = result.rgb[2];
      image.data[offset + 3] = 255;
      metadata[index] = { backdrop, source, result, intermediate };
      if (result.excursion > 1e-7) outOfGamutPixels++;
      maximumExcursion = Math.max(maximumExcursion, result.excursion);
    }
  }

  context.putImageData(image, 0, 0);
  canvas._pixelMetadata = metadata;
  return { pixels: width * height, outOfGamutPixels, maximumExcursion };
}

function attachInspector(canvas, method, swatch) {
  canvas.addEventListener("pointermove", event => {
    const rect = canvas.getBoundingClientRect();
    const x = Math.max(0, Math.min(canvas.width - 1, Math.floor((event.clientX - rect.left) * canvas.width / rect.width)));
    const y = Math.max(0, Math.min(canvas.height - 1, Math.floor((event.clientY - rect.top) * canvas.height / rect.height)));
    const data = canvas._pixelMetadata?.[y * canvas.width + x];
    if (!data) return;
    document.querySelector("#inspectorTitle").textContent = `${methodInfo[method].title} · ${swatch.id} · (${x}, ${y})`;
    document.querySelector("#inspectBackdrop").textContent = data.backdrop.join(", ");
    document.querySelector("#inspectSource").textContent = data.source.join(", ");
    document.querySelector("#inspectOutput").textContent = data.result.rgb.join(", ");
    document.querySelector("#inspectGamut").textContent = data.result.gamut;
  });
}

function createMethodRow(method) {
  const info = methodInfo[method];
  const started = performance.now();
  const section = document.createElement("section");
  section.className = "method-row";
  section.innerHTML = `<div class="method-heading"><h2>${info.title}</h2><p>${info.detail}</p></div><div class="swatch-grid"></div>`;
  const grid = section.querySelector(".swatch-grid");
  let totalPixels = 0;
  let outOfGamutPixels = 0;
  let maximumExcursion = 0;

  for (const swatch of swatches) {
    const figure = document.createElement("article");
    figure.className = "swatch";
    figure.innerHTML = `<header><strong>${swatch.id}</strong><span>${swatch.label}</span></header><canvas aria-label="${info.title}, swatch ${swatch.id}: ${swatch.label}"></canvas>`;
    const canvas = figure.querySelector("canvas");
    attachInspector(canvas, method, swatch);
    const metrics = renderSwatch(canvas, method, swatch);
    totalPixels += metrics.pixels;
    outOfGamutPixels += metrics.outOfGamutPixels;
    maximumExcursion = Math.max(maximumExcursion, metrics.maximumExcursion);
    grid.append(figure);
  }
  const elapsed = performance.now() - started;
  const percent = totalPixels ? 100 * outOfGamutPixels / totalPixels : 0;
  section.querySelector(".method-heading p").textContent += ` · ${percent.toFixed(1)}% pre-map OOG · max ${maximumExcursion.toFixed(3)} · ${elapsed.toFixed(0)} ms JS`;
  return section;
}

async function renderAll() {
  const token = ++state.token;
  workspace.classList.add("busy");
  rows.replaceChildren();
  await new Promise(resolve => requestAnimationFrame(resolve));

  for (const method of Object.keys(methodInfo)) {
    if (token !== state.token) return;
    if (!state.activeMethods.has(method)) continue;
    rows.append(createMethodRow(method));
    await new Promise(resolve => requestAnimationFrame(resolve));
  }
  if (token === state.token) workspace.classList.remove("busy");
}

function scheduleRender() {
  workspace.classList.add("busy");
  clearTimeout(scheduleRender.timer);
  scheduleRender.timer = setTimeout(renderAll, 80);
}

caseSelect.addEventListener("change", () => { state.testCase = caseSelect.value; scheduleRender(); });
opacityInput.addEventListener("input", () => {
  state.opacity = Number(opacityInput.value) / 100;
  opacityOutput.value = `${opacityInput.value}%`;
  scheduleRender();
});
sigmaInput.addEventListener("input", () => {
  state.sigma = Number(sigmaInput.value);
  sigmaOutput.value = state.sigma.toFixed(2);
  scheduleRender();
});
resolutionSelect.addEventListener("change", () => { state.scale = Number(resolutionSelect.value); scheduleRender(); });

document.querySelectorAll(".method-toggle").forEach(button => {
  button.addEventListener("click", () => {
    const method = button.dataset.method;
    if (state.activeMethods.has(method)) state.activeMethods.delete(method);
    else state.activeMethods.add(method);
    button.classList.toggle("active", state.activeMethods.has(method));
    scheduleRender();
  });
});

const supportsColorMix = CSS.supports("color", "color-mix(in oklab, red, blue)");
const supportsGradient = CSS.supports("background-image", "linear-gradient(in oklab, red, blue)");
const support = document.querySelector("#supportStatus");
support.textContent = `color-mix(in oklab): ${supportsColorMix ? "yes" : "no"} · gradient(in oklab): ${supportsGradient ? "yes" : "no"}`;
support.classList.toggle("ok", supportsColorMix && supportsGradient);

renderAll();
