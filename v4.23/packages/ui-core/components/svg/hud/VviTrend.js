// HudVviTrend — FlySto-style vertical climb/descent trend bar
// attached to the right edge of the ALT tape.
//
// Vertical line at HUD_VVI_X with tick marks pointing LEFT (toward
// the ALT tape's label column). Numeric scale labels at +/-1000 and
// +/-2000 fpm sit on the RIGHT of the bar. A yellow fill bar grows
// from the centerline UP for climb / DOWN for descent, height
// proportional to |vsiFpm| (clamped at +/-HUD_VVI_FULL_SCALE_FPM).
// When |vsiFpm| exceeds HUD_VVI_THRESHOLD a numeric readout (rounded
// to 10 fpm) appears next to the bar's leading edge.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

const TICKS_FPM = [-2000, -1000, 1000, 2000];

export const HudVviTrend = ({ vsiFpm = 0 }) => {
  if (!Number.isFinite(vsiFpm)) return null;
  const clamped = Math.max(-H.HUD_VVI_FULL_SCALE_FPM,
                            Math.min(H.HUD_VVI_FULL_SCALE_FPM, vsiFpm));
  const cx = H.HUD_VVI_X;
  const cy = H.HUD_VVI_CY;
  const halfH = H.HUD_VVI_HALF_H;
  // Explicit y offset from font metrics — see H.hudGlyphOffset.
  const tickDy = H.hudGlyphOffset(H.HUD_VVI_TICK_FONT_SIZE);
  const valueDy = H.hudGlyphOffset(H.HUD_VVI_VALUE_FONT_SIZE);
  // y grows downward; positive fpm (climb) places the bar above center.
  const yForFpm = (fpm) => cy - (fpm / H.HUD_VVI_FULL_SCALE_FPM) * halfH;
  const barY1 = cy;
  const barY2 = yForFpm(clamped);
  return html`
    <g data-widget="hud-vvi">
      <!-- Vertical spine -->
      <line x1=${cx} y1=${cy - halfH} x2=${cx} y2=${cy + halfH}
            stroke="var(--white)" stroke-width="2"
            shape-rendering="crispEdges" />
      <!-- Centerline (zero fpm). Slightly longer than the other ticks
           and extends only LEFT (matching FlySto: ticks read on the
           ALT-tape-facing side, scale numerals on the open side). -->
      <line x1=${cx - 14} y1=${cy} x2=${cx} y2=${cy}
            stroke="var(--white)" stroke-width="3" />
      ${TICKS_FPM.map(t => html`
        <line x1=${cx - 10} y1=${yForFpm(t)} x2=${cx} y2=${yForFpm(t)}
              stroke="var(--white)" stroke-width="2"
              shape-rendering="crispEdges" />
        <text x=${cx + 6} y=${yForFpm(t) + tickDy}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-size=${H.HUD_VVI_TICK_FONT_SIZE}
              fill="var(--white)"
              text-anchor="start">${Math.abs(t / 1000)}</text>`)}
      <!-- Yellow fill bar, growing from centerline. Above-threshold
           rendering keeps the gauge still at idle. -->
      ${Math.abs(vsiFpm) >= H.HUD_VVI_BAR_THRESHOLD && html`
        <rect x=${cx - 6}
              y=${Math.min(barY1, barY2)}
              width="6"
              height=${Math.abs(barY2 - barY1)}
              fill="var(--yellow)" />`}
      <!-- Numeric readout (rounded to 10 fpm) next to the bar's
           leading edge, on the right side. -->
      ${Math.abs(vsiFpm) >= H.HUD_VVI_THRESHOLD && html`
        <text x=${cx + 24} y=${barY2 + valueDy}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-weight="bold"
              font-size=${H.HUD_VVI_VALUE_FONT_SIZE}
              fill="var(--white)"
              text-anchor="start">${(vsiFpm > 0 ? '+' : '') + Math.round(vsiFpm / 10) * 10}</text>`}
    </g>`;
};
