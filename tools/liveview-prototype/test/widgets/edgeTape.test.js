import { mountEdgeTape } from '../../lib/widgets/edgeTape.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountEdgeTape(svg, {
    barX: 313, barW: 7, zeroY: 119,
    heightScale: 60, heightMax: 120,
    tickX1: 313, tickX2: 319, tickFirstY: 14, tickLastY: 224, tickStep: 15,
    pipX1: 306, pipX2: 312, pipYs: [118, 119, 120],
  });

  // Tick lines: (224 - 14) / 15 + 1 = 15. Plus 3 pips. Plus the bar rect.
  const lines = svg.querySelectorAll('line');
  assert.equal(lines.length, 15 + 3, '15 ladder ticks + 3 pip lines');
  const rect = svg.querySelector('rect');
  assert.truthy(rect, 'bar rect exists');

  // Positive value → bar grows up from zeroY.
  w.update({ value: 1.0 });
  assert.equal(rect.getAttribute('height'), '60', 'value=1 -> height=60');
  assert.equal(rect.getAttribute('y'), '59', 'positive: y = zeroY - height');

  // Negative value → bar grows down from zeroY.
  w.update({ value: -0.5 });
  assert.equal(rect.getAttribute('height'), '30', 'value=-0.5 -> height=30');
  assert.equal(rect.getAttribute('y'), '119', 'negative: y = zeroY');

  // Saturation.
  w.update({ value: 5 });
  assert.equal(rect.getAttribute('height'), '120', 'clamped to heightMax');
}
