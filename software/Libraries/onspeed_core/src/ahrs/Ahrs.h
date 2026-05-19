// Ahrs.h — Attitude/Heading Reference System pipeline.
//
// Four-stage model:
//
//   raw sensors ──► AHRS algorithm ──► smoothing ──► outputs
//
// Stage 1 — sensor (this file): rotation-correct raw IMU, density-
// correct IAS → TAS, compute TASdot.
//
// Stage 2 — AHRS algorithm (Madgwick.h / Ekf6Pipeline.h): consume raw-
// corrected sensors, apply own internal pre-filtering / gating /
// fusion math, emit attitude + algorithm-derived signals (pitch, roll,
// derived AOA, earth-vert-G). Each algorithm owns its own constants;
// no shared retunable firmware state.
//
// Stage 3 — smoothing (this file): apply wire-spec smoothing to raw-
// corrected sensor passthroughs (gyro running-mean, accel EMA) and to
// AHRS outputs (Kalman altitude/VSI from baro + earth-vert-G).
// Constants here are wire-format spec, NOT algorithm tuning.
//
// Stage 4 — outputs (this file): assemble final AhrsOutputs struct,
// drive log/wire/display.
//
// Mutability and threading
// ------------------------
// `Step` is not thread-safe.  Callers serialize and snapshot under
// mutex if needed.
//
// Configuration
// -------------
// Rarely-changing fields (pitch/roll bias, algorithm, sample rate,
// gyro smoothing window) live on `AhrsConfig`, passed at
// construction.  `Reconfigure(...)` re-seeds bias trig when config
// changes mid-flight.

#ifndef ONSPEED_CORE_AHRS_AHRS_H
#define ONSPEED_CORE_AHRS_AHRS_H

#include <cstdint>

#include <ahrs/Ekf6Pipeline.h>
#include <ahrs/KalmanFilter.h>
#include <ahrs/Madgwick.h>
#include <filters/EMAFilter.h>
#include <filters/RunningMean.h>
#include <types/AhrsInputs.h>
#include <types/AhrsOutputs.h>

namespace onspeed::ahrs {

/// Wire-spec accel-EMA smoothing alpha (IMU rate, 208 Hz).
/// This is a property of the M5/huVVer display protocol contract;
/// changing it changes what every wire receiver sees regardless of
/// which AHRS algorithm is active. Algorithm-internal pre-filtering
/// (see Madgwick::kAccelEmaAlpha, Ekf6Pipeline::kAccelEmaAlpha) is
/// independent; the two happen to share a value today because Madgwick
/// was tuned against the wire-side filter response.
///
/// Replay (RateAdjustedAccelEma) derives its τ from this constant;
/// the two are kept in sync via static_assert in RateAdjustedAccelEma.h.
inline constexpr float kAccSmoothing = 0.060899f;

// AHRS algorithm choice.  Integer values match `Config::iAhrsAlgorithm`
// in the sketch (0 = Madgwick, 1 = EKF6) so existing config files load
// unchanged.
enum class Algorithm : int { Madgwick = 0, Ekf6 = 1 };

// Constructor-time AHRS configuration.  Per-frame values live on
// AhrsInputs.
struct AhrsConfig {
    float pitchBiasDeg     = 0.0f;
    float rollBiasDeg      = 0.0f;
    Algorithm algorithm    = Algorithm::Madgwick;
    int   gyroSmoothingWindow = 30;     // RunningMean window
    float imuSampleRateHz  = 208.0f;
    float pressureSampleRateHz = 50.0f; // fallback dt for IAS derivative
};

class Ahrs {
public:
    explicit Ahrs(const AhrsConfig& cfg);

    // Reset filters and re-seed accel state to a level-on-the-ground
    // attitude.  Init() must be called before the first Step() to seed
    // the algorithm's initial pitch/roll from the latest accel reading.
    void Init(const AhrsInputs& seedFrame, float seedPaltFt);

    // Reconfigure mid-flight (e.g. user changed algorithm or biases).
    // Re-seeds bias trig.  Caller should then call Init() with a fresh
    // frame to seed attitude.
    void Reconfigure(const AhrsConfig& cfg);

    // Run one IMU-rate frame.  `dtSec` is the time since the previous
    // Step (typically 1/208 s).  Returns the latest AhrsOutputs snapshot.
    AhrsOutputs Step(const AhrsInputs& in, float dtSec);

    // ---- Sensor-stage accessors (raw-corrected, pre-smoothing) ----

    // Latest installation-corrected (unsmoothed) accel components, in g.
    float accelFwdCorrG()  const { return accelFwdCorr_;  }
    float accelLatCorrG()  const { return accelLatCorr_;  }
    float accelVertCorrG() const { return accelVertCorr_; }

    // ---- Smoothing-stage accessors (wire-spec) ----

