import { colors } from './colors.js';
import {
  SLIP_CENTER_X,
  SLIP_CENTER_Y,
  SLIP_BALL_X_RANGE,
} from './geometry.js';

// Slip from lateral G (SerialRead.cpp:269): Slip = LateralG × 34 / 0.04 = ×850.
// Clamped to ±99 to match the wire-format range.
export function slipFromLateralG(lateralG) {
  const v = Math.round(lateralG * 850);
  return Math.max(-99, Math.min(99, v));
}

// Ball position (SVG cx) and fill color.
// Flash logic (drawSlip :1069-1071): when high-AOA AND high-slip, alternate
// between black and red with the chevron's flashFlag cadence.
export function slipBall({ slip, percentLift, stallWarn, flashFlag }) {
  // Position: drawSlip() draws ball at CenterX + slipValue × (W-H-1) / 99 / 2.
  const cx = SLIP_CENTER_X + slip * SLIP_BALL_X_RANGE / 99;
  const cy = SLIP_CENTER_Y;

  let fill = colors.TFT_GREEN;
  if (Math.abs(slip) >= 30 && percentLift >= stallWarn) {
    fill = flashFlag ? colors.TFT_BLACK : colors.TFT_RED;
  }
  return { cx, cy, fill };
}
