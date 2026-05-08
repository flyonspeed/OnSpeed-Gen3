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
// Flap-pot ADC synthesis (Sub-task 3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md):
// Old logs (pre-PR-#221) carry only the snapped detent integer (flapsPos),
// not the raw flap-pot ADC. Without it the L/Dmax pip jumps at every detent
// transition. When flapsRawAdcAvailable is false, prepare() synthesizes a
// smoothstep sweep across each detent transition using two passes:
//   Pass 1: fill every row with the current detent's nominal pot value.
//   Pass 2: paint smoothstep windows (±kSynthHalfWindowSec centred on each
//           transition tick) that interpolate from the previous pot value to
//           the new pot value.
// Math is a verbatim port of tools/web/lib/replay/logReplay.js::synthLeverSweep.
// When flapsRawAdcAvailable is true, prepare() is a no-op and step() reads
// the ADC value from the row directly.

#ifndef ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H
#define ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H

#include <cstdint>
#include <vector>

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
//   logSampleRateHz     — sample rate of the log (50 or 208 Hz).
//   flapsRawAdcAvailable— true when the log's header carries the
//                         flapsRawADC column. When false, prepare() must
//                         be called before the first step() to synthesize
//                         ADC values for all rows.
//
// Workflow when flapsRawAdcAvailable is false (old logs):
//   1. Construct the engine.
//   2. Call prepare(rows) with the complete row sequence. This runs the
//      two-pass synth and stores the result internally.
//   3. Call step(rows[i]) for each i in order. step() reads the synth
//      value from the pre-computed array using an internal row index.
//
// Workflow when flapsRawAdcAvailable is true (new logs):
//   1. Construct the engine.
//   2. prepare() is optional (a no-op).
//   3. Call step(row) for each row. step() reads flapsRawAdc from the row.
// ============================================================================

// Half-window for the smoothstep sweep in seconds. Matches JS reference
// (synthLeverSweep sweepWindowSec=4.0 → half = sweepWindowSec/2 = 2.0).
// At 50 Hz this gives ±100 ticks on each side of the snap tick.
static constexpr float kSynthHalfWindowSec = 2.0f;

class LogReplayEngine {
public:
    LogReplayEngine(const onspeed::config::OnSpeedConfig& cfg,
                    int  logSampleRateHz,
                    bool flapsRawAdcAvailable);

    // Prepare the synth ADC table from the complete row sequence.
    //
    // When flapsRawAdcAvailable is false, this runs the two-pass synth
    // (fill steady-state values then paint smoothstep transition windows)
    // and stores the result. Subsequent step() calls read from this table
    // using an internal row counter that is reset here.
    //
    // When flapsRawAdcAvailable is true, this is a no-op.
    //
    // Must be called before the first step() when the log lacks the column.
    // Call reset() and then prepare() again to replay the same rows twice.
    void prepare(const std::vector<onspeed::LogRow>& rows);

    // Process one CSV row: compute AOA, flap index, and all downstream
    // fields. Pure over engine state (only the AOA smoother has state).
    // Calling step() for N rows on a freshly-constructed engine produces
    // the same result as ReadLogLine() running N times against the same
    // rows from a fresh AOACalculator state. (The pre-extraction code
    // called g_Sensors.AoaCalc.calculate() directly — a boot-lifetime
    // singleton whose EMA state could carry across replays. The engine
    // owns its own AOACalculator, reset per OpenReplayLog. See PR #470.)
    ReplayStepResult step(const onspeed::LogRow& row);

    // Reset AOA smoother state and synth row counter. Call between
    // independent replay sessions (e.g. different log files) to avoid
    // stale EMA from a prior run contaminating the first rows. If the
    // new session also lacks flapsRawADC, call prepare() again after reset().
    void reset();

private:
    const onspeed::config::OnSpeedConfig& cfg_;

    int  logSampleRateHz_;
    bool flapsRawAdcAvailable_;

    // AOA smoothing filter — mirrors g_Sensors.AoaCalc in the live path.
    // State persists across step() calls within one replay session.
    onspeed::AOACalculator aoaCalc_;

    // Pre-computed synthetic ADC values — one entry per row in the sequence
    // passed to prepare(). Populated by prepare() when flapsRawAdcAvailable
    // is false; empty when flapsRawAdcAvailable is true.
    std::vector<uint16_t> synthAdc_;

    // Index into synthAdc_ for the next step() call. Incremented by step().
    // Reset to 0 by reset() and by prepare().
    size_t synthAdcIdx_;

    // Resolve the aFlaps index for a given flap-position degree value.
    // Returns 0 (first entry) when no exact match is found, matching the
    // behavior of the sketch task wrapper's linear search with no match.
    int ResolveFlapIndex_(int flapPosDeg) const;

    // Nominal pot position for a given flap-position degree value.
    // Returns the first matching detent's iPotPosition, or 0 when not found.
    int PotPositionForFlap_(int flapPosDeg) const;
};

}  // namespace onspeed::replay

#endif  // ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H
