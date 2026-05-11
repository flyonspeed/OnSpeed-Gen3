// AttitudeMode.js — Mode 1 renderer driven by M5 firmware sim state.
//
// PR 2 of Project B2. Mode 1 is the synthetic horizon: sky/ground
// polygon, pitch ladder, bank arc, flight-path marker, slip ball,
// VSI edge tape, and four corner readouts (IAS/PALT/G/AOA).
//
// Mirrors main.cpp:711-840 (case 1) — every layout constant, color,
// and gating choice in here corresponds to a line over there. The
// rendering math itself lives in the SVG primitives in ../index.js.

import { html } from '../../../vendor/preact-standalone.js';
import * as G from '../../../core/geometry.js';
import { colors } from '../../../core/colors.js';
import { fmt } from '../../../core/format.js';
import {
  Horizon, PitchLadder, BankArc, AircraftSymbol, TopPointer,
  FlightPathMarker, PitchReadout, SlipBall, EdgeTape, CornerReadout,
  PCT_DASHES,
} from '../index.js';
import { m5FmtIasKt, m5FmtPalt } from './helpers.js';

export const AttitudeMode = ({ state, stale = false }) => {
  const aoaIsValid = state.IasIsValid !== false;
  return html`
    <svg viewBox="0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}"
         xmlns="http://www.w3.org/2000/svg"
         style="background: ${colors.TFT_BLACK}; width: 100%; height: 100%;">
      <${Horizon}     pitchDeg=${state.Pitch} rollDeg=${state.Roll} />
      <${PitchLadder} pitchDeg=${state.Pitch} rollDeg=${state.Roll} />
      <${BankArc}     rollDeg=${state.Roll} />
      <${TopPointer} />
      <${AircraftSymbol} />
      <${FlightPathMarker} pitchDeg=${state.Pitch}
                           flightPathDeg=${state.FlightPath} />
      <${PitchReadout} pitchDeg=${state.displayPitch} />
      <${SlipBall} lateralG=${state.LateralG}
                   percentLift=${state.PercentLift}
                   stallWarn=${state.StallWarnPctLift}
                   flashFlag=${false}
                   x=${G.MODE1_SLIP_X} y=${G.MODE1_SLIP_Y}
                   width=${G.MODE1_SLIP_W} height=${G.MODE1_SLIP_H} />
      <${EdgeTape} value=${state.iVSI}
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
          value=${m5FmtIasKt(state.displayIAS, aoaIsValid)}
          labelX=${G.MODE1_CORNER_LEFT_X} labelY=${G.MODE1_CORNER_TOP_LABEL_Y}
          numX=${G.MODE1_CORNER_LEFT_NUM_X} numY=${G.MODE1_CORNER_TOP_NUM_Y}
          labelColor=${colors.TFT_GREY} numColor=${colors.TFT_BLACK}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE}
          numBaseline="central" />
      <${CornerReadout} label="PALT"
          value=${m5FmtPalt(state.displayPalt)}
          labelX=${G.MODE1_CORNER_RIGHT_X} labelY=${G.MODE1_CORNER_TOP_LABEL_Y}
          numX=${G.MODE1_CORNER_TOP_RIGHT_NUM_X} numY=${G.MODE1_CORNER_TOP_NUM_Y}
          anchor="end" labelColor=${colors.TFT_GREY} numColor=${colors.TFT_BLACK}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE}
          numBaseline="central" />
      <${CornerReadout} label="G"
          value=${fmt(state.displayVerticalG, 1)}
          labelX=${G.MODE1_CORNER_LEFT_X} labelY=${G.MODE1_CORNER_BOT_LABEL_Y}
          numX=${G.MODE1_CORNER_LEFT_NUM_X} numY=${G.MODE1_CORNER_BOT_NUM_Y}
          labelColor=${colors.TFT_LIGHTGREY} numColor=${colors.TFT_WHITE}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE} />
      <${CornerReadout} label="AOA"
          value=${aoaIsValid
                    ? String(Math.min(99, Math.max(0, state.displayPercentLift))).padStart(2, '0')
                    : PCT_DASHES}
          labelX=${G.MODE1_CORNER_RIGHT_X} labelY=${G.MODE1_CORNER_BOT_LABEL_Y}
          numX=${G.MODE1_CORNER_BOT_RIGHT_NUM_X} numY=${G.MODE1_CORNER_BOT_NUM_Y}
          anchor="end" labelColor=${colors.TFT_LIGHTGREY} numColor=${colors.TFT_WHITE}
          labelFontSize=${G.MODE1_CORNER_LABEL_FONT_SIZE}
          numFontSize=${G.MODE1_CORNER_NUM_FONT_SIZE} />
    </svg>`;
};
