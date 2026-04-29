import { slipFromLateralG, slipBall } from '../lib/slipBall.js';
import { colors } from '../lib/colors.js';
import { SLIP_CENTER_X, SLIP_CENTER_Y } from '../lib/geometry.js';

export function run(assert) {
  // SerialRead.cpp:269: Slip = LateralG × 850, clamp ±99.
  assert.equal(slipFromLateralG(0),     0,   'zero lateral G -> zero slip');
  assert.equal(slipFromLateralG(0.05), 43,   '0.05 G -> 43 slip');
  assert.equal(slipFromLateralG(1.0),  99,   '1 G -> clamped 99');
  assert.equal(slipFromLateralG(-1.0), -99,  '-1 G -> clamped -99');

  // Ball position centered at zero slip.
  let r = slipBall({ slip: 0, percentLift: 50, stallWarn: 80, flashFlag: false });
  assert.equal(r.cx, SLIP_CENTER_X, 'zero slip: centered');
  assert.equal(r.cy, SLIP_CENTER_Y, 'cy always center');
  assert.equal(r.fill, colors.TFT_GREEN, 'normal: green');

  // Off-center but not stalling: still green.
  r = slipBall({ slip: 50, percentLift: 50, stallWarn: 80, flashFlag: false });
  assert.equal(r.fill, colors.TFT_GREEN, 'high slip but not stalling: green');

  // Stalling AND high slip: flashes red/black.
  r = slipBall({ slip: 50, percentLift: 90, stallWarn: 80, flashFlag: false });
  assert.equal(r.fill, colors.TFT_RED, 'stalling+slipping flash on: red');
  r = slipBall({ slip: 50, percentLift: 90, stallWarn: 80, flashFlag: true });
  assert.equal(r.fill, colors.TFT_BLACK, 'stalling+slipping flash off: black');
}
