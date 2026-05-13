// IasTape.js — FlySto-style airspeed tape with Garmin-style readout box.
//
// Mirror of AltTape on the left side of the HUD. Vertical stack of
// horizontal tick marks (every 5 kt) labeled every 10 kt, scrolling
// so the current IAS sits on the centerline. CENTERED ON the tape
// (overlapping the tick column), a Garmin-style readout box:
// stationary hundreds+tens digits alongside a sliding ones digit
// clipped to the box. The arrow tab on the box's RIGHT side notches
// RIGHT into the tick column, tip landing 2 px past the right tick
// stem.
//
// Monochrome — no Vne/Vno/Vfe color bands. Per-aircraft V-speed
// plumbing doesn't exist on the live page today; the bands wait
// until config flows through.
//
// Geometry comes from hudGeometry.js (HUD_IAS_*). The clipPath id is
// hardcoded to "hud-ias-readout-clip" — only one HudIasTape per SVG.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

// Box outline path + clip path share the exact same geometry so the
// sliding ones digits never spill past the rounded corners. The arrow
// tab sits on the RIGHT side and points RIGHT into the tape's tick
// column (the tape's centerline runs through the column where the box
// right wall sits, so the arrow tip notches inboard toward the ticks).
//
//                topL ───────────────── topR
//                 │                       │   ↘ tabTopR
//                 │                       │  ◂ tip @ tipX
//                 │                       │   ↗ tabBotR
//                botL ───────────────── botR
//
// Corner radius `r` is small (4 px) to match FlySto's box.
function buildBoxPath() {
  const L = H.HUD_IAS_BOX_LEFT;
  const R = H.HUD_IAS_BOX_RIGHT;
  const T = H.HUD_IAS_BOX_TOP;
  const B = H.HUD_IAS_BOX_BOTTOM;
  const cy = H.HUD_IAS_CY;
  const tip = H.HUD_IAS_BOX_ARROW_TIP_X;
  // Arrow tab spans 7 px above and below the centerline at the box's
  // right edge — small triangular notch toward the tape ticks.
  const tabHalf = 7;
  const r = 4;
  return [
    `M ${L + r} ${T}`,
    `L ${R - r} ${T}`,
    `A ${r} ${r} 0 0 1 ${R} ${T + r}`,
    `L ${R} ${cy - tabHalf}`,
    `L ${tip} ${cy}`,
    `L ${R} ${cy + tabHalf}`,
    `L ${R} ${B - r}`,
    `A ${r} ${r} 0 0 1 ${R - r} ${B}`,
    `L ${L + r} ${B}`,
    `A ${r} ${r} 0 0 1 ${L} ${B - r}`,
    `L ${L} ${T + r}`,
    `A ${r} ${r} 0 0 1 ${L + r} ${T}`,
    'Z',
  ].join(' ');
}

