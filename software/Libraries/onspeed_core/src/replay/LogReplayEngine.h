// replay/LogReplayEngine.h — platform-free per-row pipeline for SD log replay.
//
// Mirrors the AHRS task/engine split (sketch_common/tasks/AHRS.cpp +
// onspeed_core/ahrs/Ahrs.{h,cpp}). The FreeRTOS task wrapper
// (software/sketch_common/src/tasks/LogReplay.cpp) owns SD-card reads,
// button mocks, LED blink, and USB-CDC writes. This engine owns the
// per-row (LogRow -> ReplayStepResult) computation — AOA smoothing, flap
// index lookup, pressure-coefficient computation, and (for old logs) synth
// flapsRawADC — so the same pipeline can serve three callers without
// platform dependencies:
//
//   1. Firmware (via the FreeRTOS task wrapper).
//   2. tools/regression/host_main.cpp `replay` subcommand.
//   3. WASM build (PLAN_WASM_CORE.md Step 2) via embind.
//
// Platform-free contract: this file and LogReplayEngine.cpp must not
// include Arduino.h, FreeRTOS.h, millis(), xSemaphoreTake(), or any other
// platform header. scripts/check_core_purity.sh enforces this.
//
// Accel smoothing (Sub-task 2 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md):
// Three RateAdjustedAccelEma instances (lateral, vertical, forward) are
// constructed at the log sample rate with tau = kAccelEmaTauSec.  Per row,
// raw imuLateralG / imuVerticalG / imuForwardG are fed through them.
// Smoothed outputs are written to accelLatSmoothed / accelVertSmoothed /
// accelFwdSmoothed in ReplayStepResult — mirroring the AHRS engine's
// AccelLatFilter / AccelVertFilter / AccelFwdFilter.get() values that go
// on the M5 wire.  Raw IMU fields (imuLateralG, imuVerticalG, imuForwardG)
// remain raw for downstream consumers that need g_pIMU->Ay/Az/Ax.
//
// Streaming synth ADC (Sub-task 3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md):
// When the log lacks the flapsRawADC column (pre-PR-#221 logs), the engine
// synthesizes a smoothstep sweep across detent transitions using a circular
// buffer of ±synthHalfWindowTicks_ rows. step() returns an empty optional
// until synthHalfWindowTicks_ rows have been fed (the "not ready" lag); after
// that it returns the synth for the row synthHalfWindowTicks_ ticks in the
// past. flush() drains the remaining buffered rows after the input stream
// ends. The half-window tick count is computed from logSampleRateHz so the
// smoothstep always spans ±kSynthHalfWindowSec (2 s) of wall-clock time.
//
// When the log carries flapsRawADC, step() returns an immediate result with
// no lag (the buffer is bypassed entirely — this path is unchanged from PR 1).

#ifndef ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H
#define ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H

#include <cstdint>
#include <optional>
#include <vector>

#include <aoa/AOACalculator.h>
#include <config/OnSpeedConfig.h>
#include <filters/RateAdjustedAccelEma.h>
#include <types/LogRow.h>

namespace onspeed::replay {

// ============================================================================
// ReplayStepResult — computed outputs for one log row.
//
// The task wrapper publishes these into the sketch globals (g_Sensors,
// g_Flaps, g_AHRS, g_pIMU, g_fCoeffP, g_iDataMark) immediately after step()
// returns a value, then calls g_AudioPlay.UpdateTones(SnapshotActiveFlap()).
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
    uint16_t flapsRawAdc        = 0;   // -> g_Flaps.uValue
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

