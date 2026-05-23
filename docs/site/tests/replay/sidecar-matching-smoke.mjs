// sidecar-matching-smoke.mjs — Node smoke test for the D3 match
// decision tree (session.js / matchSidecarsToLogs).
//
// Verifies that one-log-one-sidecar pairs auto-load, multi-log folders
// surface the picker, orphaned sidecars are flagged, and empty folders
// report empty. Pure data-in / data-out test — no FSA, no Preact.
//
// Run:
//   node docs/site/tests/replay/sidecar-matching-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

globalThis.indexedDB = globalThis.indexedDB || {
  open: () => ({ onsuccess: null, onerror: null, onupgradeneeded: null }),
};
globalThis.window = globalThis.window || {};

const sessionPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/session.js'
);
const { matchSidecarsToLogs } = await import(sessionPath);

// ---------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------

let passed = 0;
let failed = 0;
const results = [];

function test(name, fn) {
  try {
    fn();
    passed++; results.push(['PASS', name]);
  } catch (e) {
    failed++;
    results.push(['FAIL', `${name}: ${e.message}`]);
  }
}

function assertEqual(a, b, msg = '') {
  if (a !== b) throw new Error(`expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}${msg ? ' — ' + msg : ''}`);
}

// Small builders — the function only reads `name` so the rest of the
// discovery-row shape is optional.
function log(name) { return { name, relativePath: name }; }
function sidecar(name) { return { name, relativePath: name }; }

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

test('one log + matching sidecar → auto-load with the pair', () => {
  const r = matchSidecarsToLogs(
    [log('log_007.csv')],
    [sidecar('log_007.csv.replay.json')]);
  assertEqual(r.kind, 'auto-load');
  assertEqual(r.log.name, 'log_007.csv');
  assertEqual(r.sidecar.name, 'log_007.csv.replay.json');
});

test('one log, no sidecar → auto-load with sidecar=null (D3 single-log case)', () => {
  const r = matchSidecarsToLogs([log('log_007.csv')], []);
  assertEqual(r.kind, 'auto-load');
  assertEqual(r.log.name, 'log_007.csv');
  assertEqual(r.sidecar, null);
});

test('multiple logs + multiple sidecars (each matching) → picker', () => {
  const r = matchSidecarsToLogs(
    [log('log_007.csv'), log('log_008.csv')],
    [sidecar('log_007.csv.replay.json'),
     sidecar('log_008.csv.replay.json')]);
  assertEqual(r.kind, 'picker');
  assertEqual(r.logs.length, 2);
  // Both logs have sidecar metadata in the map.
  assertEqual(Object.keys(r.sidecarMap).length, 2);
  assertEqual(r.sidecarMap['log_007.csv'].name, 'log_007.csv.replay.json');
});

test('multiple logs but only one sidecar matches → auto-load that pair', () => {
  // D3 fast-path: only one sidecar exists AND it matches a log.
  const r = matchSidecarsToLogs(
    [log('log_007.csv'), log('log_008.csv')],
    [sidecar('log_007.csv.replay.json')]);
  assertEqual(r.kind, 'auto-load');
  assertEqual(r.log.name, 'log_007.csv');
  assertEqual(r.sidecar.name, 'log_007.csv.replay.json');
});

test('multiple logs, no sidecars → picker', () => {
  const r = matchSidecarsToLogs(
    [log('log_007.csv'), log('log_008.csv')],
    []);
  assertEqual(r.kind, 'picker');
  assertEqual(r.logs.length, 2);
  assertEqual(Object.keys(r.sidecarMap).length, 0);
});

test('orphan: sidecar with no matching log', () => {
  // Only the sidecar present, no log file.
  const r = matchSidecarsToLogs(
    [],
    [sidecar('log_007.csv.replay.json')]);
  assertEqual(r.kind, 'orphan');
  assertEqual(r.missingLogName, 'log_007.csv');
});

test('orphan: log_X.csv present, sidecar pointing to log_007.csv', () => {
  // Folder has a log + a sidecar, but for different logs. The plan
  // calls this "ambiguous" — fall through to the picker (no auto-load
  // for an unmatched sidecar). Currently, with one log and one sidecar
  // pointing to a missing log, indexSidecars finds no match in the
  // log list, so the single-log fast-path runs and auto-loads the log
  // with sidecar=null.
  const r = matchSidecarsToLogs(
    [log('log_X.csv')],
    [sidecar('log_007.csv.replay.json')]);
  assertEqual(r.kind, 'auto-load');
  assertEqual(r.log.name, 'log_X.csv');
  assertEqual(r.sidecar, null);
});

test('empty: no logs, no sidecars', () => {
  const r = matchSidecarsToLogs([], []);
  assertEqual(r.kind, 'empty');
});

test('case-insensitive sidecar matching (APFS normalization)', () => {
  const r = matchSidecarsToLogs(
    [log('Log_007.csv')],
    [sidecar('log_007.csv.replay.json')]);
  assertEqual(r.kind, 'auto-load');
  assertEqual(r.sidecar.name, 'log_007.csv.replay.json');
});

test('handles non-array inputs gracefully', () => {
  const r1 = matchSidecarsToLogs(null, null);
  assertEqual(r1.kind, 'empty');
  const r2 = matchSidecarsToLogs(undefined, undefined);
  assertEqual(r2.kind, 'empty');
});

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
