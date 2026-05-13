// HudOverlay.js — full-frame HUD as a pure render layer.
//
// Layers a line-only OnSpeed-flavored primary flight display over
// the source video. No sky/ground fill — the GoPro footage IS the
// horizon. Lines, ticks, tapes, and the FPM are drawn on top.
//
// Per PLAN_HUD_OVERLAY.md:
//   - viewBox 1920x1080; SVG scales to fit the video frame via CSS.
//   - All numeric tunables live in core/hudGeometry.js.
//   - Reuses the existing SlipBall component for the bottom-center
//     slip indicator (same wire-frame field as the M5 panel uses).
//
// State shape: the canonical M5State from packages/ui-core/state-shape.js.
// Same input the m5modes/ renderers consume; the replay page feeds it
// from M5Sim.read() (replay) — when this lands on the live page (out
// of scope for PR-1) it will read the same shape from wsRecordToState().

import { html } from '../../vendor/preact-standalone.js';
import * as H from '../../core/hudGeometry.js';
import { SlipBall } from './index.js';
import { HudPitchLadder } from './hud/PitchLadder.js';
import { HudBankArc } from './hud/BankArc.js';
import { HudTape, HudVsiChevron } from './hud/Tape.js';
import { HudFpm } from './hud/Fpm.js';

export const HudOverlay = ({ state }) => {
  if (!state) return null;
  const aoaIsValid = state.IasIsValid !== false;
  const iasValue = aoaIsValid ? Math.max(0, state.displayIAS || 0) : 0;
  const altValue = Math.max(0, state.displayPalt || 0);
  return html`
    <svg viewBox="0 0 ${H.HUD_W} ${H.HUD_H}"
         xmlns="http://www.w3.org/2000/svg"
         preserveAspectRatio="xMidYMid meet"
         class="hud-svg">
      <${HudPitchLadder} pitchDeg=${state.Pitch ?? 0}
                          rollDeg=${state.Roll ?? 0} />
      <${HudBankArc}     rollDeg=${state.Roll ?? 0} />
      <${HudFpm} pitchDeg=${state.Pitch ?? 0}
                  flightPathDeg=${state.FlightPath ?? 0}
                  lateralG=${state.LateralG ?? 0} />
      <${HudTape}
        side="left"
        value=${iasValue}
        valid=${aoaIsValid}
        tapeX=${H.HUD_IAS_X} tapeW=${H.HUD_IAS_W} tapeH=${H.HUD_IAS_TAPE_H}
        cy=${H.HUD_IAS_CY}
        pxPerUnit=${H.HUD_IAS_PX_PER_UNIT}
        tickEvery=${H.HUD_IAS_TICK_EVERY}
        labelEvery=${H.HUD_IAS_LABEL_EVERY}
        tickLenMajor=${H.HUD_IAS_TICK_LEN_MAJOR}
        tickLenMinor=${H.HUD_IAS_TICK_LEN_MINOR}
        labelFontSize=${H.HUD_IAS_LABEL_FONT_SIZE}
        boxH=${H.HUD_IAS_BOX_H}
        boxFontSize=${H.HUD_IAS_BOX_FONT_SIZE}
        boxPadX=${H.HUD_IAS_BOX_PAD_X}
        formatLabel=${(v) => String(Math.round(v))}
        clipId="hud-tape-clip-ias" />
      <${HudTape}
        side="right"
        value=${altValue}
        valid=${true}
        tapeX=${H.HUD_ALT_X} tapeW=${H.HUD_ALT_W} tapeH=${H.HUD_ALT_TAPE_H}
        cy=${H.HUD_ALT_CY}
        pxPerUnit=${H.HUD_ALT_PX_PER_UNIT}
        tickEvery=${H.HUD_ALT_TICK_EVERY}
        labelEvery=${H.HUD_ALT_LABEL_EVERY}
        tickLenMajor=${H.HUD_ALT_TICK_LEN_MAJOR}
        tickLenMinor=${H.HUD_ALT_TICK_LEN_MINOR}
        labelFontSize=${H.HUD_ALT_LABEL_FONT_SIZE}
        boxH=${H.HUD_ALT_BOX_H}
        boxFontSize=${H.HUD_ALT_BOX_FONT_SIZE}
        boxPadX=${H.HUD_ALT_BOX_PAD_X}
        formatLabel=${(v) => String(Math.round(v))}
        clipId="hud-tape-clip-alt" />
      <${HudVsiChevron} vsiFpm=${state.iVSI ?? 0} />
      <${SlipBall} lateralG=${state.LateralG ?? 0}
                    percentLift=${state.PercentLift ?? 0}
                    stallWarn=${state.StallWarnPctLift ?? 100}
                    flashFlag=${false}
                    x=${H.HUD_SLIP_X} y=${H.HUD_SLIP_Y}
                    width=${H.HUD_SLIP_W} height=${H.HUD_SLIP_H} />
    </svg>`;
};
