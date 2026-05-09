// logReplay.js — thin async wrapper around the WASM-bound C++ LogReplayEngine.
//
// Algorithm code lives in C++ at
// software/Libraries/onspeed_core/src/replay/LogReplayEngine.{h,cpp}.
// This wrapper marshals JS objects across the WASM boundary; it does NOT
// implement any algorithm.
//
// Drift impossible by construction: this wrapper, the firmware-side replay
// path, and host_main replay all invoke the same compiled C++.
//
// NOTE: at the time this PR ships, no caller in this repo's master branch
// imports this module. The Replay UI lives on sam/video-overlay; its current
// hand-ported logReplay.js (carrying KACC_TAU_S=0.50 and synthLeverSweep)
// will be replaced with imports of this wrapper when that branch rebases
// onto WASM master.
//
// Public API:
//
//   class LogReplayEngine
//     static async create(cfg, logSampleRateHz, flapsRawAdcAvailable)
//                    -> Promise<LogReplayEngine>
//     step(row)      -> plain JS object with ReplayStepResult fields,
//                       OR null when flapsRawAdcAvailable=false and the
//                       synth circular buffer is still filling (lag period).
//     flush()        -> Array of plain JS objects (tail rows buffered during
//                       the synth lag; empty when flapsRawAdcAvailable=true)
//     reset()        -> void
//     delete()       -> void  (free WASM heap; call when done)
//
// cfg shape: the object returned by parseConfigXml() / parse_config().
// row shape: plain JS object with log-row fields.  Keys:
//   pfwdSmoothed, p45Smoothed, pStaticMbar, paltFt, iasKt,
//   iasValid,
//   flapsPos, flapsRawAdc, flapsRawAdcPresent,
//   imuVerticalG, imuLateralG, imuForwardG,
//   imuRollRateDps, imuPitchRateDps, imuYawRateDps,
//   pitchDeg, rollDeg, flightPathDeg, vsiFpm,
//   dataMark
//
// Result object fields (from StepResultToVal in bindings.cpp):
//   iasKt, paltFt, iasValid, aoaDeg, coeffP,
//   flapsPos, flapsIndex, flapsRawAdc, flapsRawAdcPresent,
//   pitchDeg, rollDeg, flightPathDeg, kalmanVsiMps,
//   imuForwardG, imuLateralG, imuVerticalG,
//   imuRollRateDps, imuPitchRateDps, imuYawRateDps,
//   accelLatSmoothed, accelVertSmoothed, accelFwdSmoothed,
//   dataMark
//
// Typical usage (Replay UI load-time pre-pass):
//
//   import { LogReplayEngine } from './logReplay.js';
//   import { hasFlapsRawAdc, detectLogSampleRate } from './parseLog.js';
//
//   const rate    = detectLogSampleRate(parsedLog);
//   const hasPot  = hasFlapsRawAdc(parsedLog);
//   const engine  = await LogReplayEngine.create(cfg, rate, hasPot);
//
//   const results = [];
//   for (const row of parsedLog.rows) {
//       const r = engine.step(row);
//       if (r !== null) results.push(r);  // skip null during synth lag period
//   }
//   results.push(...engine.flush());   // drain synth tail (empty if hasPot)
//   engine.delete();
//
//   // Build per-row lookup for video frame → result (O(1) seek).
//   // Note: when flapsRawAdcAvailable=false, results.length < parsedLog.rows.length
//   // for the first synthHalfWindowTicks rows, but flush() makes up the difference.
//   const byIndex = results;

import { getWasmCore } from './wasm_core.js';

class LogReplayEngine {
    // Private constructor — use LogReplayEngine.create() instead.
    // _wasmEngine is the embind-managed object returned by
    // `new wasm.LogReplayEngine(...)`.
    constructor(_wasmEngine) {
        this._engine = _wasmEngine;
    }

    /**
     * Async factory.  Loads (or reuses cached) WASM module and constructs
     * the engine.
     *
     * @param {object}  cfg                  - Parsed config (from parseConfigXml).
     * @param {number}  logSampleRateHz      - 50 or 208.
     * @param {boolean} flapsRawAdcAvailable - true when log carries flapsRawADC.
     * @returns {Promise<LogReplayEngine>}
     */
    static async create(cfg, logSampleRateHz, flapsRawAdcAvailable) {
        const wasm = await getWasmCore();
        const wasmEngine = new wasm.LogReplayEngine(
            cfg,
            logSampleRateHz,
            !!flapsRawAdcAvailable
        );
        return new LogReplayEngine(wasmEngine);
    }

    /**
     * Process one log row through the C++ replay pipeline.
     *
     * When flapsRawAdcAvailable is true (log has flapsRawADC column):
     *   Always returns a ReplayStepResult object.
     *
     * When flapsRawAdcAvailable is false (old logs without the column):
     *   Returns null for the first synthHalfWindowTicks rows while the
     *   synth circular buffer fills.  After the buffer is full, returns
     *   the synth result for the row synthHalfWindowTicks ticks in the past.
     *   Callers must skip null returns and call flush() after the last row.
     *
     * @param {object} row - Log row object (see file header for field list).
     * @returns {object|null} ReplayStepResult as a plain JS object, or null
     *   during the synth lag period (flapsRawAdcAvailable=false only).
     *   Also returns null if called after delete() (guard for mis-use).
     */
    step(row) {
        if (!this._engine) return null;
        return this._engine.step(row);
    }

    /**
     * Drain remaining tail rows from the synth lag buffer.
     *
     * When flapsRawAdcAvailable is false, returns up to synthHalfWindowTicks
     * result objects for the final rows held in the half-window delay.
     * Call once after the last step() to collect these rows.
     *
     * When flapsRawAdcAvailable is true, returns an empty array (nothing
     * is buffered; step() always emits immediately).
     *
     * @returns {object[]} Array of ReplayStepResult objects (may be empty).
     */
    flush() {
        if (!this._engine) return [];
        const arr = this._engine.flush();
        // embind val::array() interops as a JS Array; return it directly.
        // Belt-and-suspenders: handle both native and embind-backed forms.
        if (Array.isArray(arr)) return arr;
        const result = [];
        for (let i = 0, n = (arr.length || 0); i < n; i++) result.push(arr[i]);
        return result;
    }

    /**
     * Reset EMA and AOA smoother state.
     * Call between independent replay sessions (different log files) so
     * stale filter state from a prior run doesn't contaminate the first rows.
     */
    reset() {
        if (this._engine) this._engine.reset();
    }

    /**
     * Free WASM heap memory.
     * Call when the engine is no longer needed (e.g., on new log load or
     * component unmount).  After delete(), step()/flush()/reset() return
     * safe no-op / null / [] values.
     */
    delete() {
        if (this._engine) {
            this._engine.delete();
            this._engine = null;
        }
    }
}

export { LogReplayEngine };
