// Ahrs.cpp — see Ahrs.h.
//
// Four-stage AHRS pipeline:
//
//   raw sensors ──► AHRS algorithm ──► smoothing ──► outputs
//
// Ahrs::Step orchestrates the four stages; the algorithm itself
// (Madgwick / EKF6 / future EKFQ) lives behind a uniform Inputs/Outputs
// seam and owns its internal pre-filtering and gating.

#include <ahrs/Ahrs.h>

#include <cmath>

#include <util/OnSpeedTypes.h>

namespace onspeed::ahrs {

namespace {

// IAS-derivative EMA alpha at the pressure-sensor cadence. Kept at the
// legacy value so the regression-snapshot golden matches bit-for-bit.
// Not algorithm-tunable; if an algorithm wants a different TASdot
// smoothing it can apply its own EMA internally on `in.tasDotMps2`.
constexpr float kIasSmoothing      = 0.0179f;
constexpr float kIasTauFactor      = (1.0f / kIasSmoothing) - 1.0f;

// Kalman tuning for the baro+accel altitude/VSI smoother.
constexpr float kKalZVariance      = 0.79078f;
constexpr float kKalAccelVariance  = 1.0f;
constexpr float kKalAccelBiasVar   = 1e-11f;

// OAT validity window matches legacy logic: |T_C| < 100.
inline bool oatInBand(float c) { return c > -100.0f && c < 100.0f; }

}   // namespace

// ----------------------------------------------------------------------------

Ahrs::Ahrs(const AhrsConfig& cfg)
    : cfg_(cfg)
    , imuDeltaTime_(1.0f / cfg.imuSampleRateHz)
    , gyroRollAvg_(cfg.gyroSmoothingWindow > 0 ? cfg.gyroSmoothingWindow : 1)
    , gyroPitchAvg_(cfg.gyroSmoothingWindow > 0 ? cfg.gyroSmoothingWindow : 1)
    , gyroYawAvg_(cfg.gyroSmoothingWindow > 0 ? cfg.gyroSmoothingWindow : 1)
    , accelFwdWireFilter_(kAccSmoothing)
    , accelLatWireFilter_(kAccSmoothing)
    , accelVertWireFilter_(kAccSmoothing)
{
    // Seed the wire-side accel EMA with the "level on the ground" rest
    // state (Z = +1 g, X = Y = 0) so the first frames before any IMU
    // sample lands yield sane wire values rather than degenerate
    // zeros. +1g for level matches OnSpeed's reaction-force convention
    // (what pilots see in the CSV "VerticalG" column).
    accelFwdWireFilter_.seed(0.0f);
    accelLatWireFilter_.seed(0.0f);
    accelVertWireFilter_.seed(+1.0f);

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
    // Compute the seed pitch/roll from the supplied IMU sample using
    // the same accelPitch/accelRoll helpers IMU::PitchAC / RollAC uses,
    // then add the installation bias.  Mirrors the legacy AHRS::Init:
    //
    //   SmoothedPitch = g_pIMU->PitchAC() + g_Config.fPitchBias;
    //   SmoothedRoll  = g_pIMU->RollAC()  + g_Config.fRollBias;
    const float fSeedPitch = onspeed::accelPitch(
        seedFrame.imu.accelXG, seedFrame.imu.accelYG, seedFrame.imu.accelZG)
        + cfg_.pitchBiasDeg;
    const float fSeedRoll  = onspeed::accelRoll(
        seedFrame.imu.accelXG, seedFrame.imu.accelYG, seedFrame.imu.accelZG)
        + cfg_.rollBiasDeg;

    recomputeBiasTrig_();

    // Dispatch to the active algorithm's Init.
    if (cfg_.algorithm == Algorithm::Ekf6) {
        ekf6_.Init(fSeedPitch, fSeedRoll);
    } else {
        madgwick_.Init(cfg_.imuSampleRateHz, fSeedPitch, fSeedRoll);
    }

    // Seed the Kalman altitude/VSI filter with the supplied baro alt.
    kalman_.Configure(kKalZVariance, kKalAccelVariance, kKalAccelBiasVar,
                      onspeed::ft2m(seedPaltFt), 0.0f, 0.0f);

    // Seed published outputs so a consumer reading before the first
    // Step sees the level-on-the-ground attitude rather than zeros.
    outputs_.pitchDeg = fSeedPitch;
    outputs_.rollDeg  = fSeedRoll;

    // NOTE: TAS state (tas_, prevTas_, tasDotSmoothed_,
    // lastIasUpdateUs_) is intentionally NOT reset here. Legacy
    // AHRS::Init() also did not reset it. Init() is called from the
    // web UI on every config save; zeroing TAS would inject a
    // one-frame forward-accel-comp glitch during deceleration. The
    // constructor initializes these fields once at boot; we leave
    // them alone on Init().
}

// ----------------------------------------------------------------------------

void Ahrs::updateTas_(const AhrsInputs& in)
{
    // Stage-1 TAS computation. Density-correct IAS → TAS using OAT and
    // pressure altitude; smooth the derivative with a variable-rate
    // EMA at IAS update cadence. Two density-correction powf calls are
    // expensive — running at 50 Hz instead of 208 Hz saves ~150 µs/cycle.
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

        // Guard pow() base values: a negative/zero base with a fractional
        // exponent returns NaN. Bad OAT could make fOAT_k <= 0; extreme
        // density altitude could make the IAS divisor <= 0.
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

    if (lastIasUpdateUs_ == 0) {
        lastIasUpdateUs_ = uIasUpdateUs;
        prevTas_ = tas_;
        tasDotSmoothed_ = 0.0f;
    } else {
        // Unsigned subtract handles the 71-minute micros() wrap: diff
        // is always the positive elapsed delta.
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
    // Use measured dt when available; fall back to nominal sample rate
    // if dt is invalid. Same NaN/inf/<=0/giant-step guards as legacy.
    if (std::isnan(dtSec) || std::isinf(dtSec) || dtSec <= 0.0f) {
        dtSec = imuDeltaTime_;
    }
    if (dtSec > 4.0f * imuDeltaTime_) {
        dtSec = imuDeltaTime_;
    }

    // ================================================================
    // Stage 1 — Sensor.
    // ================================================================

    // 1a. TAS update (density correction + EMA-smoothed derivative) at
    //     pressure-sensor cadence (~50 Hz).
    updateTas_(in);

    // 1b. Installation bias correction. Yaw bias is always zero, so
    //     sin(yaw)=0 and cos(yaw)=1 are folded into the rotation.
    const float sp = fSinPitch_, cp = fCosPitch_;
    const float sr = fSinRoll_,  cr = fCosRoll_;

    const float RollRateCorr  = in.imu.gyroRollDps  *  cp +
                                in.imu.gyroPitchDps * (sr * sp) +
                                in.imu.gyroYawDps   * (cr * sp);
    const float PitchRateCorr = in.imu.gyroPitchDps *  cr +
                                in.imu.gyroYawDps   * -sr;
    const float YawRateCorr   = in.imu.gyroRollDps  * -sp +
                                in.imu.gyroPitchDps * (sr * cp) +
                                in.imu.gyroYawDps   * (cp * cr);

    accelVertCorr_ = -in.imu.accelXG *  sp +
                      in.imu.accelYG * (sr * cp) +
                      in.imu.accelZG * (cr * cp);
    accelLatCorr_  =  in.imu.accelYG *  cr +
                      in.imu.accelZG * -sr;
    accelFwdCorr_  =  in.imu.accelXG *  cp +
                      in.imu.accelYG * (sr * sp) +
                      in.imu.accelZG * (cr * sp);

    // ================================================================
    // Stage 2 — AHRS algorithm.
    //
    // The active algorithm owns its own internal pre-filtering, gating,
    // and fusion. We hand it raw-corrected sensors + sensor-stage TAS
    // state + IAS for its own gate decision.
    // ================================================================

    float SmoothedPitch = outputs_.pitchDeg;
    float SmoothedRoll  = outputs_.rollDeg;
    float DerivedAOA    = outputs_.derivedAoaDeg;
    float EarthVertG    = outputs_.earthVertG;

    if (cfg_.algorithm == Algorithm::Ekf6) {
        const Ekf6Pipeline::Inputs ekfIn = {
            /* accelFwdCorrG    */ accelFwdCorr_,
            /* accelLatCorrG    */ accelLatCorr_,
            /* accelVertCorrG   */ accelVertCorr_,
            /* rollRateCorrDps  */ RollRateCorr,
            /* pitchRateCorrDps */ PitchRateCorr,
            /* yawRateCorrDps   */ YawRateCorr,
            /* tasMps           */ tas_,
            /* tasDotMps2       */ tasDotSmoothed_,
            /* iasKt            */ in.sensors.iasKt,
            /* gammaRad         */ onspeed::deg2rad(outputs_.flightPathDeg),
            /* dtSec            */ dtSec,
        };
        const Ekf6Pipeline::Outputs ekfOut = ekf6_.Step(ekfIn);
        SmoothedPitch   = ekfOut.pitchDeg;
        SmoothedRoll    = ekfOut.rollDeg;
        DerivedAOA      = ekfOut.derivedAoaDeg;
        EarthVertG      = ekfOut.earthVertG;
        algoCompFwdG_   = ekfOut.accelFwdCompG;
        algoCompLatG_   = ekfOut.accelLatCompG;
        algoCompVertG_  = ekfOut.accelVertCompG;
        algoCompFadeIn_ = ekfOut.compFadeIn;
    } else {
        const Madgwick::Inputs madIn = {
            /* accelFwdCorrG    */ accelFwdCorr_,
            /* accelLatCorrG    */ accelLatCorr_,
            /* accelVertCorrG   */ accelVertCorr_,
            /* rollRateCorrDps  */ RollRateCorr,
            /* pitchRateCorrDps */ PitchRateCorr,
            /* yawRateCorrDps   */ YawRateCorr,
            /* tasMps           */ tas_,
            /* tasDotMps2       */ tasDotSmoothed_,
            /* iasKt            */ in.sensors.iasKt,
            /* dtSec            */ dtSec,
        };
        const Madgwick::Outputs madOut = madgwick_.Step(madIn);
        SmoothedPitch   = madOut.pitchDeg;
        SmoothedRoll    = madOut.rollDeg;
        EarthVertG      = madOut.earthVertG;
        algoCompFwdG_   = madOut.accelFwdCompG;
        algoCompLatG_   = madOut.accelLatCompG;
        algoCompVertG_  = madOut.accelVertCompG;
        algoCompFadeIn_ = madOut.compFadeIn;
        // Madgwick doesn't track alpha; DerivedAOA is computed from
        // SmoothedPitch and FlightPath in the output stage below.
    }

    // ================================================================
    // Stage 3 — Smoothing (wire-spec).
    // ================================================================

    // 3a. Wire-side accel EMA. Algorithm-blind. Tracks the M5/huVVer
    //     wire protocol contract τ. NOT used by any AHRS algorithm.
    accelFwdWireFilter_.update(accelFwdCorr_);
    accelLatWireFilter_.update(accelLatCorr_);
    accelVertWireFilter_.update(accelVertCorr_);

    // 3b. Display-rate gyro running means (wire-spec).
    gyroRollAvg_.addValue(RollRateCorr);
    const float gRoll  = gyroRollAvg_.getFastAverage();
    gyroPitchAvg_.addValue(PitchRateCorr);
    const float gPitch = gyroPitchAvg_.getFastAverage();
    gyroYawAvg_.addValue(YawRateCorr);
    const float gYaw   = gyroYawAvg_.getFastAverage();

    // 3c. Kalman altitude/VSI from baro + earth-vert-G. The
    //     measurement-noise tuning here is wire/log-spec (a future
    //     algorithm could bypass this Kalman entirely and produce its
    //     own altitude/VSI; for now both algorithms feed it).
    volatile float kalmanAltMeters = 0.0f;
    volatile float kalmanVsiMps    = 0.0f;
    kalman_.Update(onspeed::ft2m(in.sensors.paltFt),
                   onspeed::g2mps(EarthVertG),
                   dtSec,
                   &kalmanAltMeters, &kalmanVsiMps);

    // ================================================================
    // Stage 4 — Outputs.
    // ================================================================

    // 4a. VSI zeroing on the ground. Keyed off in.sensors.iasAlive — the
    //     user-facing display gate — so VSI/FlightPath share visibility
    //     state with the IAS readout. (Algorithm-internal gating is
    //     separately handled inside each algorithm's IAS gate.)
    float kalVsiMpsForFlightPath = static_cast<float>(kalmanVsiMps);
    if (!in.sensors.iasAlive) {
        kalVsiMpsForFlightPath = 0.0f;
    }

    // 4b. FlightPath ramp-in. After the iasAlive rising edge, the
    //     Kalman's VSI may have built up while the gate was closed;
    //     unclamping in one frame produces an instant FlightPath step.
    //     Scale by the active algorithm's compFadeIn (each algorithm
    //     ramps its own comp factors with the same shape) so the
    //     FlightPath ramps in over the algorithm's τ instead of
    //     stepping. See issue #114 / PR #275.
    //
    //     Until A2's decoupling work lands, both algorithms use the
    //     legacy compFadeIn shape against in.sensors.iasAlive, so the
    //     ramp behaviour matches master bit-for-bit.
    float FlightPath;
    if (in.sensors.iasAlive) {
        const float vsiFaded = kalVsiMpsForFlightPath * algoCompFadeIn_;
        FlightPath = onspeed::rad2deg(onspeed::safeAsin(vsiFaded / tas_));
    } else {
        FlightPath = 0.0f;
    }

    // 4c. DerivedAOA. Madgwick doesn't track alpha as a state, so it's
    //     computed from SmoothedPitch − FlightPath. EKF6 tracks alpha
    //     directly and set DerivedAOA above; leave it alone.
    if (cfg_.algorithm != Algorithm::Ekf6) {
        DerivedAOA = SmoothedPitch - FlightPath;
    }

    // 4d. Publish.
    kalmanAltMeters_ = static_cast<float>(kalmanAltMeters);
    kalmanVsiMps_    = kalVsiMpsForFlightPath;

    outputs_.pitchDeg         = SmoothedPitch;
    outputs_.rollDeg          = SmoothedRoll;
    outputs_.flightPathDeg    = FlightPath;
    outputs_.derivedAoaDeg    = DerivedAOA;
    outputs_.tasMps           = tas_;
    outputs_.tasDotMps2       = tasDotSmoothed_;
    outputs_.kalmanAltFt      = onspeed::m2ft(kalmanAltMeters_);
    outputs_.kalmanVsiFpm     = onspeed::mps2fpm(kalmanVsiMps_);
    outputs_.earthVertG       = EarthVertG;
    outputs_.gyroRollFiltDps  = gRoll;
    outputs_.gyroPitchFiltDps = gPitch;
    outputs_.gyroYawFiltDps   = gYaw;
    outputs_.timestampUs      = in.imu.timestampUs;

    return outputs_;
}

}   // namespace onspeed::ahrs
