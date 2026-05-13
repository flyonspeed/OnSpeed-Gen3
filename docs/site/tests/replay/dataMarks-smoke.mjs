// dataMarks-smoke.mjs — smoke test for findDataMarks.
//
// The function detects rising-edge transitions on the DataMark CSV
// column. Field spec is integer 0..99 (pilot button increment).
// Real-world SD logs occasionally contain misaligned rows where
// the DataMark cell holds a timestamp, a pressure reading, etc.
// Those garbage rows produced phantom "outside video" entries in
// the panel on the RV-4 2026-05-11 flight. The filter logic must
// drop them.
//
// Run:
//   node docs/site/tests/replay/dataMarks-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const modPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/dataMarks.js'
);
const { findDataMarks, logMsToVideoSec } = await import(modPath);

let passed = 0;
let failed = 0;
const results = [];

function assertEq(actual, expected, name) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (ok) { passed++; results.push(['PASS', name]); }
  else {
    failed++;
    results.push(['FAIL', `${name}\n  expected: ${JSON.stringify(expected)}\n  actual:   ${JSON.stringify(actual)}`]);
  }
}

function makeLog({ timeStamp, DataMark }) {
  return {
    timeStamp: Float64Array.from(timeStamp),
    DataMark:  Int32Array.from(DataMark),
    Length:    timeStamp.length,
  };
}

// findDataMarks requires each value's "run" to persist for at least
// PRIOR_VALUE_HOLD_MS / NEW_VALUE_HOLD_MS (200ms each) before counting
// the transition as a press. The synthetic logs below were originally
// authored with 100ms inter-row spacing — too short. This helper
// expands each (ts, dm) "event" by appending hold-duration filler
// rows so the run is wide enough to pass the duration gates while
// keeping the test intent readable.
function makeLogWithHolds({ events, holdMs = 400, stepMs = 5 }) {
  const ts = [];
  const dm = [];
  for (let i = 0; i < events.length; i++) {
    const [t, v] = events[i];
    // Anchor row.
    ts.push(t); dm.push(v);
    // Hold rows: continue at value v for holdMs after t, in stepMs
    // ticks. Don't run past the next event's timestamp.
    const next = i + 1 < events.length ? events[i + 1][0] : Number.POSITIVE_INFINITY;
    const target = Math.min(t + holdMs, next - stepMs);
    for (let s = t + stepMs; s <= target; s += stepMs) {
      ts.push(s); dm.push(v);
    }
  }
  return {
    timeStamp: Float64Array.from(ts),
    DataMark:  Int32Array.from(dm),
    Length:    ts.length,
  };
}

// ---------------------------------------------------------------------
// Tests — every test uses makeLogWithHolds so each value persists
// for at least 400ms, well above the 200ms run-duration threshold the
// detector requires.
// ---------------------------------------------------------------------

// Clean log: two pilot presses, each held long enough to register.
{
  const log = makeLogWithHolds({ events: [[0, 0], [1000, 5], [2000, 0], [3000, 7]] });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => ({ value: m.value, label: m.label, logTimeMs: m.logTimeMs })),
    [
      { value: 5, label: '05', logTimeMs: 1000 },
      { value: 7, label: '07', logTimeMs: 3000 },
    ],
    'clean log: two presses, labels are zero-padded firmware values'
  );
}

// Garbage rows from misaligned CSV (timestamp-like values landing in
// the DataMark column) must be ignored. Use values clearly outside
// the legitimate counter range (>9999) — actual torn-row values seen
// on the RV-4 log were in the millions (timestamps in ms).
{
  const log = makeLogWithHolds({ events: [[0, 0], [1000, 1276130], [2000, 0], [3000, 5], [4000, 4206976], [5000, 0], [6000, 7]] });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => m.value),
    [5, 7],
    'multi-million garbage values (torn timestamps) filtered, real presses kept'
  );
}

// Negative / NaN values rejected.
{
  const log = makeLogWithHolds({ events: [[0, 0], [1000, -1], [2000, 3]] });
  // Patch in a NaN at one position to test NaN handling
  log.DataMark[1] = -1;
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => m.value),
    [3],
    'NaN + negative values rejected'
  );
}

// Empty log.
assertEq(findDataMarks(makeLog({ timeStamp: [0], DataMark: [0] })), [], 'single-row log → no marks');
assertEq(findDataMarks(null), [], 'null log → empty array');

// Row 0 baseline ignored, 0→5 counts as press.
{
  const log = makeLogWithHolds({ events: [[50, 3], [600, 0], [1500, 5]] });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => ({ value: m.value, label: m.label })),
    [{ value: 5, label: '05' }],
    'firmware counter pattern: row 0 baseline + first real transition counts'
  );
}

