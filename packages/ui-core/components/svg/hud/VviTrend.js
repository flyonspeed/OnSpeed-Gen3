// HudVviTrend — vertical climb/descent trend bar on the right edge.
//
// Centerline at HUD_VVI_CY; bar height proportional to |vsiFpm|, clamped
// to +/-HUD_VVI_FULL_SCALE_FPM. Yellow bar grows from the centerline up
// (climb) or down (descent). Ticks at +/-1000 and +/-2000 fpm. Numeric
// readout (rounded to 10 fpm) appears next to the bar's leading edge
// when |vsiFpm| exceeds HUD_VVI_THRESHOLD.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

const TICKS_FPM = [-2000, -1000, 0, 1000, 2000];

export const HudVviTrend = ({ vsiFpm = 0 }) => {
  if (!Number.isFinite(vsiFpm)) return null;
  const clamped = Math.max(-H.HUD_VVI_FULL_SCALE_FPM,
                            Math.min(H.HUD_VVI_FULL_SCALE_FPM, vsiFpm));
  const cx = H.HUD_VVI_X;
  const cy = H.HUD_VVI_CY;
  const halfH = H.HUD_VVI_HALF_H;
  // y grows downward; positive fpm (climb) places the bar above center.
  const yForFpm = (fpm) => cy - (fpm / H.HUD_VVI_FULL_SCALE_FPM) * halfH;
  const barY1 = cy;
  const barY2 = yForFpm(clamped);
  return html`
    <g data-widget="hud-vvi">
      <line x1=${cx} y1=${cy - halfH} x2=${cx} y2=${cy + halfH}
            stroke="var(--white)" stroke-width="2"
            shape-rendering="crispEdges" />
      <line x1=${cx - 14} y1=${cy} x2=${cx + 14} y2=${cy}
            stroke="var(--white)" stroke-width="3" />
      ${TICKS_FPM.filter(t => t !== 0).map(t => html`
        <line x1=${cx - 8} y1=${yForFpm(t)} x2=${cx + 8} y2=${yForFpm(t)}
              stroke="var(--white)" stroke-width="2"
              shape-rendering="crispEdges" />
        <text x=${cx + 16} y=${yForFpm(t)}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-size=${H.HUD_VVI_TICK_FONT_SIZE}
              fill="var(--white)"
              text-anchor="start" dominant-baseline="central">${Math.abs(t / 1000)}</text>`)}
      ${Math.abs(vsiFpm) >= H.HUD_VVI_BAR_THRESHOLD && html`
        <line x1=${cx} y1=${barY1} x2=${cx} y2=${barY2}
              stroke="var(--yellow)" stroke-width="8"
              stroke-linecap="butt" />`}
      ${Math.abs(vsiFpm) >= H.HUD_VVI_THRESHOLD && html`
        <text x=${cx + 50} y=${barY2}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-weight="bold"
              font-size=${H.HUD_VVI_VALUE_FONT_SIZE}
              fill="var(--white)"
              text-anchor="start" dominant-baseline="central">${(vsiFpm > 0 ? '+' : '') + Math.round(vsiFpm / 10) * 10}</text>`}
    </g>`;
};
