import { colors } from './colors.js';

// Chevron color decisions for the M5 indexer.
// Returns { top, bottom } where each is one of TFT_DARKGREY / TFT_YELLOW /
// TFT_RED / TFT_GREEN.
//
// Both chevron halves on a row share the same color (same gate fires for
// both). Source: drawAOA() in main.cpp:905-1003.
export function chevronColors({ percentLift, anchors, flashFlag }) {
  const onSpeedSlow = anchors[4];
  const stallWarn   = anchors[7];
  const tonesOn     = anchors[2];
  const chevMid     = onSpeedSlow + (stallWarn - onSpeedSlow) / 2;

  // Top chevron — escalates yellow → red as AOA approaches stall.
  let top = colors.TFT_DARKGREY;
  if      (percentLift > onSpeedSlow && percentLift <= chevMid)   top = colors.TFT_YELLOW;
  else if (percentLift > chevMid     && percentLift <= stallWarn) top = colors.TFT_RED;
  else if (percentLift > stallWarn   && !flashFlag)               top = colors.TFT_RED;
  // else (above stallWarn AND flashing) → stays DARKGREY (the "off" half of the flash).

  // Bottom chevron — green when audio low tone is playing (in [tonesOn, onSpeedSlow)).
  let bottom = colors.TFT_DARKGREY;
  if (percentLift >= tonesOn && percentLift < onSpeedSlow) bottom = colors.TFT_GREEN;

  return { top, bottom };
}
