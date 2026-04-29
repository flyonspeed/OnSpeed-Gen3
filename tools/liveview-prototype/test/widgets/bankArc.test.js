import { mountBankArc } from '../../lib/widgets/bankArc.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountBankArc(svg);

  // Outer ring: 6 BAR_LONG <line>s + 1 ARROW_OUT <polygon>.
  // Inner ring: 4 BAR_SHORT <line>s + 2 ROUND_DOT <circle>s.
  const lines    = svg.querySelectorAll('line');
  const polys    = svg.querySelectorAll('polygon');
  const circles  = svg.querySelectorAll('circle');
  assert.equal(lines.length,   10, '6 long + 4 short bar lines');
  assert.equal(polys.length,    1, 'one ARROW_OUT polygon at 270');
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
