// All five OnSpeed display modes as Preact components. Each mode is a
// pure function of `r` (the WebSocket record) plus a `stale` flag for
// the M5-style stale-data overlay.

import { html } from './vendor/preact-standalone.js';
import * as G from './core/geometry.js';
import { colors } from './core/colors.js';
import { fmt, fmtSigned } from './core/format.js';
import {
  Indexer, PercentLiftNumber, CornerReadout, DataMark, FlapCircle, SlipBall, EdgeTape,
  Horizon, PitchLadder, BankArc, AircraftSymbol, TopPointer, FlightPathMarker,
  PitchReadout, DecelGauge, GHistory, StaleOverlay,
} from './components/svg/index.js';

// Format a numeric corner readout. The M5 hardware (main.cpp:773-779)
// shows IAS / G / PALT unconditionally — they're independent signals
// and don't share AOA's mute-below-threshold gate. We mirror that here:
// only `undefined` / `null` / `NaN` (i.e. before the first WebSocket
// frame arrives) collapses to '—'. Once data is on the wire, every
// number renders, even when the IAS-mute gate has set AOA to its
// `-100` sentinel.
//
// Delegates to `fmt`/`fmtSigned` from core/format.js so values that
// round to -0.0 render as 0.0 (or +0.0 in signed contexts).
const fmtNum = (v, digits = 0, signed = false) =>
  signed ? fmtSigned(v, digits) : fmt(v, digits);

// Build the per-frame anchors array from a record.
const anchorsFromRec = (r) => [
  0, 0,
  r.tonesOnPctLift,
  r.onSpeedFastPctLift,
  r.onSpeedSlowPctLift,
  0,
  r.pipPctLift,
  r.stallWarnPctLift,
];

const flashFlagNow = () => (Math.floor(performance.now() / 250) % 2) === 1;

const Panel = ({ children, stale }) => html`
  <svg viewBox="0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}"
       xmlns="http://www.w3.org/2000/svg"
       style="background: ${colors.TFT_BLACK}; width: 100%; height: 100%;">
    ${children}
    <${StaleOverlay} stale=${stale} />
  </svg>`;

// --- Mode 0: AOA Primary --------------------------------------------------
//
// Mode 2 is the same renderer with `numericDisplay=false` — corners + flap
// circle hide, slip ball + gOnset stay, indexer + percent number stay.
// Matches the C++ structure at main.cpp:511-635 where both cases call
// displayAOA() with the gate flag.
export const Mode0 = ({ r, stale, numericDisplay = true }) => {
  const anchors = anchorsFromRec(r);
  const flashFlag = flashFlagNow();
  const aoaIsValid = r.aoaIsValid !== false;
  return html`
    <${Panel} stale=${stale}>
      <${Indexer} percentLift=${r.percentLift} anchors=${anchors}
                  flashFlag=${flashFlag} aoaIsValid=${aoaIsValid} />
      ${numericDisplay && html`
        <${CornerReadout} label="IAS"
            value=${fmtNum(r.iasKt, 0)}
            labelX=${G.CORNER_LEFT_X} labelY=${G.CORNER_LABEL_Y}
            numX=${G.CORNER_LEFT_X + 2} numY=${G.CORNER_NUM_Y} />
        <${CornerReadout} label="G"
            value=${fmtNum(r.verticalG, 1, true)}
            labelX=${G.CORNER_RIGHT_X} labelY=${G.CORNER_LABEL_Y}
            numX=${G.CORNER_RIGHT_X} numY=${G.CORNER_NUM_Y} anchor="end" />
        <${FlapCircle} flapPos=${r.flapsDeg}
            flapsMin=${r.flapsMinDeg} flapsMax=${r.flapsMaxDeg} />`}
      <${SlipBall} lateralG=${r.lateralG} percentLift=${r.percentLift}
                   stallWarn=${r.stallWarnPctLift} flashFlag=${flashFlag} />
      <${EdgeTape} value=${r.gOnsetRate} />
      <${PercentLiftNumber} percent=${r.percentLift} aoaIsValid=${aoaIsValid} />
      <${DataMark} value=${r.dataMark} />
    <//>`;
};