    // Wire-side accel EMA: what M5/huVVer/log receive on the wire.
    // Algorithm-blind; tracks the wire-format protocol contract.
    float accelFwdSmoothedG()  const { return accelFwdWireFilter_.get();  }
    float accelLatSmoothedG()  const { return accelLatWireFilter_.get();  }
    float accelVertSmoothedG() const { return accelVertWireFilter_.get(); }

    // ---- Algorithm-internal-state diagnostic accessors ----
    //
    // Delegates to the active algorithm. Useful for integration tests
    // and console diagnostics that want to observe the algorithm's
    // internal post-EMA-and-comp accel + the rising-edge fade-in
    // coefficient. NOT a wire field; algorithm-specific (different
    // algorithms in the future may compute these from different α/τ).
    float accelFwdCompG()  const { return algoCompFwdG_;  }
    float accelLatCompG()  const { return algoCompLatG_;  }
    float accelVertCompG() const { return algoCompVertG_; }
    float compFadeIn()     const { return algoCompFadeIn_; }

    // ---- Output-stage accessors ----

    // Currently published outputs (mirrors of the last Step result).
    const AhrsOutputs& latest() const { return outputs_; }

    // Convenience: TAS in m/s (== outputs_.tasMps).  Some legacy callers
    // care about this even when no fresh frame was just processed.
    float tasMps() const { return tas_; }

    // The ImuTask currently uses fImuDeltaTime as a fallback when the
    // measured dt is invalid.  Expose for the sketch shim.
    float imuDeltaTimeSec() const { return imuDeltaTime_; }

    // Latest Kalman altitude/VSI in legacy units (meters, m/s).  These
    // mirror the legacy AHRS.KalmanAlt/KalmanVSI fields exactly — same
    // unit, same zeroing rule.
    float kalmanAltMeters() const { return kalmanAltMeters_; }
    float kalmanVsiMps()    const { return kalmanVsiMps_;    }

    // Algorithm selector and config pitch/roll bias for sketch shim.
    Algorithm algorithm() const { return cfg_.algorithm; }
    float pitchBiasDeg()  const { return cfg_.pitchBiasDeg; }
    float rollBiasDeg()   const { return cfg_.rollBiasDeg;  }

private:
    // Internal helper: recompute trig of installation bias.
    void recomputeBiasTrig_();

    // Internal helper: density-correct + EMA-smooth TAS from latest IAS.
    void updateTas_(const AhrsInputs& in);

    AhrsConfig cfg_;
    float      imuDeltaTime_ = 0.0f;

    // Precomputed trig of installation bias angles
    float fSinPitch_ = 0.0f, fCosPitch_ = 1.0f;
    float fSinRoll_  = 0.0f, fCosRoll_  = 1.0f;

    // Latest unsmoothed installation-corrected acceleration (in g).
    // Stage 1 (sensor) output.
    float accelFwdCorr_  = 0.0f;
    float accelLatCorr_  = 0.0f;
    float accelVertCorr_ = +1.0f;

    // Display-rate gyro running means (stage 3 — smoothing).  Capacity
    // from cfg.gyroSmoothingWindow.
    RunningMean gyroRollAvg_;
    RunningMean gyroPitchAvg_;
    RunningMean gyroYawAvg_;

    // Wire-side accel EMA (stage 3 — smoothing).  Tracks the wire-
    // format protocol contract τ.  Algorithm-blind: fed raw-corrected
    // accels every frame regardless of which algorithm is running.
    EMAFilter accelFwdWireFilter_;
    EMAFilter accelLatWireFilter_;
    EMAFilter accelVertWireFilter_;

    // AHRS algorithms (stage 2).  We hold both so Reconfigure() can
    // route to either without re-allocating.  Each owns its internal
    // pre-filtering, gating, and fusion state.
    Madgwick     madgwick_;
    Ekf6Pipeline ekf6_;

    // Altitude/VSI Kalman (stage 3 — smoothing on baro + earth-vert-G).
    KalmanFilter kalman_;

    // Cached algorithm-internal diagnostics published per frame from
    // the active algorithm's Outputs struct. See accelFwdCompG() /
    // compFadeIn() accessors above.
    float algoCompFwdG_   = 0.0f;
    float algoCompLatG_   = 0.0f;
    float algoCompVertG_  = 0.0f;
    float algoCompFadeIn_ = 0.0f;

    // TAS state (m/s).  Stage 1 output (density-corrected airspeed).
    float tas_     = 0.0f;
    float prevTas_ = 0.0f;
    float tasDotSmoothed_ = 0.0f;
    uint32_t lastIasUpdateUs_ = 0;

    // Latest Kalman outputs in legacy units (mirrors the published
    // outputs_.kalmanAltFt/Vsi but in meters and m/s).
    float kalmanAltMeters_ = 0.0f;
    float kalmanVsiMps_    = 0.0f;

    // Latest published outputs.
    AhrsOutputs outputs_{};
};

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_AHRS_H
