// Right-edge vertical bar gauge with a tick ladder + zero pip. Used by
// Mode 0's G-onset tape and (later) Mode 1/3's VSI tape. The bar grows
// upward when value > 0 and downward when value < 0, clamped to heightMax.

import { colors } from '../colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountEdgeTape(parent, {
  barX, barW, barColor = colors.TFT_YELLOW,
  zeroY,
  heightScale, heightMax,
  tickX1, tickX2, tickFirstY, tickLastY, tickStep,
  pipX1, pipX2, pipYs,
}) {
  const group = mk(parent, 'g', { 'data-widget': 'edge-tape' });

  for (let y = tickFirstY; y <= tickLastY; y += tickStep) {
    mk(group, 'line', {
      x1: tickX1, y1: y, x2: tickX2, y2: y,
      stroke: colors.TFT_GREY, 'stroke-width': 1,
    });
  }
  for (const y of pipYs) {
    mk(group, 'line', {
      x1: pipX1, y1: y, x2: pipX2, y2: y,
      stroke: colors.TFT_GREY, 'stroke-width': 1,
    });
  }

  const bar = mk(group, 'rect', {
    x: barX, y: zeroY, width: barW, height: 0, fill: barColor,
  });

  function update({ value }) {
    const h   = Math.min(heightMax, Math.abs(value * heightScale));
    const top = value > 0 ? zeroY - h : zeroY;
    bar.setAttribute('y', top);
    bar.setAttribute('height', h);
  }

  return { el: group, update };
}
