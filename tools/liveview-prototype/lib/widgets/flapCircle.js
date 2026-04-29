// Flap position circle widget: gray circle + rotating triangle + stop dots
// + numeric label. Mirrors displayAOA() main.cpp:783-827.

import { colors } from '../colors.js';
import { flapWidgetFrac, flapTriangleTransform } from '../flapWidget.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountFlapCircle(parent, {
  cx, cy, r,
  triangleTipR,
  arcRad,
  stopR,
  labelFontSize = 16,
}) {
  const group = mk(parent, 'g', { 'data-widget': 'flap-circle' });

  mk(group, 'circle', { cx, cy, r, fill: colors.TFT_DARKGREY });
  const triangle = mk(group, 'path', {
    fill: colors.TFT_DARKGREY, 'shape-rendering': 'crispEdges',
  });

  // Stop dots at the arc endpoints (main.cpp:807-813).
  mk(group, 'circle', {
    cx: cx + Math.cos(0) * stopR,
    cy: cy + Math.sin(0) * stopR,
    r: 1, fill: colors.TFT_WHITE,
  });
  mk(group, 'circle', {
    cx: cx + Math.cos(arcRad) * stopR,
    cy: cy + Math.sin(arcRad) * stopR,
    r: 1, fill: colors.TFT_WHITE,
  });

  // C++ draws the flap angle with FSS12 + middle_center datum at (cx, cy)
  // (main.cpp:826). dominant-baseline: central centers the text vertically
  // on its anchor y. text-anchor: middle centers horizontally.
  const label = mk(group, 'text', {
    x: cx, y: cy,
    'font-family': 'Helvetica, Arial, sans-serif',
    'font-size': labelFontSize,
    fill: colors.TFT_WHITE,
    'text-anchor': 'middle',
    'dominant-baseline': 'central',
  });
  label.textContent = '0';

  function update({ flapPos, flapsMin, flapsMax }) {
    const frac = flapWidgetFrac(flapPos, flapsMin, flapsMax);
    const angleDeg = flapTriangleTransform(frac);
    const a = angleDeg * Math.PI / 180;
    const apexX = cx + Math.cos(a) * triangleTipR;
    const apexY = cy + Math.sin(a) * triangleTipR;
    const topX  = cx + Math.sin(a) * r;
    const topY  = cy - Math.cos(a) * r;
    const botX  = cx - Math.sin(a) * r;
    const botY  = cy + Math.cos(a) * r;
    triangle.setAttribute('d', `M ${topX} ${topY} L ${apexX} ${apexY} L ${botX} ${botY} Z`);
    label.textContent = String(flapPos);
  }

  return { el: group, update };
}
