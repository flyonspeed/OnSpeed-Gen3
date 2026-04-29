import { mountHorizon } from '../../lib/widgets/horizon.js';
import * as G from '../../lib/geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountHorizon(svg);

  // After mount, expect: 1 sky rect, 1 ground polygon, 2 lines, 1 pivot.
  const rects   = svg.querySelectorAll('rect');
  const polys   = svg.querySelectorAll('polygon');
  const lines   = svg.querySelectorAll('line');
  const circles = svg.querySelectorAll('circle');
  assert.equal(rects.length, 1, 'one sky rect');
  assert.equal(polys.length, 1, 'one ground polygon');
  assert.equal(lines.length, 2, 'two horizon lines (black + blue back-edge)');
  assert.equal(circles.length, 1, 'one center pivot');

  // Level: pivot at (CX, CY).
  w.update({ pitchDeg: 0, rollDeg: 0 });
  assert.equal(Number(circles[0].getAttribute('cx')), G.MODE1_HORIZON_CX, 'pivot CX at level');
  assert.equal(Number(circles[0].getAttribute('cy')), G.MODE1_HORIZON_CY, 'pivot CY at level');

  // Pitch up (+10°), no roll: horizon translates DOWN by 30 px (10 × scale 3).
  w.update({ pitchDeg: 10, rollDeg: 0 });
  assert.equal(Number(circles[0].getAttribute('cx')), G.MODE1_HORIZON_CX, 'pitch up: pivot CX unchanged');
  assert.equal(Number(circles[0].getAttribute('cy')), G.MODE1_HORIZON_CY + 30, 'pitch up: pivot CY +30');

  // Roll +10°, no pitch: horizon line endpoints shift symmetrically.
  // px1 = CX - 2W*cos(10°), py1 = CY + 2W*sin(10°).
  w.update({ pitchDeg: 0, rollDeg: 10 });
  const blackLine = lines[0];  // first line is the black horizon line
  const x1 = Number(blackLine.getAttribute('x1'));
  const y1 = Number(blackLine.getAttribute('y1'));
  const expectedX1 = G.MODE1_HORIZON_CX - 2 * G.M5_PANEL_W * Math.cos(10 * Math.PI / 180);
  const expectedY1 = G.MODE1_HORIZON_CY + 2 * G.M5_PANEL_W * Math.sin(10 * Math.PI / 180);
  assert.near(x1, expectedX1, 0.01, 'roll +10: horizon line x1 matches roll math');
  assert.near(y1, expectedY1, 0.01, 'roll +10: horizon line y1 matches roll math');
}
