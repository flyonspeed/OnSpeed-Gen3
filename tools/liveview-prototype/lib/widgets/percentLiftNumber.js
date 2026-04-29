// Percent-lift number with black outline. The C++ stamps 9 black copies at
// ±3 px and one white copy on top (displayAOA() main.cpp:735-753); SVG uses
// two stacked text elements (black-stroked outline first, white fill on
// top) wrapped in a `<g>` group that the caller is responsible for keeping
// as the last child of the SVG so the number stays painted above the index
// bar at high AOA. The wrapper group exposes a `bringToFront()` method that
// re-appends itself; the mode file calls it once per frame to be safe.
//
// Centered above the indexer (text-anchor: middle at INDEXER_CX). The C++
// uses PERCENT_X_POS=140 with FSSB18 bitmap kerning to land visually
// centered; SVG with browser sans-serif needs the explicit anchor.

import { colors } from '../colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function mountPercentLiftNumber(parent, { cx, baselineY, fontSize, outlinePx }) {
  // Wrap in a <g> so we can reliably re-append (bringToFront) without
  // moving two elements separately. Also gives us a single stacking unit.
  const group = document.createElementNS(SVG_NS, 'g');
  group.setAttribute('data-widget', 'percent-lift-number');

  const baseAttrs = {
    x: cx, y: baselineY,
    'font-family': 'Helvetica, Arial, sans-serif',
    'font-weight': 'bold',
    'font-size': fontSize,
    'dominant-baseline': 'alphabetic',
    'text-anchor': 'middle',
  };

  // Outline: same text rendered with thick black stroke, no fill.
  // C++ stamps 9 black copies at ±3 offset (a 6 px-wide black halo
  // around the white digits — main.cpp:735-753). SVG stroke-width is
  // doubled because half of stroke spreads outside the glyph and half
  // inside; outlinePx*2 gives ≈outlinePx of visible halo on each side.
  //
  // Group starts hidden until first valid AOA arrives — paired with
  // the indexer bar's hidden-until-data behavior to avoid showing a
  // misleading "00% lift" before the WebSocket connects.
  group.style.visibility = 'hidden';

  const outline = document.createElementNS(SVG_NS, 'text');
  for (const k in baseAttrs) outline.setAttribute(k, baseAttrs[k]);
  outline.setAttribute('fill', 'none');
  outline.setAttribute('stroke', colors.TFT_BLACK);
  outline.setAttribute('stroke-width', outlinePx * 2);
  outline.setAttribute('stroke-linejoin', 'round');
  outline.textContent = '00';
  group.appendChild(outline);

  // Fill: same text rendered with white fill, no stroke. Drawn on top
  // so the digit interiors are white over the black halo.
  const fill = document.createElementNS(SVG_NS, 'text');
  for (const k in baseAttrs) fill.setAttribute(k, baseAttrs[k]);
  fill.setAttribute('fill', colors.TFT_WHITE);
  fill.textContent = '00';
  group.appendChild(fill);

  parent.appendChild(group);

  function update({ percent, aoaIsValid = true }) {
    if (!aoaIsValid) {
      group.style.visibility = 'hidden';
      return;
    }
    group.style.visibility = 'visible';
    const s = String(percent).padStart(2, '0');
    outline.textContent = s;
    fill.textContent = s;
  }

  // Re-append the group to its current parent so it's always the last
  // child (= painted last = on top of any bar that overlaps).
  function bringToFront() {
    if (group.parentNode) group.parentNode.appendChild(group);
  }

  return { el: group, update, bringToFront };
}
