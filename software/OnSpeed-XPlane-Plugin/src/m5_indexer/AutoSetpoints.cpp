// AutoSetpoints — pure-function helper for deriving plugin AOA
// setpoints from X-Plane's stall-warn AOA dataref.  See AutoSetpoints.h.

#include "AutoSetpoints.h"

namespace onspeed_xplane::indexer {

DerivedSetpoints DeriveSetpointsFromStallWarn(float stallWarnAoa,
                                              const NaoaFractions& naoa)
{
    DerivedSetpoints out{};

    if (stallWarnAoa < kMinPlausibleStallWarnAoaDeg) {
        // Dataref absent or implausibly small.  Caller leaves the
        // live globals untouched so the previous frame's values
        // stand (or compiled-in defaults if this is the first call).
        out.applied = false;
        return out;
    }

    // Implicit alpha_stall scales the StallWarn anchor up to where the
    // 0.92 fraction lands at acf_stall_warn_alpha.  Guard against a
    // hand-edited NAOA where stallWarn is zero or negative — return
    // applied=false rather than divide by zero.
    if (naoa.stallWarn <= 0.0f) {
        out.applied = false;
        return out;
    }
    const float alphaStall = stallWarnAoa / naoa.stallWarn;

    out.ldmax       = naoa.ldmax       * alphaStall;
    out.onSpeedFast = naoa.onSpeedFast * alphaStall;
    out.onSpeedSlow = naoa.onSpeedSlow * alphaStall;
    out.stallWarn   = stallWarnAoa;     // by construction
    out.applied     = true;
    return out;
}

}  // namespace onspeed_xplane::indexer
