// WindTriangle.h
//
// Wind-triangle solver: given ownship ground velocity (NED), attitude, and
// true airspeed, solve for the wind vector in NED.
//
//   V_ground = V_air + V_wind   (vector addition in NED)
//   V_wind   = V_ground - V_air
//
// V_air is reconstructed by rotating the body-frame airspeed vector
// [TAS, 0, 0] into NED via yaw and pitch. The "[TAS, 0, 0]" approximation
// ignores AOA and sideslip; the resulting horizontal-wind error is
// (1 - cos(alpha))*TAS, ~1.5% at alpha=10 deg. The vertical component
// absorbs sin(alpha)*TAS as a false updraft/downdraft.
//
// Yaw frame: the caller is responsible for matching yaw to the NED frame
// of the ground-velocity vector. VN-300 yaw is magnetic-north by default
// unless the unit is configured with WMM declination; GnssVelNed is always
// true-north. A mismatch produces a wind-direction error equal to the
// local magnetic declination. OnSpeed does not correct for this.

#ifndef ONSPEED_CORE_AERO_WIND_TRIANGLE_H
#define ONSPEED_CORE_AERO_WIND_TRIANGLE_H

#include <optional>

namespace onspeed::aero {

struct WindNed {
    float windSpeedMps;     // horizontal magnitude (m/s)
    float windDirDeg;       // "from" direction, [0, 360) measured CW from N
    float windVerticalMps;  // positive = updraft (negated NED-down)
};

// Minimum TAS below which the wind triangle is not computed. Below this,
// GPS velocity noise dominates the residual and the answer is meaningless.
// 15 m/s ~ 29 KIAS, well below typical pattern speeds.
inline constexpr float kWindMinTasMps = 15.0f;

// Compute wind in NED from ground velocity, attitude, and TAS.
// Returns std::nullopt when:
//   - any input is non-finite, or
//   - tasMps < kWindMinTasMps (ownship not yet flying).
std::optional<WindNed> ComputeWind(
    float gnssVelNedNorthMps,
    float gnssVelNedEastMps,
    float gnssVelNedDownMps,
    float yawDeg,
    float pitchDeg,
    float tasMps);

}   // namespace onspeed::aero

#endif
