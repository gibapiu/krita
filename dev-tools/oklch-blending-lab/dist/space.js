const SAMPLE_COUNT = 129;
const POWERLESS_CHROMA = 0.0001;
const PI = Math.PI;
const TWO_PI = 2 * PI;

const algorithms = {
  srgb: { name: "Encoded-sRGB source-over", short: "sRGB", color: "#8d98a4", active: false },
  oklab: { name: "Cartesian Oklab chord", short: "Oklab", color: "#f5f7fa", active: true },
  raw: { name: "Raw OKLCH shorter arc", short: "Raw OKLCH", color: "#ff4db8", active: true },
  current: { name: "Krita current lightness toe", short: "Current", color: "#ff9f43", active: true },
  cofb: { name: "Continuous Oklab fallback", short: "COFb", color: "#8d7dff", active: false },
  hybrid: { name: "Radial toe + COFb", short: "Hybrid", color: "#29d3c2", active: true },
  endpoint: { name: "Endpoint-aware hybrid", short: "Endpoint hybrid", color: "#a5e14b", active: true },
  antipodal: { name: "Endpoint + antipodal-gated hybrid", short: "Antipodal hybrid", color: "#4fa8ff", active: true },
  rotating: { name: "Clockwise bow rotation · L bulge", short: "CW rotation", color: "#ff6f61", active: true },
};

const state = {
  rgbA: [0, 255, 250],
  rgbB: [231, 235, 0],
  mapping: "minde",
  reference: "oklab",
  sigma: 0.19,
  endpointSigma: 0.02,
  angleStart: 160,
  shellOpacity: 0.38,
  showOriginal: true,
  active: new Set(Object.entries(algorithms).filter(([, value]) => value.active).map(([key]) => key)),
  camera: { yaw: -0.73, pitch: -0.28, zoom: 1 },
  paths: new Map(),
  screenPathPoints: [],
};

const query = new URLSearchParams(location.search);
const queryA = parseHex(query.get("a") || "");
const queryB = parseHex(query.get("b") || "");
if (queryA) state.rgbA = queryA;
if (queryB) state.rgbB = queryB;
if (["none", "clip", "radial", "minde", "soft"].includes(query.get("mapping"))) state.mapping = query.get("mapping");
if (query.has("angle")) state.angleStart = Math.max(120, Math.min(179, Number(query.get("angle"))));

const canvas = document.querySelector("#gamutCanvas");
const context = canvas.getContext("2d");
const metricsBody = document.querySelector("#metricsBody");
const readout = document.querySelector("#pathReadout");
const colorA = document.querySelector("#colorA");
const colorB = document.querySelector("#colorB");
const hexA = document.querySelector("#hexA");
const hexB = document.querySelector("#hexB");
const pathSigma = document.querySelector("#pathSigma");
const endpointSigma = document.querySelector("#endpointSigma");
const angleStart = document.querySelector("#angleStart");

document.querySelector("#mappingSelect").value = state.mapping;
angleStart.value = String(state.angleStart);
document.querySelector("#angleStartOutput").value = `${state.angleStart}°`;

const clamp01 = value => Math.max(0, Math.min(1, value));
const mix = (a, b, amount) => a + (b - a) * amount;
const mix3 = (a, b, amount) => [mix(a[0], b[0], amount), mix(a[1], b[1], amount), mix(a[2], b[2], amount)];
const subtract3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add3 = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const scale3 = (value, scale) => [value[0] * scale, value[1] * scale, value[2] * scale];
const dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross3 = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
const length3 = value => Math.hypot(value[0], value[1], value[2]);

function normalized3(value) {
  const length = length3(value);
  return length > 1e-12 ? scale3(value, 1 / length) : [0, 0, 0];
}

function rotateAroundAxis(value, axis, angle) {
  const cosine = Math.cos(angle);
  const sine = Math.sin(angle);
  return add3(
    add3(scale3(value, cosine), scale3(cross3(axis, value), sine)),
    scale3(axis, dot3(axis, value) * (1 - cosine)));
}

function decodeSrgb8(value) {
  value /= 255;
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
}

