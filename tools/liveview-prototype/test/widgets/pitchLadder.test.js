import { mountPitchLadder } from '../../lib/widgets/pitchLadder.js';
import * as G from '../../lib/geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountPitchLadder(svg);

  // Counts: short ticks for i in -85..+85 step 10 — the stride lands on
  // ±5 multiples and never hits 0, so 18 entries.
  // Long ticks for i in -90..+90 step 10 INCLUDING 0 (matches the C++
  // loop at main.cpp:1328), so 19. Each long tick has a numeric label.
  const lines = svg.querySelectorAll('line');
  const texts = svg.querySelectorAll('text');
  assert.equal(lines.length, 18 + 19, '18 short + 19 long ticks');
  assert.equal(texts.length, 19, '19 long-tick labels (includes 0°)');

  // Label text format is `${i}°` (Unicode degree symbol after the
  // integer) per main.cpp:1342's `String(i)+"o"`.
  const labelTexts = Array.from(texts).map(t => t.textContent);
  assert.truthy(labelTexts.includes('10°'),  'label "10°" present');
  assert.truthy(labelTexts.includes('-10°'), 'label "-10°" present');
  assert.truthy(labelTexts.includes('0°'),   'label "0°" present');

  // Level + roll=0: tick at i=10 sits at (CX, CY + 10*3 = CY + 30). The
  // tick row is horizontal (cosR=1, sinR=0), so x1/x2 are CX ± longHalfW.
  w.update({ pitchDeg: 0, rollDeg: 0 });
  // Find the long-tick line for i=10. Long lines come AFTER short lines
  // in the DOM. Long-tick sequence including 0 is
  //   -90 -80 -70 -60 -50 -40 -30 -20 -10 0 10 20 30 40 50 60 70 80 90
  // so i=10 is at index 10.
  const longTicks = Array.from(lines).slice(18);  // skip 18 short ticks
  const tick10 = longTicks[10];
  const halfW = G.MODE1_LADDER_LONG_HALF_W;
  assert.near(Number(tick10.getAttribute('x1')), G.MODE1_HORIZON_CX - halfW, 0.01,
              'i=10 long tick x1 at level');
  assert.near(Number(tick10.getAttribute('y1')), G.MODE1_HORIZON_CY - 30, 0.01,
              'i=10 long tick y at -30 (positive pitch labels above horizon)');
}
