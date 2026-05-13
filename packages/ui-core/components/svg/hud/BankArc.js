// BankArc.js — top-of-frame bank indicator.
//
// A static reference arc at the top of the HUD with tick marks at
// ±10°/±20°/±30°/±45°/±60° and a stationary downward triangle pointer
// at the 12-o'clock position. The arc itself (with its ticks) rotates
// by -roll, so the ticks slide under the stationary pointer to show
// current bank angle.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

// Single radial tick. Drawn at angle `deg` measured from "up" (12
// o'clock, where deg=0). long=true → thicker, deeper tick.
const tickFor = (deg, long) => {
  // SVG rotates clockwise; positive bank = right wing low, which we
  // render as the ticks rotating clockwise under the pointer. Treat
  // 0° as straight up.
  const rad = (deg - 90) * Math.PI / 180;
  const r1 = H.HUD_BANK_R;
  const r2 = H.HUD_BANK_R - (long ? H.HUD_BANK_TICK_LONG : H.HUD_BANK_TICK_SHORT);
  const cosA = Math.cos(rad);
  const sinA = Math.sin(rad);
  return html`
    <line x1=${H.HUD_BANK_CX + r1 * cosA} y1=${H.HUD_BANK_CY + r1 * sinA}
          x2=${H.HUD_BANK_CX + r2 * cosA} y2=${H.HUD_BANK_CY + r2 * sinA}
          stroke=${H.HUD_LINE_COLOR} stroke-width=${H.HUD_BANK_STROKE}
          shape-rendering="crispEdges" />`;
};

export const HudBankArc = ({ rollDeg = 0 }) => {
  // Stationary downward-pointing pointer at 12 o'clock: tip touches
  // the arc, base sits H.HUD_BANK_POINTER_H above the arc.
  const tipY = H.HUD_BANK_CY - H.HUD_BANK_R;
  const baseY = tipY - H.HUD_BANK_POINTER_H;
  return html`
    <g data-widget="hud-bank-arc">
      <g transform="rotate(${-rollDeg} ${H.HUD_BANK_CX} ${H.HUD_BANK_CY})">
        ${H.HUD_BANK_TICKS.map(t => tickFor(t.deg, t.long))}
      </g>
      <polygon points="${H.HUD_BANK_CX},${tipY}
                       ${H.HUD_BANK_CX - H.HUD_BANK_POINTER_HALF_W},${baseY}
                       ${H.HUD_BANK_CX + H.HUD_BANK_POINTER_HALF_W},${baseY}"
               fill=${H.HUD_BANK_POINTER_COLOR}
               stroke=${H.HUD_LINE_COLOR} stroke-width="2"
               stroke-linejoin="miter" />
    </g>`;
};
