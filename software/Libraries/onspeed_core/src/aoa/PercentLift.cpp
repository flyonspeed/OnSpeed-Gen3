// PercentLift.cpp — implementation
//
// Honest single-linear normalization between alpha_0 and alpha_stall.
// See PercentLift.h for the full contract.

#include <aoa/PercentLift.h>

#include <cmath>
#include <limits>

namespace onspeed {
namespace aoa {

namespace {

// Returns the raw (unclamped) lift fraction `(aoaDeg - alpha_0) / span`,
// or NaN when the inputs make the fraction undefined (IAS invalid,
// non-finite AOA, or degenerate calibration with span <= 0).
//
// Defensive ceiling: when alpha_stall isn't calibrated (or has been
// configured below stall-warn), fall back to a synthetic stall body
// angle of stallWarn * 100/90.  Mirrors the historical fallback so
// an uncalibrated unit reads roughly the same at the upper end as
// it always has.
//
// alpha_0 is per-flap (typically negative on aircraft with positive
// wing incidence) and is the floor of the percent-lift fraction —
// see CLAUDE.md's body-angle convention block.
static float ComputeLiftFraction(
    float aoaDeg,
    const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
    bool iasValid)
{
    if (!iasValid) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    if (!std::isfinite(aoaDeg)) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    float alphaStall = flapCfg.fAlphaStall;
    if (alphaStall <= flapCfg.fSTALLWARNAOA) {
        alphaStall = flapCfg.fSTALLWARNAOA * 100.0f / 90.0f;
    }

    const float span = alphaStall - flapCfg.fAlpha0;
    if (span <= 0.0f) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    return (aoaDeg - flapCfg.fAlpha0) / span;
}

}  // namespace

float ComputePercentLift(float aoaDeg,
                         const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
                         bool iasValid)
{
    const float fraction = ComputeLiftFraction(aoaDeg, flapCfg, iasValid);
    if (!std::isfinite(fraction)) {
        return 0.0f;
    }

    float pct = fraction * 100.0f;

    // Clamp to [0.0, 99.9] — saturation convention, never reads 100.
    // The 99.9 ceiling (rather than 99.0) is load-bearing for the wire
    // encoding: BuildDisplayFrame multiplies by 10 and truncates for
    // the `%03u` tenths field, which must stay strictly < 1000.
    if (pct < 0.0f)  pct = 0.0f;
    if (pct > 99.9f) pct = 99.9f;
    return pct;
}

}  // namespace aoa
}  // namespace onspeed
