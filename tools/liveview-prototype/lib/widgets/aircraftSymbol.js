// Fixed yellow aircraft reference symbol drawn over the horizon.
// Mirrors the section of AiGraph() at main.cpp:1192-1231 that draws the
// airplane glyph, NOT the bank-angle arc markers from :1136-1171
// (those live in bankArc.js) and NOT the top-pointer triangle at
// :1237-1248 (that lives in topPointer.js).
//
// The C++ renders the body as 7 PARALLEL stamps at y-offsets {-3..+3},
// not as a single thick stroke. Each stamp is a 4-segment polyline:
//   - Left horizontal (px1, y) → (px1+75, y) at width 3*arcSize/4=75
//   - Diagonal down-right    (px2, y) → (px5, py5+y) — apex is (px5, py5)
//   - Diagonal up-right      (px5, py5+y) → (px3, y)
//   - Right horizontal (px3, y) → (px3+75, y)
//
// Color per offset: y=-3 black, y=-2..+2 yellow, y=+3 black. This produces
// a 5-px-thick yellow stripe with 1-px black outline above + below.
//
// Wingtip end-caps: 6-px-tall black vertical lines at the LEFT wingtip
// (px1) and RIGHT wingtip (px4), spanning y-3..y+2 (main.cpp:1230-1231).
//
// Center pivot: 6-px-radius yellow filled circle with a 1-px black outline
// (main.cpp:1192-1193, bullsEye = 2 × HEIGHT/80 = 6).
//
// The symbol is static — it does not rotate or translate. The horizon
// underneath rotates around it. update() is a noop.

import { colors } from '../colors.js';
import * as G from '../geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountAircraftSymbol(parent, {
  cx = G.MODE1_HORIZON_CX,
  cy = G.MODE1_HORIZON_CY,
  innerHalfW = G.MODE1_AIRCRAFT_INNER_HALF_W,
  outerHalfW = G.MODE1_AIRCRAFT_OUTER_HALF_W,
  wingHalfLen = G.MODE1_AIRCRAFT_WING_HALF_LEN,
  droopDy    = G.MODE1_AIRCRAFT_DROOP_DY,
  centerR    = G.MODE1_AIRCRAFT_CENTER_R,
} = {}) {
  const group = mk(parent, 'g', { 'data-widget': 'aircraft-symbol' });

  // Body anchor coordinates (main.cpp:1181-1190). With arcSize=100,
  // arcSize/4=25, 3*arcSize/4=75:
  //   px1 = cx - 100 (left wingtip)
  //   px2 = cx -  25 (left inner — start of left diagonal)
  //   px3 = cx +  25 (right inner — end of right diagonal)
  //   px4 = cx + 100 (right wingtip)
  //   px5 = cx       (apex)
  //   py5 = cy + 25  (apex y, droopDy below cy)
  const px1 = cx - outerHalfW;
  const px2 = cx - innerHalfW;
  const px3 = cx + innerHalfW;
  const px4 = cx + outerHalfW;
  const px5 = cx;
  const py5 = cy + droopDy;

  // Stamp the 7 parallel polyline copies in C++ paint order
  // (main.cpp:1195-1228). Yellow rows at offsets 0, -1, -2 paint first,
  // then black at -3, then yellow at +1, +2, then black at +3. The
  // black rows last in each half land on top of the yellow they bound,
  // giving a 5-px-thick yellow stripe with a clean 1-px black outline
  // top + bottom. shape-rendering: crispEdges keeps the diagonals
  // from anti-aliasing into the outlines.
  const orderedRows = [
    { dy:  0, color: colors.TFT_YELLOW },
    { dy: -1, color: colors.TFT_YELLOW },
    { dy: -2, color: colors.TFT_YELLOW },
    { dy: -3, color: colors.TFT_BLACK  },
    { dy: +1, color: colors.TFT_YELLOW },
    { dy: +2, color: colors.TFT_YELLOW },
    { dy: +3, color: colors.TFT_BLACK  },
  ];
  for (const { dy, color } of orderedRows) {
    // Single polyline traces left-wingtip → left-inner → apex → right-inner
    // → right-wingtip. Using polyline (not <path>) keeps the 4-segment
    // structure explicit and lets shape-rendering sharpen the corners.
    const points = [
      `${px1},${cy + dy}`,
      `${px2},${cy + dy}`,
      `${px5},${py5 + dy}`,
      `${px3},${cy + dy}`,
      `${px4},${cy + dy}`,
    ].join(' ');
    mk(group, 'polyline', {
      points,
      fill: 'none',
      stroke: color,
      'stroke-width': 1,
      'shape-rendering': 'crispEdges',
    });
  }
  // wingHalfLen is implicit in (px3 + 75 = px4); kept as a config name
  // for callers that want to override.
  void wingHalfLen;

  // Wing-tip end-caps (main.cpp:1230-1231):
  // drawFastVLine(px1, py1-3, 6, TFT_BLACK) → 6-px-tall vertical line
  // at x=px1 covering y=cy-3..cy+2.
  for (const tipXcap of [px1, px4]) {
    mk(group, 'line', {
      x1: tipXcap, y1: cy - 3, x2: tipXcap, y2: cy + 2,
      stroke: colors.TFT_BLACK, 'stroke-width': 1,
      'shape-rendering': 'crispEdges',
    });
  }

  // Center yellow circle with black outline (main.cpp:1192-1193).
  // 6-px-radius filled yellow + 1-px black ring on top.
  mk(group, 'circle', {
    cx, cy, r: centerR, fill: colors.TFT_YELLOW,
  });
  mk(group, 'circle', {
    cx, cy, r: centerR, fill: 'none',
    stroke: colors.TFT_BLACK, 'stroke-width': 1,
  });

  // No-op update — symbol is static.
  function update() { /* noop */ }

  return { el: group, update };
}
