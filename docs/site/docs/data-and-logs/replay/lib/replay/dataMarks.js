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

export function findDataMarks(log) {
  if (!log || !log.DataMark || !log.timeStamp) return [];
  const N = log.Length;
  if (N < 2) return [];

  const out = [];
  // Treat anything different from the previous logged value as a
  // transition. The first row's mark is included only if it's
  // non-zero (otherwise every flight starts with a "mark 0" the
  // pilot didn't actually press).
  let prev = isValidMark(log.DataMark[0]) ? log.DataMark[0] : 0;
  if (prev > 0) {
    out.push({
      rowIdx:    0,
      logTimeMs: log.timeStamp[0],
      value:     prev,
      label:     '01',
    });
  }
  for (let i = 1; i < N; i++) {
    const v = log.DataMark[i];
    if (!isValidMark(v)) continue;
    if (v !== prev) {
      // Skip the immediate post-power-up "mark = 0" zero-edge that
      // happens when the column initializes; only count transitions
      // whose new value is non-zero (i.e. an actual button press).
      if (v > 0) {
        out.push({
          rowIdx:    i,
          logTimeMs: log.timeStamp[i],
          value:     v,
          label:     String(out.length + 1).padStart(2, '0'),
        });
      }
      prev = v;
    }
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
