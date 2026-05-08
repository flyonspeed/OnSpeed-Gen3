// replay/LogReplayEngine.cpp — platform-free per-row pipeline for SD log replay.
//
// See LogReplayEngine.h for the architecture overview.
//
// This implementation mirrors ReadLogLine() in
// software/sketch_common/src/tasks/LogReplay.cpp, extracted verbatim
// except that the global side-effects (g_Sensors.*, g_Flaps.*, g_AHRS.*,
// g_pIMU->*, g_fCoeffP, g_iDataMark) are written into a ReplayStepResult
// returned by value instead.  The calling task wrapper publishes that
// struct into the sketch globals and then calls
// g_AudioPlay.UpdateTones(SnapshotActiveFlap()).
//
// Behavior is preserved bit-for-bit. No EMA rate correction, no ADC
// synthesis — those are PRs 2 and 3.

#include <replay/LogReplayEngine.h>

#include <util/OnSpeedTypes.h>

using onspeed::pressureCoeff;
using onspeed::fpm2mps;
using onspeed::AOACalculatorResult;
using onspeed::config::OnSpeedConfig;

namespace onspeed::replay {

// ============================================================================

LogReplayEngine::LogReplayEngine(const OnSpeedConfig& cfg,
                                 int  logSampleRateHz,
                                 bool flapsRawAdcAvailable)
    : cfg_(cfg)
    , logSampleRateHz_(logSampleRateHz)
    , flapsRawAdcAvailable_(flapsRawAdcAvailable)
    , aoaCalc_(cfg.iAoaSmoothing)
{
}

// ============================================================================

void LogReplayEngine::reset()
{
    aoaCalc_.reset();
}

// ============================================================================

int LogReplayEngine::ResolveFlapIndex_(int flapPosDeg) const
{
    for (int i = 0; i < (int)cfg_.aFlaps.size(); i++)
    {
        if (flapPosDeg == cfg_.aFlaps[i].iDegrees)
            return i;
    }
    return 0;   // no match: fall back to first entry (same as task)
}

// ============================================================================

ReplayStepResult LogReplayEngine::step(const onspeed::LogRow& row)
{
    ReplayStepResult out;

    // --- Unpack pressure, flap, and sensor fields from the row ---
    // Mirrors the global write-back in ReadLogLine() lines 222-252.

    out.pfwdSmoothed       = row.pfwdSmoothed;
    out.p45Smoothed        = row.p45Smoothed;
    out.flapsPos           = row.flapsPos;
    out.flapsRawAdcPresent = row.flapsRawAdcPresent;
    if (row.flapsRawAdcPresent)
        out.flapsRawAdc    = row.flapsRawAdc;

    // Pressure coefficient from smoothed pitot + 45-degree sensor.
    // Mirrors: g_fCoeffP = pressureCoeff(g_Sensors.PfwdSmoothed, g_Sensors.P45Smoothed)
    out.coeffP = pressureCoeff(out.pfwdSmoothed, out.p45Smoothed);

    // Resolve flap index. Mirrors the for-loop in ReadLogLine() that sets
    // g_Flaps.iIndex when g_Flaps.iPosition matches a configured detent.
    out.flapsIndex = ResolveFlapIndex_(out.flapsPos);

    // --- Air data ---
    out.paltFt  = row.paltFt;
    out.iasKt   = row.iasKt;
    out.iasValid = row.iasValid;

    // --- Data mark ---
    out.dataMark = row.dataMark;

    // --- Kalman VSI: convert fpm from log to m/s for g_AHRS.KalmanVSI ---
    // Mirrors: g_AHRS.KalmanVSI = fpm2mps(row.vsiFpm)
    out.kalmanVSI = fpm2mps(row.vsiFpm);

    // --- IMU state ---
    out.imuForwardG     = row.imuForwardG;
    out.imuLateralG     = row.imuLateralG;
    out.imuVerticalG    = row.imuVerticalG;
    out.imuRollRateDps  = row.imuRollRateDps;
    out.imuPitchRateDps = row.imuPitchRateDps;  // un-negated raw value
    out.imuYawRateDps   = row.imuYawRateDps;

    // --- AHRS state ---
    out.pitchDeg      = row.pitchDeg;
    out.rollDeg       = row.rollDeg;
    out.flightPathDeg = row.flightPathDeg;

    // --- Corrected accel: g_AHRS.AccelLatCorr  = g_pIMU->Ay ---
    //                      g_AHRS.AccelVertCorr = g_pIMU->Az ---
    out.accelLatCorr  = row.imuLateralG;
    out.accelVertCorr = row.imuVerticalG;

    // --- AOA calculation ---
    // Mirrors ReadLogLine() lines 269-273. Uses the resolved flap index to
    // select the calibration curve. aoaCalc_ holds smoothing state across
    // calls just as g_Sensors.AoaCalc does in the live path.
    if (!cfg_.aFlaps.empty())
    {
        const onspeed::SuCalibrationCurve& curve =
            cfg_.aFlaps[out.flapsIndex].AoaCurve;
        AOACalculatorResult result =
            aoaCalc_.calculate(out.pfwdSmoothed, out.p45Smoothed, curve);
        out.aoa    = result.aoa;
        out.coeffP = result.coeffP;
    }
    // else: no flap config — aoa stays 0.0, coeffP from pressureCoeff above

    return out;
}

}  // namespace onspeed::replay
