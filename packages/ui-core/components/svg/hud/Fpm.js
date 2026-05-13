// Fpm.js — Flight-Path Marker for the HUD.
//
// A small magenta circle with three radial wings (left, right, top
// tick) that floats at the position the airplane is actually flying
// toward. Position math:
//
//   y_offset_px = (FlightPath - Pitch) * pixels-per-degree
//
//                 (positive FlightPath is climb; subtracting pitch
//                 puts the marker BELOW the horizon when descending
//                 and ABOVE when climbing)
//
//   x_offset_px = -LateralG * lateral-px-per-g       (clamped)
//
// FPM lateral motion approximation
// --------------------------------
// A real HUD's FPM slides horizontally with YAW RATE (ground-track
// delta from heading). The OnSpeed wire frame ships pitch, roll, and
// sideslip (LateralG) but NOT yaw rate. We approximate lateral
// motion from LateralG:
//
//   • In COORDINATED flight LateralG ≈ 0 → the FPM stays centered.
//     Wrong for a turning aircraft (which would have a real FPM
//     sliding with the turn), but visually plausible since the
//     turning airframe in the GoPro footage suggests the motion.
//
//   • In SKIDS / SLIPS LateralG is non-zero → the FPM slides toward
//     the down-slip side. RIGHT for the case OnSpeed pilots care
//     about most — uncoordinated flight (slipping turns, base-to-
//     final overshoots, cross-controlled stalls) is the danger
//     pattern.
//
// Document the limitation in code so future readers don't think this
// is a bug. A future wire-format bump that adds yaw rate would
// replace this with the proper ground-track FPM math.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

export const HudFpm = ({ pitchDeg = 0, flightPathDeg = 0, lateralG = 0 }) => {
  const dy = (flightPathDeg - pitchDeg) * H.HUD_PITCH_PX_PER_DEG;
  // Lateral slide is OPPOSITE the body-frame lateral G: positive
  // LateralG = airframe accelerating right = sideslip pushes the
  // velocity vector LEFT relative to the nose, so the FPM slides LEFT.
  // (Same sign convention as slipBall: negate the wire value.)
  const dxRaw = -lateralG * H.HUD_FPM_LAT_PX_PER_G;
  const dx = Math.max(-H.HUD_FPM_LAT_MAX_PX,
                       Math.min(H.HUD_FPM_LAT_MAX_PX, dxRaw));
  const cx = H.HUD_FPM_CX + dx;
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
