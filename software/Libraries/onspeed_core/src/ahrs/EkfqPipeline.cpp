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
    //    dominates tas / tasDot; feeding those into EKFQ at rest
    //    contaminates the centripetal terms inside h(x).  Hysteresis
    //    prevents chatter at the gate edge.
    const bool gateWasOpen = iasGate_;
    if (!iasGate_ && in.iasKt >= kIasGateRisingKt) {
        iasGate_ = true;
    } else if (iasGate_ && in.iasKt < kIasGateFallingKt) {
        iasGate_ = false;
    }
    out.iasGateRisingEdge = (!gateWasOpen && iasGate_);

    // 2) compFadeIn ramp.  Open gate → ramp to 1 over τ; closed gate →
    //    snap to 0.  EKFQ consumes (tas * compFadeIn, tasDot *
    //    compFadeIn) as its measurement inputs so the centripetal /
    //    TASdot terms inside h(x) fade in smoothly after the iasGate
    //    rising edge.
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

    // 4) On the gate's rising edge, reset EKFQ's vertical-channel
    //    covariance so z/vz/b_az re-open after a long gate-closed
    //    taxi.  Without this, the filter trusts its possibly-stale
    //    z/vz estimates and would lag the real altitude/VSI for tens
    //    of seconds.
    if (out.iasGateRisingEdge) {
        ekfq_.resetVerticalCovariance(in.baroAltMeters);
    }

    // 5) Run the EKFQ predict + correct.  Sign-convention plumbing:
    //
    //   ax, ay: raw post-EMA g → m/s², no sign flip.
    //   az    : OnSpeed's +1g-level convention → NED -g-level.
    //           Negate on input.
    //   p, q  : OnSpeed's internal IMU sign → aerospace standard
    //           (+p right-wing-down, +q nose-up).  Negate on input.
    //   r     : no flip.
    //   tas / tasDot: faded by compFadeIn so they zero at rest.
    EKFQ::Measurements meas = {
        /* ax */          emaFwdG  * kEkfGravityMps2,
        /* ay */          emaLatG  * kEkfGravityMps2,
        /* az */         -emaVertG * kEkfGravityMps2,
        /* p  */         -onspeed::deg2rad(in.rollRateCorrDps),
        /* q  */         -onspeed::deg2rad(in.pitchRateCorrDps),
        /* r  */          onspeed::deg2rad(in.yawRateCorrDps),
        /* baroAlt */     in.baroAltMeters,
        /* tas */         in.tasMps      * compFadeIn_,
        /* tasDot */      in.tasDotMps2  * compFadeIn_,
        /* updateBaro */  true,
    };
    ekfq_.update(meas, in.dtSec);

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
