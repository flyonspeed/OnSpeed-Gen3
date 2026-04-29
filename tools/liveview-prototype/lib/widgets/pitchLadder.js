// Pitch ladder: tick marks every 10° from -90..+90 plus numeric labels on
// the long ticks. Mirrors pitchGraph() at main.cpp:1284-1345.
//
// Two passes in the C++:
//   1. Short ticks for i in -85..+85 step 10 with half-width 0.10×g_arcSize
//   2. Long ticks for i in -90..+90 step 10 with half-width 0.20×g_arcSize,
//      plus a numeric label "Io" rendered at the long tick's right end
//      (offset xRotate*0.75 along the rolled axis).
//
// All ticks (and labels) move with the rolled+pitched body — same pxc/pyc
// center as horizon.js. Each tick i is offset along the roll-perpendicular
// axis by i × HEIGHT/80 (= i × 3 px) from pxc/pyc.

import { colors } from '../colors.js';
import * as G from '../geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';
const HALF_PI = Math.PI / 2;
const DEG = Math.PI / 180;

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountPitchLadder(parent, {
  cx = G.MODE1_HORIZON_CX,
  cy = G.MODE1_HORIZON_CY,
  pitchScale = G.MODE1_PITCH_HEIGHT_SCALE,
  step = G.MODE1_LADDER_STEP_DEG,
  shortHalfW = G.MODE1_LADDER_SHORT_HALF_W,
  longHalfW  = G.MODE1_LADDER_LONG_HALF_W,
  shortRange = G.MODE1_LADDER_SHORT_RANGE,
  longRange  = G.MODE1_LADDER_LONG_RANGE,
  labelOffset = G.MODE1_LADDER_LABEL_OFFSET,
  fontSize = G.MODE1_LADDER_FONT_SIZE,
} = {}) {
  const group = mk(parent, 'g', { 'data-widget': 'pitch-ladder' });

  // Build per-tick records. Each record has a `<line>` and (for long ticks)
  // a `<text>` whose position is updated each frame.
  const shortTicks = [];
  for (let i = -shortRange; i <= shortRange; i += step) {
    if (i === 0) continue;  // 0° tick is the horizon itself
    const line = mk(group, 'line', {
      x1: 0, y1: 0, x2: 0, y2: 0,
      stroke: colors.TFT_BLACK, 'stroke-width': 1,
    });
    shortTicks.push({ i, line });
  }
  const longTicks = [];
  for (let i = -longRange; i <= longRange; i += step) {
    if (i === 0) continue;
    const line = mk(group, 'line', {
      x1: 0, y1: 0, x2: 0, y2: 0,
      stroke: colors.TFT_BLACK, 'stroke-width': 1,
    });
    // ML_DATUM (middle-left) per main.cpp:1342: text-anchor=start,
    // dominant-baseline=central. Append a degree symbol — the C++
    // appends 'o' which the FreeSans bitmap renders as a degree
    // glyph; SVG can use the proper Unicode U+00B0 directly.
    const text = mk(group, 'text', {
      x: 0, y: 0,
      'font-family': "'B612', 'Helvetica Neue', Arial, sans-serif",
      'font-size': fontSize,
      fill: colors.TFT_BLACK,
      'text-anchor': 'start',
      'dominant-baseline': 'central',
    });
    text.textContent = `${i}°`;
    longTicks.push({ i, line, text });
  }

  function drawTickRow({ i, line }, halfW, pxc, pyc, sinR, cosR) {
    // Tick anchor: offset from horizon center along the roll-perp axis.
    // Mirrors main.cpp:1313-1317 — at roll=0 a positive i lands ABOVE
    // the horizon (pyc - i × pitchScale), matching the convention that
    // positive pitch labels sit above the horizon line.
    const ax = pxc - i * pitchScale * sinR;
    const ay = pyc - i * pitchScale * cosR;
    // Tick endpoints span ±halfW along the roll axis.
    const dx = halfW * cosR, dy = halfW * sinR;
    line.setAttribute('x1', ax - dx);
    line.setAttribute('y1', ay + dy);
    line.setAttribute('x2', ax + dx);
    line.setAttribute('y2', ay - dy);
  }

  function update({ pitchDeg, rollDeg }) {
    const r = rollDeg * DEG;
    const sinR = Math.sin(r), cosR = Math.cos(r);
    const pxc = cx + pitchDeg * pitchScale * sinR;
    const pyc = cy + pitchDeg * pitchScale * cosR;

    for (const t of shortTicks) drawTickRow(t, shortHalfW, pxc, pyc, sinR, cosR);
    for (const t of longTicks) {
      drawTickRow(t, longHalfW, pxc, pyc, sinR, cosR);
      // Label sits past the right end of the long tick (main.cpp:1340-1342),
      // offset by labelOffset along the roll axis.
      const ax = pxc - t.i * pitchScale * sinR;
      const ay = pyc - t.i * pitchScale * cosR;
      const lx = ax + (longHalfW + labelOffset) * cosR;
      const ly = ay - (longHalfW + labelOffset) * sinR;
      t.text.setAttribute('x', lx);
      t.text.setAttribute('y', ly);
    }
  }

  // Seed at level.
  update({ pitchDeg: 0, rollDeg: 0 });

  return { el: group, update };
}
