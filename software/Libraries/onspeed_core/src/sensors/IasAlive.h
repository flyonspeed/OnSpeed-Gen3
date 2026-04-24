// IasAlive.h — air-data validity helpers (pressure deadband + hysteretic
// IAS-alive state machine).
//
// Pulled into onspeed_core so the sketch driver (SensorIO.cpp) and the
// regression harness (tools/regression/host_main.cpp) share the same
// implementation.  Without a shared function, a future change to either
// side would silently drift the two.
//
// Thresholds
// ----------
// `kIasAliveRisingKt` (20 kt) and `kIasAliveFallingKt` (15 kt) match the
// Dynon SkyView air-data validity cutoff documented in the SkyView SE
// Pilot's User Guide (Rev D).  See issue #109 for the full citation
// list — Garmin G3X, ARINC 429 SSM/NCD, and Inertial Labs ADC all use
// comparable thresholds.  20 kt is well above the pitot sensor noise
// floor on the Honeywell HSCDRNN1.6BASA3 and well below any operational
// airspeed; the 5 kt hysteresis prevents chatter near the threshold.
//
// `kPfwdDeadbandCounts` (3 counts) clamps smoothed pitot pressure to
// zero when within the sensor's residual noise floor.  The HSC sensor
// has a ~1-2 LSB random walk after the median + averaging stage; 3
// counts adds ~1 count of margin.  Corresponds to ~4.4 kt IAS at the
// floor (below the 20 kt iasAlive gate, so it never affects in-flight
// behavior).

#ifndef ONSPEED_CORE_SENSORS_IAS_ALIVE_H
#define ONSPEED_CORE_SENSORS_IAS_ALIVE_H

#include <cmath>

namespace onspeed::sensors {

// Rising-edge IAS threshold for air-data validity (knots).
inline constexpr float kIasAliveRisingKt = 20.0f;

// Falling-edge IAS threshold for air-data validity (knots).
// Must be strictly less than kIasAliveRisingKt to provide hysteresis.
inline constexpr float kIasAliveFallingKt = 15.0f;

// Pitot pressure deadband (counts, after median + averaging).
// |PfwdSmoothed| below this value is clamped to zero; see header.
inline constexpr float kPfwdDeadbandCounts = 3.0f;

// Apply the hysteretic IAS-alive state machine.
// Returns the new `iasAlive` state given the previous state and the
// current IAS in knots.
//
//   previous = false, iasKt >= kIasAliveRisingKt  -> true
//   previous = true,  iasKt <  kIasAliveFallingKt -> false
//   otherwise                                     -> previous (unchanged)
inline bool UpdateIasAlive(bool previous, float iasKt)
{
    if (!previous && iasKt >= kIasAliveRisingKt)  return true;
    if ( previous && iasKt <  kIasAliveFallingKt) return false;
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
