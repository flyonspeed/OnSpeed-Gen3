// HudOverlay.js — full-frame HUD as a pure render layer.
//
// FlySto-style attitude indicator rendered directly at 1920x1080:
// pitch ladder + bank arc + flight-path marker. Around the central
// ADI:
//   - OnSpeed logo top-left.
//   - Right-side VVI trend bar (HudVviTrend).
//   - Right-side FlySto-style ALT tape with a Garmin readout box
//     anchored on the tape centerline (HudAltTape).
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
import { HudVviTrend } from './hud/VviTrend.js';
import { HudAltTape } from './hud/AltTape.js';
import { HudIasTape } from './hud/IasTape.js';
import { HudPitchLadder } from './hud/PitchLadder.js';
import { HudBankArc } from './hud/BankArc.js';
import { HudFpm } from './hud/Fpm.js';
import { HudOnSpeedLogo } from './hud/OnSpeedLogo.js';

export const HudOverlay = ({ state, pitchOffsetDeg = 0 }) => {
  if (!state) return null;
  // Tapes read the 20 Hz raw IAS / Palt fields rather than the 500 ms
  // displayIAS / displayPalt snapshots. The tape's scrolling visual
  // requires sub-second updates to look smooth; the throttled display
  // fields exist for M5 text readouts where 2 Hz prevents flicker.
  const altValue = Math.max(0, Math.round(state.Palt || 0));
  const offset = Number.isFinite(pitchOffsetDeg) ? pitchOffsetDeg : 0;
  const pitch = Number.isFinite(state.Pitch) ? state.Pitch : 0;
  const roll  = Number.isFinite(state.Roll)  ? state.Roll  : 0;
  const fpDeg = Number.isFinite(state.FlightPath) ? state.FlightPath : pitch;

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
        <${HudOnSpeedLogo} />

        <${HudPitchLadder} pitchDeg=${pitch + offset} rollDeg=${roll} />
        <${HudBankArc}     rollDeg=${roll} />
        <${HudFpm}         pitchDeg=${pitch} flightPathDeg=${fpDeg} />

        <${HudVviTrend} vsiFpm=${state.iVSI ?? 0} />
        <${HudAltTape}  altitudeFt=${altValue} />
        <${HudIasTape}  iasKt=${state.IAS ?? 0} />

        <${SlipBall} lateralG=${state.LateralG ?? 0}
                     percentLift=${state.PercentLift ?? 0}
                     stallWarn=${state.StallWarnPctLift ?? 100}
                     flashFlag=${false}
                     x=${H.HUD_SLIP_X} y=${H.HUD_SLIP_Y}
                     width=${H.HUD_SLIP_W} height=${H.HUD_SLIP_H} />
      </g>
    </svg>`;
};
