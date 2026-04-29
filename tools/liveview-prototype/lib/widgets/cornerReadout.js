// A single corner readout: a colored label above a white number.
// Mode 0 uses two of these (IAS top-left, G top-right); Mode 1 uses four
// (IAS, PALT, G, AOA%). Caller passes geometry per corner.

import { colors } from '../colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountCornerReadout(parent, {
  labelText,
  labelX, labelY,
  numX, numY,
  labelAnchor = 'start',
  labelColor = colors.TFT_GREEN,
  numColor   = colors.TFT_WHITE,
  labelFontSize,
  numFontSize,
  // 'alphabetic' (default) means numY is the text baseline — matches
  // M5 setCursor convention. 'central' means numY is the vertical
  // CENTER of the glyph — useful for aligning to a feature on the
  // background (e.g. the top tick of the pitch ladder).
  numBaseline = 'alphabetic',
}) {
  const group = mk(parent, 'g', { 'data-widget': 'corner' });

  mk(group, 'text', {
    x: labelX, y: labelY,
    'font-family': "'B612', 'Helvetica Neue', Arial, sans-serif",
    'font-size': labelFontSize,
    fill: labelColor,
    'text-anchor': labelAnchor,
  }).textContent = labelText;

  const num = mk(group, 'text', {
    x: numX, y: numY,
    'font-family': "'B612', 'Helvetica Neue', Arial, sans-serif",
    'font-weight': 'bold',
    'font-size': numFontSize,
    fill: numColor,
    'text-anchor': labelAnchor,
    'dominant-baseline': numBaseline,
  });
  num.textContent = '0';

  function update({ value, formatter }) {
    num.textContent = formatter ? formatter(value) : String(value);
  }

  return { el: group, update };
}
