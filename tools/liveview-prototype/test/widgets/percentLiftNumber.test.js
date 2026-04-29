import { mountPercentLiftNumber } from '../../lib/widgets/percentLiftNumber.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountPercentLiftNumber(svg, { cx: 160, baselineY: 27, fontSize: 26, outlinePx: 3 });

  // The widget wraps an outline-stroked + filled pair of <text> elements
  // in a <g> group so bringToFront() can re-append a single node. The
  // first <text> child is the outline; both children share x / font-size
  // / text-anchor.
  assert.equal(w.el.tagName, 'g', 'is a group element');
  const outline = w.el.querySelector('text');
  assert.truthy(outline, 'group contains a text element');
  assert.equal(outline.getAttribute('text-anchor'), 'middle', 'centered');
  assert.equal(outline.getAttribute('x'), '160', 'cx applied to x');
  assert.equal(outline.getAttribute('font-size'), '26', 'fontSize applied');
  assert.equal(outline.textContent, '00', 'initial text 00');

  // After update(), both <text> children of the group share the same content;
  // assert via the outline (first text child).
  w.update({ percent: 7 });
  assert.equal(outline.textContent, '07', 'pads to 2 digits');
  w.update({ percent: 87 });
  assert.equal(outline.textContent, '87', 'two-digit input passes through');
  w.update({ percent: 100 });
  assert.equal(outline.textContent, '100', '3-digit no padding');
}
