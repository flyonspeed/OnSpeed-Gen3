// DataMark detection for OnSpeed SD logs.
//
// The DataMark CSV column is an integer 0..99 that wraps. Pilots
// bump it from the panel during flight to flag interesting moments
// ("nailed an OnSpeed approach", "stall warn just chirped", ...).
// Each bump is a transition in the column — we don't care about
// the absolute value, only the rising edge from the previous row's
// value to a new value.
//
// Returns an array of:
//   { rowIdx, logTimeMs, value, label }
//
// where label is a 1-based ordinal ("01", "02", ...) so the panel
// can show a stable identifier the pilot can talk about even after
// the absolute DataMark value wraps.

// DataMark per firmware spec is an integer 0..99. Real-world SD logs
// can have garbage rows from partial writes / power glitches where
// columns are misaligned and the DataMark cell ends up holding a
// timestamp, a fractional Pfwd reading, etc. (e.g. 8158, 17:55:19.87,
// 285.40 observed on the RV-4 2026-05-11 flight). Anything outside
// [0, 99] AND not an integer is parser noise — drop it before
// looking at transitions, otherwise every noise row produces a
// phantom "outside video" entry in the panel.
function isValidMark(v) {
  return Number.isInteger(v) && v >= 0 && v <= 99;
}

// Firmware writes DataMark as a monotonically incrementing counter:
// 0 → 1 → 2 → 3 → ... Each pilot press bumps the column by one. The
// real-world write pattern observed on RV-4 2026-05-11:
//   0 (boot) → 1 (first press) → 2 → 3 → ... → 20 → ...
// with occasional resets to 0 after a long idle period (then the
// counter continues from where it left off, e.g. 0 → 20 → 21 → ...).
//
// Garbage rows from misaligned CSV either (a) hold a value outside
// [0..99] (filtered by isValidMark), or (b) hold a valid-by-luck
// integer but have ts = 0 / ts going backwards. The ts sanity check
// catches the second class.
//
// A "press" is a row where the column transitions from a smaller
// non-negative value to a strictly-larger one (1..99). The previous
// value can be 0 (cold start / re-arm) OR a smaller non-zero (the
// counter continuing). Resets (N → 0) and same-value rows are not
// presses.
export function findDataMarks(log) {
  if (!log || !log.DataMark || !log.timeStamp) return [];
  const N = log.Length;
  if (N < 2) return [];

  const out = [];
  let prev = isValidMark(log.DataMark[0]) ? log.DataMark[0] : 0;
  for (let i = 1; i < N; i++) {
    const v = log.DataMark[i];
    if (!isValidMark(v)) continue;
    if (v === prev) continue;
    const ts = log.timeStamp[i];
    const tsOk = Number.isFinite(ts) && ts > 0 &&
                 (out.length === 0 || ts > out[out.length - 1].logTimeMs);
    // A press is a forward-going transition into a positive value.
    // The previous value may be 0 (the counter just reset / first
    // press after boot) or any smaller value (the counter continuing
    // forward, e.g. 5 → 6).
    const isPress = v > 0 && (prev === 0 || v > prev) && tsOk;
    if (isPress) {
      out.push({
        rowIdx:    i,
        logTimeMs: ts,
        value:     v,
        label:     String(out.length + 1).padStart(2, '0'),
      });
    }
    prev = v;
  }
  return out;
}

// Map a log timestamp (ms) to a video time (seconds) using the
// current sync. Returns null if sync is incomplete.
export function logMsToVideoSec(logMs, sync) {
  if (!sync) return null;
  if (!Number.isFinite(sync.logTakeoffMs) || !Number.isFinite(sync.videoTakeoffSec)) return null;
  return sync.videoTakeoffSec + (logMs - sync.logTakeoffMs) / 1000;
}
