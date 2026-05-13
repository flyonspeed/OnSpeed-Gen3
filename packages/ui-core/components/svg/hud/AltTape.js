// AltTape.js — FlySto-style altimeter tape with Garmin-style readout box.
//
// Vertical stack of horizontal tick marks (every 20 ft) labeled every
// 100 ft, scrolling so the current altitude sits on the centerline.
// CENTERED ON the tape (overlapping the tick column), a Garmin-style
// readout box: stationary thousands+hundreds digits alongside a sliding
// tens strip clipped to the box. The arrow tab on the box's LEFT side
// notches LEFT into the tick column, tip landing 2 px past the left
// tick stem. Below the tape, a static "29.92in" baro setting (the log
// doesn't carry baro).
//
// Geometry comes from hudGeometry.js (HUD_ALT_*). The clipPath id is
// hardcoded to "hud-alt-readout-clip" — only one HudAltTape per SVG.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

// Box outline path + clip path share the exact same geometry so the
// sliding tens digits never spill past the rounded corners. The arrow
// tab sits on the LEFT side and points LEFT into the tape's tick
// column (the tape's centerline runs through the column where the box
// left wall sits, so the arrow tip notches inboard toward the ticks).
//
//                topL ───────────────── topR
//   tabTopL  ↙    │                       │
//   tip @ tipX  ◂ │                       │
//   tabBotL  ↖    │                       │
//                botL ───────────────── botR
//
// Corner radius `r` is small (4 px) to match FlySto's box.
function buildBoxPath() {
  const L = H.HUD_ALT_BOX_LEFT;
  const R = H.HUD_ALT_BOX_RIGHT;
  const T = H.HUD_ALT_BOX_TOP;
  const B = H.HUD_ALT_BOX_BOTTOM;
  const cy = H.HUD_ALT_CY;
  const tip = H.HUD_ALT_BOX_ARROW_TIP_X;
  // Arrow tab spans 7 px above and below the centerline at the box's
  // left edge — small triangular notch toward the tape ticks.
  const tabHalf = 7;
  const r = 4;
  return [
    `M ${L + r} ${T}`,
    `L ${R - r} ${T}`,
    `A ${r} ${r} 0 0 1 ${R} ${T + r}`,
    `L ${R} ${B - r}`,
    `A ${r} ${r} 0 0 1 ${R - r} ${B}`,
    `L ${L + r} ${B}`,
    `A ${r} ${r} 0 0 1 ${L} ${B - r}`,
    `L ${L} ${cy + tabHalf}`,
    `L ${tip} ${cy}`,
    `L ${L} ${cy - tabHalf}`,
    `L ${L} ${T + r}`,
    `A ${r} ${r} 0 0 1 ${L + r} ${T}`,
    'Z',
  ].join(' ');
}

const pad2 = (n) => String(n).padStart(2, '0');

