// Bank-angle arc markers (rotates with -roll). Mirrors the two
// arcGraph calls at main.cpp:1133-1171:
//
//   1. arcSize=115, arcWidth=15: BAR_LONG (white) at
//      0,180,210,240,300,330 + ARROW_OUT (yellow) at 270.
//   2. arcSize=115, arcWidth=15: BAR_SHORT (white) at 250,260,280,290
//      + ROUND_DOT (white) at 225,315.
//
// Both rings live at radius 115 — the difference is the SHAPE drawn
// at each pointer angle:
//
//   BAR_LONG    — thin radial bar from r=96.25 to r=118.75
//                 (length 22.5, thickness 3.75).
//   BAR_SHORT   — thin radial bar from r=101.875 to r=113.125
//                 (length 11.25, thickness 3.6).
//   ROUND_DOT   — filled circle at r=107.5 with diameter 7.5.
//   ARROW_OUT   — filled triangle, base at outer rim r=122.5
//                 (half-width 4.95), apex (tip) inward at r=107.5.
//
// Marker geometry comes from MarkRbar / MarkRbarShort / MarkArrowOut
// / MarkRdot in lib/GaugeWidgets/GaugeWidgets.cpp lines 1891-2090.
// fillLine in those primitives renders the colored center stroke
// PLUS a 1-pixel edge (alphaBlend565(96, TFT_BLACK, color)) on every
// side; in SVG that's the stroke around each polygon.
//
// User mapping (270° = top of dial = bank 0°):
//   270  → 0°    (yellow ARROW_OUT pointer)
//   260  → +10°  short bar (right side of horizon)
//   250  → +20°  short bar
//   240  → +30°  long bar
//   225  → +45°  round dot
//   210  → +60°  long bar
//   180  →   90° long bar (horizontal, pointing left in dial coords)
//   0    →  −90° long bar
//   330  → −60°  long bar
//   315  → −45°  round dot
//   300  → −30°  long bar
//   290  → −10°  short bar
//   280  → −20°  short bar
//
// The whole group rotates with `rotate(-roll, cx, cy)` so the
// markers stay at fixed compass-degree positions while the
// underlying horizon visibly rolls.

import { colors } from '../colors.js';
import * as G from '../geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';
const DEG = Math.PI / 180;

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

// Build a 4-corner radial bar polygon. innerR/outerR set the radial
// extent; thickness is the perpendicular width. Returns nothing — the
// polygon is appended to parent. Stroke is the edge color from the
// C++ fillLine call (alpha-blended dark version of fill).
function drawRadialBar(parent, cx, cy, innerR, outerR, thickness, angleRad, fillColor, edgeColor) {
  const cosA = Math.cos(angleRad), sinA = Math.sin(angleRad);
  // Radial axis unit vector: (cosA, sinA). Perpendicular: (sinA, -cosA).
  const half = thickness / 2;
  const ix = cx + innerR * cosA, iy = cy + innerR * sinA;  // inner-center
  const ox = cx + outerR * cosA, oy = cy + outerR * sinA;  // outer-center
  // Four corners going CCW: inner-left, outer-left, outer-right, inner-right.
  const ilx = ix + half *  sinA, ily = iy + half * -cosA;
  const olx = ox + half *  sinA, oly = oy + half * -cosA;
  const orx = ox + half * -sinA, ory = oy + half *  cosA;
  const irx = ix + half * -sinA, iry = iy + half *  cosA;
  mk(parent, 'polygon', {
    points: `${ilx},${ily} ${olx},${oly} ${orx},${ory} ${irx},${iry}`,
    fill: fillColor,
    stroke: edgeColor,
    'stroke-width': 1,
    'stroke-linejoin': 'miter',
  });
}

// MarkArrowOut: filled triangle pointing inward. Base (p2-p1-p3) at
// the outer rim r=barSize+0.5*barWidth with half-width 0.33*barWidth;
// apex (p4) at inner radius barSize-0.5*barWidth.
function drawArrowOut(parent, cx, cy, barSize, barWidth, angleRad, color) {
  const tipR  = barSize - 0.5 * barWidth;
  const baseR = barSize + 0.5 * barWidth;
  const half  = 0.33 * barWidth;
  const cosA = Math.cos(angleRad), sinA = Math.sin(angleRad);
  const p4x = cx + tipR  * cosA, p4y = cy + tipR  * sinA;
  const p1x = cx + baseR * cosA, p1y = cy + baseR * sinA;
  const p2x = p1x + half *  sinA, p2y = p1y + half * -cosA;
  const p3x = p1x + half * -sinA, p3y = p1y + half *  cosA;
  mk(parent, 'polygon', {
    points: `${p4x},${p4y} ${p3x},${p3y} ${p2x},${p2y}`,
    fill: color,
    stroke: colors.TFT_BLACK,
    'stroke-width': 1,
    'stroke-linejoin': 'miter',
  });
}