    // --- Rate-adjusted-EMA smoothed accel (wire-shaped output) ---
    // These mirror the firmware's AccelLatFilter.get() / AccelVertFilter.get() /
    // AccelFwdFilter.get() values that feed the M5 wire and the slip-ball display.
    // Smoothed at log rate (50 or 208 Hz) with tau = kAccelEmaTauSec, matching
    // the AHRS engine's 208 Hz EMA in continuous-time behavior.
    // NOT the same as imuLateralG/VerticalG/ForwardG (those remain raw for g_pIMU->*).
    float accelLatSmoothed  = 0.0f;  // smoothed lateral-G  → g_AHRS.AccelLatFilter.get()
    float accelVertSmoothed = 0.0f;  // smoothed vertical-G → g_AHRS.AccelVertFilter.get()
    float accelFwdSmoothed  = 0.0f;  // smoothed forward-G  → g_AHRS.AccelFwdFilter.get()

    // --- Data mark (fed to g_iDataMark) ---
    int dataMark = 0;
};

// ============================================================================
// Synth ADC constants
//
// kSynthHalfWindowSec: wall-clock duration on each side of a transition
// where the smoothstep painter touches the output.  The tick count is
// computed per-engine-instance from logSampleRateHz:
//   synthHalfWindowTicks_ = kSynthHalfWindowSec * logSampleRateHz
// so the smoothstep always spans ±2 wall-clock seconds regardless of rate:
//   50 Hz  → 100 ticks  (~10 KB buffer)
//  208 Hz  → 416 ticks  (~42 KB buffer)
//
// kPotPerDetent: the pot-value difference per detent step used when synth
// ADC must be produced. The firmware nominal pot positions stored in
// aFlaps[i].iPotPosition are the target; the synth lerps between adjacent
// entries across the transition window.
// ============================================================================
inline constexpr float kSynthHalfWindowSec = 2.0f;  // ±2 wall-clock seconds

// ============================================================================
// LogReplayEngine
//
// INVARIANT: One log file = one sample rate.
//
// LogReplayEngine assumes logSampleRateHz is constant for the lifetime of one
// engine instance. The rate is used at construction to size the synth
// lookahead buffer (synthHalfWindowTicks_ = kSynthHalfWindowSec × rate) and
// to compute the rate-adjusted accel EMA's α. Both are baked in.
//
// If a log file's sample rate could change mid-stream, this engine would
// silently produce wrong output (over- or under-smoothing, wrong-sized synth
// window). The firmware enforces the invariant by closing the SD log file and
// opening a new one when iLogRate is toggled — see Issue #492.
//
// To replay a flight where iLogRate was toggled mid-flight, construct one
// LogReplayEngine per log file with that file's rate. Do not carry engine
// state across file boundaries.
//
// Owns the per-row processing state:
//   - AOA smoothing filter (AOACalculator, EMA over pressure coefficient).
//   - Three RateAdjustedAccelEma instances (lateral, vertical, forward)
//     that match the AHRS accel filter's continuous-time behavior at the
//     log sample rate.  Output goes to ReplayStepResult::accelLatSmoothed,
//     accelVertSmoothed, and accelFwdSmoothed — mirroring the firmware's
//     AccelLatFilter / AccelVertFilter / AccelFwdFilter.get() values.
//   - For old logs without flapsRawADC, a circular buffer for the streaming
//     smoothstep synth.
//
// Constructor parameters:
//   cfg                 — active OnSpeedConfig (aFlaps, iAoaSmoothing)
//   logSampleRateHz     — sample rate of the log (50 or 208 Hz).  Used to
//                         construct the RateAdjustedAccelEma filters at
//                         the correct rate.
//   flapsRawAdcAvailable— true when the log's header carries the
//                         flapsRawADC column. When false the engine
//                         synthesises the ADC with a streaming
//                         bounded-window smoothstep.
//
// API when flapsRawAdcAvailable == true (unchanged from PR 1):
//   step(row) always returns a non-empty optional immediately.
//   flush() returns an empty vector (nothing buffered).
//
// API when flapsRawAdcAvailable == false:
//   step(row) returns empty optional for the first synthHalfWindowTicks_
//   calls (the buffer is filling); thereafter returns the synth for the row
//   synthHalfWindowTicks_ ticks in the past.
//   flush() returns up to synthHalfWindowTicks_ results for the final rows.
//   Callers must drain flush() after the input stream ends.
// ============================================================================

