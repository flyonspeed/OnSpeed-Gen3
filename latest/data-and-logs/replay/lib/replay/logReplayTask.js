// logReplayTask.js — JS wrapper around the LogReplayTask WASM binding.
//
// Single-source CSV-row → wire-bytes pipeline (issue #514). Wraps the
// C++ LogReplayTask in onspeed_core/src/replay/LogReplayTask.{h,cpp}
// and exposes a simple JS API: construct, processRow, flush, reset,
// delete.
//
// Used by the M5-accurate replay path's "C++ engine" toggle to drive
// wire bytes from a single C++ entry point — no JS-side rowObjAt or
// buildDisplayInputs hand-derivation. See the retro at
// docs/superpowers/plans/2026-05-09-replay-retro.md for why the
// hand-derivations are drift seams.

import { getWasmCore } from './wasm_core.js';

export class LogReplayTask {
  // Static factory — constructs a task with the given parsed cfg and
  // log metadata. Throws if the WASM binding isn't available (rebuild
  // via software/Libraries/onspeed_core/wasm/build_wasm.sh).
  static async create(cfg, logSampleRateHz, flapsRawAdcAvailable) {
    const Module = await getWasmCore();
    if (typeof Module.LogReplayTask !== 'function') {
      throw new Error(
        'LogReplayTask: WASM binding not available — rebuild via ' +
        'software/Libraries/onspeed_core/wasm/build_wasm.sh');
    }
    const handle = new Module.LogReplayTask(
      cfg, logSampleRateHz, flapsRawAdcAvailable);
    return new LogReplayTask(handle);
  }

  constructor(handle) {
    this._handle = handle;
  }

  // Process one parsed row. Returns a Uint8Array of length 77 (the
  // v4.23 wire frame the M5 firmware would have received), or an
  // empty Uint8Array when the synth-path is buffering (engine returns
  // null during the kSynthHalfWindowTicks lag period). Caller should
  // skip injecting empty arrays.
  //
  // The row shape mirrors LogReplayEngine.step's input: see
  // bindings.cpp::LogRowFromVal for the canonical field list.
  processRow(rowObj) {
    return this._handle.processRow(rowObj);
  }

  // Drain the synth circular buffer at end-of-log. Returns an Array
  // of Uint8Array frames in arrival order. Empty Array when the log
  // had flapsRawAdcAvailable=true (no buffering). Caller injects each
  // tail frame in order.
  flush() {
    return this._handle.flush();
  }

  // Reset state (engine EMA filters, synth buffer, iasAlive). Call on
  // backward scrub or replay re-init.
  reset() {
    this._handle.reset();
  }

  // Diagnostic accessor: the engine's most recent ReplayStepResult as
  // a plain JS object. Valid only after a processRow that returned a
  // non-empty Uint8Array (during synth lag the engine has no result
  // to report; older calls' state is whatever the default-constructed
  // ReplayStepResult held — do not rely on the values then).
  lastStep() {
    return this._handle.lastStep();
  }

  // Diagnostic: the iDegrees of every flap detent in the C++ task's
  // cfg, in storage order. Verifies the cfg round-trip preserved
  // detent ordering.
  cfgFlapsDegrees() {
    return this._handle.cfgFlapsDegrees();
  }

  // Free the WASM-side handle. Required to release WASM heap memory
  // when the task is no longer needed.
  delete() {
    if (this._handle) {
      this._handle.delete();
      this._handle = null;
    }
  }
}
