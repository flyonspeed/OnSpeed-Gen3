// WindTriangle.cpp

#include <aero/WindTriangle.h>
#include <util/OnSpeedTypes.h>

#include <cmath>

namespace onspeed::aero {

std::optional<WindNed> ComputeWind(
    float gnssVelNedNorthMps,
    float gnssVelNedEastMps,
    float gnssVelNedDownMps,
    float yawDeg,
    float pitchDeg,
    float tasMps)
{
    if (!std::isfinite(gnssVelNedNorthMps) ||
        !std::isfinite(gnssVelNedEastMps)  ||
        !std::isfinite(gnssVelNedDownMps)  ||
        !std::isfinite(yawDeg)             ||
        !std::isfinite(pitchDeg)           ||
        !std::isfinite(tasMps))
        return std::nullopt;

    if (tasMps < kWindMinTasMps)
        return std::nullopt;

    const float yawRad   = onspeed::deg2rad(yawDeg);
    const float pitchRad = onspeed::deg2rad(pitchDeg);
    const float cosTheta = std::cos(pitchRad);
    const float sinTheta = std::sin(pitchRad);
    const float cosPsi   = std::cos(yawRad);
    const float sinPsi   = std::sin(yawRad);

    // Body-frame airspeed [TAS, 0, 0] rotated to NED via 3-2-1 (yaw-pitch-roll).
    // Roll drops out because the body x-axis is roll-invariant.
    const float airN =  tasMps * cosTheta * cosPsi;
    const float airE =  tasMps * cosTheta * sinPsi;
    const float airD = -tasMps * sinTheta;

    // Wind = ground - air, NED.
    const float wN = gnssVelNedNorthMps - airN;
    const float wE = gnssVelNedEastMps  - airE;
    const float wD = gnssVelNedDownMps  - airD;

    const float speed = std::sqrt(wN * wN + wE * wE);

    // "From" direction: negate the going-to vector and convert to compass.
    float dirDeg = onspeed::rad2deg(std::atan2(-wE, -wN));
    if (dirDeg < 0.0f) dirDeg += 360.0f;
    if (dirDeg >= 360.0f) dirDeg -= 360.0f;   // exact-360 wraps to 0

    WindNed out;
    out.windSpeedMps    = speed;
    out.windDirDeg      = dirDeg;
    out.windVerticalMps = -wD;
    return out;
}

}   // namespace onspeed::aero
