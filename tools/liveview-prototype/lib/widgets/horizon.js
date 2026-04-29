// Sky/ground horizon polygon driven by pitch + roll.
// Matches the math in AiGraph() at main.cpp:1085-1122.
//
// The sky is a single full-panel cyan rect under everything. The ground
// is a wide rotated polygon (computed each frame) whose top edge is the
// horizon line. The polygon is drawn LARGE enough that its outer edges
// always lie outside the SVG viewport — the SVG clips it for us.
//
// We compute the same four corners the C++ does (px1..px4, py1..py4):
//
//   pxc = CX + pitch × scale × sin(roll)
//   pyc = CY + pitch × scale × cos(roll)
//   xRotate = 2W × cos(roll), yRotate = 2W × sin(roll)
//   horizon line endpoints (px1, py1) = (pxc - xRotate, pyc + yRotate)
//                          (px2, py2) = (pxc + xRotate, pyc - yRotate)
//   bottom edge offset by 3H downward in the roll-rotated frame:
//     (px3, py3) = (px1 + 3H cos(r-π/2), py1 - 3H sin(-r-π/2))
//     (px4, py4) = (px2 - 3H cos(r+π/2), py2 + 3H sin(r+π/2))

import { colors } from '../colors.js';
import * as G from '../geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';
const HALF_PI = Math.PI / 2;

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountHorizon(parent, {
  cx = G.MODE1_HORIZON_CX,
  cy = G.MODE1_HORIZON_CY,
  pitchScale = G.MODE1_PITCH_HEIGHT_SCALE,
  panelW = G.M5_PANEL_W,
  panelH = G.M5_PANEL_H,
} = {}) {
  const group = mk(parent, 'g', { 'data-widget': 'horizon' });

  // Sky: full-panel cyan background rect (main.cpp:1085 fillSprite TFT_CYAN).
  mk(group, 'rect', {
    x: 0, y: 0, width: panelW, height: panelH, fill: colors.TFT_CYAN,
  });

  // Ground polygon — points updated per-frame.
  const ground = mk(group, 'polygon', {
    points: '',
    fill: colors.TFT_BROWN,
  });

  // Horizon line (thin black, drawn after ground so it's visible).
  const horizonLine = mk(group, 'line', {
    x1: 0, y1: 0, x2: 0, y2: 0,
    stroke: colors.TFT_BLACK, 'stroke-width': 1,
  });
  // Back-edge blue line (the parallel offset edge — main.cpp:1121).
  const backLine = mk(group, 'line', {
    x1: 0, y1: 0, x2: 0, y2: 0,
    stroke: colors.TFT_BLUE, 'stroke-width': 1,
  });
  // Center pivot dot (main.cpp:1122 fillCircle pxc, pyc, 3, BLACK).
  const pivot = mk(group, 'circle', {
    cx: cx, cy: cy, r: 3, fill: colors.TFT_BLACK,
  });

  function update({ pitchDeg, rollDeg }) {
    const r = rollDeg * Math.PI / 180;
    const sinR = Math.sin(r), cosR = Math.cos(r);

    const pxc = cx + pitchDeg * pitchScale * sinR;
    const pyc = cy + pitchDeg * pitchScale * cosR;

    const xRot = 2 * panelW * cosR;
    const yRot = 2 * panelW * sinR;

    const px1 = pxc - xRot, py1 = pyc + yRot;
    const px2 = pxc + xRot, py2 = pyc - yRot;

    const px3 = px1 + 3 * panelH * Math.cos( r - HALF_PI);
    const py3 = py1 - 3 * panelH * Math.sin(-r - HALF_PI);
    const px4 = px2 - 3 * panelH * Math.cos( r + HALF_PI);
    const py4 = py2 + 3 * panelH * Math.sin( r + HALF_PI);

    // Polygon order (px1, py1) → (px2, py2) → (px4, py4) → (px3, py3)
    // matches the two C++ triangles fillTriangle(p1,p2,p3) +
    // fillTriangle(p3,p4,p2) merged into a single quad.
    ground.setAttribute('points',
      `${px1},${py1} ${px2},${py2} ${px4},${py4} ${px3},${py3}`);

    horizonLine.setAttribute('x1', px1); horizonLine.setAttribute('y1', py1);
    horizonLine.setAttribute('x2', px2); horizonLine.setAttribute('y2', py2);
    backLine.setAttribute('x1', px3); backLine.setAttribute('y1', py3);
    backLine.setAttribute('x2', px4); backLine.setAttribute('y2', py4);
    pivot.setAttribute('cx', pxc); pivot.setAttribute('cy', pyc);
  }

  // Seed at level (pitch=0, roll=0).
  update({ pitchDeg: 0, rollDeg: 0 });

  return { el: group, update };
}
