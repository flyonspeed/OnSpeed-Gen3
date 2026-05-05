// Port of mapPct2Display() from software/OnSpeed-M5-Display/src/main.cpp.
// Maps a percent-lift value (0.0..99.9) to a y-coordinate within the
// indexer widget (1..192).  The y-coordinate is in the M5's native
// 320×240 panel space; the SVG viewBox matches, so this y is also
// the SVG y.
//
// `anchors` is the same 8-element array the C++ uses (PctAnchors), where:
//   anchors[0] = 0  (alpha_0 floor; always zero in percent space)
//   anchors[2] = tonesOnPctLift   (operational; chevron + audio gate)
//   anchors[3] = onSpeedFastPctLift  (donut bottom edge)
//   anchors[4] = onSpeedSlowPctLift  (donut top edge / top-chevron lower gate)
//   anchors[6] = pipPctLift          (visual L/Dmax pip)
//   anchors[7] = stallWarnPctLift    (top-chevron flash threshold; color only)
// Slots 1 and 5 are unused.
//
// Y values: 192 = bottom of widget, 1 = top.  The upper ramp tops out
// at percent_lift = 99 — the lift-envelope ceiling — independent of
// the active detent's stall-warn percent.  Stall-warn drives chevron
// flash-red color logic in chevronColors(); it does not gate Y here.
//
// Two variants:
//   * `mapPct2Display` — returns a fractional y (no rounding).  Use
//     this for SVG, which renders fractional pixel coordinates with
//     subpixel anti-aliasing — the LiveView gets visibly smoother bar
//     motion than the M5 hardware (which has to round to integer
//     pixels because M5GFX::fillRect only accepts ints).
//   * `mapPct2DisplayInt` — returns the rounded y, matching the C++
//     M5 firmware's `mapPct2Display(int aoaPct, ...)` exactly.  Used
//     by the geometry-invariant tests and any M5-parity check.
export function mapPct2Display(aoaPct, anchors) {
  if      (aoaPct <= anchors[0])                              return 192;
  else if (aoaPct >  anchors[0] && aoaPct <= anchors[3])      return mapFloat(aoaPct, anchors[0], anchors[3], 192, 115);
  else if (aoaPct >  anchors[3] && aoaPct <= anchors[4])      return mapFloat(aoaPct, anchors[3], anchors[4], 115,  78);
  else if (aoaPct >  anchors[4] && aoaPct <= 99)              return mapFloat(aoaPct, anchors[4], 99,          78,  1);
  else                                                         return 1;
}

export function mapPct2DisplayInt(aoaPct, anchors) {
  const y = mapPct2Display(aoaPct, anchors);
  return Math.round(y);
}

// Linear interpolation. Returns a float; the caller decides whether
// to round (SVG benefits from fractional, M5GFX doesn't).
function mapFloat(x, inLow, inHigh, outLow, outHigh) {
  if (inHigh === inLow) return outLow;
  return (x - inLow) * (outHigh - outLow) / (inHigh - inLow) + outLow;
}
