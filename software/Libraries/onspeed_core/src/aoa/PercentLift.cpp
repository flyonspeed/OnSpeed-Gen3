// PercentLift.cpp - implementation
//
// Honest single-linear normalization between alpha_0 and alpha_stall.
// See PercentLift.h for the full contract.

#include <aoa/PercentLift.h>

namespace onspeed {
namespace aoa {

int ComputePercentLift(float aoaDeg,
                       const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
                       bool iasValid)
{
    if (!iasValid) {
        return 0;
    }

    // Defensive ceiling: when alpha_stall isn't calibrated (or has been
    // configured below stall-warn), fall back to a synthetic stall body
    // angle of stallWarn * 100/90.  Mirrors the historical fallback so
    // an uncalibrated unit reads roughly the same at the upper end as
    // it always has.
    float alphaStall = flapCfg.fAlphaStall;
    if (alphaStall <= flapCfg.fSTALLWARNAOA) {
        alphaStall = flapCfg.fSTALLWARNAOA * 100.0f / 90.0f;
    }

    const float span = alphaStall - flapCfg.fAlpha0;
    if (span <= 0.0f) {
        // Degenerate calibration (alpha_0 >= alpha_stall).  No meaningful
        // fraction; return 0 rather than divide-by-zero.
        return 0;
    }

    const float fraction = (aoaDeg - flapCfg.fAlpha0) / span;
    int pct = static_cast<int>(fraction * 100.0f);

    // Clamp to [0, 99] — saturation convention, never reads 100.
    if (pct < 0)  pct = 0;
    if (pct > 99) pct = 99;
    return pct;
}

}  // namespace aoa
}  // namespace onspeed
