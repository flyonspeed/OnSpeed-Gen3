import { mountPitchReadout } from '../../lib/widgets/pitchReadout.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountPitchReadout(svg);

  // 1 rect + 1 text + 1 degree circle.
  assert.equal(svg.querySelectorAll('rect').length, 1, 'rect exists');
  assert.equal(svg.querySelectorAll('text').length, 1, 'text exists');
  assert.equal(svg.querySelectorAll('circle').length, 1, 'degree circle exists');

  w.update({ pitchDeg: 7.3 });
  assert.equal(svg.querySelector('text').textContent, '7.3', 'positive pitch one decimal');
  w.update({ pitchDeg: -2.45 });
  assert.equal(svg.querySelector('text').textContent, '-2.5', 'negative pitch rounds half-up');
}
