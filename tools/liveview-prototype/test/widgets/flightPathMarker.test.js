import { mountFlightPathMarker } from '../../lib/widgets/flightPathMarker.js';
import * as G from '../../lib/geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountFlightPathMarker(svg);

  // Three rings + three wing bars.
  const circles = svg.querySelectorAll('circle');
  const lines   = svg.querySelectorAll('line');
  assert.equal(circles.length, 3, 'three FPV rings');
  assert.equal(lines.length, 3,  'three FPV wing/top bars');

  // Level: transform should be translate(0, 0).
  w.update({ pitchDeg: 0, flightPathDeg: 0 });
  const g = svg.querySelector('g[data-widget="flight-path-marker"]');
  assert.equal(g.getAttribute('transform'), 'translate(0, 0)', 'level: no translate');

  // Pitch=10 (nose up), FP=0 (flight is level): FP appears BELOW the
  // aircraft symbol. fpY = 120 - (0 - 10)*3 = 150. dy = +30.
  w.update({ pitchDeg: 10, flightPathDeg: 0 });
  assert.equal(g.getAttribute('transform'), 'translate(0, 30)',
               'pitch up + FP level: marker below center by 30 px');

  // Clamp to panel height.
  w.update({ pitchDeg: 100, flightPathDeg: 0 });
  // fpY = 120 - (0-100)*3 = 420 → clamped to 239. dy = 119.
  assert.equal(g.getAttribute('transform'), `translate(0, ${G.M5_PANEL_H - 1 - 120})`,
               'extreme value clamped to panel-1');
}
