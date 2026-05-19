// Madgwick.h — AHRS-stage Madgwick attitude pipeline.
//
// The OnSpeed AHRS layer is conceived as four stages:
//
//   raw sensors ──► AHRS (Madgwick / EKF6 / ...) ──► smoothing ──► outputs
//
// The AHRS stage is owned by an algorithm class. Each algorithm class
// (this file for Madgwick; EKF6.h for the legacy 6-state Euler EKF;
// future EKFQ for the 11-state quaternion EKF) takes raw-corrected
// sensor inputs and produces algorithm outputs — pitch, roll, derived
// AOA, earth-vertical-G. Internal pre-filtering (accel smoothing,
// compFadeIn ramp, comp-factor computation) and internal validity
// decisions (when to apply centripetal compensation) live INSIDE the
// algorithm class. Different algorithms get different constants and
// different gates without retuning shared firmware-side state.
//
// The wire/log smoothing of accels for display purposes (the
// `accelFwdSmoothed` / `accelLatSmoothed` / `accelVertSmoothed` fields
// on the AhrsOutputs struct) is a SEPARATE stage owned by Ahrs::Step,
// not by any algorithm. Those values track a wire-spec time constant
// independent of which AHRS algorithm is active.
//
// Madgwick's internal accel EMA happens to use the same alpha as the
// wire-spec EMA today (kAccelEmaAlpha == legacy kAccSmoothing). That's
// historical — Madgwick was tuned against this exact filter response.
// A future re-tune of either side can move independently.

#ifndef ONSPEED_CORE_AHRS_MADGWICK_H
#define ONSPEED_CORE_AHRS_MADGWICK_H

#include <ahrs/MadgwickFusion.h>

#include <filters/EMAFilter.h>

namespace onspeed::ahrs {

class Madgwick {
public:
    // Madgwick-internal tuning constants. Compile-time; no user config.
    // A future PR can promote these to a struct + setConfig() if Optuna
    // ever tunes Madgwick the way it tunes EKFQ.

    /// Accel pre-filter EMA alpha at IMU sample rate.
    static constexpr float kAccelEmaAlpha    = 0.060899f;
    /// Comp-fade ramp time constant (seconds).
    static constexpr float kCompFadeTauSec   = 0.5f;
    /// IAS rising-edge / falling-edge thresholds for the algorithm's
    /// internal centripetal-comp gate. Currently mirror the firmware's
    /// legacy display thresholds; a future PR may decouple if Madgwick
    /// turns out to want different numbers from the display gate.
    static constexpr float kIasGateRisingKt  = 20.0f;
    static constexpr float kIasGateFallingKt = 15.0f;

    /// Per-frame inputs. Raw-corrected sensors (post-installation-bias
    /// rotation), plus TAS state and the timestep.
    struct Inputs {
        // Installation-corrected raw accels (g).
        float accelFwdCorrG  = 0.0f;
        float accelLatCorrG  = 0.0f;
        float accelVertCorrG = +1.0f;

        // Installation-corrected gyro rates (deg/s).
        float rollRateCorrDps  = 0.0f;
        float pitchRateCorrDps = 0.0f;
        float yawRateCorrDps   = 0.0f;

        // Density-corrected TAS (m/s) and smoothed TAS derivative (m/s^2)
        // computed at pressure-sensor cadence by the sensor stage.
        float tasMps      = 0.0f;
        float tasDotMps2  = 0.0f;

        // Raw IAS (knots). Used internally by Madgwick's centripetal-comp
        // gate. Independent of any display threshold.
        float iasKt = 0.0f;

        // Per-frame timestep (seconds).
        float dtSec = 1.0f / 208.0f;
    };

    /// Per-frame outputs from the Madgwick AHRS stage.
    struct Outputs {
        float pitchDeg      = 0.0f;
        float rollDeg       = 0.0f;
        float earthVertG    = 0.0f;
        /// Compensated accels (post-EMA + post-comp-factor) the fusion
        /// algorithm actually consumed this frame. Exposed for tests
        /// and for the unit-test snapshot harness; NOT a wire field.
        float accelFwdCompG  = 0.0f;
        float accelLatCompG  = 0.0f;
        float accelVertCompG = 0.0f;
        /// Quaternion produced by the fusion math this frame. Used by
        /// downstream EarthVertG computation; exposed for tests.
        float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
        /// Comp-fade ramp coefficient at end-of-frame (0..1). Exposed
        /// for tests.
        float compFadeIn = 0.0f;
        /// Internal IAS gate state at end-of-frame. Exposed for tests.
        bool iasGate = false;
    };

    Madgwick();

    /// Seed the fusion algorithm with the supplied initial attitude.
    /// Caller is responsible for installation-bias correction of the
    /// seed values (i.e. seedPitchDeg and seedRollDeg are already in
    /// the published, bias-applied frame).
    void Init(float sampleRateHz, float seedPitchDeg, float seedRollDeg);

    /// Run one AHRS-stage frame. Returns the algorithm's outputs for
    /// this frame.
    Outputs Step(const Inputs& in);

private:
    ::onspeed::MadgwickFusion fusion_;

    // Accel pre-filter (Madgwick-internal).
    EMAFilter accelFwdFilter_;
    EMAFilter accelLatFilter_;
    EMAFilter accelVertFilter_;

    // Comp-fade ramp state.
    float compFadeIn_ = 0.0f;

    // Internal hysteretic IAS gate. Independent of any display gate.
    bool iasGate_ = false;
};

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_MADGWICK_H
