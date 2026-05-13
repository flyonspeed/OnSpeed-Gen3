// HudOverlay.js — full-frame HUD as a pure render layer.
//
// FlySto-style attitude indicator rendered directly at 1920x1080:
// pitch ladder + bank arc + flight-path marker, no airspeed/altimeter
// tapes. Around the central ADI:
//   - Three top boxed readouts (IAS / MH / PALT). MH is only rendered
//     when the caller passes a finite magneticHeading prop (logs that
//     don't carry efisMagHeading get no MH box).
//   - A right-side VVI trend bar (HudVviTrend).
//   - The existing SlipBall at bottom-center.
//
// `pitchOffsetDeg` is a per-flight camera-misalignment compensator —
// it adds to pitch for the pitch ladder ONLY. The FPM stays on raw
// pitch because the camera offset doesn't change the airframe's
// actual flight-path-vs-airframe relationship; it only shifts where
// the rendered horizon needs to land to match the cockpit's visible
// horizon. State shape: the canonical M5State from
// packages/ui-core/state-shape.js.

import { html } from '../../vendor/preact-standalone.js';
import * as H from '../../core/hudGeometry.js';
import { SlipBall } from './index.js';
import { HudTopReadout } from './hud/TopReadout.js';
import { HudVviTrend } from './hud/VviTrend.js';
import { HudPitchLadder } from './hud/PitchLadder.js';
import { HudBankArc } from './hud/BankArc.js';
import { HudFpm } from './hud/Fpm.js';

// MH is rendered as a zero-padded 3-digit value (0..359) so a magnetic
// heading of 7° reads "007" — same convention as a panel HSI.
const pad3 = (n) => String(((Math.round(n) % 360) + 360) % 360).padStart(3, '0');

export const HudOverlay = ({ state, pitchOffsetDeg = 0, magneticHeading = null }) => {
  if (!state) return null;
  const aoaIsValid = state.IasIsValid !== false;
  const iasValue = aoaIsValid ? Math.max(0, Math.round(state.displayIAS || 0)) : '---';
  const altValue = Math.max(0, Math.round(state.displayPalt || 0));
  const offset = Number.isFinite(pitchOffsetDeg) ? pitchOffsetDeg : 0;
  const pitch = Number.isFinite(state.Pitch) ? state.Pitch : 0;
  const roll  = Number.isFinite(state.Roll)  ? state.Roll  : 0;
  const fpDeg = Number.isFinite(state.FlightPath) ? state.FlightPath : pitch;
  const showMh = Number.isFinite(magneticHeading);

  // Inline drop-shadow filter so the rasterized MP4 export gets the
  // same legibility halo as the live page. replay.css applies a CSS
  // filter via a class selector, but the export path serializes the
  // SVG to a blob URL and parses it in isolation — page CSS doesn't
  // follow. Inlining the filter into the SVG itself works on both
  // surfaces.
  return html`
    <svg viewBox="0 0 ${H.HUD_W} ${H.HUD_H}"
         xmlns="http://www.w3.org/2000/svg"
         preserveAspectRatio="xMidYMid meet"
         class="hud-svg">
      <defs>
        <filter id="hud-shadow" x="-2%" y="-2%" width="104%" height="104%">
          <feDropShadow dx="0" dy="0" stdDeviation="1.5"
                        flood-color="black" flood-opacity="0.85" />
          <feDropShadow dx="0" dy="0" stdDeviation="3"
                        flood-color="black" flood-opacity="0.6" />
        </filter>
      </defs>
      <g filter="url(#hud-shadow)">
        <${HudTopReadout} label="IAS"  value=${iasValue}
                          x=${H.HUD_TOP_LEFT_X}   y=${H.HUD_TOP_Y} />
        ${showMh && html`
          <${HudTopReadout} label="MH" value=${pad3(magneticHeading)}
                            x=${H.HUD_TOP_CENTER_X} y=${H.HUD_TOP_Y} />`}
        <${HudTopReadout} label="PALT" value=${altValue}
                          x=${H.HUD_TOP_RIGHT_X}  y=${H.HUD_TOP_Y} />

        <${HudPitchLadder} pitchDeg=${pitch + offset} rollDeg=${roll} />
        <${HudBankArc}     rollDeg=${roll} />
        <${HudFpm}         pitchDeg=${pitch} flightPathDeg=${fpDeg} />

        <${HudVviTrend} vsiFpm=${state.iVSI ?? 0} />

        <${SlipBall} lateralG=${state.LateralG ?? 0}
                     percentLift=${state.PercentLift ?? 0}
                     stallWarn=${state.StallWarnPctLift ?? 100}
                     flashFlag=${false}
                     x=${H.HUD_SLIP_X} y=${H.HUD_SLIP_Y}
                     width=${H.HUD_SLIP_W} height=${H.HUD_SLIP_H} />
      </g>
    </svg>`;
};
