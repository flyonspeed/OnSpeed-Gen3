import { mountCornerReadout } from '../../lib/widgets/cornerReadout.js';
import { colors } from '../../lib/colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountCornerReadout(svg, {
    labelText: 'IAS',
    labelX: 5, labelY: 90,
    numX: 7,  numY: 130,
    labelAnchor: 'start',
    labelFontSize: 22, numFontSize: 26,
  });

  const texts = Array.from(svg.querySelectorAll('text'));
  assert.equal(texts.length, 2, 'two text nodes (label + number)');
  assert.equal(texts[0].textContent, 'IAS', 'label content');
  assert.equal(texts[0].getAttribute('fill'), colors.TFT_GREEN, 'label is green by default');
  assert.equal(texts[1].textContent, '0', 'number starts at 0');

  w.update({ value: 75, formatter: v => String(Math.round(v)) });
  assert.equal(texts[1].textContent, '75', 'rounded value applied');

  w.update({ value: 1.234 });
  assert.equal(texts[1].textContent, '1.234', 'no formatter -> default String()');
}
