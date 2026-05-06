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
    const float alphaStall = LerpStallAoa(alphaStallNoFlap,
                                          alphaStallFullFlap,
                                          flapRatio);
    if (alphaStall < kMinPlausibleStallAoaDeg) {
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
