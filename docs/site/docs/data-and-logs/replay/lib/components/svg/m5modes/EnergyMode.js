// EnergyMode.js — Mode 0 renderer driven by M5 firmware sim state.
//
// PR 2 of Project B2. The M5 firmware (compiled to WASM) decides what
// numbers to display; this component just lays them out as SVG. There
// is no rendering math here beyond "read M5 state var, position SVG
// element."
//
// State source: `state` is the frozen object returned by
// `tools/web/lib/replay/m5sim/m5sim.js::M5Sim.read()`. Every value
// reflects the firmware's own snapshots:
//   - displayIAS / displayPalt / displayPercentLift / displayVerticalG
//     update every 500 ms (matches the M5 panel's text cadence).
//   - PercentLift / Slip / gOnsetRate / FlapPos update on each wire
//     frame (~20 Hz) — these drive the indexer/ball/edge-tape geometry.
//   - The anchor pcts (TonesOnPctLift, OnSpeedFastPctLift, etc.) are
//     wire-derived and update with each frame; the indexer maps
//     PercentLift through these anchors.
//
// SlipBall accepts body-frame lateralG directly (positive = airframe
// accelerating rightward). state.LateralG flows from the wire frame
// via m5sim.read() and is optionally smoothed by renderSmooth() before
// reaching here. SlipBall internally maps lateralG to a clamped slip
// integer for ball positioning — clamping at the rendering site means
// the upstream PresentationFilter can smooth in continuous-G space at
// all magnitudes without a saturation dead-zone.

import { html } from '../../../../../../../../../packages/ui-core/vendor/preact-standalone.js';
import * as G from '../../../../../../../../../packages/ui-core/core/geometry.js';
import { colors } from '../../../../../../../../../packages/ui-core/core/colors.js';
import { fmtSigned } from '../../../../../../../../../packages/ui-core/core/format.js';
import {
  Indexer, PercentLiftNumber, CornerReadout, DataMark,
  FlapCircle, SlipBall, EdgeTape,
} from '../../../../../../../../../packages/ui-core/components/svg/index.js';
import { m5FlashFlagNow, m5FmtIasKt } from './helpers.js';

// Build the per-frame anchors array the existing Indexer expects.
//
// Layout (from modes.js::anchorsFromRec): [0, 0, tonesOn, fastOnSpd,
// slowOnSpd, 0, pip, stallWarn]. PR 2 reads each from the M5 sim
// state.
const anchorsFromState = (state) => [
  0, 0,
  state.TonesOnPctLift,
  state.OnSpeedFastPctLift,
  state.OnSpeedSlowPctLift,
  0,
  state.PipPctLift,
  state.StallWarnPctLift,
];

// EnergyMode (Mode 0) — indexer + corners + flap + slip + edge tape +
// percent number + datamark.
//
// numericDisplay gates the "outer numerics" — corners, flap circle. With
// it false, the renderer matches Mode 2 (indexer-only). The pattern
// mirrors main.cpp:701-708 / 842-851 where the same `displayAOA()`
// produces both modes via the `numericDisplay` flag.
export const EnergyMode = ({ state, stale = false, numericDisplay = true }) => {
  const flashFlag = m5FlashFlagNow();
  const aoaIsValid = state.IasIsValid !== false;
  const anchors = anchorsFromState(state);

  // PercentLift digit comes from `displayPercentLift` (the firmware's
  // 500 ms snapshot); chevron color comparisons use `PercentLift`
  // (every-frame). This matches the M5 panel's behavior — the digit
  // updates twice a second while the chevrons track the wire rate.
  // PercentLiftNumber accepts a percent that it truncates internally.

  return html`
    <svg viewBox="0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}"
         xmlns="http://www.w3.org/2000/svg"
         style="background: ${colors.TFT_BLACK}; width: 100%; height: 100%;">
      <${Indexer} percentLift=${state.PercentLift} anchors=${anchors}
                  flashFlag=${flashFlag} aoaIsValid=${aoaIsValid} />
      ${numericDisplay && html`
        <${CornerReadout} label="IAS"
            value=${m5FmtIasKt(state.displayIAS, aoaIsValid)}
            labelX=${G.CORNER_LEFT_X} labelY=${G.CORNER_LABEL_Y}
            numX=${G.CORNER_LEFT_X + 2} numY=${G.CORNER_NUM_Y} />
        <${CornerReadout} label="G"
            value=${fmtSigned(state.displayVerticalG, 1)}
            labelX=${G.CORNER_RIGHT_X} labelY=${G.CORNER_LABEL_Y}
            numX=${G.CORNER_RIGHT_X} numY=${G.CORNER_NUM_Y} anchor="end" />
        <${FlapCircle} flapPos=${state.FlapPos}
            flapsMin=${state.FlapsMinDeg} flapsMax=${state.FlapsMaxDeg} />`}
      <${SlipBall} lateralG=${state.LateralG}
                   percentLift=${state.PercentLift}
                   stallWarn=${state.StallWarnPctLift}
                   flashFlag=${flashFlag} />
      <${EdgeTape} value=${state.gOnsetRate} />
      <${PercentLiftNumber} percent=${state.displayPercentLift}
                            aoaIsValid=${aoaIsValid} />
      <${DataMark} value=${state.DataMark} />
    </svg>`;
};
