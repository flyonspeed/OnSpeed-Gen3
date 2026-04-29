import { donutColors } from '../lib/donutColors.js';
import { colors } from '../lib/colors.js';

const anchors = [0, 0, 33, 51, 64, 0, 33, 80];  // fast=51, slow=64, range=13

export function run(assert) {
  // Below fast — all gray.
  let r = donutColors({ percentLift: 30, anchors });
  assert.equal(r.bottomArc, colors.TFT_DARKGREY, 'below fast: bottom arc gray');
  assert.equal(r.topArc,    colors.TFT_DARKGREY, 'below fast: top arc gray');
  assert.equal(r.dot,       colors.TFT_DARKGREY, 'below fast: dot gray');
  // At fast (lower edge): bottom arc on, top arc off, dot off.
  r = donutColors({ percentLift: 51, anchors });
  assert.equal(r.bottomArc, colors.TFT_GREEN,    'at fast: bottom on');
  assert.equal(r.topArc,    colors.TFT_DARKGREY, 'at fast: top off');
  assert.equal(r.dot,       colors.TFT_DARKGREY, 'at fast: dot off');
  // Centered (= 51 + 0.5 × 13 = 57.5): all three on.
  r = donutColors({ percentLift: 57.5, anchors });
  assert.equal(r.bottomArc, colors.TFT_GREEN, 'centered: bottom on');
  assert.equal(r.topArc,    colors.TFT_GREEN, 'centered: top on');
  assert.equal(r.dot,       colors.TFT_GREEN, 'centered: dot on');
  // At slow (upper edge): top arc on, bottom off, dot off.
  r = donutColors({ percentLift: 64, anchors });
  assert.equal(r.bottomArc, colors.TFT_DARKGREY, 'at slow: bottom off');
  assert.equal(r.topArc,    colors.TFT_GREEN,    'at slow: top on');
  assert.equal(r.dot,       colors.TFT_DARKGREY, 'at slow: dot off');
}
