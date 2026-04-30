import { colors } from './colors.js';

// Donut segment colors. Three independent segments per drawAOA() :1011-1031.
// Returns { topArc, bottomArc, dot }, each TFT_GREEN or TFT_DARKGREY.
//
// Range gates inside the OnSpeed band [fast, slow]:
//   bottom arc: [fast, fast + 0.75 × range]    — lower 75%
//   top arc:    [fast + 0.25 × range, slow]    — upper 75%
//   center dot: [fast + 0.25 × range, slow - 0.25 × range]  — middle 50%
// (range = slow - fast)
export function donutColors({ percentLift, anchors }) {
  const fast = anchors[3];
  const slow = anchors[4];
  const range = slow - fast;

  const bottomArc = (percentLift >= fast && percentLift <= fast + 0.75 * range)
    ? colors.TFT_GREEN : colors.TFT_DARKGREY;
  const topArc = (percentLift >= fast + 0.25 * range && percentLift <= slow)
    ? colors.TFT_GREEN : colors.TFT_DARKGREY;
  const dot = (percentLift >= fast + 0.25 * range && percentLift <= slow - 0.25 * range)
    ? colors.TFT_GREEN : colors.TFT_DARKGREY;

  return { topArc, bottomArc, dot };
}
