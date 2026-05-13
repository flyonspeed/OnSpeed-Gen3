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

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

// Clean log: three pilot presses, each preceded by 0.
assertEq(
  findDataMarks(makeLog({
    timeStamp: [0, 100, 200, 300, 400, 500],
    DataMark:  [0, 0,   5,   0,   7,   0],
  })),
  [
    { rowIdx: 2, logTimeMs: 200, value: 5, label: '01' },
    { rowIdx: 4, logTimeMs: 400, value: 7, label: '02' },
  ],
  'clean log: two presses detected with ordinal labels'
);

// Garbage rows mixed in (misaligned CSV). Values like 8158 and
// 17.55 must be rejected — they're not pilot presses.
assertEq(
  findDataMarks(makeLog({
    timeStamp: [0, 100, 200, 300, 400, 500, 600],
    DataMark:  [0, 8158, 0,  5,   285, 0,   7],   // 8158, 285 = noise
  })),
  [
    { rowIdx: 3, logTimeMs: 300, value: 5, label: '01' },
    { rowIdx: 6, logTimeMs: 600, value: 7, label: '02' },
  ],
  'garbage values 8158/285 filtered, real presses kept'
);

// Negative / NaN values rejected.
assertEq(
  findDataMarks({
    timeStamp: Float64Array.from([0, 100, 200, 300]),
    DataMark:  Float64Array.from([0, NaN, -1,  3]),
    Length:    4,
  }),
  [{ rowIdx: 3, logTimeMs: 300, value: 3, label: '01' }],
  'NaN + negative values rejected'
);

// Empty log.
assertEq(findDataMarks(makeLog({ timeStamp: [0], DataMark: [0] })), [], 'single-row log → no marks');
assertEq(findDataMarks(null), [], 'null log → empty array');

// First row with a real mark must be included — but only when ts > 0.
// (A row-0 with ts=0 is the misaligned-CSV signature; pilots can't
// physically press DataMark on the first sample of a fresh log
// because the firmware hasn't completed init yet anyway.)
assertEq(
  findDataMarks(makeLog({
    timeStamp: [50, 100, 200],
    DataMark:  [3,  0,   5],
  })),
  [
    { rowIdx: 0, logTimeMs: 50,  value: 3, label: '01' },
    { rowIdx: 2, logTimeMs: 200, value: 5, label: '02' },
  ],
  'first row with non-zero mark + positive ts counts as mark 01'
);

// First row with non-zero mark but ts=0: reject (CSV misalignment).
assertEq(
  findDataMarks(makeLog({
    timeStamp: [0, 100, 200],
    DataMark:  [3, 0,   5],
  })),
  [{ rowIdx: 2, logTimeMs: 200, value: 5, label: '01' }],
  'first row mark with ts=0: rejected, second mark relabeled to 01'
);

// Boundary: 99 valid, 100 noise.
assertEq(
  findDataMarks(makeLog({
    timeStamp: [0, 100, 200, 300],
    DataMark:  [0, 99,  0,   100],
  })),
  [{ rowIdx: 1, logTimeMs: 100, value: 99, label: '01' }],
  'value 99 ok, 100 rejected'
);

// A row whose ts <= 0 is a misaligned CSV row; reject even if the
// DataMark cell happens to be a valid integer. Real-world signature
// from RV-4 2026-05-11.
assertEq(
  findDataMarks(makeLog({
    timeStamp: [0, 100, 200, 0,   400],
    DataMark:  [0, 5,   0,   7,   0],   // row 3 looks like press "7" but ts=0
  })),
  [{ rowIdx: 1, logTimeMs: 100, value: 5, label: '01' }],
  'ts=0 mid-log: row rejected even with valid DataMark'
);

// A row whose ts went backwards from the previous accepted mark is
// also a misaligned-CSV signature.
assertEq(
  findDataMarks(makeLog({
    timeStamp: [0, 1000, 2000, 500, 4000],
    DataMark:  [0, 5,    0,    7,   0],   // row 3 ts=500 < row 1 ts=1000
  })),
  [{ rowIdx: 1, logTimeMs: 1000, value: 5, label: '01' }],
  'backwards ts: row rejected even with valid DataMark'
);

// Direct N→M transition with no intervening 0 is two corrupted rows
// landing both-valid by coincidence, not two presses. Reject the
// second.
assertEq(
  findDataMarks(makeLog({
    timeStamp: [0, 100, 200, 300, 400, 500],
    DataMark:  [0, 5,   6,   0,   7,   0],   // 5→6 with no 0 between
  })),
  [
    { rowIdx: 1, logTimeMs: 100, value: 5, label: '01' },
    { rowIdx: 4, logTimeMs: 400, value: 7, label: '02' },
  ],
  'direct N→M (no zero between): second value rejected'
);

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
