// Static yellow top-pointer triangle. Sits at the very top of the
// dial as a fixed "this is straight up" reference. Does NOT rotate
// with bank — the rotating ARROW_OUT yellow at angle 270° (drawn by
// bankArc.js) is what sweeps over the bank-tick ring.
//
// Position matches main.cpp:1237-1248. C++ uses arcSize=100, arcWidth=15:
//   apex  (px1, py1) = (cx, cy - 100 + 7.5)  = (159, 26.5)
//   base  (px2, py2) = (cx - 7.5, cy - 100 + 30) = (151.5, 49)
//   base  (px3, py3) = (cx + 7.5, cy - 100 + 30) = (166.5, 49)

import { colors } from '../colors.js';
import * as G from '../geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountTopPointer(parent, {
  cx = G.MODE1_HORIZON_CX,
  cy = G.MODE1_HORIZON_CY,
} = {}) {
  const group = mk(parent, 'g', { 'data-widget': 'top-pointer' });

  // C++ formula at main.cpp:1237-1248 with arcSize=100, arcWidth=15:
  //   apex   = (cx, cy - 100 + 7.5)  = (159, 26.5)
  //   base L = (cx - 7.5, cy - 100 + 30) = (151.5, 49)
  //   base R = (cx + 7.5, cy - 100 + 30) = (166.5, 49)
  const ARC_SIZE  = 100;
  const ARC_WIDTH = 15;
  const tipX  = cx;
  const tipY  = cy - ARC_SIZE + ARC_WIDTH / 2;
  const baseY = cy - ARC_SIZE + 2 * ARC_WIDTH;
  const half  = ARC_WIDTH / 2;

  mk(group, 'polygon', {
    points: `${tipX},${tipY} ${cx - half},${baseY} ${cx + half},${baseY}`,
    fill: colors.TFT_YELLOW,
    stroke: colors.TFT_BLACK,
    'stroke-width': 1,
  });

  // Static; nothing to update per frame.
  function update() { /* noop */ }

  return { el: group, update };
}
