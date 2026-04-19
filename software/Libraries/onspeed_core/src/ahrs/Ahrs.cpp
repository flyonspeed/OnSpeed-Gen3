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
constexpr float kAccSmoothing      = 0.060899f;            // EMA alpha for accels
constexpr float kIasSmoothing      = 0.0179f;              // EMA alpha for IAS-derived TAS
constexpr float kIasTauFactor      = (1.0f / kIasSmoothing) - 1.0f;
constexpr float kMinIasForFlightPath = 25.0f;              // kt
constexpr float kEkfGravityMps2    = 9.80665f;
constexpr float kKalZVariance      = 0.79078f;
constexpr float kKalAccelVariance  = 26.0638f;
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
    // Seed accel filters with the legacy "level on the ground" rest state
    // (Z=-1g, X=Y=0) so the first frames before the IMU has produced a
    // sample yield a sane attitude rather than a degenerate (0,0,0).
    accelFwdFilter_.seed(0.0f);
    accelLatFilter_.seed(0.0f);
    accelVertFilter_.seed(-1.0f);

    accelVertCorr_ = -1.0f;

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

    // Reset TAS state so the first Step does not see stale derivative.
    tas_ = 0.0f;
    prevTas_ = 0.0f;
    tasDotSmoothed_ = 0.0f;
    lastIasUpdateUs_ = 0;
    iasWasBelowThreshold_ = true;
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

    // 4. Linear acceleration compensation: forward, centripetal lateral,
    //    centripetal vertical.  When EKF6 is active, use its bias-corrected
    //    rates from the previous timestep for more consistent compensation.
    const float AccelFwdCompFactor = onspeed::mps2g(tasDotSmoothed_);

    float fYawRateForComp   = YawRateCorr;
    float fPitchRateForComp = PitchRateCorr;
    if (cfg_.algorithm == Algorithm::Ekf6) {
        const onspeed::EKF6::State prevState = ekf6_.getState();
        fYawRateForComp   = YawRateCorr   - onspeed::rad2deg(prevState.br);
        fPitchRateForComp = PitchRateCorr - onspeed::rad2deg(prevState.bq);
    }
    const float AccelLatCompFactor  =
        onspeed::mps2g(onspeed::deg2rad(tas_ * fYawRateForComp));
    const float AccelVertCompFactor =
        onspeed::mps2g(onspeed::deg2rad(tas_ * fPitchRateForComp));

    accelFwdComp_  = accelFwdFilter_.update(accelFwdCorr_)   - AccelFwdCompFactor;
    accelLatComp_  = accelLatFilter_.update(accelLatCorr_)   - AccelLatCompFactor;
    accelVertComp_ = accelVertFilter_.update(accelVertCorr_) + AccelVertCompFactor;

    // 5. Run the chosen attitude filter.
    float SmoothedPitch = outputs_.pitchDeg;
    float SmoothedRoll  = outputs_.rollDeg;
    float DerivedAOA    = outputs_.derivedAoaDeg;
    float EarthVertG    = outputs_.earthVertG;

    if (cfg_.algorithm == Algorithm::Ekf6) {
        // EKF6 expects aerospace sign convention:
        //   - az = -g in level flight (sensor measures reaction to gravity)
        //   - OnSpeed pipeline uses NED where az=+g, so negate vertical
        //   - Accel in m/s^2 (AccelComp in g, multiply by 9.80665)
        //   - Gyro in rad/s (RateCorr in deg/s, convert)
        //   - gamma in radians
        float gamma_rad = onspeed::deg2rad(outputs_.flightPathDeg);

        onspeed::EKF6::Measurements meas = {
            /* ax */    accelFwdComp_  * kEkfGravityMps2,
            /* ay */    accelLatComp_  * kEkfGravityMps2,
            /* az */   -accelVertComp_ * kEkfGravityMps2,
            /* p  */    onspeed::deg2rad(RollRateCorr),
            /* q  */    onspeed::deg2rad(PitchRateCorr),
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
    float kalVsiMpsForFlightPath = static_cast<float>(kalmanVsiMps);
    if (in.sensors.iasKt < kMinIasForFlightPath) {
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
    float FlightPath;
    if (in.sensors.iasKt >= kMinIasForFlightPath) {
        FlightPath = onspeed::rad2deg(onspeed::safeAsin(kalVsiMpsForFlightPath / tas_));
    } else {
        FlightPath = 0.0f;
    }

    if (cfg_.algorithm != Algorithm::Ekf6) {
        DerivedAOA = SmoothedPitch - FlightPath;
    }

    // ---- Publish outputs ----
    outputs_.pitchDeg        = SmoothedPitch;
    outputs_.rollDeg         = SmoothedRoll;
    outputs_.flightPathDeg   = FlightPath;
    outputs_.derivedAoaDeg   = DerivedAOA;
    outputs_.tasMps          = tas_;
    outputs_.tasDotMps2      = tasDotSmoothed_;
    outputs_.kalmanAltFt     = onspeed::m2ft(static_cast<float>(kalmanAltMeters));
    outputs_.kalmanVsiFpm    = onspeed::mps2fpm(kalVsiMpsForFlightPath);
    outputs_.earthVertG      = EarthVertG;
    outputs_.gyroRollFiltDps  = gRoll;
    outputs_.gyroPitchFiltDps = gPitch;
    outputs_.gyroYawFiltDps   = gYaw;
    outputs_.timestampUs      = in.imu.timestampUs;

    return outputs_;
}

}   // namespace onspeed::ahrs
