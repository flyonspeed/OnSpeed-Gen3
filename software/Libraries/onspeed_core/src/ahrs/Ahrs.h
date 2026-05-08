// Ahrs.h — Attitude/Heading Reference System pipeline (PR 3.2 extraction).
//
// Consumes per-frame IMU + sensor samples; produces smoothed pitch, roll,
// flight-path, TAS, derived AOA, and Kalman altitude/VSI.  This class is
// the platform-free port of the sketch-side `AHRS` class — every
// arithmetic operation in `Ahrs::Step` matches the order, type, and
// constant values of the legacy `AHRS::Process` body bit-for-bit.  The
// snapshot regression harness verifies the port has zero numerical drift.
//
// Pipeline (one Step call):
//
//   1. EMA-smooth raw accels (alpha = kAccSmoothing = 0.060899).
//   2. Apply installation bias (precomputed sin/cos of pitch/roll bias).
//   3. Run Madgwick or EKF6 algorithm to fuse gyro + accel into attitude.
//   4. Compute density-corrected TAS from IAS + Palt + OAT — but only
//      when the IAS-update timestamp advances (≈ 50 Hz, not 208 Hz).
//   5. Compute TAS first derivative via a variable-rate EMA.
//   6. Run Kalman on altitude + earth-vertical-G for altitude/VSI.
//   7. FlightPath = asin(VSI / TAS); DerivedAOA = pitch - flight path.
//
// Mutability and threading
// ------------------------
// `Step` is not thread-safe.  Callers (the sketch's IMU task) are
// expected to serialize calls and snapshot the outputs under a mutex if
// other tasks need to read them.  This class itself does no locking.
//
// Configuration
// -------------
// Rarely-changing fields (pitch/roll bias, algorithm, sample rate, gyro
// smoothing window) live on `AhrsConfig`, passed at construction.  The
// `Reconfigure(...)` method re-initializes filter state when the user
// changes config mid-flight (e.g. via the web UI).

#ifndef ONSPEED_CORE_AHRS_AHRS_H
#define ONSPEED_CORE_AHRS_AHRS_H

#include <cstdint>

#include <ahrs/EKF6.h>
#include <ahrs/KalmanFilter.h>
#include <ahrs/MadgwickFusion.h>
#include <filters/EMAFilter.h>
#include <filters/RunningMean.h>
#include <types/AhrsInputs.h>
#include <types/AhrsOutputs.h>

namespace onspeed::ahrs {

/// Firmware accelerometer EMA smoothing coefficient at IMU rate (208 Hz).
/// Used inside Ahrs::Step()'s accelLatFilter_ / accelVertFilter_ /
/// accelFwdFilter_.  Replay (RateAdjustedAccelEma) derives its τ from this
/// constant; the two are kept in sync via static_assert in
/// RateAdjustedAccelEma.h.
inline constexpr float kAccSmoothing = 0.060899f;

// AHRS algorithm choice.  Integer values match `Config::iAhrsAlgorithm`
// in the sketch (0 = Madgwick, 1 = EKF6) so existing config files load
// unchanged.
enum class Algorithm : int { Madgwick = 0, Ekf6 = 1 };

// Constructor-time AHRS configuration.  Values that change rarely (or
// only when the user toggles a setting and we re-init); per-frame values
// live on AhrsInputs.
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

    // Reset all filters and re-seed accel state to a level-on-the-ground
    // attitude.  Init() must be called before the first Step() to seed
    // the algorithm's initial pitch/roll from the latest accel reading.
    // (This mirrors the legacy two-phase Init/Process split.)
    void Init(const AhrsInputs& seedFrame, float seedPaltFt);

    // Reconfigure mid-flight (e.g. user changed algorithm or biases).
    // Re-seeds bias trig and re-initializes the chosen filter.  Caller
    // should then call Init() with a fresh frame to seed attitude.
    void Reconfigure(const AhrsConfig& cfg);

    // Run one IMU-rate frame.  `dtSec` is the time since the previous
    // Step (typically 1/208 s).  Returns the latest AhrsOutputs snapshot.
    AhrsOutputs Step(const AhrsInputs& in, float dtSec);

    // ---- Accessors used by sketch-side compatibility shims and tests ----

    // Latest installation-corrected (unsmoothed) accel components, in g.
    float accelFwdCorrG()  const { return accelFwdCorr_;  }
    float accelLatCorrG()  const { return accelLatCorr_;  }
    float accelVertCorrG() const { return accelVertCorr_; }

    // Latest installation-corrected and smoothed accel components.
    float accelFwdSmoothedG()  const { return accelFwdFilter_.get();  }
    float accelLatSmoothedG()  const { return accelLatFilter_.get();  }
    float accelVertSmoothedG() const { return accelVertFilter_.get(); }

