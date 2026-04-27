// FlapWidgetMath.h — pure helper for the M5 flap-position widget.
//
// Maps a flap lever position (in degrees) plus the configured travel
// range into a [0, 1] fraction-of-travel suitable for driving an
// indicator's rotation arc.
//
// Anchored to the configured min/max so reflex flaps (negative min)
// and aircraft with non-RV travel ranges render endpoint-correctly:
// frac=0 at flapsMinDeg, frac=1 at flapsMaxDeg, regardless of the
// absolute degree values.
//
// Edge cases:
//   * Single-position aircraft (min == max): returns 0.5 so the
//     widget parks mid-arc rather than dividing by zero.
//   * Misconfigured (max < min): same fallback.
//   * Lever below min or above max: clamps to 0 or 1 (the consumer
//     can still display the literal degree value separately if it
//     wants to surface "out of configured range").
//
// Pure / header-only / no platform dependencies — exercised by
// test/test_flap_widget_math/ in the native suite.

#ifndef ONSPEED_CORE_GAUGES_FLAP_WIDGET_MATH_H
#define ONSPEED_CORE_GAUGES_FLAP_WIDGET_MATH_H

namespace onspeed::gauges {

inline float FlapWidgetFrac(int flapPosDeg, int flapsMinDeg, int flapsMaxDeg)
{
    const int span = flapsMaxDeg - flapsMinDeg;
    if (span <= 0) return 0.5f;
    float fracRaw = static_cast<float>(flapPosDeg - flapsMinDeg)
                  / static_cast<float>(span);
    if (fracRaw < 0.0f) fracRaw = 0.0f;
    if (fracRaw > 1.0f) fracRaw = 1.0f;
    return fracRaw;
}

}  // namespace onspeed::gauges

#endif  // ONSPEED_CORE_GAUGES_FLAP_WIDGET_MATH_H