export const HudIasTape = ({ iasKt = 0 }) => {
  const ias = Number.isFinite(iasKt) ? Math.max(0, iasKt) : 0;
  // Scroll offset: positive frac → tape needs to shift downward so
  // the tick at nearest5Below stays aligned. fracPx is how many px of
  // tape have scrolled past the centerline since the last 5-kt mark.
  const nearest5Below = Math.floor(ias / 5) * 5;
  const frac = (ias - nearest5Below) / 5;                    // 0..1
  const fracPx = frac * H.HUD_IAS_PX_PER_5_KT;
  // Render 30 ticks straddling the centerline (-75..+75 kt from
  // nearest5Below). Each tick i is at IAS nearest5Below + i*5, and
  // its y is HUD_IAS_CY - i*HUD_IAS_PX_PER_5_KT + fracPx.
  const ticks = [];
  for (let i = -15; i <= 15; i++) {
    const tickIas = nearest5Below + i * 5;
    if (tickIas < 0) continue;  // no negative IAS labels
    const y = H.HUD_IAS_CY - i * H.HUD_IAS_PX_PER_5_KT + fracPx;
    const isMajor = tickIas % 10 === 0;
    // Ticks extend LEFTWARD from HUD_IAS_X (the right/inboard edge of
    // the tick column).
    const tickEnd = H.HUD_IAS_X - (isMajor ? H.HUD_IAS_TICK_LONG : H.HUD_IAS_TICK_SHORT);
    ticks.push(html`
      <line x1=${H.HUD_IAS_X} y1=${y}
            x2=${tickEnd} y2=${y}
            stroke="var(--white)"
            stroke-width=${H.HUD_IAS_TICK_STROKE}
            stroke-linecap="round" />`);
    if (isMajor) {
      const labelX = H.HUD_IAS_X - H.HUD_IAS_TICK_LONG - H.HUD_IAS_LABEL_OFFSET_X;
      ticks.push(html`
        <text x=${labelX} y=${y}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-size=${H.HUD_IAS_LABEL_FONT_SIZE}
              fill="var(--white)"
              text-anchor="end" dominant-baseline="central">${tickIas}</text>`);
    }
  }

  // Readout box digits:
  //   - Stationary "hundreds+tens" digits: floor(ias / 10). For
  //     95 → "9"; for 113 → "11"; for 7 → "0".
  //   - Sliding ones strip: three single-digit numbers (curr, up1,
  //     down1) stacked vertically. The strip translates by
  //     frac * HUD_IAS_ONES_SLIDE_PX as IAS advances within a 1-kt
  //     window. The ones digit changes every 1 kt; tape ticks are at
  //     every 5 kt; so within one tape pitch the strip jogs through
  //     5 ones-digit cycles.
  //
  // ones-digit values are computed from the rounded-DOWN integer IAS
  // so the strip steps cleanly at integer-knot boundaries. Use
  // `nearest1Below` for the integer floor and `frac1` for the
  // ones-strip slide phase.
  const nearest1Below = Math.floor(ias);
  const frac1 = ias - nearest1Below;                          // 0..1
  const tens = Math.floor(nearest1Below / 10);
  const onesCurr = ((nearest1Below % 10) + 10) % 10;
  const onesUp   = (((nearest1Below + 1) % 10) + 10) % 10;
  const onesDown = (((nearest1Below - 1) % 10) + 10) % 10;

  // Strip y-offset: as IAS rises within a 1-kt window, the strip
  // shifts DOWNWARD by frac*slidePx. At frac1=0 the "current" digit
  // sits exactly on the box centerline; at frac1=1 current has slid
  // down by one slot, replaced from above by the "up" digit (the new
  // current 1 kt higher). FlySto's slide direction.
  const stripDy = frac1 * H.HUD_IAS_ONES_SLIDE_PX;

  const boxPath = buildBoxPath();
  const clipId = 'hud-ias-readout-clip';

  // Stationary tens digits anchored on the LEFT half of the box
  // (closest to the outer edge, farthest from the right-side arrow
  // tab); sliding ones strip anchored to their RIGHT (closest to the
  // arrow tab). Both vertically centered on HUD_IAS_CY. Mirror of
  // ALT's box-internal layout: stationary digits at ~24 px right of
  // left wall (left-anchored), sliding strip at ~62 px right of left
  // wall (left-anchored).
  const tensX = H.HUD_IAS_BOX_LEFT + 24;
  const onesX = H.HUD_IAS_BOX_LEFT + 80;

  return html`
    <g data-widget="hud-ias-tape">
      <defs>
        <clipPath id=${clipId}>
          <path d=${boxPath} />
        </clipPath>
      </defs>

      <!-- ticks + labels (drawn first so the box sits on top) -->
      ${ticks}

      <!-- Garmin readout box -->
      <path d=${boxPath}
            fill=${H.HUD_IAS_BOX_FILL}
            stroke="var(--white)" stroke-width="2" />

      <!-- Stationary tens digits -->
      <text x=${tensX} y=${H.HUD_IAS_CY}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-weight="bold"
            font-size=${H.HUD_IAS_BOX_FONT_SIZE}
            fill="var(--white)"
            text-anchor="start" dominant-baseline="central">${tens}</text>

      <!-- Sliding ones strip, clipped to the box outline -->
      <g clip-path="url(#${clipId})">
        <g transform="translate(0 ${stripDy})">
          <text x=${onesX} y=${H.HUD_IAS_CY - H.HUD_IAS_ONES_SLIDE_PX}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_IAS_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start" dominant-baseline="central">${onesUp}</text>
          <text x=${onesX} y=${H.HUD_IAS_CY}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_IAS_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start" dominant-baseline="central">${onesCurr}</text>
          <text x=${onesX} y=${H.HUD_IAS_CY + H.HUD_IAS_ONES_SLIDE_PX}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_IAS_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start" dominant-baseline="central">${onesDown}</text>
        </g>
      </g>
    </g>`;
};
