// AHRS.cpp — sketch-side wrapper around onspeed::ahrs::Ahrs.
//
// This file is the post-PR-3.2 thin-shim version of the original ~390-line
// AHRS class.  All filter math now lives in
// software/Libraries/onspeed_core/src/ahrs/Ahrs.{h,cpp}; this wrapper
// snapshots the global state (IMU, sensors, EFIS) into an AhrsInputs
// struct each frame, calls core's Step(), and mirrors the result into the
// public fields legacy consumers still read.

#include <math.h>

#include "src/Globals.h"
#include "src/drivers/IMU330.h"
#include "src/drivers/SensorIO.h"

using onspeed::accelPitch;
using onspeed::accelRoll;

// Smoothing alpha for the legacy EMA accel filters.  Kept here only so
// the filter objects on the AHRS class can be constructed with the same
// coefficient as core (cosmetic — `update()` is never called on them).
// Source the constant from the public AHRS header to keep all three sites
// in sync; see the static_assert in RateAdjustedAccelEma.h.
static constexpr float kAccSmoothing = onspeed::ahrs::kAccSmoothing;

// ----------------------------------------------------------------------------

namespace {

onspeed::ahrs::Algorithm AlgorithmFromConfig(int iAhrsAlgorithm)
{
    return (iAhrsAlgorithm == 1)
        ? onspeed::ahrs::Algorithm::Ekf6
        : onspeed::ahrs::Algorithm::Madgwick;
}

}   // namespace

// ----------------------------------------------------------------------------

onspeed::ahrs::AhrsConfig AHRS::MakeCfg_() const
{
    onspeed::ahrs::AhrsConfig cfg;
    cfg.pitchBiasDeg     = g_Config.fPitchBias;
    cfg.rollBiasDeg      = g_Config.fRollBias;
    cfg.algorithm        = AlgorithmFromConfig(g_Config.iAhrsAlgorithm);
    cfg.gyroSmoothingWindow = iGyroSmoothing_;
    cfg.imuSampleRateHz  = fImuSampleRate;
    cfg.pressureSampleRateHz = static_cast<float>(kPressureSampleRateHz);
    return cfg;
}

// ----------------------------------------------------------------------------

onspeed::AhrsInputs AHRS::SnapshotInputs_() const
{
    onspeed::AhrsInputs in;
    in.imu     = g_pIMU->Snapshot();
    in.sensors = g_Sensors.Snapshot();
    in.iasUpdateTimestampUs = g_Sensors.uIasUpdateUs;

    // Replicate the legacy OAT-source priority (AHRS.cpp:147-161):
    //   * Use EFIS OAT when calibration source is EFIS, EFIS reads are
    //     enabled, and the EFIS data has been seen within 2 seconds.
    //   * Otherwise, fall back to the internal DS18B20 sensor when the
    //     bOatSensor toggle is on.
    in.useEfisOat = g_Config.bCalSourceEfis
                  && g_Config.bReadEfisData
                  && g_EfisSerial.IsDataFresh(2000);
    in.efisOatCelsius = in.useEfisOat ? g_EfisSerial.suEfis.OAT : 0.0f;
    in.useInternalOat = g_Config.bOatSensor;

    return in;
}

// ----------------------------------------------------------------------------

void AHRS::PublishCoreState_()
{
    const onspeed::AhrsOutputs& out = core_.latest();

    SmoothedPitch = out.pitchDeg;
    SmoothedRoll  = out.rollDeg;
    FlightPath    = out.flightPathDeg;
    DerivedAOA    = out.derivedAoaDeg;
    EarthVertG    = out.earthVertG;

    fTAS           = core_.tasMps();
    TASdotSmoothed = out.tasDotMps2;

    // KalmanAlt and KalmanVSI are stored in legacy units (meters and m/s
    // respectively) for source compatibility.  Core exposes the raw
    // metric values via kalmanAltMeters()/kalmanVsiMps() to avoid the
    // m -> ft -> m round-trip the published outputs would otherwise
    // require.  Consumers wrap these in m2ft()/mps2fpm() — same as
    // before extraction.
    KalmanAlt = core_.kalmanAltMeters();
    KalmanVSI = core_.kalmanVsiMps();

    AccelFwdCorr  = core_.accelFwdCorrG();
    AccelLatCorr  = core_.accelLatCorrG();
    AccelVertCorr = core_.accelVertCorrG();

    // Mirror smoothed accel values into the legacy EMA filter objects so
    // consumers using `g_AHRS.AccelFwdFilter.get()` see the same numbers
    // they always did.  `seed()` overwrites the value without touching
    // the alpha — a no-op on the underlying filter chain since
    // `update()` is never invoked on these mirror filters anymore.
    AccelFwdFilter .seed(core_.accelFwdSmoothedG());
    AccelLatFilter .seed(core_.accelLatSmoothedG());
    AccelVertFilter.seed(core_.accelVertSmoothedG());

    gRoll  = out.gyroRollFiltDps;
    gPitch = out.gyroPitchFiltDps;
    gYaw   = out.gyroYawFiltDps;
}

