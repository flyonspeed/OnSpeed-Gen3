// Ekf6Pipeline.h — AHRS-stage wrapper for the EKF6 6-state Euler EKF.
//
// Mirrors the Madgwick AHRS-stage wrapper: owns internal accel
// pre-filtering, compFadeIn ramp, IAS-gate hysteresis state, and the
// OnSpeed↔EKF6 sign-convention plumbing. The underlying EKF6 fusion
// code (EKF6.h) is unchanged and continues to be the unit-testable
// six-state filter; this class is just the AHRS-stage seam.
//
// Constants are kept at the legacy Madgwick-tuned values for behaviour
// parity with master. EKF6 is on death row (replaced by EKFQ in PR
// #576); a future PR can promote these to a tunable config struct if
// EKF6 outlives expectations.

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
        /// AHRS layer uses this to publish a "filter reset" signal so
        /// downstream display can clear any iasGate=false visuals.
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
