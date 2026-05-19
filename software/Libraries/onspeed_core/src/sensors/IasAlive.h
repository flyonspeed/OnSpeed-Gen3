// IasAlive.h — air-data validity helpers (pitot pressure deadband +
// hysteretic IAS-display state machine).
//
// Pulled into onspeed_core so the sketch driver (SensorIO.cpp) and the
// regression harness (tools/regression/host_main.cpp) share the same
// implementation.  Without a shared function, a future change to
// either side would silently drift the two.
//
// Display-gate semantics
// ----------------------
// `iasDisplayable` answers the producer-side question "is this IAS
// reading suitable for display to the pilot?" It is NOT consumed by
// any AHRS algorithm (each algorithm makes its own internal trust-
// pitot decision against raw IAS); it gates the wire / log / display
// readouts only.
//
// The rising-edge threshold (knots) is pilot-tunable via
// `OnSpeedConfig::iIasDisplayThresholdKt` (default 20).  Falling edge
// is `rising - kIasDisplayHysteresisKt` (5 kt) to prevent chatter
// near the threshold.  Sentinel `threshold == 0` means "never blank"
// (always-show, regardless of raw IAS) — matches the
// `iMuteAudioUnderIAS == 0` always-on sentinel convention.
//
// `kPfwdDeadbandCounts` (3 counts) clamps smoothed pitot pressure to
// zero when within the sensor's residual noise floor.  The HSC sensor
// has a ~1-2 LSB random walk after the median + averaging stage; 3
// counts adds ~1 count of margin.  Corresponds to ~4.4 kt IAS at the
// floor (below the default 20 kt display gate, so it never affects
// in-flight behavior).

#ifndef ONSPEED_CORE_SENSORS_IAS_ALIVE_H
#define ONSPEED_CORE_SENSORS_IAS_ALIVE_H

#include <cmath>

namespace onspeed::sensors {

// Hardcoded hysteresis band (knots) between the rising and falling
// edges of the IAS display gate.  Internal; not pilot-tunable.
inline constexpr float kIasDisplayHysteresisKt = 5.0f;

// Pitot pressure deadband (counts, after median + averaging).
// |PfwdSmoothed| below this value is clamped to zero; see header.
inline constexpr float kPfwdDeadbandCounts = 3.0f;

// Apply the hysteretic IAS-display state machine.  Returns the new
// `iasDisplayable` state given the previous state, the current IAS
// (knots), and a rising-edge threshold (knots) sourced from
// OnSpeedConfig::iIasDisplayThresholdKt.  Falling edge is
// `risingKt - kIasDisplayHysteresisKt`.
//
// Sentinel: `risingKt == 0.0f` means "never blank" — the function
// returns `true` unconditionally.  Matches the
// `iMuteAudioUnderIAS == 0` always-on sentinel.
//
//   risingKt == 0                                  -> true
//   previous = false, iasKt >= risingKt            -> true
//   previous = true,  iasKt <  risingKt - hyst     -> false
//   otherwise                                      -> previous
inline bool UpdateIasDisplayable(bool previous, float iasKt, float risingKt)
{
    if (risingKt <= 0.0f) return true;
    const float fallingKt = risingKt - kIasDisplayHysteresisKt;
    if (!previous && iasKt >= risingKt)  return true;
    if ( previous && iasKt <  fallingKt) return false;
    return previous;
}

// Apply the pitot pressure deadband.
// Returns 0 when |counts| is within the sensor noise floor,
// otherwise the input value unchanged.  Splitting this from the
// deadband application site keeps SensorIO::Read() readable and makes
// the magic constant directly unit-testable.
inline float ApplyPfwdDeadband(float countsSmoothed)
{
    return (std::fabs(countsSmoothed) < kPfwdDeadbandCounts)
           ? 0.0f
           : countsSmoothed;
}

}   // namespace onspeed::sensors

#endif   // ONSPEED_CORE_SENSORS_IAS_ALIVE_H
