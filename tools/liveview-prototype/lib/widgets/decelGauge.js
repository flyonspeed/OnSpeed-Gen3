// Vertical band gauge with a sliding pointer for the deceleration rate.
// Mirrors displayDecelGauge() at main.cpp:1350-1380.
//
// Layout: 102×210 rounded rectangle, red background with a green
// "OnSpeed" band y=87..123. White pointer 7 px tall slides up/down
// based on SmoothedDecelRate (knots/sec). Numeric pips "-3,-2,-1,0,1"
// drawn outside the gauge to the left.
//
// NOTE — there is a SECOND decel gauge in the firmware at
// software/OnSpeed-Gen3-ESP32/Web/html_calibration.h (driven by
// javascript_calibration.h). That one is used in the calibration
// wizard's decel-from-Vmax step: 160×340 desktop widget with red+green+red
// bands, range -4..+2 kt/s, animated via group-translate. We deliberately
// do NOT share code — the two gauges have different visual designs,
// dimensions, ranges, and live in different render environments
// (firmware-embedded HTML vs. browser ES modules). If a third decel
// gauge appears, revisit and extract a shared primitive then.

import { colors } from '../colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountDecelGauge(parent, {
  gaugeX, gaugeY, gaugeW, gaugeH, gaugeRadius,
  greenX, greenY, greenW, greenH,
  pointerX, pointerW, pointerH, pointerHalfH, pointerYMin, pointerYMax,
  decelScale, decelOffset,
  labelX, labels, labelFontSize,
  pipX1, pipX2,
}) {
  const group = mk(parent, 'g', { 'data-widget': 'decel-gauge' });

  // Red background + green band + outline.
  mk(group, 'rect', {
    x: gaugeX, y: gaugeY, width: gaugeW, height: gaugeH,
    rx: gaugeRadius, ry: gaugeRadius, fill: colors.TFT_RED,
  });
  mk(group, 'rect', {
    x: greenX, y: greenY, width: greenW, height: greenH,
    fill: colors.TFT_GREEN,
  });
  mk(group, 'rect', {
    x: gaugeX, y: gaugeY, width: gaugeW, height: gaugeH,
    rx: gaugeRadius, ry: gaugeRadius, fill: 'none',
    stroke: colors.TFT_LIGHTGREY, 'stroke-width': 1,
  });

  // Pointer — 7 px white bar with a 1 px black outline. Updates by
  // resetting `y` per frame. CSS transition smooths 20 Hz steps.
  // Mounted hidden; first valid update unhides.
  const pointer = mk(group, 'rect', {
    x: pointerX, y: 138, width: pointerW, height: pointerH,
    fill: colors.TFT_WHITE,
    stroke: colors.TFT_BLACK, 'stroke-width': 1,
    style: 'transition: y 100ms linear;',
    visibility: 'hidden',
  });

  // Numeric labels + matching pip ticks.
  for (const lbl of labels) {
    mk(group, 'text', {
      x: labelX, y: lbl.y,
      'font-family': 'Helvetica, Arial, sans-serif',
      'font-size': labelFontSize,
      fill: colors.TFT_WHITE,
      'text-anchor': 'end',
      'dominant-baseline': 'central',
    }).textContent = lbl.text;

    mk(group, 'line', {
      x1: pipX1, y1: lbl.y, x2: pipX2, y2: lbl.y,
      stroke: colors.TFT_LIGHTGREY, 'stroke-width': 1,
    });
  }

  function update({ decelRate, dataValid = true }) {
    if (!dataValid) {
      pointer.setAttribute('visibility', 'hidden');
      return;
    }
    pointer.setAttribute('visibility', 'visible');
    // Pointer y is the TOP of the rect; decelOffset is where the
    // CENTER lands at decelRate=0. Subtract pointerHalfH to get top.
    let y = decelScale * decelRate + decelOffset - pointerHalfH;
    if (y < pointerYMin) y = pointerYMin;
    if (y > pointerYMax) y = pointerYMax;
    pointer.setAttribute('y', y);
  }

  return { el: group, update };
}
