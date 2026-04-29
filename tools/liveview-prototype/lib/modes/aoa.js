import * as G from '../geometry.js';
import { colors } from '../colors.js';
import { slipFromLateralG } from '../slipBall.js';
import { mountIndexer } from '../widgets/indexer.js';
import { mountPercentLiftNumber } from '../widgets/percentLiftNumber.js';
import { mountCornerReadout } from '../widgets/cornerReadout.js';
import { mountFlapCircle } from '../widgets/flapCircle.js';
import { mountSlipBall } from '../widgets/slipBall.js';
import { mountEdgeTape } from '../widgets/edgeTape.js';

// Build the mode-0 SVG once. Returns { el, update(record) }.
//
// This file is composition only — every visual element is a `mountX` widget
// from `lib/widgets/`. Mode 1 (Attitude), Mode 2 (Indexer-only), Mode 3
// (Energy), and Mode 4 (G-history) reuse the same widgets with mode-specific
// geometry.
export function mountAoa(rootEl) {
  const SVG_NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}`);
  svg.setAttribute('xmlns', SVG_NS);
  svg.style.background = colors.TFT_BLACK;
  svg.style.width  = '100%';
  svg.style.height = '100%';

  // Indexer (chevrons + donut + index bar + L/Dmax pips + bounding rect).
  const indexer = mountIndexer(svg);

  // Corner readouts — IAS top-left, G top-right.
  const ias = mountCornerReadout(svg, {
    labelText: 'IAS',
    labelX: G.CORNER_LEFT_X, labelY: G.CORNER_LABEL_Y,
    numX:    G.CORNER_LEFT_X + 2, numY: G.CORNER_NUM_Y,
    labelAnchor: 'start',
    labelFontSize: G.CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.CORNER_NUM_FONT_SIZE,
  });
  const gReadout = mountCornerReadout(svg, {
    labelText: 'G',
    labelX: G.CORNER_RIGHT_X, labelY: G.CORNER_LABEL_Y,
    numX:    G.CORNER_RIGHT_X, numY: G.CORNER_NUM_Y,
    labelAnchor: 'end',
    labelFontSize: G.CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.CORNER_NUM_FONT_SIZE,
  });

  // Flap circle + numeric label + stop dots.
  const flap = mountFlapCircle(svg, {
    cx: G.FLAP_CX, cy: G.FLAP_CY, r: G.FLAP_R,
    triangleTipR: G.FLAP_TRIANGLE_TIP_R,
    arcRad: G.FLAP_ARC_RAD,
    stopR:  G.FLAP_STOP_R,
  });

  // Slip ball — Mode 0's wide layout (W=160, H=34).
  const slip = mountSlipBall(svg, {
    x: G.SLIP_X, y: G.SLIP_Y, width: G.SLIP_W, height: G.SLIP_H,
  });

  // G-onset right-edge tape.
  const gOnset = mountEdgeTape(svg, {
    barX: G.GONSET_BAR_X, barW: G.GONSET_BAR_W,
    barColor: colors.TFT_YELLOW,
    zeroY: G.GONSET_ZERO_Y,
    heightScale: G.GONSET_HEIGHT_SCALE,
    heightMax:   G.GONSET_HEIGHT_MAX,
    tickX1: G.GONSET_TICK_X1, tickX2: G.GONSET_TICK_X2,
    tickFirstY: G.GONSET_TICK_FIRST_Y,
    tickLastY:  G.GONSET_TICK_LAST_Y,
    tickStep:   G.GONSET_TICK_STEP,
    pipX1: G.GONSET_PIP_X1, pipX2: G.GONSET_PIP_X2,
    pipYs: [G.GONSET_PIP_Y_TOP, G.GONSET_PIP_Y_MIDDLE, G.GONSET_PIP_Y_BOT],
  });

  // Percent-lift number drawn LAST so it stays on top of the index bar at
  // high AOA (where the bar rises into the percent's y-band).
  const pctLift = mountPercentLiftNumber(svg, {
    cx: G.PCT_LIFT_X, baselineY: G.PCT_LIFT_Y,
    fontSize: G.PCT_LIFT_FONT_SIZE,
    outlinePx: G.PCT_LIFT_OUTLINE_PX,
  });

  rootEl.appendChild(svg);

  function update(rec) {
    // Build anchors array in the slot convention pct2y/chevron/donut expect.
    // Mirrors PctAnchors[] populated by displayAOA() in main.cpp:719-726.
    const anchors = [
      0,                          // [0] alpha_0 floor
      0,                          // [1] unused
      rec.tonesOnPctLift,         // [2] tonesOn — chevron + audio gate
      rec.onSpeedFastPctLift,     // [3] OnSpeedFast — donut bottom edge
      rec.onSpeedSlowPctLift,     // [4] OnSpeedSlow — donut top / chevron lower gate
      0,                          // [5] unused
      rec.pipPctLift,             // [6] L/Dmax pip
      rec.stallWarnPctLift,       // [7] StallWarn — top-chevron flash threshold
    ];
    // 250 ms flash flag — same cadence as flashFlag in main.cpp's loop().
    const flashFlag = (Math.floor(performance.now() / 250) % 2) === 1;

    indexer.update({ percentLift: rec.percentLift, anchors, flashFlag });
    ias.update({ value: rec.iasKt, formatter: v => String(Math.round(v)) });
    gReadout.update({ value: rec.verticalG, formatter: v => (v >= 0 ? '+' : '') + v.toFixed(1) });
    flap.update({ flapPos: rec.flapsDeg, flapsMin: rec.flapsMinDeg, flapsMax: rec.flapsMaxDeg });
    slip.update({
      slip: slipFromLateralG(rec.lateralG),
      percentLift: rec.percentLift,
      stallWarn: rec.stallWarnPctLift,
      flashFlag,
    });
    gOnset.update({ value: rec.gOnsetRate });
    pctLift.update({ percent: rec.percentLift });
  }

  return { el: svg, update };
}
