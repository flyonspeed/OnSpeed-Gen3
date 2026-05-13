// clipContains-smoke.mjs — pure-helper smoke test for marksWithinClip.
//
// The helper backs the "Contains: Mark 17, Mark 18" inline list in an
// expanded clip row. Containment is half-open on the upper edge so
// adjacent clips (like the ones produced by "Clip → next") don't
// double-count the mark that separates them.
//
// Run:
//   node docs/site/tests/replay/clipContains-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const modPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/clipContains.js'
);
const { marksWithinClip } = await import(modPath);

let passed = 0;
let failed = 0;
const results = [];

function assertEq(actual, expected, name) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (ok) { passed++; results.push(['PASS', name]); }
  else {
    failed++;
    results.push(['FAIL',
      `${name}\n  expected: ${JSON.stringify(expected)}\n  actual:   ${JSON.stringify(actual)}`]);
  }
}

function mk(value, logTimeMs) {
  return { value, logTimeMs, label: String(value).padStart(2, '0') };
}

const marks = [
  mk(1, 1000),
  mk(2, 2000),
  mk(3, 3000),
  mk(4, 4000),
  mk(5, 5000),
];

assertEq(
  marksWithinClip({ startMs: 1500, endMs: 3500 }, marks).map(m => m.value),
  [2, 3],
  'inclusive on lower edge, exclusive on upper — interior marks');

assertEq(
  marksWithinClip({ startMs: 1000, endMs: 3000 }, marks).map(m => m.value),
  [1, 2],
  'start edge included, end edge excluded — mark 3 belongs to the next clip');

assertEq(
  marksWithinClip({ startMs: 3000, endMs: 5000 }, marks).map(m => m.value),
  [3, 4],
  'adjacent clip starts at the prior clip end — mark 3 lands here, not the previous one');

assertEq(
  marksWithinClip({ startMs: 1500, endMs: 1600 }, marks).map(m => m.value),
  [],
  'empty range between marks');

assertEq(
  marksWithinClip({ startMs: 0, endMs: 10000 }, marks).map(m => m.value),
  [1, 2, 3, 4, 5],
  'range covering all marks');

assertEq(marksWithinClip(null, marks), [], 'null clip → empty');
assertEq(marksWithinClip({ startMs: 1000, endMs: 2000 }, null), [], 'null marks → empty');
assertEq(
  marksWithinClip({ startMs: 2000, endMs: 2000 }, marks),
  [],
  'zero-width range → empty');
assertEq(
  marksWithinClip({ startMs: 3000, endMs: 1000 }, marks),
  [],
  'inverted range → empty');
assertEq(
  marksWithinClip({ startMs: NaN, endMs: 2000 }, marks),
  [],
  'NaN start → empty');

// Mark with non-finite logTimeMs gets skipped without crashing.
const noisyMarks = [
  mk(1, 1000),
  { value: 2, logTimeMs: NaN, label: '02' },
  mk(3, 3000),
];
assertEq(
  marksWithinClip({ startMs: 0, endMs: 10000 }, noisyMarks).map(m => m.value),
  [1, 3],
  'skip marks with non-finite logTimeMs');

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