class LogReplayEngine {
public:
    LogReplayEngine(const onspeed::config::OnSpeedConfig& cfg,
                    int  logSampleRateHz,
                    bool flapsRawAdcAvailable);

    // Process one CSV row.
    //
    // When flapsRawAdcAvailable was true at construction, always returns a
    // populated result (no lag, no buffering).
    //
    // When flapsRawAdcAvailable was false (old logs without the column):
    //   - Returns empty for the first synthHalfWindowTicks_ calls while the
    //     circular buffer fills.
    //   - Thereafter returns the synth for the row synthHalfWindowTicks_
    //     ticks in the past. Both edges of any detent transition are visible
    //     in the ±synthHalfWindowTicks_ window; the smoothstep paints correctly.
    std::optional<ReplayStepResult> step(const onspeed::LogRow& row);

    // Drain remaining buffered rows after the input stream ends.
    //
    // When flapsRawAdcAvailable was false, returns up to synthHalfWindowTicks_
    // results for the tail rows that haven't been emitted yet. Callers
    // must process these after the last step() call.
    //
    // When flapsRawAdcAvailable was true, returns an empty vector.
    std::vector<ReplayStepResult> flush();

    // Reset AOA smoother state and synth buffer. Call between independent
    // replay sessions (e.g. different log files) to avoid stale state from
    // a prior run contaminating the first rows.
    void reset();

    // Total rows fed to step() since construction or last reset().
    // Useful for tests that verify the lag contract.
    int rowsFed() const { return rowsFed_; }

    // Test-only: expose internal buffer capacity for memory-bound regression
    // tests. Lets tests verify that circBuf_ never grows past its construction-
    // time allocation without reaching into private members.
    // Production code does not need this — do not add non-test call sites.
    size_t bufferCapacity() const { return circBuf_.capacity(); }

    // Test-only: expose the current transition count so overflow/eviction tests
    // can assert the table never exceeds kMaxTransitions.
    // Invariant: numTransitionsForTest() <= kMaxTransitions at all times.
    // Production code does not need this — do not add non-test call sites.
    int numTransitionsForTest() const { return numTransitions_; }

private:
    const onspeed::config::OnSpeedConfig& cfg_;
    bool flapsRawAdcAvailable_;

    // synthHalfWindowTicks_: number of ticks on each side of a transition
    // where the smoothstep painter touches the output.  Set at construction
    // from kSynthHalfWindowSec * logSampleRateHz.  Fixed for the lifetime of
    // one engine instance; the "one file = one rate" invariant (Issue #492)
    // means this never changes mid-stream.
    //   50 Hz  → 100 ticks (~10 KB buffer)
    //  208 Hz  → 416 ticks (~42 KB buffer)
    int synthHalfWindowTicks_;

    // AOA smoothing filter — mirrors g_Sensors.AoaCalc in the live path.
    // State persists across step() calls within one replay session.
    onspeed::AOACalculator aoaCalc_;

    // Rate-adjusted-EMA accel filters — mirror the AHRS engine's
    // accelLatFilter_ / accelVertFilter_ / accelFwdFilter_ (alpha=0.060899
    // at 208 Hz).  Constructed at logSampleRateHz with tau=kAccelEmaTauSec
    // so the continuous-time frequency response matches the firmware's filter
    // at any supported log rate (50 or 208 Hz).
    onspeed::filters::RateAdjustedAccelEma accelLatEma_;
    onspeed::filters::RateAdjustedAccelEma accelVertEma_;
    onspeed::filters::RateAdjustedAccelEma accelFwdEma_;

