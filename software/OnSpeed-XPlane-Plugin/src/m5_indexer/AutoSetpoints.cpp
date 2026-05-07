// AutoSetpoints — pure-function helper for deriving plugin AOA
// setpoints from X-Plane's stall datarefs.  See AutoSetpoints.h.

#include "AutoSetpoints.h"

#include <algorithm>

namespace onspeed_xplane::indexer {

DerivedSetpoints DeriveSetpointsFromDatarefs(float stallWarnAoa,
                                             float vsKt,
                                             float vsoKt,
                                             float flapRatio,
                                             const NaoaFractions& naoa)
{
    DerivedSetpoints out{};

    if (stallWarnAoa < kMinPlausibleStallWarnAoaDeg) {
        out.applied = false;
        return out;
    }

    if (naoa.stallWarn <= 0.0f) {
        out.applied = false;
        return out;
    }

    const float alphaStallClean = stallWarnAoa / naoa.stallWarn;

    float alphaStall = alphaStallClean;
    if (vsKt >= kMinPlausibleVsKt && vsoKt >= kMinPlausibleVsKt) {
        const float vsRatio       = vsoKt / vsKt;
        const float alphaStallFull = alphaStallClean * vsRatio * vsRatio;
        const float r              = std::clamp(flapRatio, 0.0f, 1.0f);
        alphaStall = alphaStallClean + r * (alphaStallFull - alphaStallClean);
    }

    out.ldmax       = naoa.ldmax       * alphaStall;
    out.onSpeedFast = naoa.onSpeedFast * alphaStall;
    out.onSpeedSlow = naoa.onSpeedSlow * alphaStall;
    out.stallWarn   = naoa.stallWarn   * alphaStall;
    out.applied     = true;
    return out;
}

}  // namespace onspeed_xplane::indexer
