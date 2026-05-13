// PitchLadder.js — full-frame HUD pitch ladder.
//
// Horizon line + pitch ticks at ±10°, ±20°, ±30°. The whole group is
// translated vertically by `pitch * px-per-degree` and rotated by
// `-roll` around the HUD center, so the lines rotate with the airframe
// and slide vertically as the nose pitches up/down — the classic ADI
// behaviour. The first cut here rotates the ladder (per the plan's
// "Open questions" note that the rotated version reads more like a
// real ADI).

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

// One pitch line at angle `i` (degrees). Solid ticks at ±10°/±20°,
// dashed at ±30°. The 0° line is the full-width horizon line.
const pitchLine = (i) => {
  const y = i * H.HUD_PITCH_PX_PER_DEG;
  if (i === 0) {
    return html`
      <line x1=${H.HUD_CX - H.HUD_HORIZON_HALF_W} y1=${H.HUD_CY + y}
            x2=${H.HUD_CX + H.HUD_HORIZON_HALF_W} y2=${H.HUD_CY + y}
            stroke=${H.HUD_HORIZON_COLOR} stroke-width=${H.HUD_HORIZON_STROKE}
            shape-rendering="crispEdges" />`;
  }
  const absI = Math.abs(i);
  const half = absI === 30
    ? H.HUD_PITCH_TICK_HALF_W_DASHED
    : absI === 20
      ? H.HUD_PITCH_TICK_HALF_W_LONG
      : H.HUD_PITCH_TICK_HALF_W_SHORT;
  const dasharray = absI === 30 ? '20 14' : null;
  const labelX = H.HUD_CX + half + H.HUD_PITCH_LABEL_OFFSET;
  return html`
    <g>
      <line x1=${H.HUD_CX - half} y1=${H.HUD_CY + y}
            x2=${H.HUD_CX + half} y2=${H.HUD_CY + y}
            stroke=${H.HUD_LINE_COLOR} stroke-width=${H.HUD_PITCH_TICK_STROKE}
            stroke-dasharray=${dasharray}
            shape-rendering="crispEdges" />
      <text x=${labelX} y=${H.HUD_CY + y}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-size=${H.HUD_PITCH_LABEL_FONT_SIZE}
            fill=${H.HUD_LINE_COLOR}
            text-anchor="start" dominant-baseline="central">${absI}</text>
      <text x=${H.HUD_CX - half - H.HUD_PITCH_LABEL_OFFSET} y=${H.HUD_CY + y}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-size=${H.HUD_PITCH_LABEL_FONT_SIZE}
            fill=${H.HUD_LINE_COLOR}
            text-anchor="end" dominant-baseline="central">${absI}</text>
    </g>`;
};

export const HudPitchLadder = ({ pitchDeg = 0, rollDeg = 0 }) => {
  // Translate by -pitch so a positive pitch (nose up) moves the
  // horizon down on screen, matching the standard ADI convention.
  const transY = pitchDeg * H.HUD_PITCH_PX_PER_DEG;
  return html`
    <g data-widget="hud-pitch-ladder"
       transform="rotate(${-rollDeg} ${H.HUD_CX} ${H.HUD_CY}) translate(0 ${transY})">
      ${pitchLine(0)}
      ${pitchLine(-30)}
      ${pitchLine(-20)}
      ${pitchLine(-10)}
      ${pitchLine(10)}
      ${pitchLine(20)}
      ${pitchLine(30)}
    </g>`;
};
