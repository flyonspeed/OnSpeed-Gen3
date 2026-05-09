// parseLog.js — helpers for inspecting parsed OnSpeed SD log metadata.
//
// These helpers answer questions about the log structure that the
// LogReplayEngine constructor needs: whether the raw flap-pot ADC column
// is present, and what sample rate was used to record the log.
//
// Usage:
//   import { hasFlapsRawAdc, detectLogSampleRate } from './parseLog.js';
//
//   const rate = detectLogSampleRate(parsedLog);     // 50 or 208
//   const hasPot = hasFlapsRawAdc(parsedLog);        // true / false
//
//   const engine = new LogReplayEngine(cfg, rate, hasPot);
//
// parsedLog shape expected by both helpers:
//   {
//     columns: string[],    // CSV header column names (case-sensitive)
//     rows:    object[],    // one object per data row, field names = column names
//   }
//
// The `columns` field must be populated by the log parser that produces
// parsedLog.  The `rows` field is only needed by detectLogSampleRate.

// ---------------------------------------------------------------------------
// hasFlapsRawAdc(parsedLog) -> boolean
//
// Returns true when the log CSV carries a raw flap-pot ADC column.
// Logs written before ~PR #372 don't have this column; without it the
// LogReplayEngine cannot reproduce the smooth L/Dmax pip interpolation
// across detent transitions.
//
// Column name check is case-insensitive to handle both `flapsRawADC`
// (the original LogSensor spelling) and `flapsRawAdc` (camelCase variants).
// ---------------------------------------------------------------------------
function hasFlapsRawAdc(parsedLog) {
    if (!parsedLog || !Array.isArray(parsedLog.columns)) return false;
    return parsedLog.columns.some(
        c => c.toLowerCase() === 'flapsrawadc'
    );
}

// ---------------------------------------------------------------------------
// detectLogSampleRate(parsedLog) -> 50 | 208
//
// Infers the log's sample rate from the average inter-row timestamp delta.
// Returns 50 or 208; defaults to 50 for ambiguous or short logs.
//
// The firmware logs at either 50 Hz (20 ms/row, default) or 208 Hz
// (~4.8 ms/row, when iLogRate=208 in config).  The timeStamp column
// is the sketch's millis() at sample time.
//
// Heuristics:
//   - Average first ~10 row deltas to reduce noise.
//   - Accept 50 Hz if average dt is in [15, 25] ms.
//   - Accept 208 Hz if average dt is in [3, 7] ms.
//   - Fall back to 50 for any other value (e.g., very short log, corrupt
//     timestamps, or a future rate we don't know about).
//
// The timeStamp column may be named "timeStamp" (firmware spelling) or
// "timestamp" (lowercase, from some test fixtures).  Both are checked.
// ---------------------------------------------------------------------------
function detectLogSampleRate(parsedLog) {
    if (!parsedLog || !Array.isArray(parsedLog.rows) ||
        parsedLog.rows.length < 10) {
        return 50;  // fallback: not enough rows to measure
    }

    // Resolve the timestamp key (case-insensitive).
    const firstRow = parsedLog.rows[0];
    let tsKey = null;
    for (const k of Object.keys(firstRow)) {
        if (k.toLowerCase() === 'timestamp') { tsKey = k; break; }
    }
    if (tsKey === null) return 50;  // no timestamp column

    // Average delta over the first 9 intervals (rows 0..9).
    const t0 = Number(parsedLog.rows[0][tsKey]);
    const t9 = Number(parsedLog.rows[9][tsKey]);
    if (!Number.isFinite(t0) || !Number.isFinite(t9)) return 50;

    const avgDeltaMs = (t9 - t0) / 9;

    if (avgDeltaMs >= 15 && avgDeltaMs <= 25) return 50;
    if (avgDeltaMs >= 3  && avgDeltaMs <= 7)  return 208;
    return 50;  // unknown rate — default to 50
}

export { hasFlapsRawAdc, detectLogSampleRate };
