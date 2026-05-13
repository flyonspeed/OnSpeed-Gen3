// HudOverlay.js — full-frame HUD as a pure render layer.
//
// FlySto-style primary flight overlay layered over the source video.
// No sky/ground fill — the GoPro footage is the horizon. Lines,
// ticks, tapes, FPM, slip ball, and a single G-load readout are
// drawn on top.
//
// Scope intentionally narrow (matches the four widgets pilots
// requested for v1): pitch ladder + bank arc, IAS tape, ALT tape +
// numeric VSI, slip ball + G readout. No heading tape, no
// TAS/GS/wind block, no OAT/ISA/AGL stack.
//
// State shape: the canonical M5State from packages/ui-core/state-shape.js.
// The replay page applies its PresentationSmoother to LateralG /
// VerticalG before passing state in, so the slip ball + G readout
// match what the M5 inset renders at the same instant.

import { html } from '../../vendor/preact-standalone.js';
import * as H from '../../core/hudGeometry.js';
import { SlipBall } from './index.js';
import { HudPitchLadder } from './hud/PitchLadder.js';
import { HudBankArc } from './hud/BankArc.js';
import { HudTape, HudVsiReadout, HudGReadout } from './hud/Tape.js';
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
                  flightPathDeg=${state.FlightPath ?? 0} />
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
      <${HudVsiReadout} vsiFpm=${state.iVSI ?? 0} />
      <${SlipBall} lateralG=${state.LateralG ?? 0}
                    percentLift=${state.PercentLift ?? 0}
                    stallWarn=${state.StallWarnPctLift ?? 100}
                    flashFlag=${false}
                    x=${H.HUD_SLIP_X} y=${H.HUD_SLIP_Y}
                    width=${H.HUD_SLIP_W} height=${H.HUD_SLIP_H} />
      <${HudGReadout} verticalG=${state.VerticalG ?? 1} />
    </svg>`;
};
