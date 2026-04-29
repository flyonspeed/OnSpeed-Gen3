// Fixed yellow aircraft reference symbol drawn over the horizon.
// Mirrors the section of AiGraph() at main.cpp:1174-1248 that draws
// the airplane glyph (NOT the bank-angle arc markers from :1136-1171,
// which are deferred — see Stage 2 report).
//
// Components (top-down):
//   - Top yellow triangle pointer (the static "you-are-here" marker
//     at the top of the bank arc), main.cpp:1237-1248.
//   - 7 px-thick yellow horizontal wings extending from x±25 to x±100,
//     with x±100 capped by black 6 px-tall vertical bars at the wing
//     tips (main.cpp:1230-1231) and a 1 px black outline above and
//     below (main.cpp:1210-1213, 1225-1228).
//   - 7 px-thick yellow droop "V": diagonals from (x-25, y) down to
//     (x, y+25) and from (x, y+25) up to (x+25, y).
//   - 6 px yellow center circle with black outline (main.cpp:1192-1193).
//
// The whole symbol is static — it does NOT rotate or translate. The
// horizon underneath rotates around it. update() is a noop.

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
  droopDy    = G.MODE1_AIRCRAFT_DROOP_DY,
  centerR    = G.MODE1_AIRCRAFT_CENTER_R,
  barT       = G.MODE1_AIRCRAFT_BAR_THICKNESS,
} = {}) {
  const group = mk(parent, 'g', { 'data-widget': 'aircraft-symbol' });

  // Top yellow triangle pointer at top of bank arc (main.cpp:1237-1248).
  // arcSize=100, arcWidth=15 in the C++ — tip y = cy - 100 + 15/2 = 26,
  // base at cy - 100 + 30 = 49, base half-width = 15/2 = 7.5.
  const tipX = cx, tipY = cy - 100 + 15 / 2;
  const baseY = cy - 100 + 2 * 15;
  const baseHalfW = 15 / 2;
  mk(group, 'polygon', {
    points: `${tipX},${tipY} ${cx - baseHalfW},${baseY} ${cx + baseHalfW},${baseY}`,
    fill: colors.TFT_YELLOW,
    stroke: colors.TFT_BLACK,
    'stroke-width': 1,
  });

  // Wings — yellow body, black 1-px outline above + below, black tips.
  // Use stroked horizontal lines so the 7 px thickness is just stroke-width.
  // Left wing: from (cx - outerHalfW, cy) to (cx - innerHalfW, cy).
  mk(group, 'line', {
    x1: cx - outerHalfW, y1: cy, x2: cx - innerHalfW, y2: cy,
    stroke: colors.TFT_YELLOW, 'stroke-width': barT,
  });
  // Right wing.
  mk(group, 'line', {
    x1: cx + innerHalfW, y1: cy, x2: cx + outerHalfW, y2: cy,
    stroke: colors.TFT_YELLOW, 'stroke-width': barT,
  });
  // 1-px black outline ON top + bottom edges of each wing.
  // Each wing is barT (7) tall; top edge is at y = cy - barT/2 ≈ cy-3,
  // bottom at cy + 3.
  const outlineYTop = cy - (barT - 1) / 2;
  const outlineYBot = cy + (barT - 1) / 2;
  for (const wingX of [
    [cx - outerHalfW, cx - innerHalfW],
    [cx + innerHalfW, cx + outerHalfW],
  ]) {
    mk(group, 'line', {
      x1: wingX[0], y1: outlineYTop, x2: wingX[1], y2: outlineYTop,
      stroke: colors.TFT_BLACK, 'stroke-width': 1,
    });
    mk(group, 'line', {
      x1: wingX[0], y1: outlineYBot, x2: wingX[1], y2: outlineYBot,
      stroke: colors.TFT_BLACK, 'stroke-width': 1,
    });
  }
  // Wing tips (main.cpp:1230-1231): 6 px-tall black vertical caps.
  for (const tipXcap of [cx - outerHalfW, cx + outerHalfW]) {
    mk(group, 'line', {
      x1: tipXcap, y1: outlineYTop, x2: tipXcap, y2: outlineYBot,
      stroke: colors.TFT_BLACK, 'stroke-width': 1,
    });
  }

  // Droop diagonals: V-shape from (cx - innerHalfW, cy) down to (cx, cy + droopDy)
  // and back up to (cx + innerHalfW, cy). main.cpp:1196-1197.
  mk(group, 'line', {
    x1: cx - innerHalfW, y1: cy, x2: cx, y2: cy + droopDy,
    stroke: colors.TFT_YELLOW, 'stroke-width': barT,
  });
  mk(group, 'line', {
    x1: cx, y1: cy + droopDy, x2: cx + innerHalfW, y2: cy,
    stroke: colors.TFT_YELLOW, 'stroke-width': barT,
  });

  // Center yellow circle with black outline (main.cpp:1192-1193).
  mk(group, 'circle', {
    cx, cy, r: centerR, fill: colors.TFT_YELLOW,
    stroke: colors.TFT_BLACK, 'stroke-width': 1,
  });

  // No-op update — symbol is static.
  function update() { /* noop */ }

  return { el: group, update };
}
