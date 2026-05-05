// PercentLift.cpp - implementation
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
// `ComputePercentLift` and `ComputePercentLiftTenths` share this helper
// so the alpha_0 floor, alpha_stall fallback, and span-zero guard
// stay in lockstep — a future edit to any of those rules touches one
// place.  Each public function applies its own scale + clamp on the
// returned fraction.
//
// Defensive ceiling: when alpha_stall isn't calibrated (or has been
// configured below stall-warn), fall back to a synthetic stall body
// angle of stallWarn * 100/90.  Mirrors the historical fallback so
// an uncalibrated unit reads roughly the same at the upper end as
// it always has.
//
// alpha_0 is per-flap (typically negative on aircraft with positive
// wing incidence) and is the floor of the percent-lift fraction —
// see CLAUDE.md's body-angle convention block.  Both the ×100 and
// ×1000 callers must use this same alpha_0 floor or they diverge
// silently on uncalibrated units.
static float ComputeLiftFraction(
    float aoaDeg,
    const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
    bool iasValid)
{
    if (!iasValid) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    // Defend against NaN/Inf AOA explicitly.  The downstream cast
    // `static_cast<int>(NaN)` is UB; without this guard we'd rely on
    // the platform's float-to-int behaviour landing somewhere the
    // saturation clamp catches.
    if (!std::isfinite(aoaDeg)) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    float alphaStall = flapCfg.fAlphaStall;
    if (alphaStall <= flapCfg.fSTALLWARNAOA) {
        alphaStall = flapCfg.fSTALLWARNAOA * 100.0f / 90.0f;
    }

    const float span = alphaStall - flapCfg.fAlpha0;
    if (span <= 0.0f) {
        // Degenerate calibration (alpha_0 >= alpha_stall): return NaN
        // rather than divide by zero.
        return std::numeric_limits<float>::quiet_NaN();
    }

    return (aoaDeg - flapCfg.fAlpha0) / span;
}

}  // namespace

int ComputePercentLift(float aoaDeg,
                       const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
                       bool iasValid)
{
    const float fraction = ComputeLiftFraction(aoaDeg, flapCfg, iasValid);
    if (!std::isfinite(fraction)) {
        return 0;
    }

    int pct = static_cast<int>(fraction * 100.0f);

    // Clamp to [0, 99] — saturation convention, never reads 100.
    if (pct < 0)  pct = 0;
    if (pct > 99) pct = 99;
    return pct;
}

int ComputePercentLiftTenths(float aoaDeg,
                             const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
                             bool iasValid)
{
    const float fraction = ComputeLiftFraction(aoaDeg, flapCfg, iasValid);
    if (!std::isfinite(fraction)) {
        return 0;
    }

    int tenths = static_cast<int>(fraction * 1000.0f);

    // Clamp to [0, 999] — saturation convention, never reads 1000
    // (would render as 100.0% on the consumer side).
    if (tenths < 0)   tenths = 0;
    if (tenths > 999) tenths = 999;
    return tenths;
}

}  // namespace aoa
}  // namespace onspeed
