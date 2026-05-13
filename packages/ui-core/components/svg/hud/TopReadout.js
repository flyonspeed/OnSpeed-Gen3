// HudTopReadout — one of three top-edge readouts (IAS / MH / PALT).
// Black-fill rounded rect with white border; label stacked above value,
// both centered in the box.
//
// Used by HudOverlay along the top edge: IAS left, MH center (when the
// log carries efisMagHeading), PALT right. The text-shadow comes from
// the HUD's inline drop-shadow filter; this component just renders the
// box and the two text fields.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

export const HudTopReadout = ({ label, value, x, y, width, height }) => {
  const w = Number.isFinite(width)  ? width  : H.HUD_TOP_BOX_W;
  const h = Number.isFinite(height) ? height : H.HUD_TOP_BOX_H;
  return html`
    <g data-widget=${`hud-top-${label.toLowerCase()}`}>
      <rect x=${x} y=${y} width=${w} height=${h}
            fill=${H.HUD_BOX_FILL}
            stroke="var(--white)" stroke-width="2" rx="4" ry="4" />
      <text x=${x + w / 2} y=${y + h * 0.32}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-size=${H.HUD_TOP_LABEL_FONT_SIZE}
            fill="var(--white)"
            text-anchor="middle" dominant-baseline="central">${label}</text>
      <text x=${x + w / 2} y=${y + h * 0.70}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-weight="bold"
            font-size=${H.HUD_TOP_VALUE_FONT_SIZE}
            fill="var(--white)"
            text-anchor="middle" dominant-baseline="central">${value}</text>
    </g>`;
};
