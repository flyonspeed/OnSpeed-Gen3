// AutoSetpoints — pure-function helper for deriving plugin AOA
// setpoints from X-Plane's stall-AOA datarefs.  See AutoSetpoints.h.

#include "AutoSetpoints.h"

#include <algorithm>

namespace onspeed_xplane::indexer {

float LerpStallAoa(float alphaStallNoFlap,
                   float alphaStallFullFlap,
                   float flapRatio)
{
    const float r = std::clamp(flapRatio, 0.0f, 1.0f);
    return alphaStallNoFlap + r * (alphaStallFullFlap - alphaStallNoFlap);
}

DerivedSetpoints DeriveSetpointsFromStall(float alphaStallNoFlap,
                                          float alphaStallFullFlap,
                                          float flapRatio,
                                          const NaoaFractions& naoa)
{
    DerivedSetpoints out{};

    // Plausibility check on each endpoint independently.  Asymmetric
    // dataref population is common: an aircraft author may set
    // acf_max_aoa_no_flap and leave acf_max_aoa_full_flap at the
    // Plane-Maker default of 0, or vice-versa.  Without per-endpoint
    // handling, the lerp would interpolate toward 0 with flaps and
    // collapse the band on approach — the very phase of flight where
    // the cues matter most.  When one endpoint is implausible, treat
    // the plausible one as a constant across the flap range — same
    // setpoints at every flap position is conservative (no surprise
    // at deployment) and matches "the model only published one
    // number for this aircraft."
    const bool noFlapPlausible   = alphaStallNoFlap   >= kMinPlausibleStallAoaDeg;
    const bool fullFlapPlausible = alphaStallFullFlap >= kMinPlausibleStallAoaDeg;

    float alphaStall;
    if (noFlapPlausible && fullFlapPlausible) {
        alphaStall = LerpStallAoa(alphaStallNoFlap,
                                  alphaStallFullFlap,
                                  flapRatio);
    } else if (noFlapPlausible) {
        alphaStall = alphaStallNoFlap;
    } else if (fullFlapPlausible) {
        alphaStall = alphaStallFullFlap;
    } else {
        out.applied = false;
        return out;
    }

    out.ldmax       = naoa.ldmax       * alphaStall;
    out.onSpeedFast = naoa.onSpeedFast * alphaStall;
    out.onSpeedSlow = naoa.onSpeedSlow * alphaStall;
    out.stallWarn   = naoa.stallWarn   * alphaStall;
    out.applied     = true;
    return out;
}

}  // namespace onspeed_xplane::indexer
