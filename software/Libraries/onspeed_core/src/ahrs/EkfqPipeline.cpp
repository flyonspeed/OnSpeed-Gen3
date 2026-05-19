// EkfqPipeline.cpp — see EkfqPipeline.h.

#include <ahrs/EkfqPipeline.h>

#include <cmath>

#include <util/OnSpeedTypes.h>

namespace onspeed::ahrs {

namespace {
// Same value as onspeed::g2mps(1.0f); aliased for readable inline use
// inside the per-frame sign-convention block.
constexpr float kEkfGravityMps2 = 9.80665f;
}   // namespace

EkfqPipeline::EkfqPipeline()
    : accelFwdFilter_(kAccelEmaAlpha)
    , accelLatFilter_(kAccelEmaAlpha)
    , accelVertFilter_(kAccelEmaAlpha)
{
    // Seed the accel pre-filter with the production "level on the
    // ground" rest state (Z = +1g, X = Y = 0) so frames before the
    // IMU produces a real sample yield a sane attitude rather than
    // (0, 0, 0).
    accelFwdFilter_.seed(0.0f);
    accelLatFilter_.seed(0.0f);
    accelVertFilter_.seed(+1.0f);
}

void EkfqPipeline::Init(float seedPitchDeg, float seedRollDeg, float seedAltMeters)
{
    // EKFQ::init expects radians for phi/theta and meters (+up) for z.
    ekfq_.init(onspeed::deg2rad(seedRollDeg),
               onspeed::deg2rad(seedPitchDeg),
               seedAltMeters);
    // compFadeIn_, iasGate_, accel-EMA state intentionally not reset —
    // Init() is called on every web-UI config save and zeroing those
    // mid-flight would inject transients.  The constructor seeds them
    // once at boot.
}

EkfqPipeline::Outputs EkfqPipeline::Step(const Inputs& in)
{
    Outputs out;

    // 1) Hysteretic IAS gate.  Below kIasGateRisingKt, pitot noise
    //    dominates tas / tasDot; feeding those through h(x)'s
    //    centripetal terms at rest pulls EKFQ pitch a few degrees off
    //    truth.  Hysteresis prevents chatter at the gate edge.
    const bool gateWasOpen = iasGate_;
    if (!iasGate_ && in.iasKt >= kIasGateRisingKt) {
        iasGate_ = true;
    } else if (iasGate_ && in.iasKt < kIasGateFallingKt) {
        iasGate_ = false;
    }
    out.iasGateRisingEdge = (!gateWasOpen && iasGate_);

    // 2) compFadeIn ramp.  Open gate → ramp to 1 over τ; closed gate →
    //    snap to 0.  EKFQ::correct() multiplies tas / tasdot by this
    //    fade so the centripetal / TASdot terms in h(x) ramp in
    //    smoothly after iasGate opens (rather than stepping the
    //    pressure-noise jolt into the measurement model).
    if (iasGate_) {
        const float fadeAlpha = in.dtSec / (kCompFadeTauSec + in.dtSec);
        compFadeIn_ += fadeAlpha * (1.0f - compFadeIn_);
    } else {
        compFadeIn_ = 0.0f;
    }

    // 3) Tick the accel pre-filter EMAs on every frame regardless of
    //    gate state — at rest the smoothed accel should track the
    //    raw installation-corrected values so the filter doesn't see
    //    a stale snapshot when the gate opens.
    const float emaFwdG  = accelFwdFilter_.update(in.accelFwdCorrG);
    const float emaLatG  = accelLatFilter_.update(in.accelLatCorrG);
    const float emaVertG = accelVertFilter_.update(in.accelVertCorrG);

    out.accelFwdCompG  = emaFwdG;
    out.accelLatCompG  = emaLatG;
    out.accelVertCompG = emaVertG;

    // 4) Per-step TASdot smoothing at IMU rate.  Held-stale `tas`
    //    across the ~4 IMU frames between IAS-update events produces
    //    a sawtooth `raw = (tas - prev_tas) / dt`; the EMA at
    //    kTasdotEmaAlpha averages the spikes.  Matches
    //    `pipeline_quat.py` byte-for-byte (raw and smoothing both
    //    computed per-row at 208 Hz).
    const float tasdotRawMps2 = (in.tasMps - prevTasMps_) / in.dtSec;
    tasdotSmoothed_ += kTasdotEmaAlpha * (tasdotRawMps2 - tasdotSmoothed_);
    prevTasMps_ = in.tasMps;

    // 5) On the gate's rising edge, reset EKFQ's vertical-channel
    //    covariance so z/vz/b_az re-open after a long gate-closed
    //    taxi.  Without this, the filter trusts its possibly-stale
    //    z/vz estimates and would lag the real altitude/VSI for tens
    //    of seconds.
    if (out.iasGateRisingEdge) {
        ekfq_.resetVerticalCovariance(in.baroAltMeters);
    }

    // 6) Run EKFQ predict + correct as separate calls so predict()
    //    sees the un-faded TAS (needed for beta-dynamics gating via
    //    `tas > tas_min_mps`) while correct() sees the faded TAS
    //    (centripetal / TASdot terms in h(x) ramp in smoothly).
    //    Mirrors `pipeline_quat.py` which calls predict and correct
    //    with the same split.
    //
    //    Sign-convention plumbing:
    //
    //      ax, ay : raw post-EMA g → m/s², no sign flip.
    //      az     : OnSpeed's +1g-level convention → NED -g-level.
    //               Negate on input.
    //      p, q   : OnSpeed's internal IMU sign → aerospace standard
    //               (+p right-wing-down, +q nose-up).  Negate on input.
    //      r      : no flip.
    const float ax_raw =  emaFwdG  * kEkfGravityMps2;
    const float ay_raw =  emaLatG  * kEkfGravityMps2;
    const float az_raw = -emaVertG * kEkfGravityMps2;
    const float p_rps  = -onspeed::deg2rad(in.rollRateCorrDps);
    const float q_rps  = -onspeed::deg2rad(in.pitchRateCorrDps);
    const float r_rps  =  onspeed::deg2rad(in.yawRateCorrDps);

    ekfq_.predict(p_rps, q_rps, r_rps,
                  ax_raw, ay_raw, az_raw,
                  in.tasMps,                  // un-faded; beta-dynamics gate
                  in.dtSec);

    ekfq_.correct(ax_raw, ay_raw, az_raw,
                  in.tasMps      * compFadeIn_,
                  tasdotSmoothed_ * compFadeIn_,
                  q_rps, r_rps,
                  in.baroAltMeters,
                  /* updateBaro */ true);

    const EKFQ::State state = ekfq_.getState();
    out.pitchDeg      = state.pitch_deg();
    out.rollDeg       = state.roll_deg();
    out.derivedAoaDeg = onspeed::rad2deg(ekfq_.alphaKinematicRad(in.tasMps));

    // 6) Vertical channel published from EKFQ's z / vz states.  vz is
    //    NED-down internally; flip the sign here so consumers receive
    //    the firmware's +climb convention.
    out.kalmanAltMeters = state.z;
    out.kalmanVsiMps    = -state.vz;

    // 7) EarthVertG via the filter's quaternion — body→earth rotation
    //    of the unsmoothed installation-corrected vertical accel,
    //    minus the +1g level reaction-force convention.  Same formula
    //    Madgwick / Ekf6Pipeline use; gives 0 at level flight.
    out.earthVertG =
        2.0f * (state.q1 * state.q3 - state.q0 * state.q2)                         * in.accelFwdCorrG +
        2.0f * (state.q0 * state.q1 + state.q2 * state.q3)                         * in.accelLatCorrG +
        (state.q0 * state.q0 - state.q1 * state.q1 - state.q2 * state.q2 + state.q3 * state.q3) * in.accelVertCorrG - 1.0f;

    out.compFadeIn = compFadeIn_;
    out.iasGate    = iasGate_;
    return out;
}

}   // namespace onspeed::ahrs
