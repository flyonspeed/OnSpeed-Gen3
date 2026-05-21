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

// DataMark per firmware (Switch.cpp:61) is `g_iDataMark = g_iDataMark + 1`,
// a monotonically-incrementing `int`. The M5 wire and panel display
// mod-100 (`%02d`) so two digits of room is fine on screen, but the
// underlying counter can go much higher on long flights. Cap at a
// generous integer ceiling to filter the misaligned-CSV signature
// (values like 8158, 17:55:19.87, 285.40 observed on the RV-4
// 2026-05-11 flight) without rejecting legitimate ≥ 100 presses.
// 9999 is a safe upper limit: well above any realistic press count,
// well below the torn-row values we've seen.
function isValidMark(v) {
  return Number.isInteger(v) && v >= 0 && v <= 9999;
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
//
// Reject chatter: the firmware occasionally writes brief single-row
// blips of "0" between two stable "N" runs (torn-row signature or
// per-frame write race). A real pilot press has the column stable
// at the prior value for at least PRIOR_VALUE_HOLD_MS before the
// transition, and the new value stable for at least NEW_VALUE_HOLD_MS
// after. Anything shorter is a flicker. On the RV-4 log, the real
// 0→20 press held 20 for 928 seconds; the noise ones held 2 seconds
// AFTER a previous-value-0 that lasted only ~10 ms.
const PRIOR_VALUE_HOLD_MS = 200;
const NEW_VALUE_HOLD_MS   = 200;

export function findDataMarks(log) {
  if (!log || !log.DataMark || !log.timeStamp) return [];
  const N = log.Length;
  if (N < 2) return [];

  // Compress the (filtered) rows into runs of consecutive same-value
  // entries: [{ value, startTs, endTs, startRow }, ...]. Skips rows
  // with invalid DataMark or invalid timeStamp at parse boundary.
  // A run's "duration" is endTs - startTs.
  const runs = [];
  for (let i = 0; i < N; i++) {
    const v = log.DataMark[i];
    const ts = log.timeStamp[i];
    if (!isValidMark(v) || !Number.isFinite(ts) || ts <= 0) continue;
    const last = runs[runs.length - 1];
    if (last && last.value === v) {
      last.endTs = ts;
    } else {
      runs.push({ value: v, startTs: ts, endTs: ts, startRow: i });
    }
  }
  // A real press is a forward-going transition between two runs that
  // each persist long enough to be a true state — not single-row
  // blips. The 200ms thresholds are well above the firmware's log
  // cadence (5-50ms) and well below any real pilot's press interval.
  const out = [];
  for (let r = 1; r < runs.length; r++) {
    const cur  = runs[r];
    const prev = runs[r - 1];
    const isForward = cur.value > 0 && (prev.value === 0 || cur.value > prev.value);
    if (!isForward) continue;
    const prevHeld = prev.endTs - prev.startTs;
    const curHeld  = cur.endTs  - cur.startTs;
    if (prevHeld < PRIOR_VALUE_HOLD_MS) continue;
    if (curHeld  < NEW_VALUE_HOLD_MS)   continue;
    out.push({
      rowIdx:    cur.startRow,
      logTimeMs: cur.startTs,
      value:     cur.value,
      // `label` is what the panel shows. Always the firmware-written
      // DataMark value — same number the pilot saw on their device
      // and the same number the burned-in overlay renders. Multiple
      // panel rows may share the same value when the firmware counter
      // resets and the pilot bumps back to the same number minutes
      // later; distinguish them by their log/video times.
      label:     String(cur.value).padStart(2, '0'),
    });
  }
  // Sort by log time ascending. With the run-compression above the
  // output is already in time order; the explicit sort guards against
  // any future torn-row recovery that might re-introduce reordering.
  out.sort((a, b) => a.logTimeMs - b.logTimeMs);
  return out;
}

// Map a log timestamp (ms) to a video time (seconds) using the
// current sync. Returns null if sync is incomplete.
export function logMsToVideoSec(logMs, sync) {
  if (!sync) return null;
  if (!Number.isFinite(sync.logTakeoffMs) || !Number.isFinite(sync.videoTakeoffSec)) return null;
  return sync.videoTakeoffSec + (logMs - sync.logTakeoffMs) / 1000;
}