// MarkRdot: filled circle at r=barSize-0.5*barWidth with radius
// 0.25*barWidth, with a 1-pixel black outline (drawCircle in the C++).
function drawRoundDot(parent, cx, cy, barSize, barWidth, angleRad, color) {
  const r = barSize - 0.5 * barWidth;
  const dotR = 0.25 * barWidth;
  const cosA = Math.cos(angleRad), sinA = Math.sin(angleRad);
  const x = cx + r * cosA, y = cy + r * sinA;
  mk(parent, 'circle', {
    cx: x, cy: y, r: dotR, fill: color,
    stroke: colors.TFT_BLACK,
    'stroke-width': 1,
  });
}

// alphaBlend565(96, TFT_BLACK, TFT_WHITE) ≈ 38% black blend of white,
// which renders as a medium-dark grey. Hand-mixed to match the
// visual outline color the C++ produces; pure black is too harsh and
// makes the bars look bordered rather than edge-shaded.
const BAR_EDGE_WHITE = '#9f9f9f';

export function mountBankArc(parent, {
  cx = G.MODE1_HORIZON_CX,
  cy = G.MODE1_HORIZON_CY,
} = {}) {
  // The whole group rotates with -roll around (cx, cy) so the markers
  // sit at fixed dial degrees while the aircraft symbol stays put.
  const group = mk(parent, 'g', { 'data-widget': 'bank-arc' });

  // Both rings share arcSize=115, arcWidth=15.
  const ARC_R = 115;
  const ARC_W = 15;

  // BAR_LONG geometry: r ∈ [arcSize-1.25*arcWidth, arcSize+0.25*arcWidth],
  // thickness 0.25*arcWidth.
  const LONG_INNER  = ARC_R - 1.25 * ARC_W;
  const LONG_OUTER  = ARC_R + 0.25 * ARC_W;
  const LONG_THICK  = 0.25 * ARC_W;

  // BAR_SHORT geometry: r ∈ [arcSize-0.875*arcWidth, arcSize-0.125*arcWidth],
  // thickness 0.24*arcWidth.
  const SHORT_INNER = ARC_R - 0.875 * ARC_W;
  const SHORT_OUTER = ARC_R - 0.125 * ARC_W;
  const SHORT_THICK = 0.24 * ARC_W;

  // Long white bars at every 30° from 0..330 (skip 270 — that's the
  // yellow ARROW_OUT pointer slot).
  for (const deg of [0, 180, 210, 240, 300, 330]) {
    drawRadialBar(group, cx, cy, LONG_INNER, LONG_OUTER, LONG_THICK,
                  deg * DEG, colors.TFT_WHITE, BAR_EDGE_WHITE);
  }

  // Yellow ARROW_OUT triangle pointer at the top (270°).
  drawArrowOut(group, cx, cy, ARC_R, ARC_W, 270 * DEG, colors.TFT_YELLOW);

  // Short white bars at ±10°, ±20° (250, 260, 280, 290).
  for (const deg of [250, 260, 280, 290]) {
    drawRadialBar(group, cx, cy, SHORT_INNER, SHORT_OUTER, SHORT_THICK,
                  deg * DEG, colors.TFT_WHITE, BAR_EDGE_WHITE);
  }

  // Round dots at ±45° (225, 315).
  for (const deg of [225, 315]) {
    drawRoundDot(group, cx, cy, ARC_R, ARC_W, deg * DEG, colors.TFT_WHITE);
  }

  function update({ rollDeg = 0 } = {}) {
    // Counter-rotate by -roll so the markers turn with the horizon.
    group.setAttribute('transform', `rotate(${-rollDeg} ${cx} ${cy})`);
  }

  // Seed at level.
  update({ rollDeg: 0 });

  return { el: group, update };
}
