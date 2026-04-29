import * as G from '../geometry.js';
import { colors } from '../colors.js';
import { slipFromLateralG } from '../slipBall.js';
import { mountHorizon } from '../widgets/horizon.js';
import { mountPitchLadder } from '../widgets/pitchLadder.js';
import { mountBankArc } from '../widgets/bankArc.js';
import { mountTopPointer } from '../widgets/topPointer.js';
import { mountAircraftSymbol } from '../widgets/aircraftSymbol.js';
import { mountFlightPathMarker } from '../widgets/flightPathMarker.js';
import { mountPitchReadout } from '../widgets/pitchReadout.js';
import { mountCornerReadout } from '../widgets/cornerReadout.js';
import { mountSlipBall } from '../widgets/slipBall.js';
import { mountEdgeTape } from '../widgets/edgeTape.js';

// Build the Mode 1 (Attitude / Backup AI) SVG. Returns { el, update(record) }.
//
// Composition (paint order = SVG document order, back to front):
//   1. Horizon (sky + ground polygon + horizon line + center pivot dot)
//   2. Pitch ladder (tick marks + numeric labels, rotates with body)
//   3. Aircraft reference symbol (fixed yellow glyph, no rotation)
//   4. Flight path marker (magenta rings + bars, vertical-only)
//   5. Pitch readout box (small dark rect with numeric pitch)
//   6. Slip ball (160×20, NOT Mode 0's 160×34)
//   7. VSI tape (right-edge orange bar + ladder + zero pip)
//   8. Corner readouts (4 corners — IAS / PALT / G / AOA%)
export function mountAttitude(rootEl) {
  const SVG_NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}`);
  svg.setAttribute('xmlns', SVG_NS);
  svg.style.background = colors.TFT_BLACK;
  svg.style.width  = '100%';
  svg.style.height = '100%';

  const horizon       = mountHorizon(svg);
  const pitchLadder   = mountPitchLadder(svg);
  // Bank-angle arc markers paint AFTER horizon/ladder, BEFORE the
  // aircraft symbol — matches the C++ paint order at main.cpp:1136-1248.
  const bankArc       = mountBankArc(svg);
  // Static "this is up" yellow triangle at the top of the dial. Paints
  // BEFORE the aircraft symbol so the aircraft circle/wings are on
  // top if anything ever overlaps. Static — no per-frame update.
  /* eslint-disable-next-line no-unused-vars */
  const topPointer    = mountTopPointer(svg);
  /* eslint-disable-next-line no-unused-vars */
  const aircraft      = mountAircraftSymbol(svg);  // static, no per-frame update
  const fpv           = mountFlightPathMarker(svg);
  const pitchRO       = mountPitchReadout(svg);

  // Slip ball — Mode 1's narrow layout (W=160, H=20).
  const slip = mountSlipBall(svg, {
    x: G.MODE1_SLIP_X, y: G.MODE1_SLIP_Y,
    width: G.MODE1_SLIP_W, height: G.MODE1_SLIP_H,
  });

  // VSI right-edge tape — same widget as Mode 0's gOnset but with the
  // VSI scale (120/600 px/fpm), an orange bar, and the Mode 1 tick
  // ladder (every 20 px from y=19 to y=219).
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
    // Mode 1's VSI ladder ticks and zero pip are TFT_BLACK
    // (main.cpp:615, 619-621). Mode 0's gOnset uses grey by default.
    tickColor: colors.TFT_BLACK,
    pipColor:  colors.TFT_BLACK,
  });

  // Four corner readouts. main.cpp:540-595.
  // Number colors per main.cpp:577-594:
  //   - IAS (top-left) and PALT (top-right): TFT_BLACK on the sky
  //   - G (bottom-left) and AOA% (bottom-right): TFT_WHITE on the ground
  const ias = mountCornerReadout(svg, {
    labelText: 'IAS',
    labelX: G.MODE1_CORNER_LEFT_X, labelY: G.MODE1_CORNER_TOP_LABEL_Y,
    numX:    G.MODE1_CORNER_LEFT_X, numY: G.MODE1_CORNER_TOP_NUM_Y,
    labelAnchor: 'start',
    labelColor: colors.TFT_GREY,
    numColor:   colors.TFT_BLACK,
    labelFontSize: G.MODE1_CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.MODE1_CORNER_NUM_FONT_SIZE,
    // Center the digit vertically on the topmost ladder tick (y=29).
    numBaseline: 'central',
  });
  const palt = mountCornerReadout(svg, {
    labelText: 'PALT',
    labelX: G.MODE1_CORNER_RIGHT_X, labelY: G.MODE1_CORNER_TOP_LABEL_Y,
    // Number x shifted -3 from label x so the digit's right edge lines
    // up with the right edge of the "T" in "PALT".
    numX:    G.MODE1_CORNER_RIGHT_NUM_X, numY: G.MODE1_CORNER_TOP_NUM_Y,
    labelAnchor: 'end',
    labelColor: colors.TFT_GREY,
    numColor:   colors.TFT_BLACK,
    labelFontSize: G.MODE1_CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.MODE1_CORNER_NUM_FONT_SIZE,
    // Center the digit vertically on the topmost ladder tick (y=29).
    numBaseline: 'central',
  });
  const gReadout = mountCornerReadout(svg, {
    labelText: 'G',
    labelX: G.MODE1_CORNER_LEFT_X, labelY: G.MODE1_CORNER_BOT_LABEL_Y,
    numX:    G.MODE1_CORNER_LEFT_X, numY: G.MODE1_CORNER_BOT_NUM_Y,
    labelAnchor: 'start',
    labelColor: colors.TFT_LIGHTGREY,
    numColor:   colors.TFT_WHITE,
    labelFontSize: G.MODE1_CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.MODE1_CORNER_NUM_FONT_SIZE,
  });
  const aoa = mountCornerReadout(svg, {
    labelText: 'AOA',
    labelX: G.MODE1_CORNER_RIGHT_X, labelY: G.MODE1_CORNER_BOT_LABEL_Y,
    // Number x shifted -3 from label x so the digit's right edge lines
    // up with the right edge of the "A" in "AOA".
    numX:    G.MODE1_CORNER_RIGHT_NUM_X, numY: G.MODE1_CORNER_BOT_NUM_Y,
    labelAnchor: 'end',
    labelColor: colors.TFT_LIGHTGREY,
    numColor:   colors.TFT_WHITE,
    labelFontSize: G.MODE1_CORNER_LABEL_FONT_SIZE,
    numFontSize:   G.MODE1_CORNER_NUM_FONT_SIZE,
  });

  rootEl.appendChild(svg);

  // 500 ms gate for numeric text fields, matching the M5's
  // updateRateNumbers cadence (main.cpp:156). Bars (VSI), horizon, pitch
  // ladder, FPV marker, and slip ball animation all update every frame.
  const NUM_UPDATE_MS = 500;
  let numLastUpdateMs = 0;

  function update(rec) {
    // Flash flag for the slip ball's stall flash (same 250 ms cadence
    // as Mode 0).
    const flashFlag = (Math.floor(performance.now() / 250) % 2) === 1;

    // Per-frame updates.
    horizon.update({ pitchDeg: rec.pitchDeg, rollDeg: rec.rollDeg });
    pitchLadder.update({ pitchDeg: rec.pitchDeg, rollDeg: rec.rollDeg });
    bankArc.update({ rollDeg: rec.rollDeg });
    fpv.update({ pitchDeg: rec.pitchDeg, flightPathDeg: rec.flightPathDeg });
    slip.update({
      slip: slipFromLateralG(rec.lateralG),
      percentLift: rec.percentLift,
      stallWarn: rec.stallWarnPctLift,
      flashFlag,
    });
    vsi.update({ value: rec.vsiFpm });

    // Numeric-readout 500 ms gate.
    const now = performance.now();
    if (now - numLastUpdateMs >= NUM_UPDATE_MS) {
      ias.update({ value: rec.iasKt, formatter: v => String(Math.round(v)) });
      palt.update({ value: rec.paltFt, formatter: v => String(Math.round(v)).padStart(5, ' ') });
      gReadout.update({ value: rec.verticalG, formatter: v => v.toFixed(1) });
      aoa.update({ value: rec.percentLift, formatter: v => String(Math.round(v)).padStart(2, '0') });
      pitchRO.update({ pitchDeg: rec.pitchDeg });
      numLastUpdateMs = now;
    }
  }

  return { el: svg, update };
}
