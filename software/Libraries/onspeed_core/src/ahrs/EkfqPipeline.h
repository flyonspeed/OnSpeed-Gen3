// EkfqPipeline.h — AHRS-stage wrapper for the EKFQ 11-state quaternion EKF.
//
// Owns internal accel pre-filtering, the compFadeIn ramp that softens
// the iasGate rising-edge transient (here applied to the tas / tasDot
// inputs the filter consumes), the IAS-gate hysteresis state, the
// OnSpeed↔EKFQ sign-convention plumbing (az/p/q negated on input to
// the filter's standard NED-aerospace frame), and the vertical-channel
// covariance reset on the gate rising edge.
//
// EKFQ models centripetal and TASdot inside its measurement equation
// h(x), so it consumes raw post-EMA accels — no centripetal-comp
// pre-step like Madgwick.  The accelFwdCompG/etc. fields on Outputs
// surface the post-EMA values that were actually fed to the filter,
// for parity with the Madgwick diagnostic surface.
//
// EKFQ owns its own vertical channel (states z, vz, b_az) and
// publishes altitude/VSI directly via kalmanAltMeters /
// kalmanVsiMps on Outputs.  The standalone KalmanFilter in Ahrs is
// bypassed when EKFQ is the active algorithm.

#ifndef ONSPEED_CORE_AHRS_EKFQ_PIPELINE_H
#define ONSPEED_CORE_AHRS_EKFQ_PIPELINE_H

#include <ahrs/EKFQ.h>

#include <filters/EMAFilter.h>

namespace onspeed::ahrs {

class EkfqPipeline {
public:
    /// Pipeline-internal tuning. Each field's default reproduces the
    /// Optuna study `ekfq_v15` best-trial values (the same study that
    /// produced EKFQ::Config::defaults()).  These are NOT Madgwick's
    /// constants — EKFQ's signal chain was tuned against itself.
    struct PipelineConfig {
        float accelEmaAlpha;     ///< Per-axis accel EMA α applied at IMU rate
        float compFadeTauSec;    ///< compFadeIn ramp τ on iasGate transitions
        float iasGateRisingKt;   ///< IAS hysteresis rising threshold (kt)
        float tasdotEmaAlpha;    ///< TASdot EMA α at IMU rate

        /// Falling-edge threshold = iasGateRisingKt - kIasGateHysteresisKt.
        /// 5 kt fixed hysteresis matches Python pipeline_quat.py.
        static constexpr float kIasGateHysteresisKt = 5.0f;

        /// Production defaults from Optuna v15 best trial.
        static PipelineConfig defaults();
    };

    // Instance members holding the pipeline tuning. Initialised from
    // PipelineConfig::defaults() by the default ctor; replaceable via
    // the PipelineConfig-taking ctor or setPipelineConfig().

    /// Per-frame inputs.  Mirrors Madgwick::Inputs for the sensor-stage
    /// fields, plus baro altitude (EKFQ owns its vertical channel and
    /// ingests baro directly).
    ///
    /// Note: `tasMps` is the held-stale, density-corrected TAS from
    /// the sensor stage — the same value Madgwick consumes — and is
    /// repeated across the ~4 IMU frames between IAS-update events.
    /// EkfqPipeline computes its own per-step TASdot from (tasMps -
    /// prev_tasMps) / dtSec internally and smooths it at IMU rate with
    /// pipeCfg_.tasdotEmaAlpha, matching the Python pipeline's signal chain.
    /// The sensor-stage `tasDotSmoothed_` (kIasSmoothing α=0.0179 at
    /// pressure cadence) is ignored here — it's a different
    /// characteristic from what the Optuna study used.
    struct Inputs {
        float accelFwdCorrG  = 0.0f;
        float accelLatCorrG  = 0.0f;
        float accelVertCorrG = +1.0f;

        float rollRateCorrDps  = 0.0f;
        float pitchRateCorrDps = 0.0f;
        float yawRateCorrDps   = 0.0f;

        float tasMps = 0.0f;

        float iasKt = 0.0f;

        /// Baro altitude (meters, +up).  Ahrs::Step converts from the
        /// firmware's paltFt before passing.
        float baroAltMeters = 0.0f;