    // -------------------------------------------------------------------------
    // Streaming synth state — used only when flapsRawAdcAvailable_ == false.
    //
    // circBuf_: circular buffer of pre-computed ReplayStepResult values
    // (with a placeholder flapsRawAdc). The buffer holds synthHalfWindowTicks_+1
    // entries (sized at construction, never grows). When it's full, the oldest
    // entry (at offset bufHead_) is the one synthHalfWindowTicks_ ticks in the
    // past and is ready to emit after the smoothstep is applied.
    //
    // bufHead_: next slot to read for emission in circBuf_ (wraps mod capacity).
    // bufSize_: number of valid entries currently in circBuf_.
    // rowsFed_: total rows fed to step() since construction or reset().
    //
    // lastFlapPosDeg_: the flapsPos of the most recently seen row.
    // Used to detect detent transitions.
    //
    // transitionTicks_: circular-buffer absolute tick indices of each detent
    // transition detected so far. A "tick" is the rowsFed_ value AT the row
    // where flapsPos changed. Kept in a small fixed-size list; we only need
    // to remember transitions within the current ±synthHalfWindowTicks_ window.
    // -------------------------------------------------------------------------
    // The buffer holds synthHalfWindowTicks_ + 1 slots:
    //   - synthHalfWindowTicks_ rows already buffered (but not yet emitted)
    //   - 1 slot for the new row being written
    // When bufCount_ reaches synthHalfWindowTicks_ + 1, the oldest entry is
    // exactly synthHalfWindowTicks_ ticks behind the write point → emit it.
    // Sized at construction from synthHalfWindowTicks_; never grows after that.

    // Circular buffer of pre-computed results (AOA, pressure, all passthrough
    // fields already filled in). The synth overlays flapsRawAdc before emission.
    std::vector<ReplayStepResult> circBuf_;

    // Index of the oldest entry in circBuf_ (next slot to read for emission).
    int bufHead_    = 0;
    // Number of valid entries currently buffered (0..synthHalfWindowTicks_+1).
    int bufSize_    = 0;
    // Total rows fed to step() since construction or reset().
    int rowsFed_    = 0;

    // flapsPos of the last row seen (for transition detection).
    int lastFlapPosDeg_ = -1;

    // Per-transition record: the rowsFed_ value when the transition occurred,
    // the pot value before the snap, and the pot value after. Up to 8
    // simultaneous transitions can sit in the ±synthHalfWindowTicks_ window
    // (in practice ≤1 at any time for typical flight).
    struct TransitionRecord {
        int      snapTick   = 0;   // rowsFed_ value AT the transition row
        uint16_t prevPot    = 0;
        uint16_t nextPot    = 0;
    };
    static constexpr int kMaxTransitions = 8;
    TransitionRecord transitions_[kMaxTransitions];
    int numTransitions_ = 0;

    // Resolve the aFlaps index for a given flap-position degree value.
    // Returns 0 (first entry) when no exact match is found.
    int ResolveFlapIndex_(int flapPosDeg) const;

    // Look up the nominal pot position for a given flap-position degree value.
    // Returns 0 when no match is found.
    uint16_t PotForFlapPos_(int flapPosDeg) const;

    // Compute the smoothstep-blended synth ADC value for the buffer entry at
    // absolute tick emitTick (0-indexed, equals rowsFed_ at the time it was
    // pushed). Uses all stored transitions_ within the ±synthHalfWindowTicks_
    // window.
    uint16_t ComputeSynthAdc_(int emitTick, int flapPosDeg) const;

    // Push one pre-computed result into the circular buffer without emitting.
    // Returns the entry that is now ready for emission (synthHalfWindowTicks_
    // in the past), or nothing if the buffer isn't full yet.
    std::optional<ReplayStepResult> PushAndMaybeEmit_(const ReplayStepResult& res);

    // Emit the result at the oldest slot in circBuf_, applying synth ADC.
    // Caller must ensure bufSize_ > 0.
    ReplayStepResult EmitOldest_();

    // Core per-row computation shared by both paths (AOA, pressure, all
    // passthrough fields). Does NOT touch flapsRawAdc / flapsRawAdcPresent.
    ReplayStepResult ComputeBase_(const onspeed::LogRow& row);
};

}  // namespace onspeed::replay

#endif  // ONSPEED_CORE_REPLAY_LOG_REPLAY_ENGINE_H
