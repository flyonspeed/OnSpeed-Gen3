// Fpm.js — Flight-Path Marker for the HUD.
//
// Yellow glyph: circle + horizontal wing stalks + top fin. Vertical
// position is the only motion — `(FlightPath - Pitch) × pixels-per-
// degree`, matching the AI inset's FlightPathMarker math exactly so
// the two FPMs move together at every instant.
//
// LATERAL FPM MOTION — DEFERRED (issue #542).
// A "real" HUD FPM slides horizontally with the yaw-rate / ground-
// track delta from heading. OnSpeed does not currently expose yaw
// rate, and a previous spike that drove lateral motion from LateralG
// (sideslip approximation) produced jumpy motion that didn't match
// the AI inset's behavior. Revisit after the wire format gains yaw
// rate or ground track.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

export const HudFpm = ({ pitchDeg = 0, flightPathDeg = 0 }) => {
  const dy = (flightPathDeg - pitchDeg) * H.HUD_PITCH_PX_PER_DEG;
  const cx = H.HUD_FPM_CX;
  const cy = H.HUD_FPM_CY + dy;
  return html`
    <g data-widget="hud-fpm">
      <circle cx=${cx} cy=${cy} r=${H.HUD_FPM_R}
              fill="none" stroke=${H.HUD_FPM_COLOR}
              stroke-width=${H.HUD_FPM_STROKE} />
      <line x1=${cx - H.HUD_FPM_WING_INNER} y1=${cy}
            x2=${cx - H.HUD_FPM_WING_OUTER} y2=${cy}
            stroke=${H.HUD_FPM_COLOR} stroke-width=${H.HUD_FPM_STROKE} />
      <line x1=${cx + H.HUD_FPM_WING_INNER} y1=${cy}
            x2=${cx + H.HUD_FPM_WING_OUTER} y2=${cy}
            stroke=${H.HUD_FPM_COLOR} stroke-width=${H.HUD_FPM_STROKE} />
      <line x1=${cx} y1=${cy - H.HUD_FPM_R}
            x2=${cx} y2=${cy - H.HUD_FPM_R - H.HUD_FPM_TOP_TICK}
            stroke=${H.HUD_FPM_COLOR} stroke-width=${H.HUD_FPM_STROKE} />
    </g>`;
};
