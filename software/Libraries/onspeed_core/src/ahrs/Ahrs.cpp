// Ahrs.cpp — port of the legacy AHRS::Init / AHRS::Process pipeline into
// onspeed_core.  Every arithmetic operation mirrors the order, type, and
// constants of the legacy sketch code so the snapshot regression harness
// observes byte-identical outputs.  Refactor for clarity, not behavior.

#include <ahrs/Ahrs.h>

#include <cmath>

#include <util/OnSpeedTypes.h>

namespace onspeed::ahrs {

namespace {

// Legacy constants (lifted verbatim from sketch's AHRS.cpp; do not retune).
// kAccSmoothing is the public constant ::onspeed::ahrs::kAccSmoothing (Ahrs.h);
// it is referenced directly below without a local alias.
constexpr float kIasSmoothing      = 0.0179f;              // EMA alpha for IAS-derived TAS
constexpr float kIasTauFactor      = (1.0f / kIasSmoothing) - 1.0f;
// Same value as onspeed::g2mps(1.0f); aliased here for readable inline use.
constexpr float kEkfGravityMps2    = 9.80665f;
constexpr float kKalZVariance      = 0.79078f;
// Time constant (seconds) for the iasAlive rising-edge fade-in applied to
// the forward/lateral/vertical accel compensation factors.  ~0.5 s hides
// the one-frame TAS step without significantly delaying the useful comp
// signal.  See issue #114 and the compFadeIn_ comment in Ahrs.h.
constexpr float kCompFadeTauSec    = 0.5f;
// Lower bound for KalmanFilter's per-update accel-variance clamp. Higher
// values make the filter distrust the accel input and lean on baro
// altitude — smoother but laggier VSI.
constexpr float kKalAccelVariance  = 1.0f;
constexpr float kKalAccelBiasVar   = 1e-11f;

// OAT validity window matches legacy logic: |T_C| < 100.
inline bool oatInBand(float c) { return c > -100.0f && c < 100.0f; }

}   // namespace

// ----------------------------------------------------------------------------

Ahrs::Ahrs(const AhrsConfig& cfg)
    : cfg_(cfg)
    , imuDeltaTime_(1.0f / cfg.imuSampleRateHz)
    , accelFwdFilter_(kAccSmoothing)
    , accelLatFilter_(kAccSmoothing)
    , accelVertFilter_(kAccSmoothing)
    , gyroRollAvg_(cfg.gyroSmoothingWindow > 0 ? cfg.gyroSmoothingWindow : 1)
    , gyroPitchAvg_(cfg.gyroSmoothingWindow > 0 ? cfg.gyroSmoothingWindow : 1)
    , gyroYawAvg_(cfg.gyroSmoothingWindow > 0 ? cfg.gyroSmoothingWindow : 1)
{
    // Seed accel filters with the production "level on the ground" rest
    // state (Z = +1 g, X = Y = 0) so the first frames before the IMU has
    // produced a sample yield a sane attitude rather than a degenerate
    // (0,0,0). +1g for level matches OnSpeed's accelerometer reaction-
    // force convention: gravity pulls "up" on the proof mass, so the
    // sensor reads +1g along body-Z (down) at rest. This is what
    // pilots see in the CSV "VerticalG" column and on the web liveview;
    // it's also what the production IMU emits via g_pIMU->Az after the
    // axis sign-mapping in IMU330::Read.
    accelFwdFilter_.seed(0.0f);
    accelLatFilter_.seed(0.0f);
    accelVertFilter_.seed(+1.0f);

    accelVertCorr_ = +1.0f;

    recomputeBiasTrig_();
}

// ----------------------------------------------------------------------------

void Ahrs::recomputeBiasTrig_()
{
    const float fPitchBiasRad = onspeed::deg2rad(cfg_.pitchBiasDeg);
    const float fRollBiasRad  = onspeed::deg2rad(cfg_.rollBiasDeg);
    fSinPitch_ = std::sin(fPitchBiasRad);
    fCosPitch_ = std::cos(fPitchBiasRad);
    fSinRoll_  = std::sin(fRollBiasRad);
    fCosRoll_  = std::cos(fRollBiasRad);
}

// ----------------------------------------------------------------------------

void Ahrs::Reconfigure(const AhrsConfig& cfg)
{
    cfg_ = cfg;
    imuDeltaTime_ = 1.0f / cfg.imuSampleRateHz;
    recomputeBiasTrig_();
}

// ----------------------------------------------------------------------------

void Ahrs::Init(const AhrsInputs& seedFrame, float seedPaltFt)
{
    // Compute the seed pitch/roll from the supplied IMU sample using the
    // exact accelPitch/accelRoll helpers the sketch's IMU class uses for
    // PitchAC/RollAC, then add the configured installation bias.  This
    // exactly mirrors the legacy AHRS::Init() body:
    //
    //   SmoothedPitch = g_pIMU->PitchAC() + g_Config.fPitchBias;
    //   SmoothedRoll  = g_pIMU->RollAC()  + g_Config.fRollBias;
    //
    // Note: the IMU's PitchAC/RollAC use accelPitch/accelRoll on the
    // *aircraft-orientation* axes (Ax/Ay/Az), which is what we receive
    // in `imu.accelXG/YG/ZG`.
    const float fSeedPitch = onspeed::accelPitch(
        seedFrame.imu.accelXG, seedFrame.imu.accelYG, seedFrame.imu.accelZG)
        + cfg_.pitchBiasDeg;
    const float fSeedRoll  = onspeed::accelRoll(
        seedFrame.imu.accelXG, seedFrame.imu.accelYG, seedFrame.imu.accelZG)
        + cfg_.rollBiasDeg;

    recomputeBiasTrig_();

    if (cfg_.algorithm == Algorithm::Ekf6) {
        // Note: EKF6 expects radians.  EKF6 theta convention (positive =
        // nose up) matches SmoothedPitch — no negation needed.  Madgwick
        // requires negated pitch (handled below).
        ekf6_.init(onspeed::deg2rad(fSeedRoll), onspeed::deg2rad(fSeedPitch));
    } else {
        // Madgwick convention: begin() takes (sampleFreq, -pitch, roll)
        madgwick_.begin(cfg_.imuSampleRateHz, -fSeedPitch, fSeedRoll);
    }

    // Seed the Kalman altitude filter with the supplied baro altitude.
    kalman_.Configure(kKalZVariance, kKalAccelVariance, kKalAccelBiasVar,
                      onspeed::ft2m(seedPaltFt), 0.0f, 0.0f);

    // Seed published outputs so a consumer reading before the first Step
    // sees the level-on-the-ground attitude rather than zeros.
    outputs_.pitchDeg = fSeedPitch;
    outputs_.rollDeg  = fSeedRoll;

    // NOTE: TAS state (tas_, prevTas_, tasDotSmoothed_, lastIasUpdateUs_,
    // iasWasBelowThreshold_) is intentionally NOT reset here. Legacy
    // AHRS::Init() also did not reset it. Init() is called from the web
    // UI on every config save (ConfigWebServer.cpp:2052,2363 and
    // Config.cpp:52), and zeroing TAS would inject a one-frame
    // forward-accel-comp glitch (~0.05g during deceleration) that would
    // briefly perturb Madgwick/EKF6 attitude. The constructor initializes
    // these fields once at boot; we leave them alone on Init().
}

// ----------------------------------------------------------------------------

void Ahrs::updateTas_(const AhrsInputs& in)
{
    // Exact port of legacy AHRS::Process TAS block (lines 138-220 of the
    // original AHRS.cpp).  The two density-correction `powf` calls are
    // expensive on the ESP32-S3's single-precision FPU; computing them at
    // 50 Hz instead of 208 Hz saves ~150 µs/cycle with no accuracy loss
    // (IAS/Palt only update at 50 Hz).
    const uint32_t uIasUpdateUs = in.iasUpdateTimestampUs;
    if (uIasUpdateUs == lastIasUpdateUs_) {
        return;
    }

    float fOatC = 0.0f;
    bool  bHaveOat = false;

    if (in.useEfisOat) {
        fOatC    = in.efisOatCelsius;
        bHaveOat = oatInBand(fOatC);
    }

    if (!bHaveOat && in.useInternalOat) {
        fOatC    = in.sensors.oatCelsius;
        bHaveOat = oatInBand(fOatC);
    }

    if (bHaveOat) {
        const float Kelvin    = 273.15f;
        const float Temp_rate = 0.00198119993f;
        float fISA_temp_k = 15.0f - Temp_rate * in.sensors.paltFt + Kelvin;
        float fOAT_k      = fOatC + Kelvin;

        // Guard pow() base values: a negative or zero base with a fractional
        // exponent returns NaN.  Bad OAT data could make fOAT_k <= 0;
        // extreme density altitude could make the IAS divisor <= 0.
        if (fOAT_k > 0.0f) {
            float fDA      = in.sensors.paltFt + (fISA_temp_k / Temp_rate)
                             * (1.0f - std::pow(fISA_temp_k / fOAT_k, 0.2349690f));
            float fDivisor = 1.0f - 6.8755856e-6f * fDA;
            if (fDivisor > 0.0f) {
                tas_ = onspeed::kts2mps(in.sensors.iasKt
                                        / std::pow(fDivisor, 2.12794f));
            } else {
                tas_ = onspeed::kts2mps(in.sensors.iasKt
                                        * (1.0f + in.sensors.paltFt / 1000.0f * 0.02f));
            }
        } else {
            tas_ = onspeed::kts2mps(in.sensors.iasKt
                                    * (1.0f + in.sensors.paltFt / 1000.0f * 0.02f));
        }
    } else {
        tas_ = onspeed::kts2mps(in.sensors.iasKt
                                * (1.0f + in.sensors.paltFt / 1000.0f * 0.02f));
    }

    // TAS derivative for deceleration compensation.
    if (lastIasUpdateUs_ == 0) {
        lastIasUpdateUs_ = uIasUpdateUs;
        prevTas_ = tas_;
        tasDotSmoothed_ = 0.0f;
    } else {
        // Unsigned subtract handles the 71-minute micros() wrap correctly:
        // diff is always the positive elapsed delta.
        float fIasDtSeconds = static_cast<float>(
            static_cast<uint32_t>(uIasUpdateUs - lastIasUpdateUs_)) * 1.0e-6f;
        lastIasUpdateUs_ = uIasUpdateUs;

        if (std::isnan(fIasDtSeconds) || std::isinf(fIasDtSeconds)
            || fIasDtSeconds <= 0.0f) {
            fIasDtSeconds = 1.0f / cfg_.pressureSampleRateHz;
        }

        const float fTASdiff = tas_ - prevTas_;
        prevTas_ = tas_;

        const float fIasTauSeconds = imuDeltaTime_ * kIasTauFactor;
        const float fAlpha   = fIasDtSeconds / (fIasTauSeconds + fIasDtSeconds);
        const float fTASdot  = fTASdiff / fIasDtSeconds;
        tasDotSmoothed_ = fAlpha * fTASdot + (1.0f - fAlpha) * tasDotSmoothed_;
    }
}

// ----------------------------------------------------------------------------

AhrsOutputs Ahrs::Step(const AhrsInputs& in, float dtSec)
{
    // Use measured dt when available; fall back to nominal sample rate if
    // dt is invalid.  Same NaN/inf/<=0/giant-step guards as legacy.
    if (std::isnan(dtSec) || std::isinf(dtSec) || dtSec <= 0.0f) {
        dtSec = imuDeltaTime_;
    }
    if (dtSec > 4.0f * imuDeltaTime_) {
        dtSec = imuDeltaTime_;
    }

    // 1. TAS update (density correction + EMA-smoothed derivative) at
    //    pressure-sensor cadence (~50 Hz), not IMU cadence (~208 Hz).
    updateTas_(in);

    // 2. Installation bias correction.  Yaw bias is always zero, so
    //    sin(yaw)=0 and cos(yaw)=1 are folded into the rotation directly.
    const float sp = fSinPitch_, cp = fCosPitch_;
    const float sr = fSinRoll_,  cr = fCosRoll_;

    // Installation-corrected gyro values (rotation matrix with yaw=0).
    const float RollRateCorr  = in.imu.gyroRollDps  *  cp +
                                in.imu.gyroPitchDps * (sr * sp) +
                                in.imu.gyroYawDps   * (cr * sp);
    const float PitchRateCorr = in.imu.gyroPitchDps *  cr +
                                in.imu.gyroYawDps   * -sr;
    const float YawRateCorr   = in.imu.gyroRollDps  * -sp +
                                in.imu.gyroPitchDps * (sr * cp) +
                                in.imu.gyroYawDps   * (cp * cr);

    // Installation-corrected accel values (same rotation, yaw=0).
    accelVertCorr_ = -in.imu.accelXG *  sp +
                      in.imu.accelYG * (sr * cp) +
                      in.imu.accelZG * (cr * cp);
    accelLatCorr_  =  in.imu.accelYG *  cr +
                      in.imu.accelZG * -sr;
    accelFwdCorr_  =  in.imu.accelXG *  cp +
                      in.imu.accelYG * (sr * sp) +
                      in.imu.accelZG * (cr * sp);

    // 3. Display-only running averages of installation-corrected gyro.
    gyroRollAvg_.addValue(RollRateCorr);
    const float gRoll  = gyroRollAvg_.getFastAverage();
    gyroPitchAvg_.addValue(PitchRateCorr);
    const float gPitch = gyroPitchAvg_.getFastAverage();
    gyroYawAvg_.addValue(YawRateCorr);
    const float gYaw   = gyroYawAvg_.getFastAverage();

    // 4. Linear acceleration compensation: forward (TASdot), centripetal
    //    lateral (tas · yawRate), centripetal vertical (tas · pitchRate).
    //    When EKF6 is active, use its bias-corrected rates from the
    //    previous timestep for more consistent compensation.
    //
    // Gated on in.sensors.iasAlive.  Below the pitot noise floor, tas_
    // and tasDotSmoothed_ are dominated by sensor noise; leaving the
    // comp factors active injects that noise directly into the smoothed
    // accel that feeds Madgwick/EKF6.  The forward-comp path (TASdot)
    // is the heaviest hitter because small IAS jitter produces a large
    // derivative; lateral and vertical centripetal are smaller in
    // magnitude but share the same pathology.  Gating all three at the
    // same validity boundary gives the attitude filter a clean accel
    // signal on the ground.  See issue #109.
    if (in.sensors.iasAlive) {
        const float AccelFwdCompFactor = onspeed::mps2g(tasDotSmoothed_);

        float fYawRateForComp   = YawRateCorr;
        float fPitchRateForComp = PitchRateCorr;
        if (cfg_.algorithm == Algorithm::Ekf6) {
            const onspeed::EKF6::State prevState = ekf6_.getState();
            // EKF6's bq tracks the bias on its NEGATED q input (see the
            // p/q negation in the EKF6 measurement adapter below), so
            // bq in the firmware frame is -prevState.bq. r is fed
            // un-negated so br is already in the firmware frame.
            fYawRateForComp   = YawRateCorr   - onspeed::rad2deg(prevState.br);
            fPitchRateForComp = PitchRateCorr + onspeed::rad2deg(prevState.bq);
        }
        const float AccelLatCompFactor  =
            onspeed::mps2g(onspeed::deg2rad(tas_ * fYawRateForComp));
        const float AccelVertCompFactor =
            onspeed::mps2g(onspeed::deg2rad(tas_ * fPitchRateForComp));

        // Ramp the comp factors in smoothly after the iasAlive rising
        // edge.  The variable-dt EMA form (α = dt / (τ + dt)) matches the
        // updateTas_() pattern above and keeps the time constant
        // independent of IMU rate.  See issue #114.
        const float fadeAlpha = dtSec / (kCompFadeTauSec + dtSec);
        compFadeIn_ += fadeAlpha * (1.0f - compFadeIn_);

        accelFwdComp_  = accelFwdFilter_.update(accelFwdCorr_)
                         - compFadeIn_ * AccelFwdCompFactor;
        accelLatComp_  = accelLatFilter_.update(accelLatCorr_)
                         - compFadeIn_ * AccelLatCompFactor;
        accelVertComp_ = accelVertFilter_.update(accelVertCorr_)
                         + compFadeIn_ * AccelVertCompFactor;
    } else {
        // IAS not alive — tick the accel EMAs so they track the raw
        // installation-corrected readings (otherwise the smoothed accel
        // would grow stale across a long ground pause), but skip the
        // TAS- and TASdot-derived compensation factors.
        //
        // tas_ and tasDotSmoothed_ continue to be updated by updateTas_()
        // on every IAS-advance frame; with the pressure deadband clamping
        // raw IAS to zero at rest, both naturally settle to zero.  We
        // deliberately do NOT zero them here — zeroing would not reduce
        // the rising-edge TASdot transient when real IAS returns (the
        // dominant source of that transient is the TAS step from ~0 to
        // ~10 m/s in one pressure frame, which happens regardless of
        // whether we zeroed in the mean time).  Leaving updateTas_() in
        // charge keeps prevTas_ in sync with tas_, which is what its
        // derivative math needs.
        //
        // Reset the comp fade-in coefficient so the next rising edge
        // ramps in again from zero.  See issue #114.
        accelFwdComp_  = accelFwdFilter_.update(accelFwdCorr_);
        accelLatComp_  = accelLatFilter_.update(accelLatCorr_);
        accelVertComp_ = accelVertFilter_.update(accelVertCorr_);
        compFadeIn_    = 0.0f;
    }

    // 5. Run the chosen attitude filter.
    float SmoothedPitch = outputs_.pitchDeg;
    float SmoothedRoll  = outputs_.rollDeg;
    float DerivedAOA    = outputs_.derivedAoaDeg;
    float EarthVertG    = outputs_.earthVertG;

    if (cfg_.algorithm == Algorithm::Ekf6) {
        // Sign convention plumbing from OnSpeed's IMU pipeline to EKF6.
        // OnSpeed and EKF6 use opposite-sign conventions on three of
        // the seven measurement components; the negations below
        // bridge them. See EKF6.h's "OnSpeed convention mapping"
        // section for the full derivation.
        //
        // az: OnSpeed's accelVertComp_ is +1g for level flight — the
        //   "reaction force" convention pilots see in the CSV
        //   VerticalG column and on the web liveview. EKF6's
        //   measurement model uses the standard inertial-frame view
        //   where az = -g for level (the body is accelerating "up"
        //   relative to free-fall). Negate accelVertComp_ on input.
        //
        // p, q (roll, pitch rate): OnSpeed's internal IMU rates after
        //   axis sign-mapping are "+gyroPitchDps means nose-DOWN" /
        //   "+gyroRollDps means right-wing-UP" — opposite to EKF6's
        //   `phi_dot = p + ...` and `theta_dot = q*cph - r*sph` which
        //   expect "+p increases phi (right-wing-down)" / "+q
        //   increases theta (nose-up)" in the standard aerospace
        //   sense. Madgwick handles the same mismatch via its OUTPUT
        //   negation (-madgwick_.getPitch() / -madgwick_.getRoll()) +
        //   un-negated input; EKF6 doesn't negate output, so we
        //   negate the INPUT rates instead. Mirrors the Gen2 Octave
        //   reference's `-RollRateDegic` / `-PitchRateDegic` lines.
        //
        //   The CSV layer (LogCsv.cpp) emits PitchRate as
        //   -imuPitchRateDps so pilots reading the log see "+ = nose
        //   up" — same sign discipline as EKF6's, opposite of the
        //   internal raw value.
        //
        // r (yaw rate): no negation. Yaw rate doesn't appear in the
        //   accel measurement equations in OnSpeed's pre-compensated
        //   gravity-only model, so the sign-vs-firmware-convention
        //   only affects coupling of yaw rate into theta and phi via
        //   the Euler kinematic terms. The Gen2 Octave reference also
        //   leaves YawRateDegic un-negated.
        //
        // Accel: g → m/s². Gyro: deg/s → rad/s. gamma: deg → rad.
        float gamma_rad = onspeed::deg2rad(outputs_.flightPathDeg);

        onspeed::EKF6::Measurements meas = {
            /* ax */    accelFwdComp_   * kEkfGravityMps2,
            /* ay */    accelLatComp_   * kEkfGravityMps2,
            /* az */   -accelVertComp_  * kEkfGravityMps2,
            /* p  */   -onspeed::deg2rad(RollRateCorr),
            /* q  */   -onspeed::deg2rad(PitchRateCorr),
            /* r  */    onspeed::deg2rad(YawRateCorr),
            /* gamma */ gamma_rad
        };

        ekf6_.update(meas, dtSec);
        const onspeed::EKF6::State state = ekf6_.getState();

        SmoothedPitch = state.theta_deg();
        SmoothedRoll  = state.phi_deg();
        DerivedAOA    = state.alpha_deg();

        const float sph = std::sin(state.phi);
        const float cph = std::cos(state.phi);
        const float sth = std::sin(state.theta);
        const float cth = std::cos(state.theta);
        // EarthVertG: rotate body-frame accel back to earth frame and
        // subtract the level-state +1g, leaving the deviation from
        // level (in g). The `- 1.0f` is what pins this whole pipeline
        // to the +1g-for-level (production reaction-force) convention:
        // for level flight (theta=phi=0, accelVertCorr_=+1g), the
        // formula gives 0g of deviation.
        EarthVertG = -sth * accelFwdCorr_
                   + sph * cth * accelLatCorr_
                   + cph * cth * accelVertCorr_ - 1.0f;
    } else {
        madgwick_.setDeltaTime(dtSec);
        madgwick_.UpdateIMU(RollRateCorr, PitchRateCorr, YawRateCorr,
                            accelFwdComp_, accelLatComp_, accelVertComp_);

        SmoothedPitch = -madgwick_.getPitch();
        SmoothedRoll  = -madgwick_.getRoll();

        float q[4];
        madgwick_.getQuaternion(&q[0], &q[1], &q[2], &q[3]);

        // Same level-state subtraction as the EKF6 branch above —
        // pinned to +1g-for-level production convention.
        EarthVertG = 2.0f * (q[1]*q[3] - q[0]*q[2])                         * accelFwdCorr_  +
                     2.0f * (q[0]*q[1] + q[2]*q[3])                         * accelLatCorr_  +
                            (q[0]*q[0] - q[1]*q[1] - q[2]*q[2] + q[3]*q[3]) * accelVertCorr_ - 1.0f;
    }

    // 6. Kalman altitude/VSI from baro + earth-vertical-G.
    volatile float kalmanAltMeters = 0.0f;
    volatile float kalmanVsiMps    = 0.0f;
    kalman_.Update(onspeed::ft2m(in.sensors.paltFt),
                   onspeed::g2mps(EarthVertG),
                   dtSec,
                   &kalmanAltMeters, &kalmanVsiMps);

    // 7. Zero VSI when airspeed is not yet alive (rest-on-the-ground).
    //    Side effect: in EKF6 mode, reset the alpha covariance on the
    //    transition from below-threshold to above-threshold so the filter
    //    re-learns alpha from real gamma measurements.
    //
    //    Keyed off in.sensors.iasAlive rather than a raw 25 kt threshold so
    //    VSI/FlightPath/alpha-observability all share the same hysteretic
    //    air-data validity state as the compensation gate above.
    float kalVsiMpsForFlightPath = static_cast<float>(kalmanVsiMps);
    if (!in.sensors.iasAlive) {
        kalVsiMpsForFlightPath = 0.0f;
        iasWasBelowThreshold_ = true;
    } else if (iasWasBelowThreshold_ && cfg_.algorithm == Algorithm::Ekf6) {
        ekf6_.resetAlphaCovariance();
        iasWasBelowThreshold_ = false;
    } else {
        iasWasBelowThreshold_ = false;
    }

    // 8. Flight-path and (Madgwick-only) DerivedAOA.  The EKF6 path set
    //    DerivedAOA from its alpha state above.
    //
    //    The Kalman's VSI estimate may have built up while the gate was
    //    closed (baro keeps integrating during taxi), so unclamping in
    //    one frame produces an instant FlightPath step. Mirror PR #275:
    //    scale the VSI by the same compFadeIn_ coefficient that fades
    //    the accel comp factors, so FlightPath ramps in over τ ≈ 0.5 s
    //    instead of stepping. Same shape of fix, same time constant —
    //    it's the same gate-release transient on a different output.
    float FlightPath;
    if (in.sensors.iasAlive) {
        const float vsiFaded = kalVsiMpsForFlightPath * compFadeIn_;
        FlightPath = onspeed::rad2deg(onspeed::safeAsin(vsiFaded / tas_));
    } else {
        FlightPath = 0.0f;
    }

    if (cfg_.algorithm != Algorithm::Ekf6) {
        DerivedAOA = SmoothedPitch - FlightPath;
    }

    // ---- Publish outputs ----
    kalmanAltMeters_ = static_cast<float>(kalmanAltMeters);
    kalmanVsiMps_    = kalVsiMpsForFlightPath;

    outputs_.pitchDeg        = SmoothedPitch;
    outputs_.rollDeg         = SmoothedRoll;
    outputs_.flightPathDeg   = FlightPath;
    outputs_.derivedAoaDeg   = DerivedAOA;
    outputs_.tasMps          = tas_;
    outputs_.tasDotMps2      = tasDotSmoothed_;
    outputs_.kalmanAltFt     = onspeed::m2ft(kalmanAltMeters_);
    outputs_.kalmanVsiFpm    = onspeed::mps2fpm(kalmanVsiMps_);
    outputs_.earthVertG      = EarthVertG;
    outputs_.gyroRollFiltDps  = gRoll;
    outputs_.gyroPitchFiltDps = gPitch;
    outputs_.gyroYawFiltDps   = gYaw;
    outputs_.timestampUs      = in.imu.timestampUs;

    return outputs_;
}

}   // namespace onspeed::ahrs
