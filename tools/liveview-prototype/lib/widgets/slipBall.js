// Slip ball widget: tick frame + ball circle. Width/height configurable so
// callers can pass Mode 0's wider layout (W=160, H=34) or Mode 1's narrower
// one. Position math + flash logic come from the pure helper at
// `lib/slipBall.js` (kept as a separate math module so the C++ porting math
// stays close to the C++ surface).

import { colors } from '../colors.js';
import { slipBall as slipBallPos } from '../slipBall.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

export function mountSlipBall(parent, { x, y, width, height }) {
  const group = mk(parent, 'g', { 'data-widget': 'slip-ball' });
  const centerX = x + width / 2;
  const centerY = y + height / 2;
  const ballR   = height / 2 - 1;
  const xRange  = (width - height - 1) / 2;

  // Tick frame — 4 rects, 2 black + 2 white either side of the ball.
  // drawSlip() main.cpp:1066-1069.
  mk(group, 'rect', { x: centerX - height / 2 - 9, y, width: 10, height, fill: colors.TFT_BLACK });
  mk(group, 'rect', { x: centerX - height / 2 - 7, y, width:  6, height, fill: colors.TFT_WHITE });
  mk(group, 'rect', { x: centerX + height / 2,     y, width: 10, height, fill: colors.TFT_BLACK });
  mk(group, 'rect', { x: centerX + height / 2 + 2, y, width:  6, height, fill: colors.TFT_WHITE });

  const ball = mk(group, 'circle', {
    cx: centerX, cy: centerY, r: ballR, fill: colors.TFT_GREEN,
  });

  function update({ slip, percentLift, stallWarn, flashFlag }) {
    // Position: drawSlip() draws ball at CenterX + slip × xRange / 99.
    // Recomputed locally because the helper hardcodes Mode 0 geometry.
    const cx = centerX + slip * xRange / 99;
    // Fill uses the shared math so the high-AOA flash policy stays in one place.
    const { fill } = slipBallPos({ slip, percentLift, stallWarn, flashFlag });
    ball.setAttribute('cx', cx);
    ball.setAttribute('fill', fill);
  }

  return { el: group, update, centerX, centerY, ballR };
}
