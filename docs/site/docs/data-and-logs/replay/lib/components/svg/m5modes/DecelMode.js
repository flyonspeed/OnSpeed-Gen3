// DecelMode.js — Mode 3 renderer driven by M5 firmware sim state.
//
// PR 2 of Project B2. Mode 3 is the IAS-derivative gauge: a vertical
// band gauge with a sliding pointer, IAS readout (left), Kt/s readout
// (right), slip ball, VSI edge tape. Mirrors main.cpp:853-857 (the
// `displayDecelGauge()` call).
//
// The M5 firmware computes its own decel rate via a Savitzky-Golay
// derivative on wire-rate IAS — `displayDecelRate` is the smoothed
// snapshot value the panel paints. We read it directly; no JS-side
// SavGol involved.

import { html } from '../../../../../../../../../packages/ui-core/vendor/preact-standalone.js';
import * as G from '../../../../../../../../../packages/ui-core/core/geometry.js';
import { colors } from '../../../../../../../../../packages/ui-core/core/colors.js';
import { fmtSigned } from '../../../../../../../../../packages/ui-core/core/format.js';
import {
  DecelGauge, SlipBall, EdgeTape, CornerReadout, PCT_DASHES,
} from '../../../../../../../../../packages/ui-core/components/svg/index.js';
import { m5FmtIasKt } from './helpers.js';

export const DecelMode = ({ state, stale = false }) => {
  const aoaIsValid = state.IasIsValid !== false;
  const decelDisplay = state.displayDecelRate;
  return html`
    <svg viewBox="0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}"
         xmlns="http://www.w3.org/2000/svg"
         style="background: ${colors.TFT_BLACK}; width: 100%; height: 100%;">
      <${DecelGauge} decelRate=${decelDisplay} dataValid=${aoaIsValid} />
      <${SlipBall} lateralG=${state.LateralG}
                   percentLift=${state.PercentLift}
                   stallWarn=${state.StallWarnPctLift}
                   flashFlag=${false}
                   x=${G.MODE3_SLIP_X} y=${G.MODE3_SLIP_Y}
                   width=${G.MODE3_SLIP_W} height=${G.MODE3_SLIP_H} />
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
                   tickColor=${colors.TFT_LIGHTGREY} pipColor=${colors.TFT_LIGHTGREY} />
      <${CornerReadout} label="IAS"
          value=${m5FmtIasKt(state.displayIAS, aoaIsValid)}
          labelX=${G.MODE3_CORNER_LEFT_X} labelY=${G.MODE3_CORNER_LABEL_Y}
          numX=${G.MODE3_CORNER_LEFT_NUM_X} numY=${G.MODE3_CORNER_NUM_Y} />
      <${CornerReadout} label="Kt/s"
          value=${aoaIsValid ? fmtSigned(decelDisplay, 1) : PCT_DASHES}
          labelX=${G.MODE3_CORNER_RIGHT_X} labelY=${G.MODE3_CORNER_LABEL_Y}
          numX=${G.MODE3_CORNER_RIGHT_X} numY=${G.MODE3_CORNER_NUM_Y} anchor="end" />
    </svg>`;
};