// --- Mode 1: Backup AI ---------------------------------------------------
export const Mode1 = ({ r, stale }) => {
  const flashFlag = flashFlagNow();
  const aoaIsValid = r.aoaIsValid !== false;
  return html`
    <${Panel} stale=${stale}>
      <${Horizon}     pitchDeg=${r.pitchDeg} rollDeg=${r.rollDeg} />
      <${PitchLadder} pitchDeg=${r.pitchDeg} rollDeg=${r.rollDeg} />
      <${BankArc}     rollDeg=${r.rollDeg} />
      <${TopPointer} />
      <${AircraftSymbol} />
      <${FlightPathMarker} pitchDeg=${r.pitchDeg} flightPathDeg=${r.flightPathDeg} />
      <${PitchReadout} pitchDeg=${r.pitchDeg} />
      <${SlipBall} lateralG=${r.lateralG} percentLift=${r.percentLift}
                   stallWarn=${r.stallWarnPctLift} flashFlag=${flashFlag}
                   x=${G.MODE1_SLIP_X} y=${G.MODE1_SLIP_Y}
                   width=${G.MODE1_SLIP_W} height=${G.MODE1_SLIP_H} />
      <${EdgeTape} value=${r.vsiFpm}
                   barX=${G.MODE1_VSI_BAR_X} barW=${G.MODE1_VSI_BAR_W}
                   barColor=${colors.TFT_WHITE}
                   zeroY=${G.MODE1_VSI_ZERO_Y}
                   heightScale=${G.MODE1_VSI_HEIGHT_SCALE}
                   heightMax=${G.MODE1_VSI_HEIGHT_MAX}
                   tickX1=${G.MODE1_VSI_TICK_X1} tickX2=${G.MODE1_VSI_TICK_X2}
                   tickFirstY=${G.MODE1_VSI_TICK_FIRST_Y}
                   tickLastY=${G.MODE1_VSI_TICK_LAST_Y}
                   tickStep=${G.MODE1_VSI_TICK_STEP}
                   pipX1=${G.MODE1_VSI_PIP_X1} pipX2=${G.MODE1_VSI_PIP_X2}
                   pipYs=${[G.MODE1_VSI_PIP_Y_TOP, G.MODE1_VSI_PIP_Y_MIDDLE, G.MODE1_VSI_PIP_Y_BOT]}
                   tickColor=${colors.TFT_BLACK} pipColor=${colors.TFT_BLACK} />
      <${CornerReadout} label="IAS"
          value=${fmtNum(r.iasKt, 0)}
          labelX=${G.MODE1_CORNER_LEFT_X} labelY=${G.MODE1_CORNER_TOP_LABEL_Y}
          numX=${G.MODE1_CORNER_LEFT_NUM_X} numY=${G.MODE1_CORNER_TOP_NUM_Y}
          labelColor=${colors.TFT_GREY} numColor=${colors.TFT_BLACK}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE}
          numBaseline="central" />
      <${CornerReadout} label="PALT"
          value=${(r.paltFt === undefined || r.paltFt === null || Number.isNaN(r.paltFt))
                    ? '—'
                    : String(Math.round(r.paltFt)).padStart(5, ' ')}
          labelX=${G.MODE1_CORNER_RIGHT_X} labelY=${G.MODE1_CORNER_TOP_LABEL_Y}
          numX=${G.MODE1_CORNER_TOP_RIGHT_NUM_X} numY=${G.MODE1_CORNER_TOP_NUM_Y}
          anchor="end" labelColor=${colors.TFT_GREY} numColor=${colors.TFT_BLACK}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE}
          numBaseline="central" />
      <${CornerReadout} label="G"
          value=${fmtNum(r.verticalG, 1)}
          labelX=${G.MODE1_CORNER_LEFT_X} labelY=${G.MODE1_CORNER_BOT_LABEL_Y}
          numX=${G.MODE1_CORNER_LEFT_NUM_X} numY=${G.MODE1_CORNER_BOT_NUM_Y}
          labelColor=${colors.TFT_LIGHTGREY} numColor=${colors.TFT_WHITE}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE} />
      <${CornerReadout} label="AOA"
          value=${aoaIsValid ? String(Math.min(99, Math.max(0, Math.trunc(r.percentLift)))).padStart(2, '0') : '—'}
          labelX=${G.MODE1_CORNER_RIGHT_X} labelY=${G.MODE1_CORNER_BOT_LABEL_Y}
          numX=${G.MODE1_CORNER_BOT_RIGHT_NUM_X} numY=${G.MODE1_CORNER_BOT_NUM_Y}
          anchor="end" labelColor=${colors.TFT_LIGHTGREY} numColor=${colors.TFT_WHITE}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE} />
    <//>`;
};

