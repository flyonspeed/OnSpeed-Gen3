import { chevronColors } from '../lib/chevronColors.js';
import { colors } from '../lib/colors.js';

const anchors = [0, 0, 33, 51, 64, 0, 33, 80];  // tonesOn=33, fast=51, slow=64, stall=80

export function run(assert) {
  // Below tonesOn — both gray.
  let r = chevronColors({ percentLift: 20, anchors, flashFlag: false });
  assert.equal(r.top,    colors.TFT_DARKGREY, 'low AOA: top gray');
  assert.equal(r.bottom, colors.TFT_DARKGREY, 'low AOA: bottom gray');
  // In bottom-chevron green range.
  r = chevronColors({ percentLift: 50, anchors, flashFlag: false });
  assert.equal(r.bottom, colors.TFT_GREEN, 'in [tonesOn,slow): bottom green');
  assert.equal(r.top,    colors.TFT_DARKGREY, 'in [tonesOn,slow): top gray');
  // Past slow but below chevMid (=72).
  r = chevronColors({ percentLift: 68, anchors, flashFlag: false });
  assert.equal(r.top, colors.TFT_YELLOW, 'past slow: top yellow');
  // Past chevMid but at stallWarn.
  r = chevronColors({ percentLift: 75, anchors, flashFlag: false });
  assert.equal(r.top, colors.TFT_RED, 'past chevMid: top red');
  // Above stallWarn, not flashing.
  r = chevronColors({ percentLift: 90, anchors, flashFlag: false });
  assert.equal(r.top, colors.TFT_RED, 'above stallWarn, not flash: top red');
  // Above stallWarn, flashing — top goes dark for the off-half.
  r = chevronColors({ percentLift: 90, anchors, flashFlag: true });
  assert.equal(r.top, colors.TFT_DARKGREY, 'above stallWarn, flash: top dark');
}