export const HudAltTape = ({ altitudeFt = 0 }) => {
  const alt = Number.isFinite(altitudeFt) ? altitudeFt : 0;
  // Scroll offset: positive frac → tape needs to shift downward so the
  // tick at nearest20Below stays aligned. fracPx is how many px of
  // tape have scrolled past the centerline since the last 20-ft mark.
  const nearest20Below = Math.floor(alt / 20) * 20;
  const frac = (alt - nearest20Below) / 20;                  // 0..1
  const fracPx = frac * H.HUD_ALT_PX_PER_20_FT;
  // Render 30 ticks straddling the centerline (-300..+300 ft from
  // nearest20Below). Each tick i is at altitude nearest20Below + i*20,
  // and its y is HUD_ALT_CY - i*HUD_ALT_PX_PER_20_FT + fracPx (note
  // the +fracPx: the tape shifts DOWN as alt rises within the 20-ft
  // window, so the 0-ft tick remains visually anchored to where the
  // pointer expects it).
  const ticks = [];
  for (let i = -15; i <= 15; i++) {
    const tickAlt = nearest20Below + i * 20;
    const y = H.HUD_ALT_CY - i * H.HUD_ALT_PX_PER_20_FT + fracPx;
    const isMajor = tickAlt % 100 === 0;
    const tickEnd = H.HUD_ALT_X + (isMajor ? H.HUD_ALT_TICK_LONG : H.HUD_ALT_TICK_SHORT);
    ticks.push(html`
      <line x1=${H.HUD_ALT_X} y1=${y}
            x2=${tickEnd} y2=${y}
            stroke="var(--white)"
            stroke-width=${H.HUD_ALT_TICK_STROKE}
            stroke-linecap="round" />`);
    if (isMajor) {
      const labelX = H.HUD_ALT_X + H.HUD_ALT_TICK_LONG + H.HUD_ALT_LABEL_OFFSET_X;
      ticks.push(html`
        <text x=${labelX} y=${y}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-size=${H.HUD_ALT_LABEL_FONT_SIZE}
              fill="var(--white)"
              text-anchor="start" dominant-baseline="central">${tickAlt}</text>`);
    }
  }

  // Readout box digits:
  //   - Stationary "hundreds" digits: floor(alt / 100). For 6143 →
  //     "61"; for 12340 → "123"; for negative → "-1" etc. Anchored
  //     vertically on the box centerline.
  //   - Sliding tens strip: three two-digit numbers (curr, up20,
  //     down20) stacked vertically. The strip translates by
  //     -frac*HUD_ALT_TENS_SLIDE_PX so that as alt rises through a
  //     20-ft step, the "up" digits scroll into the window from above.
  //     (Sign chosen to match standard altimeter convention: alt up →
  //     tens spin up → next-larger number appears below the current,
  //     OK wait — convention is digits roll DOWN as alt rises. So the
  //     strip translates DOWNWARD as frac grows, putting the "next up"
  //     digits ABOVE current. See below.)
  const hundreds = Math.floor(alt / 100);
  const tensCurr = ((nearest20Below % 100) + 100) % 100;
  const tensUp   = (((nearest20Below + 20) % 100) + 100) % 100;
  const tensDown = (((nearest20Below - 20) % 100) + 100) % 100;

  // Strip y-offset: as alt rises within a 20-ft window, the strip
  // shifts DOWNWARD by frac*slidePx. The "current" digit at frac=0
  // sits exactly on the box centerline. At frac=1, current has slid
  // down by one slot, replaced from above by the "up" digit (which is
  // now the new current 20 ft higher). FlySto's slide direction.
  const stripDy = frac * H.HUD_ALT_TENS_SLIDE_PX;

  const boxPath = buildBoxPath();
  const clipId = 'hud-alt-readout-clip';
  // Clip the tick column to the visible vertical window so the ±15 ticks
  // (spanning ±225 px) don't bleed past the HUD_ALT_HALF_H = ±220 frame.
  // Uses the same x-extent as the backing strip plus a generous label
  // overrun so wider numeric labels (e.g. "12340") stay visible.
  const tickClipId = 'hud-alt-tick-clip';

  // Stationary thousands+hundreds digits anchored on the LEFT half of
  // the box (closest to the arrow tab); sliding tens strip anchored to
  // their RIGHT. Both vertically centered on HUD_ALT_CY. Offsets
  // mirror FlySto's box-internal layout: stationary digits at ~24 px
  // right of left wall, sliding tens at ~62 px right of left wall.
  const hundredsX = H.HUD_ALT_BOX_LEFT + 24;
  const tensX     = H.HUD_ALT_BOX_LEFT + 62;

  return html`
    <g data-widget="hud-alt-tape">
      <defs>
        <clipPath id=${clipId}>
          <path d=${boxPath} />
        </clipPath>
        <clipPath id=${tickClipId}>
          <rect x=${H.HUD_ALT_BACKING_X}
                y=${H.HUD_ALT_BACKING_Y}
                width=${H.HUD_ALT_BACKING_W}
                height=${H.HUD_ALT_BACKING_H} />
        </clipPath>
      </defs>

      <!-- Semi-transparent dark backing for legibility on busy video -->
      <rect x=${H.HUD_ALT_BACKING_X}
            y=${H.HUD_ALT_BACKING_Y}
            width=${H.HUD_ALT_BACKING_W}
            height=${H.HUD_ALT_BACKING_H}
            rx=${H.HUD_ALT_BACKING_RX}
            ry=${H.HUD_ALT_BACKING_RX}
            fill=${H.HUD_ALT_BACKING_FILL} />

      <!-- ticks + labels (drawn first so the box sits on top, clipped
           to the visible window so off-screen ticks don't render) -->
      <g clip-path="url(#${tickClipId})">
        ${ticks}
      </g>

      <!-- Static "29.92in" baro below the tape, centered on the
           ALT readout box (not the tick column) -->
      <text x=${H.HUD_ALT_BARO_X} y=${H.HUD_ALT_BARO_Y}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-size=${H.HUD_ALT_BARO_FONT_SIZE}
            fill="var(--white)"
            text-anchor="middle" dominant-baseline="central">29.92in</text>

      <!-- Garmin readout box -->
      <path d=${boxPath}
            fill=${H.HUD_ALT_BOX_FILL}
            stroke="var(--white)" stroke-width="2" />

      <!-- Stationary hundreds digits -->
      <text x=${hundredsX} y=${H.HUD_ALT_CY}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-weight="bold"
            font-size=${H.HUD_ALT_BOX_FONT_SIZE}
            fill="var(--white)"
            text-anchor="start" dominant-baseline="central">${hundreds}</text>

      <!-- Sliding tens strip, clipped to the box outline -->
      <g clip-path="url(#${clipId})">
        <g transform="translate(0 ${stripDy})">
          <text x=${tensX} y=${H.HUD_ALT_CY - H.HUD_ALT_TENS_SLIDE_PX}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_ALT_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start" dominant-baseline="central">${pad2(tensUp)}</text>
          <text x=${tensX} y=${H.HUD_ALT_CY}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_ALT_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start" dominant-baseline="central">${pad2(tensCurr)}</text>
          <text x=${tensX} y=${H.HUD_ALT_CY + H.HUD_ALT_TENS_SLIDE_PX}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_ALT_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start" dominant-baseline="central">${pad2(tensDown)}</text>
        </g>
      </g>
    </g>`;
};
