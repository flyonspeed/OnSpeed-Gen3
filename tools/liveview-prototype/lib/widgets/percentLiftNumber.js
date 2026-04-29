// Percent-lift number with black outline. The C++ stamps 9 black copies at
// ±3 px and one white copy on top (displayAOA() main.cpp:735-753); SVG with
// `paint-order: stroke` does the same with one element.
//
// Centered above the indexer (text-anchor: middle at INDEXER_CX). The C++
// uses PERCENT_X_POS=140 with FSSB18 bitmap kerning to land visually
// centered; SVG with browser sans-serif needs the explicit anchor.

import { colors } from '../colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function mountPercentLiftNumber(parent, { cx, baselineY, fontSize, outlinePx }) {
  const text = document.createElementNS(SVG_NS, 'text');
  text.setAttribute('x', cx);
  text.setAttribute('y', baselineY);
  text.setAttribute('font-family', 'Helvetica, Arial, sans-serif');
  text.setAttribute('font-weight', 'bold');
  text.setAttribute('font-size', fontSize);
  text.setAttribute('fill', colors.TFT_WHITE);
  text.setAttribute('stroke', colors.TFT_BLACK);
  text.setAttribute('stroke-width', outlinePx);
  text.setAttribute('paint-order', 'stroke');
  text.setAttribute('dominant-baseline', 'alphabetic');
  text.setAttribute('text-anchor', 'middle');
  text.textContent = '00';
  parent.appendChild(text);

  function update({ percent }) {
    text.textContent = String(percent).padStart(2, '0');
  }

  return { el: text, update };
}
