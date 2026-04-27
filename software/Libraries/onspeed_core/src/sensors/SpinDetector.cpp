// SpinDetector.cpp — implementation. See header for design and provenance.

#include <sensors/SpinDetector.h>

#include <cmath>

namespace onspeed {

namespace {

inline bool IsFiniteFloat(float v) {
    return !std::isnan(v) && !std::isinf(v);
}

inline float Abs(float v) {
    return v < 0.0f ? -v : v;
}

}   // namespace

SpinDetector::SpinDetector()
    : yawThresholdDps_(kDefaultYawThresholdDps)
    , yawHysteresisDps_(kDefaultYawHysteresisDps)
    , filterTauSec_(kDefaultFilterTauSec)
    , yawFiltered_(0.0f)
    , active_(false)
    , latchedDir_(0)
    , hasFilterSeed_(false)
    , armed_(true)
{
}

void SpinDetector::Configure(float yawThresholdDps,
                             float yawHysteresisDps,
                             float filterTauSec)
{
    if (yawThresholdDps  > 0.0f) yawThresholdDps_  = yawThresholdDps;
    if (yawHysteresisDps > 0.0f) yawHysteresisDps_ = yawHysteresisDps;
    if (filterTauSec     > 0.0f) filterTauSec_     = filterTauSec;
}

int SpinDetector::Update(float dtSec, float yawDps, float aoaDeg, float stallAoa)
{
    // Reject malformed inputs.  State holds; prior cue value returned.
    if (!(dtSec > 0.0f) ||
        !IsFiniteFloat(yawDps) ||
        !IsFiniteFloat(aoaDeg) ||
        !IsFiniteFloat(stallAoa)) {
        return active_ ? latchedDir_ : 0;
    }

    // First valid call: seed the filter to the current sample, no latch.
    // The filtered gate cannot pass on the first tick by design; this
    // matches the F/A-18 OFP v10.7 "don't fire on transient" rule and
    // mirrors the seeding pattern used by GOnsetFilter.
    //
    // Caveat: a power-cycle mid-spin starts from this unseeded state,
    // so the cue takes a few hundred ms to re-arm.  That is correct —
    // an in-progress spin presumably already had pilot attention before
    // the reboot.
    if (!hasFilterSeed_) {
        yawFiltered_   = yawDps;
        hasFilterSeed_ = true;
        return 0;
    }

    // Single-pole IIR on yaw rate.  alpha = dt / (tau + dt) so the
    // effective time constant tracks the actual cadence — caller can
    // tick at 20 Hz (wire rate) or 50 Hz (AHRS rate) without retuning.
    const float alpha = dtSec / (filterTauSec_ + dtSec);
    yawFiltered_ += alpha * (yawDps - yawFiltered_);

    const bool wing_stalled  = (aoaDeg > stallAoa);
    const bool yaw_now_fast  = (Abs(yawDps)       > yawThresholdDps_);
    const bool yaw_filt_fast = (Abs(yawFiltered_) > yawThresholdDps_);

    // Latch entry: all three signal gates plus the armed flag.
    if (!active_ && armed_ && wing_stalled && yaw_now_fast && yaw_filt_fast) {
        active_     = true;
        // Direction = anti-spin rudder = opposite the filtered yaw.
        // -1 means "press left rudder" (counters +nose-right yaw),
        // +1 means "press right rudder" (counters -nose-left yaw).
        latchedDir_ = (yawFiltered_ > 0.0f) ? -1 : +1;
        armed_      = false;       // disarm until next clean exit
    }

    // Latch exit logic:
    //
    //   - Wing un-stall is the *only* clean exit.  The airplane is no
    //     longer in autorotation by definition; clear active and
    //     re-arm so a future stall can latch a fresh direction.
    //
    //   - Yaw-hysteresis exit while wing is still stalled drops the
    //     cue (returns 0) but does NOT re-arm.  The airplane is still
    //     stalled and may still be in autorotation; the filter just
    //     transited a low value (e.g. transient over-rudder, or
    //     yaw-rate jitter).  Without the armed gate, the next gate-
    //     pass would re-latch with potentially opposite direction
    //     mid-event — exactly the F/A-18 OFP v10.7 "chasing arrows"
    //     failure mode.  Stay silent until the wing un-stalls.
    const bool yaw_cleared = (Abs(yawFiltered_) <
                              (yawThresholdDps_ - yawHysteresisDps_));

    if (active_ && yaw_cleared) {
        active_     = false;
        latchedDir_ = 0;
        // armed_ stays false: do not re-arm without a clean exit.
    }

    if (!wing_stalled) {
        active_     = false;
        latchedDir_ = 0;
        armed_      = true;
    }

    return active_ ? latchedDir_ : 0;
}

void SpinDetector::Reset()
{
    yawFiltered_   = 0.0f;
    active_        = false;
    latchedDir_    = 0;
    hasFilterSeed_ = false;
    armed_         = true;
}

}   // namespace onspeed
