// Ekf6Pipeline.h — AHRS-stage wrapper for the EKF6 6-state Euler EKF.
//
// Owns internal accel pre-filtering, the compFadeIn ramp that softens
// the iasGate rising-edge transient on the comp factors, the IAS-gate
// hysteresis state, and the OnSpeed↔EKF6 sign-convention plumbing
// (az/p/q negated on input to the filter's standard aerospace frame).
// The underlying EKF6 fusion code (EKF6.h) is the unit-testable
// six-state filter; this class is the AHRS-stage seam that adapts it
// to the Ahrs::Step Inputs/Outputs contract.
//
// Tuning constants are compile-time members of this class. Pilots do
// not tune these; an algorithm developer who wants to retune EKF6 can
// edit them directly.

#ifndef ONSPEED_CORE_AHRS_EKF6_PIPELINE_H
#define ONSPEED_CORE_AHRS_EKF6_PIPELINE_H

#include <ahrs/EKF6.h>

#include <filters/EMAFilter.h>

namespace onspeed::ahrs {

class Ekf6Pipeline {
public:
    // EKF6-pipeline-internal tuning constants. Identical to Madgwick's
    // for behaviour parity with master — the algorithms differ in fusion
    // math, not in input pre-filtering.
    static constexpr float kAccelEmaAlpha    = 0.060899f;
    static constexpr float kCompFadeTauSec   = 0.5f;
    static constexpr float kIasGateRisingKt  = 20.0f;
    static constexpr float kIasGateFallingKt = 15.0f;

    /// Per-frame inputs. Same shape as Madgwick::Inputs by design.
    struct Inputs {
        float accelFwdCorrG  = 0.0f;
        float accelLatCorrG  = 0.0f;
        float accelVertCorrG = +1.0f;

        float rollRateCorrDps  = 0.0f;
        float pitchRateCorrDps = 0.0f;
        float yawRateCorrDps   = 0.0f;

        float tasMps     = 0.0f;
        float tasDotMps2 = 0.0f;

        float iasKt = 0.0f;

        /// Flight-path angle from the smoothing stage (rad). EKF6
        /// consumes this as its 4th measurement; Madgwick doesn't have
        /// an analogue.
        float gammaRad = 0.0f;

        float dtSec = 1.0f / 208.0f;
    };

    /// Per-frame outputs. Mirrors Madgwick::Outputs shape, with the
    /// addition of derived AOA (EKF6 tracks alpha as a state) and the
    /// IAS-rising-edge signal so the AHRS layer can publish a
    /// "filter-just-reset" event for downstream consumers.
    struct Outputs {
        float pitchDeg     = 0.0f;
        float rollDeg      = 0.0f;
        float derivedAoaDeg = 0.0f;
        float earthVertG   = 0.0f;

        float accelFwdCompG  = 0.0f;
        float accelLatCompG  = 0.0f;
        float accelVertCompG = 0.0f;

        float compFadeIn = 0.0f;
        bool iasGate     = false;

        /// True on the frame where iasGate transitions false→true.
        /// Step() uses this internally to fire
        /// EKF6::resetAlphaCovariance(); exposed for tests + future
        /// diagnostic consumers.
        bool iasGateRisingEdge = false;
    };

    Ekf6Pipeline();

    /// Seed the filter with the supplied initial attitude (degrees).
    void Init(float seedPitchDeg, float seedRollDeg);

    /// Run one AHRS-stage frame.
    Outputs Step(const Inputs& in);

private:
    EKF6 ekf6_;

    EMAFilter accelFwdFilter_;
    EMAFilter accelLatFilter_;
    EMAFilter accelVertFilter_;

    float compFadeIn_ = 0.0f;
    bool iasGate_     = false;
};

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_EKF6_PIPELINE_H