// --- Mode 2: Indexer-only -------------------------------------------------
// Mirrors C++: same renderer body as Mode 0, numericDisplay=false.
export const Mode2 = ({ r, stale }) => html`
  <${Mode0} r=${r} stale=${stale} numericDisplay=${false} />`;

// --- Mode 3: Energy / decel gauge -----------------------------------------
//
// The decel pointer reflects IAS-derivative state, not AOA validity, so
// it draws unconditionally — same as the M5 hardware page.
export const Mode3 = ({ r, stale }) => {
  const flashFlag = flashFlagNow();
  return html`
    <${Panel} stale=${stale}>
      <${DecelGauge} decelRate=${r.decelRate || 0} />
      <${SlipBall} lateralG=${r.lateralG} percentLift=${r.percentLift}
                   stallWarn=${r.stallWarnPctLift} flashFlag=${flashFlag}
                   x=${G.MODE3_SLIP_X} y=${G.MODE3_SLIP_Y}
                   width=${G.MODE3_SLIP_W} height=${G.MODE3_SLIP_H} />
      <${EdgeTape} value=${r.vsiFpm}
                   barX=${G.MODE1_VSI_BAR_X} barW=${G.MODE1_VSI_BAR_W}
                   barColor=${colors.TFT_WHITE}
                   zeroY=${G.MODE1_VSI_ZERO_Y}
                   heightScale=${G.MODE1_VSI_HEIGHT_SCALE}
                   heightMax=${G.MODE1_VSI_HEIGHT_MAX}
                   tickX1=${G.MODE1_VSI_TICK_X1} tickX2=${G.MODE1_VSI_TICK_X2}
                   tickFirstY=${G.MODE1_VSI_TICK_FIRST_Y}
                   tickLastY=${G.MODE1_VSI_TICK_LAST_Y}
                   tickStep=${G.MODE1_VSI_TICK_STEP}
                   pipX1=${G.MODE1_VSI_PIP_X1} pipX2=${G.MODE1_VSI_PIP_X2}
                   pipYs=${[G.MODE1_VSI_PIP_Y_TOP, G.MODE1_VSI_PIP_Y_MIDDLE, G.MODE1_VSI_PIP_Y_BOT]}
                   tickColor=${colors.TFT_LIGHTGREY} pipColor=${colors.TFT_LIGHTGREY} />
      <${CornerReadout} label="IAS"
          value=${fmtNum(r.iasKt, 0)}
          labelX=${G.MODE3_CORNER_LEFT_X} labelY=${G.MODE3_CORNER_LABEL_Y}
          numX=${G.MODE3_CORNER_LEFT_NUM_X} numY=${G.MODE3_CORNER_NUM_Y} />
      <${CornerReadout} label="Kt/s"
          value=${fmtNum(r.decelRate, 1, true)}
          labelX=${G.MODE3_CORNER_RIGHT_X} labelY=${G.MODE3_CORNER_LABEL_Y}
          numX=${G.MODE3_CORNER_RIGHT_X} numY=${G.MODE3_CORNER_NUM_Y} anchor="end" />
    <//>`;
};

// --- Mode 4: G-history ---------------------------------------------------
//
// The 300-sample ring buffer is owned by the parent (firmware/main.js)
// because Preact tears down state on parent re-render; we don't want
// the strip-chart history to reset every time we re-render. Parent
// passes `buf` and `writeIdx` in.
export const Mode4 = ({ r, stale, gBuf, gWriteIdx }) => html`
  <${Panel} stale=${stale}>
    <${GHistory} buf=${gBuf} writeIdx=${gWriteIdx} />
  <//>`;
