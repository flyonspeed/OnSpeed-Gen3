// Madgwick.h — AHRS-stage Madgwick attitude pipeline.
//
// The OnSpeed AHRS layer is structured as four stages:
//
//   raw sensors ──► AHRS algorithm ──► smoothing ──► outputs
//
// The AHRS stage is owned by an algorithm class — this file for
// Madgwick; EkfqPipeline for EKFQ.  An algorithm class takes raw-
// corrected sensor inputs and produces algorithm outputs (pitch,
// roll, derived AOA, earth-vertical-G).  Internal pre-filtering
// (accel smoothing, compFadeIn ramp, comp-factor computation) and
// the algorithm's own validity decisions (when to apply centripetal
// compensation) live INSIDE the algorithm class.  Different
// algorithms can use different constants and different gates without
// sharing any retunable state at the Ahrs layer.
//
// The wire/log accel smoothing (`accelFwdSmoothed` /
// `accelLatSmoothed` / `accelVertSmoothed` on AhrsOutputs) is a
// SEPARATE stage owned by Ahrs::Step — it serves the wire-format
// contract, runs algorithm-blind, and does not couple to the
// algorithm-internal EMAs.

#ifndef ONSPEED_CORE_AHRS_MADGWICK_H
#define ONSPEED_CORE_AHRS_MADGWICK_H

#include <ahrs/KalmanFilter.h>
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
    /// Standalone baro+accel KalmanFilter tuning for the vertical
    /// channel (altitude/VSI).  Madgwick doesn't track altitude as a
    /// fusion state, so a separate 3-state Kalman runs on baro + the
    /// fusion's earth-vertical-G.
    static constexpr float kKalZVariance     = 0.79078f;
    static constexpr float kKalAccelVariance = 1.0f;
    static constexpr float kKalAccelBiasVar  = 1e-11f;
    /// IAS rising-edge / falling-edge thresholds for the algorithm's
    /// internal centripetal-comp gate.  Below the rising threshold,
    /// tas and tasDot are dominated by pitot noise — applying comp
    /// factors would inject that noise into the smoothed accel that
    /// feeds the fusion algorithm.  Hysteresis prevents chatter near
    /// the threshold.  Independent of the user-facing display gate
    /// (OnSpeedConfig::iIasDisplayThresholdKt) — pilots tune the
    /// display threshold for their airframe; this is the algorithm's
    /// own view of the pitot's noise floor.
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

        // Baro altitude (meters, +up).  Fed to the internal Kalman
        // filter for altitude/VSI smoothing.  Ahrs::Step converts from
        // the firmware's paltFt before passing.
        float baroAltMeters = 0.0f;

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
        /// Vertical channel published from the internal KalmanFilter
        /// (baro + earth-vert-G).  Uniform with EkfqPipeline::Outputs:
        /// each algorithm owns its own vertical channel.
        float kalmanAltMeters = 0.0f;
        float kalmanVsiMps    = 0.0f;
    };

    Madgwick();

    /// Seed the fusion algorithm with the supplied initial attitude
    /// and the vertical-channel Kalman with the supplied baro altitude.
    /// Caller is responsible for installation-bias correction of the
    /// seed angles (i.e. seedPitchDeg and seedRollDeg are already in
    /// the published, bias-applied frame).
    void Init(float sampleRateHz, float seedPitchDeg, float seedRollDeg,
              float seedAltMeters);

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

    // Standalone altitude/VSI Kalman (baro + earth-vert-G).  Madgwick
    // doesn't track altitude in its fusion state, so this 3-state
    // Kalman fills the same role for Madgwick that EKFQ's z/vz/b_az
    // states fill for that algorithm.
    ::onspeed::KalmanFilter kalman_;
};

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_MADGWICK_H