        float dtSec = 1.0f / 208.0f;
    };

    /// Per-frame outputs.  Mirrors Madgwick::Outputs shape with
    /// derivedAoaDeg added (EKFQ exposes a kinematic AOA derived from
    /// state + TAS) and kalmanAltMeters / kalmanVsiMps added (EKFQ
    /// owns the vertical channel — Ahrs::Step publishes these
    /// directly instead of running the standalone KalmanFilter).
    struct Outputs {
        float pitchDeg      = 0.0f;
        float rollDeg       = 0.0f;
        float derivedAoaDeg = 0.0f;
        float earthVertG    = 0.0f;

        /// Post-EMA accel values that were fed to the filter.  Used
        /// for the algoCompFwdG/etc. diagnostic accessors on Ahrs.
        /// EKFQ does not apply a separate post-comp subtraction, so
        /// these are the EMA-only values.
        float accelFwdCompG  = 0.0f;
        float accelLatCompG  = 0.0f;
        float accelVertCompG = 0.0f;

        /// Vertical channel published from the filter's z / vz states.
        /// Ahrs::Step routes these to outputs_.kalmanAltFt /
        /// outputs_.kalmanVsiFpm via unit conversions.  vz is
        /// NED-down internally; the value here is already flipped to
        /// the firmware's +climb convention.
        ///
        /// `ownsVerticalChannel = true` on this struct tells Ahrs::Step
        /// to bypass the standalone KalmanFilter and use these values
        /// directly.  Algorithms that don't track altitude (Madgwick)
        /// leave `ownsVerticalChannel = false` and the standalone
        /// Kalman runs on baro + earth-vert-G in stage 3c.
        bool  ownsVerticalChannel = true;
        float kalmanAltMeters     = 0.0f;
        float kalmanVsiMps        = 0.0f;

        float compFadeIn = 0.0f;
        bool iasGate     = false;

        /// True on the frame iasGate transitions false→true.  Step()
        /// uses this internally to call EKFQ::resetVerticalCovariance
        /// so the z/vz/b_az covariances re-open after a long
        /// gate-closed taxi.  Exposed for tests + future diagnostic
        /// consumers.
        bool iasGateRisingEdge = false;
    };

    /// Default-construct with PipelineConfig::defaults() values.
    EkfqPipeline();
    /// Construct with explicit pipeline tuning.
    explicit EkfqPipeline(const PipelineConfig& cfg);

    /// Replace the pipeline tuning and re-seat the accel EMA filters
    /// to the level-on-ground state (ax=0, ay=0, az=+1g). Safe for
    /// offline Optuna trials (which start from a cold pipeline);
    /// calling mid-flight will inject a transient in the accel EMA.
    void setPipelineConfig(const PipelineConfig& cfg);
    const PipelineConfig& getPipelineConfig() const { return pipeCfg_; }

    /// Direct access to the wrapped filter for per-trial config
    /// injection. Tests and the Optuna substrate use this; production
    /// code goes through Init() / Step().
    EKFQ& getEkfq() { return ekfq_; }
    const EKFQ& getEkfq() const { return ekfq_; }

    /// Seed the filter with the supplied initial attitude (degrees)
    /// and baro altitude (meters, +up).
    void Init(float seedPitchDeg, float seedRollDeg, float seedAltMeters);

    /// Run one AHRS-stage frame.
    Outputs Step(const Inputs& in);

private:
    EKFQ ekfq_;

    PipelineConfig pipeCfg_;

    EMAFilter accelFwdFilter_;
    EMAFilter accelLatFilter_;
    EMAFilter accelVertFilter_;

    float compFadeIn_ = 0.0f;
    bool iasGate_     = false;

    /// Per-step TAS-derivative smoothing.  Held-stale `tas` from the
    /// sensor stage drives a sawtooth `tasdot_raw = (tas - prev_tas) /
    /// dt` at IMU rate; EMA at pipeCfg_.tasdotEmaAlpha smooths the
    /// sawtooth. Matches the Python pipeline `pipeline_quat.py`
    /// byte-for-byte.
    float prevTasMps_       = 0.0f;
    float tasdotSmoothed_   = 0.0f;
};

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_EKFQ_PIPELINE_H
