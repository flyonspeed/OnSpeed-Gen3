import * as G from '../geometry.js';
import { colors } from '../colors.js';
import { mapPct2Display } from '../pct2y.js';
import { chevronColors } from '../chevronColors.js';
import { donutColors } from '../donutColors.js';
import { slipBall, slipFromLateralG } from '../slipBall.js';
import { flapWidgetFrac, flapTriangleTransform } from '../flapWidget.js';

// Build the mode-0 SVG once. Returns { el, update(record) }.
//
// Geometry references inline cite main.cpp source lines so future readers
// can cross-check with the M5 firmware. SVG uses `shape-rendering="crispEdges"`
// on the chevrons and pip elements where pixel sharpness matters; text
// nodes use system sans-serif at sizes tuned to FreeSans visually.
export function mountAoa(rootEl) {
  const SVG_NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}`);
  svg.setAttribute('xmlns', SVG_NS);
  svg.style.background = colors.TFT_BLACK;
  svg.style.width  = '100%';
  svg.style.height = '100%';

  // ---- Helper: construct + append element with attrs ----
  const mk = (tag, attrs, parent = svg) => {
    const e = document.createElementNS(SVG_NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    parent.appendChild(e);
    return e;
  };

  // ---- Indexer bounding rounded rect ----
  // drawAOA() main.cpp:887-888 draws two concentric rounded rects in
  // TFT_DARKGREY (1 px outline). One stroked rect approximates that here.
  mk('rect', {
    x: G.INDEXER_X,
    y: G.INDEXER_Y,
    width: G.INDEXER_WIDTH,
    height: G.INDEXER_HEIGHT,
    rx: G.INDEXER_BOX_RADIUS,
    fill: 'none',
    stroke: colors.TFT_DARKGREY,
    'stroke-width': 1,
  });

  // ---- Chevrons (4 halves, each is a rotated rectangle drawn as <rect> with transform) ----
  // The C++ renders each half as two filled triangles with explicit
  // pre-rotated corner coordinates (drawAOA() main.cpp:890-991). The SVG
  // equivalent is a rect of size (2*HALF_W) × (2*HALF_H) rotated about the
  // half-center.
  const chevronEls = {};
  const drawChevronHalf = (cx, cy, signRad) => {
    const angleDeg = (signRad * 180 / Math.PI);
    return mk('rect', {
      x: cx - G.CHEVRON_HALF_W,
      y: cy - G.CHEVRON_HALF_H,
      width: G.CHEVRON_HALF_W * 2,
      height: G.CHEVRON_HALF_H * 2,
      transform: `rotate(${angleDeg} ${cx} ${cy})`,
      fill: colors.TFT_DARKGREY,
      'shape-rendering': 'crispEdges',
    });
  };
  // Top-left half rotates by -π/8; top-right by +π/8 (drawAOA :904, :923).
  // Bottom rows mirror: bottom-left +π/8, bottom-right -π/8 (:955, :974).
  chevronEls.topLeft     = drawChevronHalf(G.CHEVRON_TOP_LEFT_CX,     G.CHEVRON_TOP_LEFT_CY,    -G.CHEVRON_ROTATION_RAD);
  chevronEls.topRight    = drawChevronHalf(G.CHEVRON_TOP_RIGHT_CX,    G.CHEVRON_TOP_RIGHT_CY,    G.CHEVRON_ROTATION_RAD);
  chevronEls.bottomLeft  = drawChevronHalf(G.CHEVRON_BOTTOM_LEFT_CX,  G.CHEVRON_BOTTOM_LEFT_CY,  G.CHEVRON_ROTATION_RAD);
  chevronEls.bottomRight = drawChevronHalf(G.CHEVRON_BOTTOM_RIGHT_CX, G.CHEVRON_BOTTOM_RIGHT_CY, -G.CHEVRON_ROTATION_RAD);

  // ---- Donut: black surround, then top/bottom arcs, then center dot ----
  // drawAOA() main.cpp:996-1019.
  // Black surround circle behind the arcs (:997).
  mk('circle', { cx: G.INDEXER_CX, cy: G.INDEXER_CY, r: G.DONUT_BLACK_R, fill: colors.TFT_BLACK });
  // Bottom arc (:1004-1006): drawArc from 0 to π. drawArc in M5GFX uses
  // start/end in radians measured from +x going CCW, so 0..π is the
  // bottom half on a y-down panel (y=sin grows downward).
  const donutBottomArc = mk('path', {
    d: arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, 0, Math.PI),
    fill: 'none',
    stroke: colors.TFT_DARKGREY,
    'stroke-width': G.DONUT_ARC_LINEWIDTH,
  });
  // Top arc (:1009-1011): from π to 2π (the upper half).
  const donutTopArc = mk('path', {
    d: arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, Math.PI, 2 * Math.PI),
    fill: 'none',
    stroke: colors.TFT_DARKGREY,
    'stroke-width': G.DONUT_ARC_LINEWIDTH,
  });
  // Black gap rect between the two arcs (:1014).
  mk('rect', {
    x: G.DONUT_GAP_X, y: G.DONUT_GAP_Y,
    width: G.DONUT_GAP_W, height: G.DONUT_GAP_H,
    fill: colors.TFT_BLACK,
  });
  // Center dot (:1019).
  const donutDot = mk('circle', {
    cx: G.INDEXER_CX, cy: G.INDEXER_CY,
    r: G.DONUT_DOT_R, fill: colors.TFT_DARKGREY,
  });

  // ---- Index bar (white moving horizontal) ----
  // drawAOA() :1025-1026.
  const indexBar = mk('rect', {
    x: G.INDEX_BAR_X, y: 192, width: G.INDEX_BAR_W, height: G.INDEX_BAR_H,
    fill: colors.TFT_WHITE,
    stroke: colors.TFT_BLACK,
    'stroke-width': 0.5,
  });

  // ---- L/Dmax pip dots (left + right), black halo + white inner ----
  // drawAOA() :1037-1040.
  const pipLeftHalo  = mk('circle', { cx: G.PIP_LEFT_CX,  cy: 192, r: G.PIP_HALO_R,  fill: colors.TFT_BLACK });
  const pipLeftInner = mk('circle', { cx: G.PIP_LEFT_CX,  cy: 192, r: G.PIP_INNER_R, fill: colors.TFT_WHITE });
  const pipRightHalo  = mk('circle', { cx: G.PIP_RIGHT_CX, cy: 192, r: G.PIP_HALO_R,  fill: colors.TFT_BLACK });
  const pipRightInner = mk('circle', { cx: G.PIP_RIGHT_CX, cy: 192, r: G.PIP_INNER_R, fill: colors.TFT_WHITE });

  // ---- Percent-lift number above indexer ----
  // displayAOA() :735-753: bold FreeSans 18pt with a 9-copy black outline at
  // ±3 px. SVG `paint-order: stroke` on a single text node approximates the
  // outline with one element instead of nine.
  const pctLiftText = mk('text', {
    x: G.PCT_LIFT_X, y: G.PCT_LIFT_Y,
    'font-family': 'Helvetica, Arial, sans-serif',
    'font-weight': 'bold',
    'font-size': G.PCT_LIFT_FONT_SIZE,
    fill: colors.TFT_WHITE,
    stroke: colors.TFT_BLACK,
    'stroke-width': G.PCT_LIFT_OUTLINE_PX,
    'paint-order': 'stroke',
    'dominant-baseline': 'alphabetic',
    'text-anchor': 'start',
  });
  pctLiftText.textContent = '00';

  // ---- Corner readouts ----
  // displayAOA() :762-779: IAS top-left, G top-right.
  const iasLabel = mk('text', {
    x: G.CORNER_LEFT_X, y: G.CORNER_LABEL_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-size': G.CORNER_LABEL_FONT_SIZE,
    fill: colors.TFT_GREEN, 'text-anchor': 'start',
  });
  iasLabel.textContent = 'IAS';
  const iasNum = mk('text', {
    x: G.CORNER_LEFT_X + 2, y: G.CORNER_NUM_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-weight': 'bold',
    'font-size': G.CORNER_NUM_FONT_SIZE, fill: colors.TFT_WHITE, 'text-anchor': 'start',
  });
  iasNum.textContent = '0';
  const gLabel = mk('text', {
    x: G.CORNER_RIGHT_X, y: G.CORNER_LABEL_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-size': G.CORNER_LABEL_FONT_SIZE,
    fill: colors.TFT_GREEN, 'text-anchor': 'end',
  });
  gLabel.textContent = 'G';
  const gNum = mk('text', {
    x: G.CORNER_RIGHT_X, y: G.CORNER_NUM_Y,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-weight': 'bold',
    'font-size': G.CORNER_NUM_FONT_SIZE, fill: colors.TFT_WHITE, 'text-anchor': 'end',
  });
  gNum.textContent = '+1.0';

  // ---- Flap circle widget ----
  // displayAOA() :783-827.
  mk('circle', { cx: G.FLAP_CX, cy: G.FLAP_CY, r: G.FLAP_R, fill: colors.TFT_GREY });
  const flapTriangle = mk('path', {
    fill: colors.TFT_GREY,
    'shape-rendering': 'crispEdges',
  });
  const flapAngleText = mk('text', {
    x: G.FLAP_CX, y: G.FLAP_CY + 4,
    'font-family': 'Helvetica, Arial, sans-serif', 'font-size': 12,
    fill: colors.TFT_WHITE, 'text-anchor': 'middle',
  });
  flapAngleText.textContent = '0';

  // ---- Slip ball ----
  // drawSlip() main.cpp:1066-1069: 4 rects, 2 black + 2 white tick frames
  // either side of the ball, total 16 px wide each side.
  mk('rect', { x: G.SLIP_CENTER_X - G.SLIP_H / 2 - 9, y: G.SLIP_Y, width: 10, height: G.SLIP_H, fill: colors.TFT_BLACK });
  mk('rect', { x: G.SLIP_CENTER_X - G.SLIP_H / 2 - 7, y: G.SLIP_Y, width:  6, height: G.SLIP_H, fill: colors.TFT_WHITE });
  mk('rect', { x: G.SLIP_CENTER_X + G.SLIP_H / 2,     y: G.SLIP_Y, width: 10, height: G.SLIP_H, fill: colors.TFT_BLACK });
  mk('rect', { x: G.SLIP_CENTER_X + G.SLIP_H / 2 + 2, y: G.SLIP_Y, width:  6, height: G.SLIP_H, fill: colors.TFT_WHITE });
  const slipBallEl = mk('circle', {
    cx: G.SLIP_CENTER_X, cy: G.SLIP_CENTER_Y, r: G.SLIP_BALL_R, fill: colors.TFT_GREEN,
  });

  // ---- G-onset right-edge tape ----
  // displayAOA() :838-857. Tick ladder: lines at every TICK_STEP from FIRST_Y
  // through LAST_Y. Zero pip is 3 horizontal 7-px lines at y=118..120.
  for (let y = G.GONSET_TICK_FIRST_Y; y <= G.GONSET_TICK_LAST_Y; y += G.GONSET_TICK_STEP) {
    mk('line', {
      x1: G.GONSET_TICK_X1, y1: y, x2: G.GONSET_TICK_X2, y2: y,
      stroke: colors.TFT_GREY, 'stroke-width': 1,
    });
  }
  for (const y of [G.GONSET_PIP_Y_TOP, G.GONSET_PIP_Y_MIDDLE, G.GONSET_PIP_Y_BOT]) {
    mk('line', {
      x1: G.GONSET_PIP_X1, y1: y, x2: G.GONSET_PIP_X2, y2: y,
      stroke: colors.TFT_GREY, 'stroke-width': 1,
    });
  }
  // Onset bar — a fillRect updated each frame.
  const onsetBar = mk('rect', {
    x: G.GONSET_BAR_X, y: G.GONSET_ZERO_Y, width: G.GONSET_BAR_W, height: 0,
    fill: colors.TFT_YELLOW,
  });

  rootEl.appendChild(svg);

  // ---- Update function (called per data tick in Task 1.2) ----
  function update(rec) {
    // Stub for now — Task 1.2 fills this in.
  }

  return { el: svg, update };
}

// SVG arc path helper — circle arc from angle start to end (radians).
// SVG y-axis is down, so positive rotation is clockwise.
function arcPath(cx, cy, r, startRad, endRad) {
  const x1 = cx + r * Math.cos(startRad);
  const y1 = cy + r * Math.sin(startRad);
  const x2 = cx + r * Math.cos(endRad);
  const y2 = cy + r * Math.sin(endRad);
  const largeArc = (endRad - startRad) > Math.PI ? 1 : 0;
  const sweep = 1;  // clockwise in SVG (y-down)
  return `M ${x1} ${y1} A ${r} ${r} 0 ${largeArc} ${sweep} ${x2} ${y2}`;
}
