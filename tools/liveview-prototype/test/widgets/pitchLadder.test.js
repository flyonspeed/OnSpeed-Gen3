import { mountPitchLadder } from '../../lib/widgets/pitchLadder.js';
import * as G from '../../lib/geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountPitchLadder(svg);

  // Counts: short ticks for i in -85..+85 step 10 — the stride lands on
  // ±5 multiples and never hits 0, so 18 entries (no skip needed).
  // Long ticks for i in -90..+90 step 10 (excluding 0), so 18.
  // Each long tick has a numeric label.
  const lines = svg.querySelectorAll('line');
  const texts = svg.querySelectorAll('text');
  assert.equal(lines.length, 18 + 18, '18 short + 18 long ticks');
  assert.equal(texts.length, 18, '18 long-tick labels');

  // First label should be "-90" and the labels list should include "10", "10°"... we used String(i) so check "10".
  const labelTexts = Array.from(texts).map(t => t.textContent);
  assert.truthy(labelTexts.includes('10'),  'label "10" present');
  assert.truthy(labelTexts.includes('-10'), 'label "-10" present');

  // Level + roll=0: tick at i=10 sits at (CX, CY + 10*3 = CY + 30). The
  // tick row is horizontal (cosR=1, sinR=0), so x1/x2 are CX ± longHalfW.
  w.update({ pitchDeg: 0, rollDeg: 0 });
  // Find the long-tick line for i=10. Long lines come AFTER short lines
  // in the DOM; the long tick at i=10 is the (10 + 90)/10 - 1 = 9-th
  // long tick (i=-90,-80,...,-10,10,...,90 with 0 skipped).
  const longTicks = Array.from(lines).slice(18);  // skip 18 short ticks
  // i=10 is at index 9 in the long-tick sequence after dropping 0:
  // -90 -80 -70 -60 -50 -40 -30 -20 -10 10 20 30 40 50 60 70 80 90.
  const tick10 = longTicks[9];
  const halfW = G.MODE1_LADDER_LONG_HALF_W;
  assert.near(Number(tick10.getAttribute('x1')), G.MODE1_HORIZON_CX - halfW, 0.01,
              'i=10 long tick x1 at level');
  assert.near(Number(tick10.getAttribute('y1')), G.MODE1_HORIZON_CY - 30, 0.01,
              'i=10 long tick y at -30 (positive pitch labels above horizon)');
}