// ----------------------------------------------------------------------------

AHRS::AHRS(int gyroSmoothing)
    : AccelFwdFilter(kAccSmoothing)
    , AccelLatFilter(kAccSmoothing)
    , AccelVertFilter(kAccSmoothing)
    , core_(onspeed::ahrs::AhrsConfig{
          /* pitchBiasDeg */          0.0f,
          /* rollBiasDeg  */          0.0f,
          /* algorithm    */          onspeed::ahrs::Algorithm::Madgwick,
          /* gyroSmoothingWindow */   gyroSmoothing,
          /* imuSampleRateHz     */   kImuSampleRateHz,
          /* pressureSampleRateHz */  static_cast<float>(kPressureSampleRateHz),
      })
    , iGyroSmoothing_(gyroSmoothing)
    , gOnsetFilter_(0.25f)  // 250 ms tau — same as legacy DisplaySerial
{
    AccelFwdCorr   = 0.0f;
    AccelLatCorr   = 0.0f;
    AccelVertCorr  = -1.0f;
    SmoothedPitch  = 0.0f;
    SmoothedRoll   = 0.0f;
    TASdotSmoothed = 0.0f;
    KalmanAlt      = 0.0f;
    KalmanVSI      = 0.0f;
    FlightPath     = 0.0f;
    EarthVertG     = 0.0f;
    DerivedAOA     = 0.0f;
    gRoll          = 0.0f;
    gPitch         = 0.0f;
    gYaw           = 0.0f;
    fImuSampleRate = kImuSampleRateHz;
    fImuDeltaTime  = 1.0f / fImuSampleRate;
    fTAS           = 0.0f;
    gOnsetRate     = 0.0f;

    AccelFwdFilter .seed(0.0f);
    AccelLatFilter .seed(0.0f);
    AccelVertFilter.seed(-1.0f);
}

// ----------------------------------------------------------------------------

void AHRS::Init(float fSampleRate)
{
    fImuSampleRate = fSampleRate;
    fImuDeltaTime  = 1.0f / fSampleRate;

    // Reconfigure core in case bias/algorithm changed since last Init.
    core_.Reconfigure(MakeCfg_());

    // Snapshot a "seed frame" from the latest IMU/sensor reads, then
    // hand it to core for filter initialization.  Mirrors the legacy
    // AHRS::Init body, which read g_pIMU->PitchAC()/RollAC() directly.
    const onspeed::AhrsInputs seed = SnapshotInputs_();
    core_.Init(seed, g_Sensors.Palt);

    // Publish initial outputs (pitch/roll seeded by core::Init).
    SmoothedPitch = core_.latest().pitchDeg;
    SmoothedRoll  = core_.latest().rollDeg;

    g_Log.printf(MsgLog::EnAHRS, MsgLog::EnWarning,
        "AHRS Init (%s, pitch bias %.1f, roll bias %.1f)\n",
        g_Config.iAhrsAlgorithm == 1 ? "EKF6" : "Madgwick",
        g_Config.fPitchBias, g_Config.fRollBias);
}

// ----------------------------------------------------------------------------

void AHRS::Process()
{
    Process(fImuDeltaTime);
}

// ----------------------------------------------------------------------------

void AHRS::Process(float fDeltaTimeSeconds)
{
    const onspeed::AhrsInputs in = SnapshotInputs_();
    core_.Step(in, fDeltaTimeSeconds);
    PublishCoreState_();

    // Tick the G-onset rate filter against the freshly-published
    // AccelVertFilter value.  Mirrors the legacy DisplaySerial path
    // (which fed `g_AHRS.AccelVertFilter.get()` to a function-static
    // GOnsetFilter ticked at 20 Hz); now the filter lives here so
    // both M5 wire format and LiveView JSON read the same smoothed
    // value, ticked at the AHRS rate (≈208 Hz on hardware).  Tau is
    // 250 ms, so the higher tick rate just gives a slightly smoother
    // output without changing the time constant.
    gOnsetRate = gOnsetFilter_.Update(AccelVertFilter.get(), fDeltaTimeSeconds);
}

// ----------------------------------------------------------------------------

float AHRS::PitchWithBias()         { return accelPitch(AccelFwdCorr,         AccelLatCorr,         AccelVertCorr); }
float AHRS::PitchWithBiasSmth()     { return accelPitch(AccelFwdFilter.get(), AccelLatFilter.get(), AccelVertFilter.get()); }
float AHRS::RollWithBias()          { return accelRoll (AccelFwdCorr,         AccelLatCorr,         AccelVertCorr); }
float AHRS::RollWithBiasSmth()      { return accelRoll (AccelFwdFilter.get(), AccelLatFilter.get(), AccelVertFilter.get()); }
