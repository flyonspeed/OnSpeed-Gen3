// replay/LogReplayEngine.h — platform-free per-row pipeline for SD log replay.
//
// Mirrors the AHRS task/engine split (sketch_common/tasks/AHRS.cpp +
// onspeed_core/ahrs/Ahrs.{h,cpp}). The FreeRTOS task wrapper
// (software/sketch_common/src/tasks/LogReplay.cpp) owns SD-card reads,
// button mocks, LED blink, and USB-CDC writes. This engine owns the
// per-row (LogRow -> ReplayStepResult) computation — AOA smoothing, flap
// index lookup, pressure-coefficient computation — so the same pipeline
// can serve three callers without platform dependencies:
//
//   1. Firmware (via the FreeRTOS task wrapper).
//   2. tools/regression/host_main.cpp `replay` subcommand.
//   3. WASM build (PLAN_WASM_CORE.md Step 2) via embind.
//
// Platform-free contract: this file and LogReplayEngine.cpp must not
// include Arduino.h, FreeRTOS.h, millis(), xSemaphoreTake(), or any other
// platform header. scripts/check_core_purity.sh enforces this.
//
// Rate-correction (PR 2 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md):
// The firmware reads IMU at 208 Hz and applies AOA EMA with alpha=0.060899
// (iAoaSmoothing=10 → alpha=0.1 in unit tests; flight builds use the fixed
// alpha from Globals.h). When a 50 Hz log is replayed, feeding one raw row
// per step would over-weight each sample 4× relative to flight. step() fixes
// this by upsampling to 208 Hz before driving the EMA: for each input row, it
// calls aoaCalc_.calculate() upsampleRatio_ times with linearly-interpolated
// pressure values between prevRow_ and the current row. Wire output cadence
// is unchanged — step() returns one ReplayStepResult per call. The
// synthesized flapsRawADC for old logs is handled in PR 3.

#ifndef ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H
#define ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H

#include <cstdint>

#include <aoa/AOACalculator.h>
#include <config/OnSpeedConfig.h>
#include <types/LogRow.h>

namespace onspeed::replay {

// ============================================================================
// ReplayStepResult — computed outputs for one log row.
//
// The task wrapper publishes these into the sketch globals (g_Sensors,
// g_Flaps, g_AHRS, g_fCoeffP, g_iDataMark) immediately after step()
// returns, then calls g_AudioPlay.UpdateTones(SnapshotActiveFlap()).
//
// Fields mirror the corresponding globals in sketch_common/src/Globals.h
// and tasks/LogReplay.cpp's ReadLogLine() write sites so the task
// wrapper body is a mechanical transcription.
// ============================================================================

struct ReplayStepResult {
    // --- Sensor state (fed to g_Sensors.*) ---
    float pfwdSmoothed   = 0.0f;
    float p45Smoothed    = 0.0f;
    float paltFt         = 0.0f;
    float iasKt          = 0.0f;
    bool  iasValid       = true;   // mirrors row.iasValid -> g_Sensors.bIasAlive
    float aoa            = 0.0f;   // smoothed AOA (degrees)
    float coeffP         = 0.0f;   // pressure coefficient

    // --- Flap state (fed to g_Flaps.*) ---
    int      flapsPos           = 0;   // snapped detent degrees -> g_Flaps.iPosition
    int      flapsIndex         = 0;   // resolved index into g_Config.aFlaps
    uint16_t flapsRawAdc        = 0;   // -> g_Flaps.uValue (only when flapsRawAdcPresent)
    bool     flapsRawAdcPresent = false;

    // --- AHRS state (fed to g_AHRS.*) ---
    float pitchDeg      = 0.0f;
    float rollDeg       = 0.0f;
    float flightPathDeg = 0.0f;
    float kalmanVSI     = 0.0f;   // m/s (g_AHRS.KalmanVSI convention)

