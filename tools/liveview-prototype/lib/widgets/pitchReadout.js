// Small dark rounded-rect over the horizon line with the numeric pitch
// angle (one decimal) and a tiny degree symbol.
// Mirrors main.cpp:561-573.

import { colors } from '../colors.js';
import * as G from '../geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountPitchReadout(parent, {
  x = G.MODE1_PITCH_READOUT_X,
  y = G.MODE1_PITCH_READOUT_Y,
  w = G.MODE1_PITCH_READOUT_W,
  h = G.MODE1_PITCH_READOUT_H,
  radius = G.MODE1_PITCH_READOUT_RADIUS,
  textX  = G.MODE1_PITCH_READOUT_TEXT_X,
  textY  = G.MODE1_PITCH_READOUT_TEXT_Y,
  degCx  = G.MODE1_PITCH_READOUT_DEG_CX,
  degCy  = G.MODE1_PITCH_READOUT_DEG_CY,
  degR   = G.MODE1_PITCH_READOUT_DEG_R,
  fontSize = G.MODE1_PITCH_READOUT_FONT_SIZE,
} = {}) {
  const group = mk(parent, 'g', { 'data-widget': 'pitch-readout' });

  // Dark grey rounded rect background.
  mk(group, 'rect', {
    x, y, width: w, height: h,
    rx: radius, ry: radius,
    fill: colors.TFT_DARKGREY,
  });

  // Numeric pitch — center datum at (textX, textY). textX is the
  // horizontal center of the dark rect; the digits are centered both
  // horizontally (text-anchor: middle) and vertically (central
  // baseline) inside the contrast region.
  const num = mk(group, 'text', {
    x: textX, y: textY,
    'font-family': 'Helvetica, Arial, sans-serif',
    'font-weight': 'bold',
    'font-size': fontSize,
    fill: colors.TFT_WHITE,
    'text-anchor': 'middle',
    'dominant-baseline': 'central',
  });
  num.textContent = '0.0';

  // Degree-symbol circle (drawn as ring via stroke).
  mk(group, 'circle', {
    cx: degCx, cy: degCy, r: degR,
    fill: 'none',
    stroke: colors.TFT_WHITE,
    'stroke-width': 1,
  });

  function update({ pitchDeg }) {
    num.textContent = pitchDeg.toFixed(1);
  }

  return { el: group, update };
}
