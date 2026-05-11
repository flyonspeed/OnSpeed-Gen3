// Port of onspeed::gauges::FlapWidgetFrac from
// software/Libraries/onspeed_core/src/gauges/FlapWidgetMath.h.
//
// Maps a current flap deg (`flapPos`) and the configured min/max range
// to a fraction in [0, 1] suitable for visual widget arc sweep. Handles:
//   - Reflex flaps (negative `flapsMin`): clamp at 0 below min, at 1 above max.
//   - Degenerate `flapsMax <= flapsMin` (single-position aircraft or
//     misconfigured): return 0.5 so the widget parks mid-arc rather
//     than NaN-ing or pinning to an endpoint.
export function flapWidgetFrac(flapPos, flapsMin, flapsMax) {
  const span = flapsMax - flapsMin;
  if (span <= 0) return 0.5;
  const fracRaw = (flapPos - flapsMin) / span;
  return Math.max(0, Math.min(1, fracRaw));
}

// Convert frac → SVG transform value for the rotating triangle.
// drawAOA() :797-:805 uses kFlapArcDeg = 40°, so frac × 40° is the angle.
export function flapTriangleTransform(frac) {
  // SVG rotates clockwise about the circle center (FLAP_CX, FLAP_CY).
  // Caller composes the full transform; this just returns degrees.
  return frac * 40;
}
