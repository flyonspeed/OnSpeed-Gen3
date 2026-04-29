// M5-style stale-data overlay.
//
// When the OnSpeed serial link is stale, the M5 paints a red X across
// the whole panel + a black "NO DATA" pill in the center. main.cpp:655-686.
// Mirror that behavior on the LiveView so the pilot gets the same
// unmistakable signal that they're looking at frozen data.
//
// One overlay per mode panel. Mounted last (paints on top of everything)
// and hidden by default. The firmware main.js toggles `setStale(bool)`
// based on the WebSocket age timer.

import { colors } from '../colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

// Mount the stale overlay into `svg`. Returns { setStale }.
//
// Composition (matches main.cpp:657-686):
//   1. Black panel fill (covers whatever was underneath).
//   2. Red X — eight lines, two diagonals each thickened to 5 px by
//      drawing a short fan (mimics the C++ which draws lines at
//      ±0..4 px offsets).
//   3. Black 120×40 pill at panel center.
//   4. White "NO DATA" text centered in the pill.
export function mountStaleOverlay(svg) {
  const group = mk(svg, 'g', { 'data-widget': 'stale-overlay' });
  group.style.visibility = 'hidden';

  // Black background fill.
  mk(group, 'rect', {
    x: 0, y: 0, width: 320, height: 240, fill: colors.TFT_BLACK,
  });

  // Red X — stack of diagonals at ±0..4 px offsets so the lines render
  // as ~5 px wide visually, matching main.cpp:658-680's 4 + 4 + 1 stamps.
  // Two diagonals: top-left to bottom-right, top-right to bottom-left.
  for (let off = -4; off <= 4; ++off) {
    // \ diagonal (TL → BR), shifted along x by off
    mk(group, 'line', {
      x1: 0 + Math.max(0, off),  y1: 0 - Math.min(0, off),
      x2: 319 + Math.min(0, off), y2: 239 - Math.max(0, off),
      stroke: colors.TFT_RED, 'stroke-width': 1,
    });
    // / diagonal (TR → BL)
    mk(group, 'line', {
      x1: 319 + Math.min(0, off), y1: 0 - Math.min(0, off),
      x2: 0 + Math.max(0, off),   y2: 239 - Math.max(0, off),
      stroke: colors.TFT_RED, 'stroke-width': 1,
    });
  }

  // Center pill — main.cpp:685's fillRect(100,100,120,40,TFT_BLACK).
  mk(group, 'rect', {
    x: 100, y: 100, width: 120, height: 40, fill: colors.TFT_BLACK,
  });

  // "NO DATA" — main.cpp:686 drawString at (160,120) with FSSB18 +
  // middle_center datum.
  mk(group, 'text', {
    x: 160, y: 120,
    'font-family': 'Helvetica, Arial, sans-serif',
    'font-weight': 'bold',
    'font-size': 24,
    fill: colors.TFT_WHITE,
    'text-anchor': 'middle',
    'dominant-baseline': 'central',
  }).textContent = 'NO DATA';

  function setStale(stale) {
    group.style.visibility = stale ? 'visible' : 'hidden';
    if (stale && group.parentNode) {
      // Re-append to keep on top — defensive against later widgets
      // (e.g. percentLiftNumber's bringToFront) that move siblings
      // around per frame.
      group.parentNode.appendChild(group);
    }
  }

  return { el: group, setStale };
}
