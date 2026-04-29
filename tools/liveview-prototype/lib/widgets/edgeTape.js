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
  // Mode 0's gOnset ladder draws ticks in TFT_GREY (main.cpp:851).
  // Mode 1's VSI ladder draws ticks in TFT_BLACK (main.cpp:615, 619-621).
  // Caller passes colors.TFT_BLACK for Mode 1; default keeps Mode 0's grey.
  tickColor = colors.TFT_GREY,
  pipColor  = colors.TFT_GREY,
}) {
  const group = mk(parent, 'g', { 'data-widget': 'edge-tape' });

  // Paint order matches the M5 (main.cpp:845-857): bar FIRST, ticks +
  // zero pip on top. SVG renders later siblings in front, so we append
  // the bar before the ticks. That way the ticks remain visible across
  // the bar instead of being hidden by it.
  // CSS transition on the bar's y/height makes 20 Hz data updates appear
  // as a smooth rise/fall instead of stepped jumps. The data is already
  // EMA-smoothed at 250 ms tau on the M5 producer side, so the values
  // arriving here are gentle; a 100 ms transition tracks them without
  // adding visible lag, while killing the per-frame stair-step look.
  const bar = mk(group, 'rect', {
    x: barX, y: zeroY, width: barW, height: 0, fill: barColor,
    style: 'transition: y 100ms linear, height 100ms linear;',
  });

  for (let y = tickFirstY; y <= tickLastY; y += tickStep) {
    mk(group, 'line', {
      x1: tickX1, y1: y, x2: tickX2, y2: y,
      stroke: tickColor, 'stroke-width': 1,
    });
  }
  for (const y of pipYs) {
    mk(group, 'line', {
      x1: pipX1, y1: y, x2: pipX2, y2: y,
      stroke: pipColor, 'stroke-width': 1,
    });
  }

  function update({ value }) {
    const h   = Math.min(heightMax, Math.abs(value * heightScale));
    const top = value > 0 ? zeroY - h : zeroY;
    bar.setAttribute('y', top);
    bar.setAttribute('height', h);
  }

  return { el: group, update };
}
