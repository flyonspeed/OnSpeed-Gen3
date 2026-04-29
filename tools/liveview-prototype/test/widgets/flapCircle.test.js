import { mountFlapCircle } from '../../lib/widgets/flapCircle.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountFlapCircle(svg, {
    cx: 23, cy: 204, r: 16,
    triangleTipR: 49, arcRad: 40 * Math.PI / 180, stopR: 49,
  });

  // Group + 4 elements (gray circle, triangle, 2 stop dots) + label = 6 children.
  assert.equal(w.el.children.length, 5, 'group has 5 children (circle, triangle, 2 stops, label)');

  const stops = Array.from(svg.querySelectorAll('circle')).filter(c => c.getAttribute('r') === '1');
  assert.equal(stops.length, 2, 'two stop dots');

  const triangle = svg.querySelector('path');
  assert.truthy(triangle, 'triangle path exists');
  assert.equal(triangle.getAttribute('d'), null, 'triangle has no d before update');

  w.update({ flapPos: 0, flapsMin: 0, flapsMax: 33 });
  assert.truthy(triangle.getAttribute('d'), 'triangle d set after update');
  const label = w.el.querySelector('text');
  assert.equal(label.textContent, '0', 'label = flapPos');

  w.update({ flapPos: 33, flapsMin: 0, flapsMax: 33 });
  assert.equal(label.textContent, '33', 'label updates');
}
