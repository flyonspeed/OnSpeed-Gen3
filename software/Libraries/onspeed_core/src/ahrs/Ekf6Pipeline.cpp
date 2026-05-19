// Ekf6Pipeline.cpp — see Ekf6Pipeline.h.

#include <ahrs/Ekf6Pipeline.h>

#include <cmath>

#include <util/OnSpeedTypes.h>

namespace onspeed::ahrs {

namespace {
// Same value as onspeed::g2mps(1.0f); aliased for readable inline use.
constexpr float kEkfGravityMps2 = 9.80665f;
}   // namespace

Ekf6Pipeline::Ekf6Pipeline()
    : accelFwdFilter_(kAccelEmaAlpha)
    , accelLatFilter_(kAccelEmaAlpha)
    , accelVertFilter_(kAccelEmaAlpha)
{
    accelFwdFilter_.seed(0.0f);
    accelLatFilter_.seed(0.0f);
    accelVertFilter_.seed(+1.0f);
}

void Ekf6Pipeline::Init(float seedPitchDeg, float seedRollDeg)
{
    // EKF6 expects radians; theta convention (positive = nose-up)
    // matches OnSpeed's pitch convention with no negation.
    ekf6_.init(onspeed::deg2rad(seedRollDeg),
               onspeed::deg2rad(seedPitchDeg));
    // compFadeIn_, iasGate_, accel-EMA state intentionally not reset
    // (same rationale as Madgwick::Init).
}

Ekf6Pipeline::Outputs Ekf6Pipeline::Step(const Inputs& in)
{
    Outputs out;

    // 1) Internal hysteretic IAS gate. Same shape as Madgwick.
    const bool gateWasOpen = iasGate_;
    if (!iasGate_ && in.iasKt >= kIasGateRisingKt) {
        iasGate_ = true;
    } else if (iasGate_ && in.iasKt < kIasGateFallingKt) {
        iasGate_ = false;
    }
    out.iasGateRisingEdge = (!gateWasOpen && iasGate_);

    if (iasGate_) {
        // EKF6 needs bias-corrected gyro rates for the centripetal comp
        // factors: the filter's previous-frame bp/bq/br are subtracted
        // from the corrected gyro before computing the comp factor, so
        // the filter and the pre-comp pipeline agree on what
        // pitch/yaw-rate "is" this frame.
        const EKF6::State prevState = ekf6_.getState();
        // EKF6's bq tracks the bias on its NEGATED q input (see the
        // p/q negation in the measurement adapter below), so bq in the
        // firmware frame is -prevState.bq. r is fed un-negated so br is
        // already in the firmware frame.
        const float yawRateForComp   = in.yawRateCorrDps
                                        - onspeed::rad2deg(prevState.br);
        const float pitchRateForComp = in.pitchRateCorrDps
                                        + onspeed::rad2deg(prevState.bq);

        const float AccelFwdCompFactor  = onspeed::mps2g(in.tasDotMps2);
        const float AccelLatCompFactor  =
            onspeed::mps2g(onspeed::deg2rad(in.tasMps * yawRateForComp));
        const float AccelVertCompFactor =
            onspeed::mps2g(onspeed::deg2rad(in.tasMps * pitchRateForComp));

        const float fadeAlpha = in.dtSec / (kCompFadeTauSec + in.dtSec);
        compFadeIn_ += fadeAlpha * (1.0f - compFadeIn_);

        out.accelFwdCompG  = accelFwdFilter_.update(in.accelFwdCorrG)
                             - compFadeIn_ * AccelFwdCompFactor;
        out.accelLatCompG  = accelLatFilter_.update(in.accelLatCorrG)
                             - compFadeIn_ * AccelLatCompFactor;
        out.accelVertCompG = accelVertFilter_.update(in.accelVertCorrG)
                             + compFadeIn_ * AccelVertCompFactor;
    } else {
        out.accelFwdCompG  = accelFwdFilter_.update(in.accelFwdCorrG);
        out.accelLatCompG  = accelLatFilter_.update(in.accelLatCorrG);
        out.accelVertCompG = accelVertFilter_.update(in.accelVertCorrG);
        compFadeIn_        = 0.0f;
    }

    // 2) On the gate's rising edge, reset EKF6's alpha covariance so
    //    the filter re-learns alpha from real gamma measurements. The
    //    alpha state was stagnant during the gate-closed period (no
    //    informative gamma); resetting the covariance opens the gain
    //    back up to absorb the new measurements quickly.
    if (out.iasGateRisingEdge) {
        ekf6_.resetAlphaCovariance();
    }

    // 3) Run the EKF6 fusion. Sign-convention plumbing:
    //
    //   az: OnSpeed's accelVertCompG is +1g for level — the reaction-
    //     force convention pilots see in the CSV. EKF6's measurement
    //     model uses the standard inertial-frame view where az = -g
    //     for level. Negate on input.
    //
    //   p, q (roll, pitch rate): OnSpeed's internal IMU rates are
    //     "+gyroPitchDps means nose-DOWN" / "+gyroRollDps means
    //     right-wing-UP" — opposite to EKF6's standard "+p increases
    //     phi (right-wing-down)" / "+q increases theta (nose-up)".
    //     Madgwick handles the mismatch via output negation;
    //     EKF6 doesn't negate output, so we negate INPUT rates here.
    //
    //   r (yaw rate): no negation.
    //
    //   Accel g → m/s², gyro deg/s → rad/s, gamma already in rad.
    EKF6::Measurements meas = {
        /* ax */    out.accelFwdCompG  * kEkfGravityMps2,
        /* ay */    out.accelLatCompG  * kEkfGravityMps2,
        /* az */   -out.accelVertCompG * kEkfGravityMps2,
        /* p  */   -onspeed::deg2rad(in.rollRateCorrDps),
        /* q  */   -onspeed::deg2rad(in.pitchRateCorrDps),
        /* r  */    onspeed::deg2rad(in.yawRateCorrDps),
        /* gamma */ in.gammaRad,
    };
    ekf6_.update(meas, in.dtSec);

    const EKF6::State state = ekf6_.getState();
    out.pitchDeg      = state.theta_deg();
    out.rollDeg       = state.phi_deg();
    out.derivedAoaDeg = state.alpha_deg();

    // 4) EarthVertG via the EKF6 Euler-angle attitude. Body→earth
    //    rotation of the unsmoothed installation-corrected vertical
    //    accel, minus the +1g level reaction-force convention.
    const float sph = std::sin(state.phi);
    const float cph = std::cos(state.phi);
    const float sth = std::sin(state.theta);
    const float cth = std::cos(state.theta);
    out.earthVertG = -sth * in.accelFwdCorrG
                   + sph * cth * in.accelLatCorrG
                   + cph * cth * in.accelVertCorrG - 1.0f;

    out.compFadeIn = compFadeIn_;
    out.iasGate    = iasGate_;
    return out;
}

}   // namespace onspeed::ahrs
