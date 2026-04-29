import { mountAircraftSymbol } from '../../lib/widgets/aircraftSymbol.js';
import * as G from '../../lib/geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountAircraftSymbol(svg);

  // Aircraft symbol now mirrors the C++ 7-stamp pattern:
  //   - 1 top pointer <polygon>
  //   - 7 body <polyline> stamps (yellow at offsets 0,-1,-2,+1,+2 and
  //     black outline at -3,+3) — matches main.cpp:1195-1228
  //   - 2 wingtip end-cap <line>s (main.cpp:1230-1231)
  //   - 2 center <circle>s (yellow fill + black ring outline,
  //     main.cpp:1192-1193).
  const polylines = svg.querySelectorAll('polyline');
  const lines     = svg.querySelectorAll('line');
  const polys     = svg.querySelectorAll('polygon');
  const circles   = svg.querySelectorAll('circle');
  assert.equal(polylines.length, 7,  '7 polyline stamps (5 yellow + 2 black outline rows)');
  assert.equal(lines.length,     2,  '2 wingtip end-cap lines');
  assert.equal(polys.length,     1,  'one top pointer triangle');
  assert.equal(circles.length,   2,  'two center circles (yellow + black ring)');

  // Center circles at (cx, cy) with the configured radius.
  const c0 = circles[0];
  assert.equal(Number(c0.getAttribute('cx')), G.MODE1_HORIZON_CX, 'circle cx');
  assert.equal(Number(c0.getAttribute('cy')), G.MODE1_HORIZON_CY, 'circle cy');
  assert.equal(Number(c0.getAttribute('r')),  G.MODE1_AIRCRAFT_CENTER_R, 'circle r=6');

  // First and last polyline traces should pass through the apex point
  // px5=cx, py5=cy+25. At dy=0 it should be (cx, cy+25); at dy=+3
  // (the bottom black row) it should be (cx, cy+28).
  const firstPts = polylines[0].getAttribute('points').split(/\s+/);
  assert.equal(firstPts[2], `${G.MODE1_HORIZON_CX},${G.MODE1_HORIZON_CY + G.MODE1_AIRCRAFT_DROOP_DY}`,
               'apex point at dy=0 is (cx, cy+25)');
  // Last row is black at dy=+3.
  const lastStroke = polylines[polylines.length - 1].getAttribute('stroke');
  assert.truthy(lastStroke && lastStroke.indexOf('panel') >= 0,
                'last polyline stroke uses --panel-bg (TFT_BLACK)');

  // update is a noop and should not throw.
  w.update();
}
