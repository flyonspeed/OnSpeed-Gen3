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

// A real pilot press follows the pattern 0 → N → 0 in the column
// (the firmware writes the new value for one frame's worth then
// resets). A direct N → M transition with no intervening 0 is a
// signature of two adjacent corrupted rows landing both-valid by
// coincidence, not two consecutive presses; drop the second one.
//
// And: a row whose timeStamp is 0 (or earlier than the previous row's
// timestamp) is a misaligned-CSV row where the time cell got pushed
// out — the DataMark cell may parse as a valid integer by luck, but
// the row isn't a real pilot press.
export function findDataMarks(log) {
  if (!log || !log.DataMark || !log.timeStamp) return [];
  const N = log.Length;
  if (N < 2) return [];

  const out = [];
  let prev = isValidMark(log.DataMark[0]) ? log.DataMark[0] : 0;
  let prevTimestamp = log.timeStamp[0];
  if (prev > 0 && Number.isFinite(prevTimestamp) && prevTimestamp > 0) {
    out.push({
      rowIdx:    0,
      logTimeMs: prevTimestamp,
      value:     prev,
      label:     '01',
    });
  }
  for (let i = 1; i < N; i++) {
    const v = log.DataMark[i];
    if (!isValidMark(v)) continue;
    if (v !== prev) {
      // Sanity: real presses sit on a row whose timeStamp is positive
      // and monotonically ahead of the prior accepted mark. Rows with
      // ts <= 0 OR ts < the previous accepted mark's ts are noise.
      const ts = log.timeStamp[i];
      const tsOk = Number.isFinite(ts) && ts > 0 &&
                   (out.length === 0 || ts > out[out.length - 1].logTimeMs);
      // Real-press pattern: the column must have been at 0 just
      // before this transition. A direct N → M (both non-zero) is
      // two corrupted-but-in-range rows back-to-back, not two
      // separate presses.
      const cameFromZero = prev === 0;
      if (v > 0 && tsOk && cameFromZero) {
        out.push({
          rowIdx:    i,
          logTimeMs: ts,
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