    // Latest acceleration after smoothing AND centripetal/forward
    // compensation (the values that go into the Madgwick/EKF6 update).
    float accelFwdCompG()  const { return accelFwdComp_;  }
    float accelLatCompG()  const { return accelLatComp_;  }
    float accelVertCompG() const { return accelVertComp_; }

    // Currently published outputs (mirrors of the last Step result).
    const AhrsOutputs& latest() const { return outputs_; }

    // Convenience: TAS in m/s (== outputs_.tasMps).  Some legacy callers
    // care about this even when no fresh frame was just processed.
    float tasMps() const { return tas_; }

    // Current fade-in coefficient applied to the forward / lateral /
    // vertical accel compensation factors.  Ramps 0 -> 1 on a τ ≈ 0.5 s
    // EMA after iasAlive rises; snaps back to 0 when iasAlive falls.
    // Exposed for unit-test observability — see issue #114.
    float compFadeIn() const { return compFadeIn_; }

    // The ImuTask currently uses fImuDeltaTime as a fallback when the
    // measured dt is invalid.  Expose for the sketch shim.
    float imuDeltaTimeSec() const { return imuDeltaTime_; }

    // Latest Kalman altitude/VSI in legacy units (meters, m/s).  These
    // mirror the legacy AHRS.KalmanAlt/KalmanVSI fields exactly — same
    // unit, same zeroing rule.  Publishing these from the same internal
    // state used to compute outputs_.kalmanAltFt avoids a float
    // round-trip through m -> ft -> m for the sketch shim that
    // re-exposes them in meters.
    float kalmanAltMeters() const { return kalmanAltMeters_; }
    float kalmanVsiMps()    const { return kalmanVsiMps_;    }

    // Algorithm selector and config pitch/roll bias for sketch shim
    // (DisplaySerial uses bias + raw accels for one of its derived fields).
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

    // Accelerometer smoothing chain (alpha = 0.060899 in legacy code).
    EMAFilter accelFwdFilter_;
    EMAFilter accelLatFilter_;
    EMAFilter accelVertFilter_;

    // Latest unsmoothed installation-corrected acceleration (in g).
    float accelFwdCorr_  = 0.0f;
    float accelLatCorr_  = 0.0f;
    float accelVertCorr_ = +1.0f;   // +1g for level (production reaction-force convention)

    // Latest fully-compensated acceleration (in g) — Madgwick/EKF6 input.
    float accelFwdComp_  = 0.0f;
    float accelLatComp_  = 0.0f;
    float accelVertComp_ = 0.0f;

    // Display-rate gyro averages.  Capacity from cfg.gyroSmoothingWindow.
    RunningMean gyroRollAvg_;
    RunningMean gyroPitchAvg_;
    RunningMean gyroYawAvg_;

    // Attitude algorithms (only one is "active" depending on cfg.algorithm,
    // but we hold both so Reconfigure can re-seed from the same struct).
    Madgwick madgwick_;
    EKF6     ekf6_;

    // Altitude/VSI Kalman.
    KalmanFilter kalman_;

    // TAS state (m/s).
    float tas_     = 0.0f;
    float prevTas_ = 0.0f;
    float tasDotSmoothed_ = 0.0f;
    uint32_t lastIasUpdateUs_ = 0;
    bool     iasWasBelowThreshold_ = true;

    // Fade-in coefficient (0..1) for the forward/lateral/vertical accel
    // compensation factors.  EMA-ramps from 0 to 1 on τ ≈ 0.5 s while
    // iasAlive is true; snaps back to 0 the frame iasAlive falls.
    //
    // Rationale: on the iasAlive rising edge (takeoff roll crossing ~20
    // kt), tas_ steps from 0 to ~10 m/s in one pressure frame, causing
    // tasDotSmoothed_ to spike and the three comp factors to jump from 0
    // to full magnitude in one frame.  The forward-comp path peaks at
    // ~3.75 g in one frame before the IAS EMA swallows it.  Fading the
    // coefficient in over ~0.5 s removes the discontinuity at the gate
    // boundary without changing steady-state behaviour.  See issue #114.
    //
    // NOT reset in Init() for the same reason tas_ state isn't reset
    // there: Init() is called on every web UI config save, and zeroing
    // the fade mid-flight would momentarily re-disable comp factors and
    // inject a new transient.  Constructor init-to-0 at boot is the
    // only zeroing needed.
    float compFadeIn_ = 0.0f;

    // Latest Kalman outputs in legacy units (mirrors the published
    // outputs_.kalmanAltFt/Vsi but in meters and m/s).  Saved so the
    // sketch shim can publish them without a float round-trip.
    float kalmanAltMeters_ = 0.0f;
    float kalmanVsiMps_    = 0.0f;

    // Latest published outputs.
    AhrsOutputs outputs_{};
};

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_AHRS_H
