import * as G from '../geometry.js';
import { colors } from '../colors.js';
import { slipFromLateralG } from '../slipBall.js';
import { mountDecelGauge } from '../widgets/decelGauge.js';
import { mountCornerReadout } from '../widgets/cornerReadout.js';
import { mountSlipBall } from '../widgets/slipBall.js';
import { mountEdgeTape } from '../widgets/edgeTape.js';

// Mode 3 — Energy / decel gauge (case 3 in main.cpp:637).
// Source: main.cpp::displayDecelGauge() at lines 1350-1432.
//
// Composition:
//   1. Vertical band gauge with sliding pointer (decel rate)
//   2. Slip ball (160×20 at y=215 — 11 px lower than Mode 0/1)
//   3. VSI right-edge tape (orange bar, light-grey ticks)
//   4. IAS top-left + Kt/s top-right corner readouts
export function mountEnergy(rootEl) {
  const SVG_NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}`);
  svg.setAttribute('xmlns', SVG_NS);
  svg.style.background = colors.TFT_BLACK;
  svg.style.width  = '100%';
  svg.style.height = '100%';

  const decel = mountDecelGauge(svg, {
    gaugeX: G.MODE3_GAUGE_X, gaugeY: G.MODE3_GAUGE_Y,
    gaugeW: G.MODE3_GAUGE_W, gaugeH: G.MODE3_GAUGE_H,
    gaugeRadius: G.MODE3_GAUGE_RADIUS,
    greenX: G.MODE3_GAUGE_GREEN_X, greenY: G.MODE3_GAUGE_GREEN_Y,
    greenW: G.MODE3_GAUGE_GREEN_W, greenH: G.MODE3_GAUGE_GREEN_H,
    pointerX: G.MODE3_POINTER_X, pointerW: G.MODE3_POINTER_W,
    pointerH: G.MODE3_POINTER_H, pointerHalfH: G.MODE3_POINTER_HALF_H,
    pointerYMin: G.MODE3_POINTER_Y_MIN, pointerYMax: G.MODE3_POINTER_Y_MAX,
    decelScale: G.MODE3_DECEL_SCALE, decelOffset: G.MODE3_DECEL_OFFSET,
    labelX: G.MODE3_GAUGE_LABEL_X, labels: G.MODE3_GAUGE_LABELS,
    labelFontSize: G.MODE3_GAUGE_LABEL_FONT_SIZE,
    pipX1: G.MODE3_PIP_X1, pipX2: G.MODE3_PIP_X2,
  });

  // Slip ball — Mode 3's narrow + lower layout (W=160, H=20, y=215).
  const slip = mountSlipBall(svg, {
    x: G.MODE3_SLIP_X, y: G.MODE3_SLIP_Y,
    width: G.MODE3_SLIP_W, height: G.MODE3_SLIP_H,
  });

  // VSI right-edge tape — same scale as Mode 1 but ticks are light-grey
  // (main.cpp:1397) instead of black. Reuses the Mode 1 constants.
  const vsi = mountEdgeTape(svg, {
    barX: G.MODE1_VSI_BAR_X, barW: G.MODE1_VSI_BAR_W,
    barColor: colors.TFT_ORANGE,
    zeroY: G.MODE1_VSI_ZERO_Y,
    heightScale: G.MODE1_VSI_HEIGHT_SCALE,
    heightMax:   G.MODE1_VSI_HEIGHT_MAX,
    tickX1: G.MODE1_VSI_TICK_X1, tickX2: G.MODE1_VSI_TICK_X2,
    tickFirstY: G.MODE1_VSI_TICK_FIRST_Y,
    tickLastY:  G.MODE1_VSI_TICK_LAST_Y,
    tickStep:   G.MODE1_VSI_TICK_STEP,
    pipX1: G.MODE1_VSI_PIP_X1, pipX2: G.MODE1_VSI_PIP_X2,
    pipYs: [G.MODE1_VSI_PIP_Y_TOP, G.MODE1_VSI_PIP_Y_MIDDLE, G.MODE1_VSI_PIP_Y_BOT],
    tickColor: colors.TFT_LIGHTGREY,
    pipColor:  colors.TFT_LIGHTGREY,
  });

  // Corner readouts — IAS top-left, Kt/s top-right. Same row geometry
  // as Mode 0's IAS/G corners.
  const ias = mountCornerReadout(svg, {
    labelText: 'IAS',
    labelX: G.MODE3_CORNER_LEFT_X, labelY: G.MODE3_CORNER_LABEL_Y,
    numX:    G.MODE3_CORNER_LEFT_NUM_X, numY: G.MODE3_CORNER_NUM_Y,
    labelAnchor: 'start',
    labelFontSize: G.CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.CORNER_NUM_FONT_SIZE,
  });
  const kts = mountCornerReadout(svg, {
    labelText: 'Kt/s',
    labelX: G.MODE3_CORNER_RIGHT_X, labelY: G.MODE3_CORNER_LABEL_Y,
    numX:    G.MODE3_CORNER_RIGHT_X, numY: G.MODE3_CORNER_NUM_Y,
    labelAnchor: 'end',
    labelFontSize: G.CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.CORNER_NUM_FONT_SIZE,
  });

  rootEl.appendChild(svg);

  const NUM_UPDATE_MS = 500;
  let numLastUpdateMs = 0;

  function update(rec) {
    const flashFlag = (Math.floor(performance.now() / 250) % 2) === 1;

    decel.update({ decelRate: rec.decelRate || 0 });
    slip.update({
      slip: slipFromLateralG(rec.lateralG),
      percentLift: rec.percentLift,
      stallWarn: rec.stallWarnPctLift,
      flashFlag,
    });
    vsi.update({ value: rec.vsiFpm });

    const now = performance.now();
    if (now - numLastUpdateMs >= NUM_UPDATE_MS) {
      ias.update({ value: rec.iasKt, formatter: v => String(Math.round(v)) });
      kts.update({ value: rec.decelRate || 0, formatter: v => (v >= 0 ? '+' : '') + v.toFixed(1) });
      numLastUpdateMs = now;
    }
  }

  return { el: svg, update };
}
