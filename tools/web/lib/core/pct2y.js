// Port of mapPct2Display() from software/OnSpeed-M5-Display/src/main.cpp:1529.
// Maps a percent-lift value (0..99) to a y-coordinate within the indexer
// widget (1..192). The y-coordinate is in the M5's native 320×240 panel
// space; the SVG viewBox matches, so this y is also the SVG y.
//
// `anchors` is the same 8-element array the C++ uses (PctAnchors), where:
//   anchors[0] = 0  (alpha_0 floor; always zero in percent space)
//   anchors[2] = tonesOnPctLift   (operational; chevron + audio gate)
//   anchors[3] = onSpeedFastPctLift  (donut bottom edge)
//   anchors[4] = onSpeedSlowPctLift  (donut top edge / top-chevron lower gate)
//   anchors[6] = pipPctLift          (visual L/Dmax pip)
//   anchors[7] = stallWarnPctLift    (top-chevron flash threshold)
// Slots 1 and 5 are unused.
//
// Y values: 192 = bottom of widget, 1 = top.
export function mapPct2Display(aoaPct, anchors) {
  if      (aoaPct <= anchors[0])                              return 192;
  else if (aoaPct >  anchors[0] && aoaPct <= anchors[3])      return map2int(aoaPct, anchors[0], anchors[3], 192, 115);
  else if (aoaPct >  anchors[3] && aoaPct <= anchors[4])      return map2int(aoaPct, anchors[3], anchors[4], 115,  78);
  else if (aoaPct >  anchors[4] && aoaPct <= anchors[7])      return map2int(aoaPct, anchors[4], anchors[7],  78,   1);
  else                                                         return 1;
}

// Linear interpolation matching the C++ map2int. Returns an integer.
function map2int(x, inLow, inHigh, outLow, outHigh) {
  if (inHigh === inLow) return outLow;
  return Math.round((x - inLow) * (outHigh - outLow) / (inHigh - inLow) + outLow);
}