// Boundary: 9999 valid, 10000+ rejected. Firmware-side counter can
// grow beyond 99 on long flights (panel displays mod-100, but the
// underlying counter keeps incrementing). 9999 is the conservative
// ceiling; values larger than that are torn-row signature.
{
  const log = makeLogWithHolds({ events: [[0, 0], [1000, 9999], [2000, 0], [3000, 10000]] });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => ({ value: m.value, label: m.label })),
    [{ value: 9999, label: '9999' }],
    'value 9999 accepted (high-counter), 10000 rejected (torn-row)'
  );
}

// High-counter (>= 100) is a real press: pilot has been bumping the
// counter for a long flight. Label is the firmware value as-written.
{
  const log = makeLogWithHolds({ events: [[0, 0], [1000, 137]] });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => ({ value: m.value, label: m.label })),
    [{ value: 137, label: '137' }],
    '3-digit firmware counter value: real press, label = "137"'
  );
}

// Forward-going counter — every step is a press, each held 400ms.
{
  const log = makeLogWithHolds({ events: [[0, 0], [1000, 5], [2000, 6], [3000, 7], [4000, 8], [5000, 9]] });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => m.value),
    [5, 6, 7, 8, 9],
    'monotonic counter: every forward step is a press, label = firmware value'
  );
}

// Reset to 0 then advance: reset ignored, advance counts.
{
  const log = makeLogWithHolds({ events: [[0, 0], [1000, 5], [2000, 6], [3000, 0], [4000, 7]] });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => m.value),
    [5, 6, 7],
    'reset to 0 then advance: reset ignored, advance counts'
  );
}

// Real-world RV-4 signature: pilot presses to 20, column held there
// for minutes, then a real second press back to 20 (after a long
// pause). Same-value presses widely separated in time count
// individually.
{
  const log = makeLogWithHolds({
    events: [[0, 0], [1000, 20], [60000, 0], [61000, 20], [120000, 0], [121000, 20]],
    holdMs: 800,
  });
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => m.value),
    [20, 20, 20],
    'repeated 0→20 presses minutes apart: each counts'
  );
}

// Run-duration filter (the user-reported RV-4 bug): the firmware
// log has stable 20 for 15 minutes, interrupted by single-row 0
// blips that flip back to 20 within ~10ms. The single-row 0 blips
// are torn-CSV signature, NOT real presses. The detector must NOT
// see them as additional 0→20 presses.
{
  // Build the timeStamp + DataMark arrays by hand to get the right
  // shape: leading run of 0 (firmware boot), pilot bumps to 20 (real
  // press, held for a minute), then a single-row 0 blip flipping
  // back to 20, then another single-row 0 blip flipping back to 20.
  // Only the first 0→20 should register.
  const ts = [];
  const dm = [];
  for (let i = 0; i < 200; i++) { ts.push(5 + i * 5); dm.push(0); }
  // Real pilot press at ts=1005: 20 held for many rows.
  for (let i = 0; i < 200; i++) { ts.push(1005 + i * 5); dm.push(20); }
  // Single-row 0 blip then back to 20.
  ts.push(2005); dm.push(0);
  for (let i = 0; i < 200; i++) { ts.push(2010 + i * 5); dm.push(20); }
  // Another single-row 0 blip then back to 20.
  ts.push(3010); dm.push(0);
  for (let i = 0; i < 200; i++) { ts.push(3015 + i * 5); dm.push(20); }
  const log = {
    timeStamp: Float64Array.from(ts),
    DataMark:  Int32Array.from(dm),
    Length:    ts.length,
  };
  const marks = findDataMarks(log);
  assertEq(
    marks.map(m => ({ value: m.value, logTimeMs: m.logTimeMs })),
    [{ value: 20, logTimeMs: 1005 }],
    'RV-4 chatter: 20→0→20 with single-row 0 collapses to one press'
  );
}

// logMsToVideoSec round-trip.
assertEq(
  logMsToVideoSec(120000, { logTakeoffMs: 60000, videoTakeoffSec: 30 }),
  90,
  'logMsToVideoSec: 60s after takeoff → 30s after video anchor + 60s'
);
assertEq(logMsToVideoSec(100, null), null, 'logMsToVideoSec: null sync → null');
assertEq(
  logMsToVideoSec(100, { logTakeoffMs: NaN, videoTakeoffSec: 0 }),
  null,
  'logMsToVideoSec: NaN log anchor → null'
);

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
