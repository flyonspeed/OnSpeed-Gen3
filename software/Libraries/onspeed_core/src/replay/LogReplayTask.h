// LogReplayTask — single-source CSV-row → wire-frame pipeline.
//
// Purpose: collapse the JS-side hand-derivations between LogReplayEngine
// and the M5 firmware's wire input into a single C++ entry point so the
// replay tool's "drift impossible by construction" invariant covers the
// data-shape transformations, not just the algorithm work.
//
// Issue #514. Retro: docs/superpowers/plans/2026-05-09-replay-retro.md.
//
// What this wraps:
//
//   per row:
//     LogRow (CSV-parsed)
//        |
//        v
//     UpdateIasAlive   (sensors/IasAlive.h hysteretic state machine —
//        |               firmware-equivalent gate the JS port got wrong)
//        v
//     LogReplayEngine::step       (alpha smoothing, accel EMA, GOnsetFilter,
//        |                         AOA polynomial — same code that ran live)
//        v
//     ReplayStepResult
//        |
//        v
//     ComputePercentLift            (aoa.PercentLift — same as DisplaySerial.cpp
//        |                            in the firmware path)
//        v
//     ComputeDisplayPctAnchors      (aoa.DisplayPctAnchors — anchors from cfg)
//        |
//        v
//     DisplayBuildInputs            (renamed/scaled fields per
//        |                            DisplaySerial.cpp's firmware fill-pass)
//        v
//     BuildDisplayFrame             (proto.DisplaySerial — produces 77 bytes)
//        |
//        v
//     wire bytes (77, v4.23 #1 frame; M5 firmware decodes verbatim)
//
// Stateful per replay session. The engine carries the AOA EMA, accel EMAs,
// gOnset filter state, and (for old logs without flapsRawADC) the synth
// circular buffer. iasAlive lives here on the task because the JS pre-pass
// wants it threaded across the whole row sweep, not just one step at a time.
//
// Resettable. Caller invokes reset() on backward scrub or re-init. After
// reset(), the next processRow seeds the EMAs as if it were row zero.
//
// Lifecycle:
//   - Caller constructs once per (cfg, log) pair.
//   - Caller invokes processRow(row) for each parsed CSV row in sequence.
//     Returns std::vector<uint8_t> of size kDisplayFrameSizeBytes (77) on
//     success, or empty when the synth path is still in lag (no wire
//     output until the circular buffer fills, ~2 sec on 50 Hz).
//   - At end-of-log, caller invokes flush() to drain the synth buffer
//     tail (matching the firmware's sketch_common LogReplay.cpp
//     end-of-file drain). Each entry in the returned vector is a 77-byte
//     wire frame.
//
// Threading: not thread-safe. Single-threaded use only (browser main
// thread or sketch task). Mirrors LogReplayEngine's contract.
//
// Testing: native unit tests in test/test_log_replay_task/. Embind
// surface in software/Libraries/onspeed_core/wasm/bindings.cpp exposes
// processRow/flush/reset to JS. End-to-end test in
// tools/web/test/wasm-smoke.mjs drives the binding with a synthetic
// LogRow stream and asserts wire bytes match what BuildDisplayFrame
// produces directly when fed equivalent inputs.

#pragma once

#include <cstdint>
#include <vector>

#include <config/OnSpeedConfig.h>
#include <proto/DisplaySerial.h>      // kDisplayFrameSizeBytes
#include <replay/LogReplayEngine.h>
#include <types/LogRow.h>

namespace onspeed::replay {

class LogReplayTask {
public:
    // Construct with the parsed flight cfg, the log's sample rate, and
    // whether the log carries the flapsRawADC column (modern path) or
    // requires the synth-from-flapsPos buffer (pre-PR-#221 logs). All
    // three are forwarded directly to the engine.
    //
    // The cfg is copied; the caller can release theirs afterward.
    LogReplayTask(const ::onspeed::config::OnSpeedConfig& cfg,
                  int                                     logSampleRateHz,
                  bool                                    flapsRawAdcAvailable);

    // Process one parsed CSV row. Returns the 77-byte wire frame the M5
    // firmware would have received for this tick, or an empty vector
    // when the synth path is still buffering (the engine's step() returns
    // std::nullopt during the kSynthHalfWindowTicks_ lag period).
    //
    // iasAlive: derived per the firmware's hysteretic state machine
    // (sensors/IasAlive.h). The CSV parser sets row.iasValid based on
    // whether the IAS column was empty (the firmware's empty-column
    // convention for bIasAlive=false), but old logs and the AHRS path
    // can disagree at edge cases — so we re-derive here from row.iasKt
    // and the previous-row state. row.iasValid as set by the parser is
    // ignored.
    //
    // Side-effects: advances engine state and iasAlive_; carries them to
    // the next call. Holds a copy of the cfg internally — the caller's
    // reference need not outlive the call.
    std::vector<uint8_t> processRow(const LogRow& row);

    // Drain any rows still buffered in the engine after the last
    // processRow. Mirrors sketch_common/src/tasks/LogReplay.cpp's
    // end-of-file drain. For modern logs (flapsRawAdcAvailable=true)
    // returns an empty vector. For old logs returns the trailing
    // ~kSynthHalfWindowTicks rows as 77-byte frames in arrival order.
    std::vector<std::vector<uint8_t>> flush();

    // Reset all state (engine EMA filters, synth buffer, iasAlive_) so
    // the next processRow starts from a cold seed. Call on backward
    // scrub or replay re-init.
    void reset();

    // Read-only access to the most recent ReplayStepResult emitted by
    // the engine, useful for diagnostics that want to compare the C++
    // engine output against the wire frame the same processRow call
    // produced. Valid only after a processRow that returned non-empty
    // (or after a flush row was drained); undefined otherwise.
    const ReplayStepResult& lastStep() const { return lastStep_; }

    // Diagnostic accessor: cfg.aFlaps as the task internally sees it.
    // Returns iDegrees of every detent, in storage order. Used to
    // verify the cfg round-trip preserved flap order.
    std::vector<int> cfgFlapsDegrees() const {
        std::vector<int> out;
        out.reserve(cfg_.aFlaps.size());
        for (const auto& f : cfg_.aFlaps) out.push_back(f.iDegrees);
        return out;
    }

private:
    ::onspeed::config::OnSpeedConfig cfg_;   // owned copy for anchors / pct-lift
    LogReplayEngine                  engine_;
    bool                             iasAlive_ = false;
    ReplayStepResult                 lastStep_{};

    // Encode one ReplayStepResult into a 77-byte wire frame. Pulls
    // anchors and percent-lift from the cfg + step result. Pure
    // function, no state mutation.
    std::vector<uint8_t> EncodeFrame_(
        const ReplayStepResult& result) const;
};

} // namespace onspeed::replay