    // --- IMU state (fed to g_pIMU->*) ---
    float imuForwardG      = 0.0f;   // Ax -> g_pIMU->Ax
    float imuLateralG      = 0.0f;   // Ay -> g_pIMU->Ay
    float imuVerticalG     = 0.0f;   // Az -> g_pIMU->Az
    float imuRollRateDps   = 0.0f;   // Gx -> g_pIMU->Gx
    float imuPitchRateDps  = 0.0f;   // Gy -> g_pIMU->Gy (un-negated raw)
    float imuYawRateDps    = 0.0f;   // Gz -> g_pIMU->Gz

    // --- Corrected accel (fed to g_AHRS.AccelLatCorr, AccelVertCorr) ---
    float accelLatCorr  = 0.0f;   // g_AHRS.AccelLatCorr  = g_pIMU->Ay
    float accelVertCorr = 0.0f;   // g_AHRS.AccelVertCorr = g_pIMU->Az

    // --- Data mark (fed to g_iDataMark) ---
    int dataMark = 0;
};

// ============================================================================
// LogReplayEngine
//
// Owns the per-row processing state: the AOA smoothing filter (an
// AOACalculator whose EMA state persists across rows — mirrors the live
// g_Sensors.AoaCalc instance).
//
// Constructor parameters:
//   cfg                 — active OnSpeedConfig (aFlaps, iAoaSmoothing)
//   logSampleRateHz     — sample rate of the log (50 or 208 Hz). Used to
//                         compute upsampleRatio_ = 208 / logSampleRateHz.
//                         At 208 Hz, ratio is 1 (no upsampling).
//   flapsRawAdcAvailable— true when the log's header carries the
//                         flapsRawADC column. Stored for PR 3; not yet
//                         used to synthesize ADC when absent.
// ============================================================================

class LogReplayEngine {
public:
    LogReplayEngine(const onspeed::config::OnSpeedConfig& cfg,
                    int  logSampleRateHz,
                    bool flapsRawAdcAvailable);

    // Process one CSV row: compute AOA, flap index, and all downstream
    // fields. Pure over engine state (only the AOA smoother has state).
    // Calling step() for N rows on a freshly-constructed engine produces
    // the same result as ReadLogLine() running N times against the same
    // rows from a fresh AOACalculator state. (The pre-extraction code
    // called g_Sensors.AoaCalc.calculate() directly — a boot-lifetime
    // singleton whose EMA state could carry across replays. The engine
    // owns its own AOACalculator, reset per OpenReplayLog. See PR #470.)
    ReplayStepResult step(const onspeed::LogRow& row);

    // Reset AOA smoother state. Call between independent replay sessions
    // (e.g. different log files) to avoid stale EMA from a prior run
    // contaminating the first rows.
    void reset();

private:
    const onspeed::config::OnSpeedConfig& cfg_;

    // Upsampling ratio for the AOA EMA: 208 / logSampleRateHz, clamped to [1, 208].
    // At 50 Hz log rate this is 4; at 208 Hz it is 1 (no upsampling).
    int upsampleRatio_;

    // Stored for PR 3 (synth ADC when column missing); not yet read by step().
    [[maybe_unused]] bool flapsRawAdcAvailable_;

    // AOA smoothing filter — mirrors g_Sensors.AoaCalc in the live path.
    // State persists across step() calls within one replay session.
    onspeed::AOACalculator aoaCalc_;

    // Previous row's pressure values, used to interpolate the upsample
    // sub-steps between prevRow_ and the current row. Zero-initialized on
    // construction; on the first call, the EMA sees constant input (the
    // current row repeated), which is fine for one sample.
    onspeed::LogRow prevRow_;

    // Resolve the aFlaps index for a given flap-position degree value.
    // Returns 0 (first entry) when no exact match is found, matching the
    // behavior of the sketch task wrapper's linear search with no match.
    int ResolveFlapIndex_(int flapPosDeg) const;
};

}  // namespace onspeed::replay

#endif  // ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H
