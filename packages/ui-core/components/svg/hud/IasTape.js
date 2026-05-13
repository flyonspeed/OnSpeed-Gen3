// IasTape.js — FlySto-style airspeed tape with Garmin-style readout box.
//
// Mirror of AltTape on the left side of the HUD. Vertical stack of
// horizontal tick marks (every 5 kt) labeled every 10 kt, scrolling
// so the current IAS sits on the centerline. CENTERED ON the tape
// (overlapping the tick column), a Garmin-style readout box:
// stationary hundreds+tens digits alongside a sliding ones digit
// clipped to the box. The arrow tab on the box's RIGHT side notches
// RIGHT into the tick column, tip landing past the right tick stem.
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
// column. `tabHalf` (12 px) matches the ALT tape so the two readout
// boxes share the same arrow-notch profile.
function buildBoxPath() {
  const L = H.HUD_IAS_BOX_LEFT;
  const R = H.HUD_IAS_BOX_RIGHT;
  const T = H.HUD_IAS_BOX_TOP;
  const B = H.HUD_IAS_BOX_BOTTOM;
  const cy = H.HUD_IAS_CY;
  const tip = H.HUD_IAS_BOX_ARROW_TIP_X;
  const tabHalf = 12;
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
  const cy = H.HUD_IAS_CY;
  // Explicit y offset from font metrics — see H.hudGlyphOffset for
  // why we avoid `dominant-baseline="central"`.
  const labelDy = H.hudGlyphOffset(H.HUD_IAS_LABEL_FONT_SIZE);
  const boxDy   = H.hudGlyphOffset(H.HUD_IAS_BOX_FONT_SIZE);

  // Scroll offset: positive frac → tape needs to shift downward so
  // the tick at nearest5Below stays aligned.
  const nearest5Below = Math.floor(ias / 5) * 5;
  const frac = (ias - nearest5Below) / 5;                    // 0..1
  const fracPx = frac * H.HUD_IAS_PX_PER_5_KT;
  const ticks = [];
  for (let i = -15; i <= 15; i++) {
    const tickIas = nearest5Below + i * 5;
    if (tickIas < 0) continue;
    const y = cy - i * H.HUD_IAS_PX_PER_5_KT + fracPx;
    const isMajor = tickIas % 10 === 0;
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
        <text x=${labelX} y=${y + labelDy}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-size=${H.HUD_IAS_LABEL_FONT_SIZE}
              fill="var(--white)"
              text-anchor="end">${tickIas}</text>`);
    }
  }

  // Readout box digits:
  //   - Stationary tens digits: floor(ias / 10). For 113 → "11";
  //     for 95 → "9"; for 7 → "0".
  //   - Sliding ones strip: three single-digit numbers (curr, up1,
  //     down1) stacked vertically. Slide phase frac1 = ias - floor(ias).
  const nearest1Below = Math.floor(ias);
  const frac1 = ias - nearest1Below;                          // 0..1
  const tens = Math.floor(nearest1Below / 10);
  const onesCurr = ((nearest1Below % 10) + 10) % 10;
  const onesUp   = (((nearest1Below + 1) % 10) + 10) % 10;
  const onesDown = (((nearest1Below - 1) % 10) + 10) % 10;
  const stripDy = frac1 * H.HUD_IAS_ONES_SLIDE_PX;

  const boxPath = buildBoxPath();
  const clipId = 'hud-ias-readout-clip';
  const tickClipId = 'hud-ias-tick-clip';

  // Stationary tens digits on the LEFT half of the box (outboard,
  // farthest from the right-side arrow tab); sliding ones strip on
  // the RIGHT half (closest to the arrow tab). Box width 70 →
  // tens at +12, ones at +48 leaves a small but visible reading
  // slot between "13" (anchor start, ~24 px wide at font-size 30)
  // and the sliding "8" (~14 px wide), so "138" reads as one
  // airspeed without the ones digit crowding the tens.
  const tensX = H.HUD_IAS_BOX_LEFT + 12;
  const onesX = H.HUD_IAS_BOX_LEFT + 48;

  return html`
    <g data-widget="hud-ias-tape">
      <defs>
        <clipPath id=${clipId}>
          <path d=${boxPath} />
        </clipPath>
        <clipPath id=${tickClipId}>
          <rect x=${H.HUD_IAS_BACKING_X}
                y=${H.HUD_IAS_BACKING_Y}
                width=${H.HUD_IAS_BACKING_W}
                height=${H.HUD_IAS_BACKING_H} />
        </clipPath>
      </defs>

      <!-- Semi-transparent dark backing for legibility on busy video -->
      <rect x=${H.HUD_IAS_BACKING_X}
            y=${H.HUD_IAS_BACKING_Y}
            width=${H.HUD_IAS_BACKING_W}
            height=${H.HUD_IAS_BACKING_H}
            rx=${H.HUD_IAS_BACKING_RX}
            ry=${H.HUD_IAS_BACKING_RX}
            fill=${H.HUD_IAS_BACKING_FILL} />

      <!-- ticks + labels (drawn first so the box sits on top, clipped
           to the visible window so off-screen ticks don't render) -->
      <g clip-path="url(#${tickClipId})">
        ${ticks}
      </g>

      <!-- Garmin readout box -->
      <path d=${boxPath}
            fill=${H.HUD_IAS_BOX_FILL}
            stroke="var(--white)" stroke-width="2" />

      <!-- Stationary tens digits -->
      <text x=${tensX} y=${cy + boxDy}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-weight="bold"
            font-size=${H.HUD_IAS_BOX_FONT_SIZE}
            fill="var(--white)"
            text-anchor="start">${tens}</text>

      <!-- Sliding ones strip, clipped to the box outline -->
      <g clip-path="url(#${clipId})">
        <g transform="translate(0 ${stripDy})">
          <text x=${onesX} y=${cy - H.HUD_IAS_ONES_SLIDE_PX + boxDy}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_IAS_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start">${onesUp}</text>
          <text x=${onesX} y=${cy + boxDy}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_IAS_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start">${onesCurr}</text>
          <text x=${onesX} y=${cy + H.HUD_IAS_ONES_SLIDE_PX + boxDy}
                font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                font-weight="bold"
                font-size=${H.HUD_IAS_BOX_FONT_SIZE}
                fill="var(--white)"
                text-anchor="start">${onesDown}</text>
        </g>
      </g>
    </g>`;
};
