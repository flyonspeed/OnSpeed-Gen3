import { mountAircraftSymbol } from '../../lib/widgets/aircraftSymbol.js';
import * as G from '../../lib/geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountAircraftSymbol(svg);

  // Counts: 1 top pointer polygon + 2 yellow wings + 4 outline lines (top
  // and bottom of left and right wing) + 2 wing tips + 2 droop lines = 10
  // <line> + 1 <polygon> + 1 <circle>.
  const lines    = svg.querySelectorAll('line');
  const polys    = svg.querySelectorAll('polygon');
  const circles  = svg.querySelectorAll('circle');
  assert.equal(lines.length,   10, '10 line elements (wings + outlines + droop)');
  assert.equal(polys.length,    1, 'one top pointer triangle');
  assert.equal(circles.length,  1, 'one center circle');

  // Center circle at (cx, cy) with the configured radius.
  const c = circles[0];
  assert.equal(Number(c.getAttribute('cx')), G.MODE1_HORIZON_CX, 'circle cx');
  assert.equal(Number(c.getAttribute('cy')), G.MODE1_HORIZON_CY, 'circle cy');
  assert.equal(Number(c.getAttribute('r')),  G.MODE1_AIRCRAFT_CENTER_R, 'circle r=6');

  // update is a noop and should not throw.
  w.update();
}
