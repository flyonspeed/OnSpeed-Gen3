// PitchLadder.js — FlySto-style pitch ladder.
//
// White horizon line spanning the central two-thirds of the frame,
// plus yellow short tick bars at ±10°, ±20°, ±30°, each with its
// absolute-value label at both ends. No dashed marks, no sky/ground
// fill, no full-frame horizon extension. The group is rotated by
// -roll and translated by `pitch × px-per-degree` around the HUD
// center — classic ADI.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

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
  const half = H.HUD_PITCH_TICK_HALF_W;
  return html`
    <g>
      <line x1=${H.HUD_CX - half} y1=${H.HUD_CY + y}
            x2=${H.HUD_CX + half} y2=${H.HUD_CY + y}
            stroke=${H.HUD_PITCH_COLOR} stroke-width=${H.HUD_PITCH_TICK_STROKE}
            shape-rendering="crispEdges" />
      <text x=${H.HUD_CX + half + H.HUD_PITCH_LABEL_OFFSET} y=${H.HUD_CY + y}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-size=${H.HUD_PITCH_LABEL_FONT_SIZE}
            fill=${H.HUD_PITCH_COLOR}
            text-anchor="start" dominant-baseline="central">${absI}</text>
      <text x=${H.HUD_CX - half - H.HUD_PITCH_LABEL_OFFSET} y=${H.HUD_CY + y}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-size=${H.HUD_PITCH_LABEL_FONT_SIZE}
            fill=${H.HUD_PITCH_COLOR}
            text-anchor="end" dominant-baseline="central">${absI}</text>
    </g>`;
};

export const HudPitchLadder = ({ pitchDeg = 0, rollDeg = 0 }) => {
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
