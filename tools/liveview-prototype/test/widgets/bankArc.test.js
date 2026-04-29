import { mountBankArc } from '../../lib/widgets/bankArc.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountBankArc(svg);

  // Both rings live at radius 115 (arcSize=115, arcWidth=15 in C++).
  //   6 BAR_LONG  → polygon (4-corner radial bar)
  //   4 BAR_SHORT → polygon (4-corner radial bar)
  //   1 ARROW_OUT → polygon (3-corner triangle, yellow at 270°)
  //   2 ROUND_DOT → circle  (filled with black outline)
  const polys    = svg.querySelectorAll('polygon');
  const circles  = svg.querySelectorAll('circle');
  // 6 long + 4 short bars + 1 arrow = 11 polygons total.
  assert.equal(polys.length,   11, '6 long + 4 short bar polygons + 1 ARROW_OUT');
  assert.equal(circles.length,  2, 'two ROUND_DOT circles');

  // At rollDeg=0, group transform is rotate(0 cx cy).
  const group = svg.querySelector('[data-widget="bank-arc"]');
  const t = group.getAttribute('transform');
  assert.truthy(t && t.startsWith('rotate(0 '),
                'bank arc has no rotation at level');

  // After update with rollDeg=30, transform should rotate by -30.
  w.update({ rollDeg: 30 });
  const t30 = group.getAttribute('transform');
  assert.truthy(t30 && t30.startsWith('rotate(-30 '),
                'rolling +30° rotates the arc by -30°');
}
