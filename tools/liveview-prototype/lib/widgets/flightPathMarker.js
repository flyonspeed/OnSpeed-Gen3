// Magenta flight-path-vector marker. Mirrors main.cpp:1251-1277.
//
// Three concentric rings at radii 12/13/14 plus four perpendicular wing
// bars (left/right horizontal, top vertical). The marker translates
// vertically by `(flightPath - pitch) × 3 px/deg` (HEIGHT/40 in C++);
// fpX stays at the panel center (the C++ does NOT roll the marker).

import { colors } from '../colors.js';
import * as G from '../geometry.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountFlightPathMarker(parent, {
  cx = G.MODE1_FPV_CX,
  baseCy = 120,                // main.cpp:1255 uses 120, not g_py0=119
  ringRadii = G.MODE1_FPV_RING_RADII,
  wingInner = G.MODE1_FPV_WING_INNER,
  wingOuter = G.MODE1_FPV_WING_OUTER,
  barT = G.MODE1_FPV_BAR_THICKNESS,
  panelH = G.M5_PANEL_H,
} = {}) {
  // Use a translatable group — only y changes per frame.
  const group = mk(parent, 'g', {
    'data-widget': 'flight-path-marker',
    transform: `translate(0, 0)`,
    style: 'transition: transform 100ms linear;',
  });

  // Three concentric rings at the marker's local origin.
  for (const r of ringRadii) {
    mk(group, 'circle', {
      cx, cy: baseCy, r,
      fill: 'none',
      stroke: colors.TFT_MAGENTA,
      'stroke-width': 1,
    });
  }

  // Left/right wing bars — 3 px tall (drawn as a single 3-px stroked line).
  mk(group, 'line', {
    x1: cx - wingOuter, y1: baseCy, x2: cx - wingInner, y2: baseCy,
    stroke: colors.TFT_MAGENTA, 'stroke-width': barT,
  });
  mk(group, 'line', {
    x1: cx + wingInner, y1: baseCy, x2: cx + wingOuter, y2: baseCy,
    stroke: colors.TFT_MAGENTA, 'stroke-width': barT,
  });
  // Top tick — 3 px wide, from baseCy - wingInner up to baseCy - wingOuter.
  mk(group, 'line', {
    x1: cx, y1: baseCy - wingInner, x2: cx, y2: baseCy - wingOuter,
    stroke: colors.TFT_MAGENTA, 'stroke-width': barT,
  });

  function update({ pitchDeg, flightPathDeg }) {
    // main.cpp:1255: fpY = 120 - (flightPath - pitch) × 120/40, then constrain 0..239.
    let fpY = baseCy - (flightPathDeg - pitchDeg) * 3;
    fpY = Math.max(0, Math.min(panelH - 1, fpY));
    const dy = fpY - baseCy;
    group.setAttribute('transform', `translate(0, ${dy})`);
  }

  // Seed at level (flightPath = pitch = 0).
  update({ pitchDeg: 0, flightPathDeg: 0 });

  return { el: group, update };
}
