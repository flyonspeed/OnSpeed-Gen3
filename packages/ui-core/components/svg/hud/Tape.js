// Tape.js — vertical scrolling numeric strip for IAS / ALT.
//
// The visible strip is centered vertically on the HUD. The current
// value sits in a highlighted center box; ticks and labels scroll
// behind it so the currently-indicated value always reads correctly
// in the center box.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

// One tick + (optionally) one label. Drawn at `value` units; SVG-y is
// translated by `-(value - currentValue) × pxPerUnit` so currentValue
// always renders at the centerline.
const tickRow = (props) => {
  const {
    side, value, currentValue, pxPerUnit, cy,
    tickX, tickLenMajor, tickLenMinor,
    labelEvery, labelX, labelAnchor, labelFontSize,
  } = props;
  const dy = -(value - currentValue) * pxPerUnit;
  const isMajor = value % labelEvery === 0;
  const tickLen = isMajor ? tickLenMajor : tickLenMinor;
  // Side decides which way the tick pokes out of the tape.
  const tx1 = side === 'left' ? tickX : tickX - tickLen;
  const tx2 = side === 'left' ? tickX + tickLen : tickX;
  return html`
    <g transform="translate(0 ${dy})">
      <line x1=${tx1} y1=${cy} x2=${tx2} y2=${cy}
            stroke=${H.HUD_LINE_COLOR} stroke-width="3"
            shape-rendering="crispEdges" />
      ${isMajor && html`
        <text x=${labelX} y=${cy}
              font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
              font-size=${labelFontSize}
              fill=${H.HUD_LINE_COLOR}
              text-anchor=${labelAnchor} dominant-baseline="central">${value}</text>`}
    </g>`;
};

// `side` = 'left' (IAS) or 'right' (ALT). The tape area is clipped to
// the visible window so off-strip ticks don't bleed past the edges.
export const HudTape = ({
  side, value = 0, valid = true,
  tapeX, tapeW, tapeH, cy,
  pxPerUnit, tickEvery, labelEvery,
  tickLenMajor, tickLenMinor, labelFontSize,
  boxH, boxFontSize, boxPadX, formatLabel,
  clipId,
}) => {
  const fmt = formatLabel || ((v) => String(v));
  // Range of values to draw: enough above + below the current value
  // to fill the visible window with a margin. Snap to multiples of
  // tickEvery.
  const v0 = Math.round(value);
  const halfRangeUnits = Math.ceil((tapeH / 2) / pxPerUnit) + tickEvery;
  const minVal = Math.floor((v0 - halfRangeUnits) / tickEvery) * tickEvery;
  const maxVal = Math.ceil((v0 + halfRangeUnits) / tickEvery) * tickEvery;
  const ticks = [];
  for (let v = minVal; v <= maxVal; v += tickEvery) {
    if (v < 0) continue;  // Negative IAS/ALT doesn't render — pilot-visible math.
    ticks.push(tickRow({
      side,
      value: v,
      currentValue: value,
      pxPerUnit,
      cy,
      tickX: side === 'left' ? tapeX + tapeW : tapeX,
      tickLenMajor, tickLenMinor,
      labelEvery,
      labelX: side === 'left' ? tapeX + tapeW - tickLenMajor - 8 : tapeX + tickLenMajor + 8,
      labelAnchor: side === 'left' ? 'end' : 'start',
      labelFontSize,
    }));
  }

  // Center highlight box — black background, current value in white.
  const boxX = side === 'left' ? tapeX : tapeX;
  const boxY = cy - boxH / 2;
  const boxText = valid ? fmt(value) : '---';

  return html`
    <g data-widget=${`hud-tape-${side}`}>
      <defs>
        <clipPath id=${clipId}>
          <rect x=${tapeX} y=${cy - tapeH / 2} width=${tapeW} height=${tapeH} />
        </clipPath>
      </defs>
      <g clip-path="url(#${clipId})">
        ${ticks}
      </g>
      <rect x=${boxX - 2} y=${boxY}
            width=${tapeW + 4} height=${boxH}
            fill=${H.HUD_BOX_FILL}
            stroke=${H.HUD_LINE_COLOR} stroke-width="2" />
      <text x=${boxX + tapeW / 2} y=${cy}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-weight="bold"
            font-size=${boxFontSize}
            fill=${H.HUD_LINE_COLOR}
            text-anchor="middle" dominant-baseline="central">${boxText}</text>
    </g>`;
};

// VSI chevron — vertical bar to the right of the ALT tape. Up chevron
// for climb, down for descent; length proportional to |VSI|, clamped.
export const HudVsiChevron = ({ vsiFpm = 0 }) => {
  const sign = vsiFpm >= 0 ? -1 : 1;   // SVG up = negative y
  const mag = Math.min(Math.abs(vsiFpm) / H.HUD_VSI_FULL_SCALE_FPM, 1);
  const len = mag * H.HUD_VSI_MAX_LEN_PX;
  const x = H.HUD_VSI_CHEVRON_X;
  const y0 = H.HUD_ALT_CY;
  const y1 = y0 + sign * len;
  if (len < 4) return null;
  return html`
    <g data-widget="hud-vsi">
      <line x1=${x} y1=${y0} x2=${x} y2=${y1}
            stroke=${H.HUD_LINE_COLOR} stroke-width=${H.HUD_VSI_STROKE}
            shape-rendering="crispEdges" />
      <polyline points="${x - 10},${y1 - sign * 12}
                        ${x},${y1}
                        ${x + 10},${y1 - sign * 12}"
                fill="none" stroke=${H.HUD_LINE_COLOR}
                stroke-width=${H.HUD_VSI_STROKE} stroke-linejoin="miter" />
    </g>`;
};
