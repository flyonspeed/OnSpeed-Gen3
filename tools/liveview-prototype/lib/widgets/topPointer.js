// Static yellow top-pointer triangle. Sits at the very top of the
// dial as a fixed "this is straight up" reference. Does NOT rotate
// with bank — the rotating ARROW_OUT yellow at angle 270° (drawn by
// bankArc.js) is what sweeps over the bank-tick ring.
//
// Origin: main.cpp:1237-1248. C++ uses arcSize=100, arcWidth=15:
//   apex  (px1, py1) = (cx, cy - 100 + 7.5)  = (159, 26.5)
//   base  (px2, py2) = (cx - 7.5, cy - 100 + 30) = (151.5, 49)
//   base  (px3, py3) = (cx + 7.5, cy - 100 + 30) = (166.5, 49)
//
// Intentional divergence from the C++ position: the user-side review
// against the wasm-live A/B noted that the static triangle visually
// touches the rotating ARROW_OUT (which has its tip at y=11.5 when
// bank=0) — placing the static apex at y=26.5 reads as one big arrow
// pair right at the top, not two separate features. Moving the
// static triangle UP by ~17 px puts its apex at y≈10, sitting just
// above the rotating arrow's tip with clear visual separation. The
// "tip pointing up" orientation is preserved (apex at smaller y,
// base at larger y) — same shape, just shifted.

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

  // Triangle dimensions match the C++ (15 px tall body, 7.5 px base
  // half-width) but the y-position is shifted up by 17 px so the
  // apex clears the rotating ARROW_OUT tip at y=11.5.
  //
  //   apex   = (cx, cy - 117 + 7.5)  = (159, 9.5)
  //   base L = (cx - 7.5, cy - 117 + 30) = (151.5, 32)
  //   base R = (cx + 7.5, cy - 117 + 30) = (166.5, 32)
  //
  // (Equivalent to "C++ formula with arcSize=117 instead of 100".)
  const ARC_SIZE  = 117;
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
