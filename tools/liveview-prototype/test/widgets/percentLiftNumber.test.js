import { mountPercentLiftNumber } from '../../lib/widgets/percentLiftNumber.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountPercentLiftNumber(svg, { cx: 160, baselineY: 27, fontSize: 26, outlinePx: 3 });

  assert.equal(w.el.tagName, 'text', 'is a text element');
  assert.equal(w.el.getAttribute('text-anchor'), 'middle', 'centered');
  assert.equal(w.el.getAttribute('x'), '160', 'cx applied to x');
  assert.equal(w.el.getAttribute('font-size'), '26', 'fontSize applied');
  assert.equal(w.el.textContent, '00', 'initial text 00');

  w.update({ percent: 7 });
  assert.equal(w.el.textContent, '07', 'pads to 2 digits');
  w.update({ percent: 87 });
  assert.equal(w.el.textContent, '87', 'two-digit input passes through');
  w.update({ percent: 100 });
  assert.equal(w.el.textContent, '100', '3-digit no padding');
}