function encodeLinear(value) {
  value = clamp01(value);
  return 255 * (value <= 0.0031308 ? 12.92 * value : 1.055 * value ** (1 / 2.4) - 0.055);
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

function rgb8ToOklab(rgb) {
  return linearRgbToOklab(rgb.map(decodeSrgb8));
}

function oklabToLinearRgb([lightness, a, b]) {
  let l = lightness + 0.3963377774 * a + 0.2158037573 * b;
  let m = lightness - 0.1055613458 * a - 0.0638541728 * b;
  let s = lightness - 0.0894841775 * a - 1.291485548 * b;
  l = l * l * l;
  m = m * m * m;
  s = s * s * s;
  return [
    4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
    -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s,
  ];
}

function labToLch([lightness, a, b]) {
  const chroma = Math.hypot(a, b);
  let hue = Math.atan2(b, a);
  if (hue < 0) hue += TWO_PI;
  return { lightness, chroma, hue, powerless: chroma <= POWERLESS_CHROMA };
}

function lchToLab({ lightness, chroma, hue, powerless = false }) {
  if (powerless || chroma === 0) return [lightness, 0, 0];
  return [lightness, chroma * Math.cos(hue), chroma * Math.sin(hue)];
}

function inGamut(lab) {
  return oklabToLinearRgb(lab).every(channel => channel >= -2e-5 && channel <= 1.00002);
}

function deltaE(one, two) {
  return Math.hypot(one[0] - two[0], one[1] - two[1], one[2] - two[2]);
}

function rgbCssFromLab(lab, alpha = 1) {
  const rgb = oklabToLinearRgb(lab).map(value => Math.round(encodeLinear(value)));
  return `rgba(${rgb[0]}, ${rgb[1]}, ${rgb[2]}, ${alpha})`;
}

function huePair(from, to) {
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
  const [fromHue, toHue] = huePair(from, to);
  const lightness = mix(from.lightness, to.lightness, amount);
  const chroma = mix(from.chroma, to.chroma, amount);
  const hue = mix(fromHue, toHue, amount);
  return { lab: [lightness, chroma * Math.cos(hue), chroma * Math.sin(hue)], chroma };
}

function directedPolarInterpolate(fromLab, toLab, amount, direction) {
  const from = labToLch(fromLab);
  const to = labToLch(toLab);
  let fromHue = from.hue;
  let toHue = to.hue;
  if (from.powerless) fromHue = to.powerless ? 0 : toHue;
  if (to.powerless) toHue = fromHue;

  let delta = toHue - fromHue;
  if (direction > 0 && delta < 0) delta += TWO_PI;
  if (direction < 0 && delta > 0) delta -= TWO_PI;

  const lightness = mix(from.lightness, to.lightness, amount);
  const chroma = mix(from.chroma, to.chroma, amount);
  const hue = fromHue + delta * amount;
  return [lightness, chroma * Math.cos(hue), chroma * Math.sin(hue)];
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
  const transformed = applyToe(lab[0]);
  if (Math.abs(lab[0]) < 1e-12) return [transformed, 0, 0];
  const scale = transformed / lab[0];
  return [transformed, lab[1] * scale, lab[2] * scale];
}

function unradialize(lab) {
  const lightness = removeToe(lab[0]);
  if (Math.abs(lab[0]) < 1e-12) return [lightness, 0, 0];
  const scale = lightness / lab[0];
  return [lightness, lab[1] * scale, lab[2] * scale];
}

function cofb(fromLab, toLab, amount, sigma, endpointGate = 0, polarMultiplier = 1) {
  const polar = polarInterpolate(fromLab, toLab, amount);
  const cartesian = mix3(fromLab, toLab, amount);
  let weight = polar.chroma / (polar.chroma + sigma);
  if (endpointGate > 0) {
    const minimumEndpointChroma = Math.min(Math.hypot(fromLab[1], fromLab[2]), Math.hypot(toLab[1], toLab[2]));
    weight *= minimumEndpointChroma / (minimumEndpointChroma + endpointGate);
  }
  weight *= polarMultiplier;
  return [polar.lab[0], mix(cartesian[1], polar.lab[1], weight), mix(cartesian[2], polar.lab[2], weight)];
}

function smootherstep(amount) {
  amount = clamp01(amount);
  return amount * amount * amount * (amount * (amount * 6 - 15) + 10);
}

function antipodalGate(fromLab, toLab) {
  const from = labToLch(fromLab);
  const to = labToLch(toLab);
  if (from.powerless || to.powerless) return 1;
  const separation = Math.abs(Math.atan2(Math.sin(to.hue - from.hue), Math.cos(to.hue - from.hue)));
  const start = state.angleStart * PI / 180;
  if (separation <= start) return 1;
  return 1 - smootherstep((separation - start) / (PI - start));
}

function clockwiseBowInterpolate(fromLab, toLab, amount) {
  const from = labToLch(fromLab);
  const to = labToLch(toLab);
  if (from.powerless || to.powerless) return polarInterpolate(fromLab, toLab, amount).lab;

  let counterclockwiseSeparation = to.hue - from.hue;
  if (counterclockwiseSeparation < 0) counterclockwiseSeparation += TWO_PI;
  const start = state.angleStart * PI / 180;
  const end = TWO_PI - start;

  if (counterclockwiseSeparation <= start) {
    return directedPolarInterpolate(fromLab, toLab, amount, 1);
  }
  if (counterclockwiseSeparation >= end) {
    return directedPolarInterpolate(fromLab, toLab, amount, -1);
  }

  const rotationAmount = smootherstep((counterclockwiseSeparation - start) / (end - start));
  const positiveArc = directedPolarInterpolate(fromLab, toLab, amount, 1);
  const negativeArc = directedPolarInterpolate(fromLab, toLab, amount, -1);
  const chordPoint = mix3(fromLab, toLab, amount);
  const chordAxis = normalized3(subtract3(toLab, fromLab));
  const positiveOffset = subtract3(positiveArc, chordPoint);
  const negativeOffset = subtract3(negativeArc, chordPoint);
  const positiveParallel = dot3(positiveOffset, chordAxis);
  const negativeParallel = dot3(negativeOffset, chordAxis);
  const positivePerpendicular = subtract3(positiveOffset, scale3(chordAxis, positiveParallel));
  const negativePerpendicular = subtract3(negativeOffset, scale3(chordAxis, negativeParallel));
  const positiveRadius = length3(positivePerpendicular);
  const negativeRadius = length3(negativePerpendicular);

  if (positiveRadius < 1e-10 || negativeRadius < 1e-10 || length3(chordAxis) < 1e-10) {
    return mix3(positiveArc, negativeArc, rotationAmount);
  }

  const positiveDirection = scale3(positivePerpendicular, 1 / positiveRadius);
  const negativeDirection = scale3(negativePerpendicular, 1 / negativeRadius);
  let signedAngle = Math.atan2(
    dot3(chordAxis, cross3(positiveDirection, negativeDirection)),
    dot3(positiveDirection, negativeDirection));
  if (signedAngle > 0) signedAngle -= TWO_PI;

  const rotatedDirection = rotateAroundAxis(positiveDirection, chordAxis, signedAngle * rotationAmount);
  const perpendicular = scale3(rotatedDirection, mix(positiveRadius, negativeRadius, rotationAmount));
  const parallel = scale3(chordAxis, mix(positiveParallel, negativeParallel, rotationAmount));
  return add3(chordPoint, add3(perpendicular, parallel));
}

function sampleAlgorithm(id, rgbA, rgbB, labA, labB, amount) {
  switch (id) {
    case "srgb": return rgb8ToOklab(mix3(rgbA, rgbB, amount));
    case "oklab": return mix3(labA, labB, amount);
    case "raw": return polarInterpolate(labA, labB, amount).lab;
    case "current": {
      const result = polarInterpolate(labA, labB, amount).lab;
      if (labA[0] < TOE_BOUNDARY || labB[0] < TOE_BOUNDARY) {
        result[0] = removeToe(mix(applyToe(labA[0]), applyToe(labB[0]), amount));
      }
      return result;
    }
    case "cofb": return cofb(labA, labB, amount, state.sigma);
    case "hybrid": return unradialize(cofb(radialize(labA), radialize(labB), amount, state.sigma));
    case "endpoint": return unradialize(cofb(radialize(labA), radialize(labB), amount, state.sigma, state.endpointSigma));
    case "antipodal": {
      const transformedA = radialize(labA);
      const transformedB = radialize(labB);
      return unradialize(cofb(
        transformedA,
        transformedB,
        amount,
        state.sigma,
        state.endpointSigma,
        antipodalGate(transformedA, transformedB)));
    }
    case "rotating": return clockwiseBowInterpolate(labA, labB, amount);
    default: throw new Error(`Unknown algorithm ${id}`);
  }
}

function maxChromaAt(lightness, hue) {
  if (lightness <= 0 || lightness >= 1) return 0;
  let low = 0;
  let high = 0.5;
  const candidate = chroma => lchToLab({ lightness, chroma, hue });
  while (inGamut(candidate(high)) && high < 1.5) high *= 1.5;
  for (let iteration = 0; iteration < 17; iteration++) {
    const middle = (low + high) / 2;
    if (inGamut(candidate(middle))) low = middle;
    else high = middle;
  }
  return low;
}

function componentClip(lab) {
  return linearRgbToOklab(oklabToLinearRgb(lab).map(clamp01));
}

function radialClamp(lab) {
  if (inGamut(lab)) return lab;
  const lch = labToLch(lab);
  lch.chroma = Math.min(lch.chroma, maxChromaAt(lch.lightness, lch.hue));
  lch.powerless = lch.chroma <= POWERLESS_CHROMA;
  return lchToLab(lch);
}

function softRadialCompression(lab) {
  const lch = labToLch(lab);
  if (lch.lightness <= 0 || lch.lightness >= 1 || lch.chroma <= 0) return radialClamp(lab);
  const maximum = maxChromaAt(lch.lightness, lch.hue);
  const knee = maximum * 0.85;
  if (lch.chroma <= knee || maximum <= knee) return lab;
  const span = maximum - knee;
  lch.chroma = knee + span * (1 - Math.exp(-(lch.chroma - knee) / span));
  return lchToLab(lch);
}

function localMinde(lab) {
  if (inGamut(lab)) return lab;
  const origin = labToLch(lab);
  if (origin.lightness >= 1) return [1, 0, 0];
  if (origin.lightness <= 0) return [0, 0, 0];
  const JND = 0.02;
  const epsilon = 0.0001;
  let clipped = componentClip(lab);
  let difference = deltaE(clipped, lab);
  if (difference < JND) return clipped;
  let min = 0;
  let max = origin.chroma;
  let minInGamut = true;

  while (max - min > epsilon) {
    const chroma = (min + max) / 2;
    const current = lchToLab({ ...origin, chroma, powerless: chroma <= POWERLESS_CHROMA });
    if (minInGamut && inGamut(current)) {
      min = chroma;
      continue;
    }
    clipped = componentClip(current);
    difference = deltaE(clipped, current);
    if (difference < JND) {
      if (JND - difference < epsilon) return clipped;
      minInGamut = false;
      min = chroma;
    } else {
      max = chroma;
    }
  }
  return clipped;
}

function mapLab(lab) {
  switch (state.mapping) {
    case "none": return lab;
    case "clip": return componentClip(lab);
    case "radial": return radialClamp(lab);
    case "minde": return localMinde(lab);
    case "soft": return softRadialCompression(lab);
    default: return lab;
  }
}

function buildPath(id) {
  const labA = rgb8ToOklab(state.rgbA);
  const labB = rgb8ToOklab(state.rgbB);
  const original = [];
  const mapped = [];
  for (let index = 0; index < SAMPLE_COUNT; index++) {
    const amount = index / (SAMPLE_COUNT - 1);
    const lab = sampleAlgorithm(id, state.rgbA, state.rgbB, labA, labB, amount);
    original.push(lab);
    mapped.push(mapLab(lab));
  }
  return { original, mapped };
}

function pathLength(points) {
  let length = 0;
  for (let index = 1; index < points.length; index++) length += deltaE(points[index - 1], points[index]);
  return length;
}

function calculateMetrics(path, reference) {
  let preMapOut = 0;
  let postMapOut = 0;
  let altered = 0;
  let mapTotal = 0;
  let mapMaximum = 0;
  let referenceTotal = 0;
  for (let index = 0; index < SAMPLE_COUNT; index++) {
    if (!inGamut(path.original[index])) preMapOut++;
    if (!inGamut(path.mapped[index])) postMapOut++;
    const mappingDelta = deltaE(path.original[index], path.mapped[index]);
    if (mappingDelta > 0.001) altered++;
    mapTotal += mappingDelta;
    mapMaximum = Math.max(mapMaximum, mappingDelta);
    referenceTotal += deltaE(path.mapped[index], reference.mapped[index]);
  }
  const chord = deltaE(path.mapped[0], path.mapped.at(-1));
  return {
    preMapOut: 100 * preMapOut / SAMPLE_COUNT,
    postIn: 100 * (SAMPLE_COUNT - postMapOut) / SAMPLE_COUNT,
    altered: 100 * altered / SAMPLE_COUNT,
    meanMap: mapTotal / SAMPLE_COUNT,
    maxMap: mapMaximum,
    meanReference: referenceTotal / SAMPLE_COUNT,
    stretch: chord > 1e-8 ? pathLength(path.mapped) / chord : 1,
  };
}

function buildAllPaths() {
  state.paths.clear();
  for (const id of Object.keys(algorithms)) state.paths.set(id, buildPath(id));
}

function generateGamutSurface(subdivisions = 24) {
  const points = [];
  const addPoint = linearRgb => {
    const lab = linearRgbToOklab(linearRgb);
    const encoded = linearRgb.map(channel => Math.round(encodeLinear(channel)));
    points.push({ lab, color: `rgb(${encoded[0]} ${encoded[1]} ${encoded[2]})` });
  };
  for (let fixedChannel = 0; fixedChannel < 3; fixedChannel++) {
    const variable = [0, 1, 2].filter(channel => channel !== fixedChannel);
    for (const fixedValue of [0, 1]) {
      for (let first = 0; first <= subdivisions; first++) {
        for (let second = 0; second <= subdivisions; second++) {
          const rgb = [0, 0, 0];
          rgb[fixedChannel] = fixedValue;
          rgb[variable[0]] = first / subdivisions;
          rgb[variable[1]] = second / subdivisions;
          addPoint(rgb);
        }
      }
    }
  }
  return points;
}

const gamutSurface = generateGamutSurface();

function resizeCanvas() {
  const bounds = canvas.getBoundingClientRect();
  const ratio = Math.min(2, window.devicePixelRatio || 1);
  const width = Math.max(1, Math.round(bounds.width * ratio));
  const height = Math.max(1, Math.round(bounds.height * ratio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return { width: bounds.width, height: bounds.height };
}

function worldFromLab([lightness, a, b]) {
  return { x: a / 0.36, y: (lightness - 0.5) * 1.8, z: b / 0.36 };
}

function projectLab(lab, viewport) {
  const world = worldFromLab(lab);
  const cosineYaw = Math.cos(state.camera.yaw);
  const sineYaw = Math.sin(state.camera.yaw);
  const cosinePitch = Math.cos(state.camera.pitch);
  const sinePitch = Math.sin(state.camera.pitch);
  const xYaw = cosineYaw * world.x + sineYaw * world.z;
  const zYaw = -sineYaw * world.x + cosineYaw * world.z;
  const yPitch = cosinePitch * world.y - sinePitch * zYaw;
  const zPitch = sinePitch * world.y + cosinePitch * zYaw;
  const distance = 2.8 / state.camera.zoom;
  const factor = Math.min(viewport.width, viewport.height) * 1.05 / Math.max(1.25, distance - zPitch);
  return { x: viewport.width / 2 + xYaw * factor, y: viewport.height / 2 - yPitch * factor, depth: zPitch, factor };
}

function drawLine3D(points, viewport, style, width = 1, dash = []) {
  if (points.length < 2) return;
  context.beginPath();
  points.forEach((point, index) => {
    const projected = projectLab(point, viewport);
    if (index === 0) context.moveTo(projected.x, projected.y);
    else context.lineTo(projected.x, projected.y);
  });
  context.strokeStyle = style;
  context.lineWidth = width;
  context.setLineDash(dash);
  context.stroke();
  context.setLineDash([]);
}

function drawCoordinateGuides(viewport) {
  context.save();
  for (const lightness of [0.25, 0.5, 0.75]) {
    const ring = [];
    for (let index = 0; index <= 64; index++) {
      const angle = TWO_PI * index / 64;
      ring.push([lightness, 0.34 * Math.cos(angle), 0.34 * Math.sin(angle)]);
    }
    drawLine3D(ring, viewport, "rgba(180, 194, 202, .11)", 1);
  }
  drawLine3D([[0, 0, 0], [1, 0, 0]], viewport, "rgba(230, 238, 241, .62)", 1.3);
  drawLine3D([[0.5, -0.38, 0], [0.5, 0.38, 0]], viewport, "rgba(255, 110, 110, .46)", 1);
  drawLine3D([[0.5, 0, -0.38], [0.5, 0, 0.38]], viewport, "rgba(110, 170, 255, .46)", 1);
  context.font = "700 11px ui-monospace, monospace";
  for (const [label, lab, color] of [
    ["L", [1.04, 0, 0], "#edf1f3"], ["+a", [0.5, 0.42, 0], "#ff8f8f"], ["+b", [0.5, 0, 0.42], "#ffd474"],
  ]) {
    const point = projectLab(lab, viewport);
    context.fillStyle = color;
    context.fillText(label, point.x + 3, point.y - 3);
  }
  context.restore();
}

function drawArrow(from, to, color) {
  const angle = Math.atan2(to.y - from.y, to.x - from.x);
  const size = 7;
  context.beginPath();
  context.moveTo(to.x, to.y);
  context.lineTo(to.x - size * Math.cos(angle - 0.55), to.y - size * Math.sin(angle - 0.55));
  context.lineTo(to.x - size * Math.cos(angle + 0.55), to.y - size * Math.sin(angle + 0.55));
  context.closePath();
  context.fillStyle = color;
  context.fill();
}

function render3D() {
  const viewport = resizeCanvas();
  context.clearRect(0, 0, viewport.width, viewport.height);
  drawCoordinateGuides(viewport);

  if (state.shellOpacity > 0) {
    const projectedSurface = gamutSurface.map(point => ({ ...projectLab(point.lab, viewport), color: point.color }));
    projectedSurface.sort((one, two) => one.depth - two.depth);
    context.save();
    context.globalAlpha = state.shellOpacity;
    for (const point of projectedSurface) {
      const size = Math.max(1.1, Math.min(2.6, point.factor * 0.018));
      context.fillStyle = point.color;
      context.fillRect(point.x - size / 2, point.y - size / 2, size, size);
    }
    context.restore();
  }

  state.screenPathPoints = [];
  for (const [id, path] of state.paths) {
    if (!state.active.has(id)) continue;
    const definition = algorithms[id];
    if (state.showOriginal && state.mapping !== "none") {
      drawLine3D(path.original, viewport, `${definition.color}70`, 1.4, [4, 4]);
    }
    drawLine3D(path.mapped, viewport, definition.color, 3);
    const projected = path.mapped.map((point, index) => ({ ...projectLab(point, viewport), id, index, lab: point, original: path.original[index] }));
    state.screenPathPoints.push(...projected);
    for (const index of [32, 64, 96]) drawArrow(projected[index - 2], projected[index], definition.color);
  }

  const endpoints = [
    { lab: rgb8ToOklab(state.rgbA), rgb: state.rgbA, label: "A" },
    { lab: rgb8ToOklab(state.rgbB), rgb: state.rgbB, label: "B" },
  ];
  for (const endpoint of endpoints) {
    const point = projectLab(endpoint.lab, viewport);
    context.beginPath();
    context.arc(point.x, point.y, 7, 0, TWO_PI);
    context.fillStyle = `rgb(${endpoint.rgb.join(" ")})`;
    context.fill();
    context.strokeStyle = "#fff";
    context.lineWidth = 2;
    context.stroke();
    context.fillStyle = "#fff";
    context.font = "800 11px ui-monospace, monospace";
    context.fillText(endpoint.label, point.x + 10, point.y - 7);
  }
}

function renderPathStrip(canvasElement, points) {
  const ratio = Math.min(2, window.devicePixelRatio || 1);
  const width = 170;
  const height = 16;
  canvasElement.width = width * ratio;
  canvasElement.height = height * ratio;
  const stripContext = canvasElement.getContext("2d");
  stripContext.setTransform(ratio, 0, 0, ratio, 0, 0);
  for (let x = 0; x < width; x++) {
    const index = Math.round(x / (width - 1) * (points.length - 1));
    stripContext.fillStyle = rgbCssFromLab(points[index]);
    stripContext.fillRect(x, 0, 1, height);
  }
}

function metricClass(value, goodThreshold) {
  return value <= goodThreshold ? "metric-good" : "metric-warn";
}

function renderMetrics() {
  const reference = state.paths.get(state.reference);
  metricsBody.replaceChildren();
  for (const [id, path] of state.paths) {
    if (!state.active.has(id)) continue;
    const definition = algorithms[id];
    const metrics = calculateMetrics(path, reference);
    const row = document.createElement("tr");
    row.innerHTML = `
      <td><span class="metric-name" style="--path-color:${definition.color}"><span class="metric-dot"></span>${definition.short}</span></td>
      <td><canvas aria-label="Rendered color strip for ${definition.name}"></canvas></td>
      <td class="${metricClass(metrics.preMapOut, 0.01)}">${metrics.preMapOut.toFixed(1)}%</td>
      <td class="${metricClass(100 - metrics.postIn, 0.01)}">${metrics.postIn.toFixed(1)}%</td>
      <td class="${metricClass(metrics.altered, 0.01)}">${metrics.altered.toFixed(1)}%</td>
      <td>${metrics.meanMap.toFixed(4)}</td>
      <td>${metrics.maxMap.toFixed(4)}</td>
      <td>${metrics.meanReference.toFixed(4)}</td>
      <td>${metrics.stretch.toFixed(3)}×</td>`;
    metricsBody.append(row);
    renderPathStrip(row.querySelector("canvas"), path.mapped);
  }
}

function formatColorInfo(rgb) {
  const lch = labToLch(rgb8ToOklab(rgb));
  return `rgb ${rgb.join(" ")}\nL ${lch.lightness.toFixed(4)} · C ${lch.chroma.toFixed(4)}\nh ${(lch.hue * 180 / PI).toFixed(1)}°`;
}

function updateEndpointControls() {
  const toHex = rgb => `#${rgb.map(channel => channel.toString(16).padStart(2, "0")).join("")}`;
  const valueA = toHex(state.rgbA);
  const valueB = toHex(state.rgbB);
  colorA.value = valueA;
  colorB.value = valueB;
  hexA.value = valueA;
  hexB.value = valueB;
  document.querySelector("#colorAInfo").textContent = formatColorInfo(state.rgbA);
  document.querySelector("#colorBInfo").textContent = formatColorInfo(state.rgbB);
}

function parseHex(value) {
  const match = value.trim().match(/^#?([0-9a-f]{6})$/i);
  if (!match) return null;
  return [0, 2, 4].map(index => Number.parseInt(match[1].slice(index, index + 2), 16));
}

function recompute() {
  buildAllPaths();
  render3D();
  renderMetrics();
  updateEndpointControls();
}

function buildAlgorithmToggles() {
  const container = document.querySelector("#algorithmToggles");
  for (const [id, definition] of Object.entries(algorithms)) {
    const label = document.createElement("label");
    label.className = "algorithm-toggle";
    label.style.setProperty("--path-color", definition.color);
    label.innerHTML = `<input type="checkbox" ${state.active.has(id) ? "checked" : ""}><span class="path-swatch"></span><span>${definition.name}</span>`;
    label.querySelector("input").addEventListener("change", event => {
      if (event.target.checked) state.active.add(id);
      else state.active.delete(id);
      render3D();
      renderMetrics();
    });
    container.append(label);
  }
}

function setEndpoint(which, rgb) {
  if (!rgb) return;
  state[which === "A" ? "rgbA" : "rgbB"] = rgb;
  recompute();
}

colorA.addEventListener("input", () => setEndpoint("A", parseHex(colorA.value)));
colorB.addEventListener("input", () => setEndpoint("B", parseHex(colorB.value)));
hexA.addEventListener("change", () => setEndpoint("A", parseHex(hexA.value)) || updateEndpointControls());
hexB.addEventListener("change", () => setEndpoint("B", parseHex(hexB.value)) || updateEndpointControls());
document.querySelector("#swapColors").addEventListener("click", () => {
  [state.rgbA, state.rgbB] = [state.rgbB, state.rgbA];
  recompute();
});
document.querySelectorAll(".presets button").forEach(button => {
  button.addEventListener("click", () => {
    state.rgbA = parseHex(button.dataset.a);
    state.rgbB = parseHex(button.dataset.b);
    recompute();
  });
});

document.querySelector("#mappingSelect").addEventListener("change", event => { state.mapping = event.target.value; recompute(); });
document.querySelector("#referenceSelect").addEventListener("change", event => { state.reference = event.target.value; renderMetrics(); });
pathSigma.addEventListener("input", () => {
  state.sigma = Number(pathSigma.value);
  document.querySelector("#pathSigmaOutput").value = state.sigma.toFixed(2);
  recompute();
});
endpointSigma.addEventListener("input", () => {
  state.endpointSigma = Number(endpointSigma.value);
  document.querySelector("#endpointSigmaOutput").value = state.endpointSigma.toFixed(3);
  recompute();
});
angleStart.addEventListener("input", () => {
  state.angleStart = Number(angleStart.value);
  document.querySelector("#angleStartOutput").value = `${state.angleStart}°`;
  recompute();
});
document.querySelector("#shellOpacity").addEventListener("input", event => { state.shellOpacity = Number(event.target.value) / 100; render3D(); });
document.querySelector("#showOriginal").addEventListener("change", event => { state.showOriginal = event.target.checked; render3D(); });

document.querySelectorAll("[data-view]").forEach(button => {
  button.addEventListener("click", () => {
    document.querySelectorAll("[data-view]").forEach(item => item.classList.toggle("active", item === button));
    if (button.dataset.view === "top") Object.assign(state.camera, { yaw: 0, pitch: -PI / 2 + 0.001, zoom: 1 });
    else if (button.dataset.view === "front") Object.assign(state.camera, { yaw: 0, pitch: 0, zoom: 1 });
    else Object.assign(state.camera, { yaw: -0.73, pitch: -0.28, zoom: 1 });
    render3D();
  });
});

let dragging = false;
let previousPointer = null;
canvas.addEventListener("pointerdown", event => {
  dragging = true;
  previousPointer = { x: event.clientX, y: event.clientY };
  canvas.setPointerCapture(event.pointerId);
});
canvas.addEventListener("pointermove", event => {
  if (dragging && previousPointer) {
    state.camera.yaw += (event.clientX - previousPointer.x) * 0.008;
    state.camera.pitch = Math.max(-1.52, Math.min(1.52, state.camera.pitch + (event.clientY - previousPointer.y) * 0.008));
    previousPointer = { x: event.clientX, y: event.clientY };
    render3D();
    return;
  }
  const bounds = canvas.getBoundingClientRect();
  const x = event.clientX - bounds.left;
  const y = event.clientY - bounds.top;
  let nearest = null;
  let nearestDistance = 90;
  for (const point of state.screenPathPoints) {
    const distance = (point.x - x) ** 2 + (point.y - y) ** 2;
    if (distance < nearestDistance) {
      nearest = point;
      nearestDistance = distance;
    }
  }
  if (!nearest) {
    readout.textContent = "Hover a path to inspect its direction and gamut state.";
    return;
  }
  const lch = labToLch(nearest.lab);
  const originalInGamut = inGamut(nearest.original);
  const mapDelta = deltaE(nearest.original, nearest.lab);
  readout.textContent = `${algorithms[nearest.id].name} · t ${(nearest.index / (SAMPLE_COUNT - 1)).toFixed(3)} · L ${lch.lightness.toFixed(4)} · C ${lch.chroma.toFixed(4)} · h ${(lch.hue * 180 / PI).toFixed(1)}° · pre-map ${originalInGamut ? "in gamut" : "OUT"} · map ΔE ${mapDelta.toFixed(4)}`;
});
canvas.addEventListener("pointerup", event => { dragging = false; previousPointer = null; canvas.releasePointerCapture(event.pointerId); });
canvas.addEventListener("pointercancel", () => { dragging = false; previousPointer = null; });
canvas.addEventListener("wheel", event => {
  event.preventDefault();
  state.camera.zoom = Math.max(0.6, Math.min(2.2, state.camera.zoom * Math.exp(-event.deltaY * 0.001)));
  render3D();
}, { passive: false });

new ResizeObserver(render3D).observe(canvas);
buildAlgorithmToggles();
recompute();
