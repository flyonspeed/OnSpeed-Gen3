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

// VSI numeric readout — FlySto style: a signed number floating just
// outside the altimeter centerline. Renders only when |VSI| exceeds
// HUD_VSI_THRESHOLD fpm so the readout doesn't churn at idle.
export const HudVsiReadout = ({ vsiFpm = 0 }) => {
  if (!Number.isFinite(vsiFpm)) return null;
  if (Math.abs(vsiFpm) < H.HUD_VSI_THRESHOLD) return null;
  const rounded = Math.round(vsiFpm / 10) * 10;  // tens of fpm
  const sign = rounded > 0 ? '+' : '';
  return html`
    <g data-widget="hud-vsi">
      <text x=${H.HUD_VSI_X} y=${H.HUD_VSI_Y}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-weight="bold"
            font-size=${H.HUD_VSI_FONT_SIZE}
            fill=${H.HUD_VSI_COLOR}
            text-anchor="end" dominant-baseline="central">${sign}${rounded}</text>
    </g>`;
};

// G-load readout — "X.X G" rendered near the slip ball. Reads the
// vertical-G (load factor) channel. Negative G clamped at -1 for
// the display; positive unconstrained.
export const HudGReadout = ({ verticalG = 1 }) => {
  if (!Number.isFinite(verticalG)) return null;
  const g = Math.max(-9.9, Math.min(9.9, verticalG));
  return html`
    <g data-widget="hud-g">
      <text x=${H.HUD_G_X} y=${H.HUD_G_Y}
            font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
            font-weight="bold"
            font-size=${H.HUD_G_FONT_SIZE}
            fill=${H.HUD_LINE_COLOR}
            text-anchor="start" dominant-baseline="central">${g.toFixed(1)} G</text>
    </g>`;
};
