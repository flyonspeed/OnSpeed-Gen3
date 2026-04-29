// Bank-angle arc markers (rotates with -roll). Mirrors the two
// arcGraph calls at main.cpp:1136-1171:
//
//   1. arcSize=100, arcWidth=15: BAR_LONG at 0,180,210,240,300,330,
//      ARROW_OUT (yellow) at 270.
//   2. arcSize=115, arcWidth=15: BAR_SHORT at 250,260,280,290,
//      ROUND_DOT at 225,315.
//
// The whole group transforms with `rotate(-roll, cx, cy)` so the
// markers stay at fixed degrees while the gauge appears to move under
// the aircraft symbol.
//
// Marker geometry comes from MarkRbar / MarkRbarShort / MarkArrowOut /
// MarkRdot in lib/GaugeWidgets/GaugeWidgets.cpp. Coordinate convention
// matches gaugeWidgets: angle 0 = right (+x), 90 = down (+y), 180 =
// left, 270 = up. So 270° = directly above the center.

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

// MarkRbar: long radial bar from inner radius (barSize - 1.25*barWidth)
// to outer radius (barSize + 0.25*barWidth), thickness 0.25*barWidth
// perpendicular to the radial direction.
function drawBarLong(parent, cx, cy, barSize, barWidth, angleRad, color) {
  const innerR = barSize - 1.25 * barWidth;
  const outerR = barSize + 0.25 * barWidth;
  const thickness = 0.25 * barWidth;
  const cosA = Math.cos(angleRad), sinA = Math.sin(angleRad);
  const x1 = cx + innerR * cosA, y1 = cy + innerR * sinA;
  const x2 = cx + outerR * cosA, y2 = cy + outerR * sinA;
  mk(parent, 'line', {
    x1, y1, x2, y2,
    stroke: color,
    'stroke-width': thickness,
    'stroke-linecap': 'butt',
  });
}

// MarkRbarShort: same but inner = barSize - 0.875*barWidth, outer =
// barSize - 0.125*barWidth, thickness 0.24*barWidth.
function drawBarShort(parent, cx, cy, barSize, barWidth, angleRad, color) {
  const innerR = barSize - 0.875 * barWidth;
  const outerR = barSize - 0.125 * barWidth;
  const thickness = 0.24 * barWidth;
  const cosA = Math.cos(angleRad), sinA = Math.sin(angleRad);
  const x1 = cx + innerR * cosA, y1 = cy + innerR * sinA;
  const x2 = cx + outerR * cosA, y2 = cy + outerR * sinA;
  mk(parent, 'line', {
    x1, y1, x2, y2,
    stroke: color,
    'stroke-width': thickness,
    'stroke-linecap': 'butt',
  });
}

// MarkArrowOut: filled triangle pointing inward. Apex (p4) at radius
// (barSize - 0.5*barWidth), base at (barSize + 0.5*barWidth) ± 0.33*
// barWidth perpendicular.
function drawArrowOut(parent, cx, cy, barSize, barWidth, angleRad, color) {
  const tipR  = barSize - 0.5 * barWidth;
  const baseR = barSize + 0.5 * barWidth;
  const half  = 0.33 * barWidth;
  const cosA = Math.cos(angleRad), sinA = Math.sin(angleRad);
  const p4x = cx + tipR  * cosA, p4y = cy + tipR  * sinA;
  const p1x = cx + baseR * cosA, p1y = cy + baseR * sinA;
  // Perpendicular to radial: (sin, -cos) for one side, (-sin, cos) other.
  const p2x = p1x + half *  sinA, p2y = p1y + half * -cosA;
  const p3x = p1x + half * -sinA, p3y = p1y + half *  cosA;
  mk(parent, 'polygon', {
    points: `${p4x},${p4y} ${p3x},${p3y} ${p2x},${p2y}`,
    fill: color,
    stroke: colors.TFT_BLACK,
    'stroke-width': 1,
  });
}

// MarkRdot: filled circle at radius (barSize - 0.5*barWidth) with
// radius 0.25*barWidth.
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

export function mountBankArc(parent, {
  cx = G.MODE1_HORIZON_CX,
  cy = G.MODE1_HORIZON_CY,
} = {}) {
  // The whole group rotates with -roll around (cx, cy) so the markers
  // sit at fixed dial degrees while the aircraft symbol stays put.
  const group = mk(parent, 'g', { 'data-widget': 'bank-arc' });

  // Outer ring (arcSize=100, arcWidth=15): long bars + the yellow
  // ARROW_OUT pointer at 270° (top).
  const OUTER_R = 100;
  const OUTER_W = 15;
  for (const deg of [0, 180, 210, 240, 300, 330]) {
    drawBarLong(group, cx, cy, OUTER_R, OUTER_W, deg * DEG, colors.TFT_WHITE);
  }
  drawArrowOut(group, cx, cy, OUTER_R, OUTER_W, 270 * DEG, colors.TFT_YELLOW);

  // Inner ring (arcSize=115, arcWidth=15): short bars at every 10°
  // halfway between the long bars, plus round dots at 225/315.
  const INNER_R = 115;
  const INNER_W = 15;
  for (const deg of [250, 260, 280, 290]) {
    drawBarShort(group, cx, cy, INNER_R, INNER_W, deg * DEG, colors.TFT_WHITE);
  }
  for (const deg of [225, 315]) {
    drawRoundDot(group, cx, cy, INNER_R, INNER_W, deg * DEG, colors.TFT_WHITE);
  }

  function update({ rollDeg = 0 } = {}) {
    // Counter-rotate by -roll so the markers turn with the horizon.
    group.setAttribute('transform', `rotate(${-rollDeg} ${cx} ${cy})`);
  }

  // Seed at level.
  update({ rollDeg: 0 });

  return { el: group, update };
}
