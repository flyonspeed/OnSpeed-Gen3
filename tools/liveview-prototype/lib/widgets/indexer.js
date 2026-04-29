// Indexer widget: bounding rect + 4 chevron halves + 3-segment donut + index
// bar + L/Dmax pip dots. Composes the chevron/donut/pct2y math modules with
// SVG mounting so multiple modes (Mode 0 AOA, Mode 2 Indexer-only) can reuse
// it without duplicating the structure.
//
// Geometry references inline cite main.cpp source lines so future readers
// can cross-check with the M5 firmware.

import * as G from '../geometry.js';
import { colors } from '../colors.js';
import { mapPct2Display } from '../pct2y.js';
import { chevronColors } from '../chevronColors.js';
import { donutColors } from '../donutColors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

// SVG arc path helper — circle arc from angle start to end (radians).
// SVG y-axis is down, so positive rotation is clockwise.
function arcPath(cx, cy, r, startRad, endRad) {
  const x1 = cx + r * Math.cos(startRad);
  const y1 = cy + r * Math.sin(startRad);
  const x2 = cx + r * Math.cos(endRad);
  const y2 = cy + r * Math.sin(endRad);
  const largeArc = (endRad - startRad) > Math.PI ? 1 : 0;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${largeArc} 1 ${x2} ${y2}`;
}

// Mount the indexer into `parent`. Returns { el, update({percentLift, anchors, flashFlag}) }.
export function mountIndexer(parent) {
  const group = mk(parent, 'g', { 'data-widget': 'indexer' });

  // Bounding rounded rect (drawAOA() main.cpp:887-888 — two concentric rects
  // at TFT_DARKGREY for a 2 px border).
  mk(group, 'rect', {
    x: G.INDEXER_X, y: G.INDEXER_Y,
    width: G.INDEXER_WIDTH, height: G.INDEXER_HEIGHT,
    rx: G.INDEXER_BOX_RADIUS,
    fill: 'none', stroke: colors.TFT_DARKGREY, 'stroke-width': 2,
  });

  // Chevron halves. Each is a rotated rect.
  const drawChevronHalf = (cx, cy, signRad) => mk(group, 'rect', {
    x: cx - G.CHEVRON_HALF_W,
    y: cy - G.CHEVRON_HALF_H,
    width: G.CHEVRON_HALF_W * 2,
    height: G.CHEVRON_HALF_H * 2,
    transform: `rotate(${signRad * 180 / Math.PI} ${cx} ${cy})`,
    fill: colors.TFT_DARKGREY,
    'shape-rendering': 'crispEdges',
  });
  const chev = {
    topLeft:     drawChevronHalf(G.CHEVRON_TOP_LEFT_CX,     G.CHEVRON_TOP_LEFT_CY,    -G.CHEVRON_ROTATION_RAD),
    topRight:    drawChevronHalf(G.CHEVRON_TOP_RIGHT_CX,    G.CHEVRON_TOP_RIGHT_CY,    G.CHEVRON_ROTATION_RAD),
    bottomLeft:  drawChevronHalf(G.CHEVRON_BOTTOM_LEFT_CX,  G.CHEVRON_BOTTOM_LEFT_CY,  G.CHEVRON_ROTATION_RAD),
    bottomRight: drawChevronHalf(G.CHEVRON_BOTTOM_RIGHT_CX, G.CHEVRON_BOTTOM_RIGHT_CY, -G.CHEVRON_ROTATION_RAD),
  };

  // Donut: black surround, then top/bottom arcs + gap rect, then center dot.
  mk(group, 'circle', { cx: G.INDEXER_CX, cy: G.INDEXER_CY, r: G.DONUT_BLACK_R, fill: colors.TFT_BLACK });
  const donutBottomArc = mk(group, 'path', {
    d: arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, 0, Math.PI),
    fill: 'none', stroke: colors.TFT_DARKGREY, 'stroke-width': G.DONUT_ARC_LINEWIDTH,
  });
  const donutTopArc = mk(group, 'path', {
    d: arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, Math.PI, 2 * Math.PI),
    fill: 'none', stroke: colors.TFT_DARKGREY, 'stroke-width': G.DONUT_ARC_LINEWIDTH,
  });
  mk(group, 'rect', {
    x: G.DONUT_GAP_X, y: G.DONUT_GAP_Y,
    width: G.DONUT_GAP_W, height: G.DONUT_GAP_H,
    fill: colors.TFT_BLACK,
  });
  const donutDot = mk(group, 'circle', {
    cx: G.INDEXER_CX, cy: G.INDEXER_CY, r: G.DONUT_DOT_R, fill: colors.TFT_DARKGREY,
  });

  // Index bar (the moving white horizontal that shows current AOA).
  // C++ at main.cpp:1024-1026: fillRect TFT_WHITE then drawRect TFT_BLACK
  // — i.e. white interior with a 1-px black border on all 4 sides.
  // shape-rendering: crispEdges so SVG renders the border at exactly 1 px
  // without antialiased blur.
  //
  // Mounted hidden — the legacy /live did the same with `visibility:
  // hidden` on its `aoaline` rect, gating visibility on `AOA > -20`
  // (the N/A sentinel boundary). Without the gate, percentLift=0 on
  // first load would map to the bottom of the indexer — a misleading
  // "0% lift, you're stalled" indication. First update() unhides.
  const indexBar = mk(group, 'rect', {
    x: G.INDEX_BAR_X, y: 192, width: G.INDEX_BAR_W, height: G.INDEX_BAR_H,
    fill: colors.TFT_WHITE,
    stroke: colors.TFT_BLACK,
    'stroke-width': 1,
    'shape-rendering': 'crispEdges',
    visibility: 'hidden',
  });

  // L/Dmax pip dots — black halo + white inner, both sides.
  const pipLeftHalo  = mk(group, 'circle', { cx: G.PIP_LEFT_CX,  cy: 192, r: G.PIP_HALO_R,  fill: colors.TFT_BLACK });
  const pipLeftInner = mk(group, 'circle', { cx: G.PIP_LEFT_CX,  cy: 192, r: G.PIP_INNER_R, fill: colors.TFT_WHITE });
  const pipRightHalo  = mk(group, 'circle', { cx: G.PIP_RIGHT_CX, cy: 192, r: G.PIP_HALO_R,  fill: colors.TFT_BLACK });
  const pipRightInner = mk(group, 'circle', { cx: G.PIP_RIGHT_CX, cy: 192, r: G.PIP_INNER_R, fill: colors.TFT_WHITE });

  function update({ percentLift, anchors, flashFlag, aoaIsValid = true }) {
    // Hide the index bar when AOA is invalid (N/A sentinel). Same gate
    // the legacy /live used (visibility: hidden when AOA <= -20).
    indexBar.setAttribute('visibility', aoaIsValid ? 'visible' : 'hidden');

    const indexY = mapPct2Display(percentLift, anchors);
    indexBar.setAttribute('y', indexY);

    const ldmaxY = mapPct2Display(anchors[6], anchors);
    pipLeftHalo.setAttribute('cy', ldmaxY);
    pipLeftInner.setAttribute('cy', ldmaxY);
    pipRightHalo.setAttribute('cy', ldmaxY);
    pipRightInner.setAttribute('cy', ldmaxY);

    const c = chevronColors({ percentLift, anchors, flashFlag });
    chev.topLeft.setAttribute('fill',     c.top);
    chev.topRight.setAttribute('fill',    c.top);
    chev.bottomLeft.setAttribute('fill',  c.bottom);
    chev.bottomRight.setAttribute('fill', c.bottom);

    const d = donutColors({ percentLift, anchors });
    donutBottomArc.setAttribute('stroke', d.bottomArc);
    donutTopArc.setAttribute('stroke', d.topArc);
    donutDot.setAttribute('fill', d.dot);
  }

  return { el: group, update };
}
