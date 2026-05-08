// Unit tests for the pure ring-buffer helpers behind
// `useSmoothedField`.  The Preact hook wraps these with state /
// effects; the algorithm itself (window fill, mean, refresh) is
// covered here without dragging Preact into the test environment.
//
// Run with:  node tools/web/test/smoothedField.mjs
// Exit code 0 = all pass.

import { makeSmoothState, ingest, mean, pending, clear }
  from '../lib/core/smoothedField.js';

let passed = 0;
let failed = 0;
const results = [];

function ok(cond, msg) {
  if (cond) { passed++; results.push(['  PASS', msg]); }
  else      { failed++; results.push(['  FAIL', msg]); }
}
function eq(actual, expected, msg) {
  if (actual === expected) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)}`]); }
}
function near(actual, expected, eps, msg) {
  if (Math.abs(actual - expected) <= eps) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${actual}, want ${expected} (±${eps})`]); }
}

// ---------------------------------------------------------------------
// Empty buffer: pending=true, mean=null.
// ---------------------------------------------------------------------
{
  const s = makeSmoothState(4);
  eq(pending(s), true,  'fresh state is pending');
  eq(mean(s),    null,  'fresh state has null mean');
}

// ---------------------------------------------------------------------
// Partial fill: still pending, but mean is computed over count slots.
// (Useful for diagnostics; the hook hides this until full to keep the
// pilot-facing transition crisp.)
// ---------------------------------------------------------------------
{
  const s = makeSmoothState(4);
  ingest(s, 1);
  ingest(s, 3);
  eq(pending(s), true, 'partial fill is still pending');
  near(mean(s), 2.0, 1e-9, 'mean over 2 of 4 = 2.0');
}

// ---------------------------------------------------------------------
// Full fill: pending flips false, mean is over all N samples.
// ---------------------------------------------------------------------
{
  const s = makeSmoothState(4);
  ingest(s, 1); ingest(s, 2); ingest(s, 3); ingest(s, 4);
  eq(pending(s), false, 'full fill is no longer pending');
  near(mean(s), 2.5, 1e-9, 'mean over [1,2,3,4] = 2.5');
}

// ---------------------------------------------------------------------
// Sliding window: oldest sample is overwritten on wrap.
// ---------------------------------------------------------------------
{
  const s = makeSmoothState(3);
  ingest(s, 1); ingest(s, 2); ingest(s, 3);
  near(mean(s), 2.0, 1e-9, 'mean [1,2,3] = 2.0');
  ingest(s, 4);  // overwrites the 1
  near(mean(s), 3.0, 1e-9, 'mean [4,2,3] = 3.0 after slide');
  ingest(s, 5);  // overwrites the 2
  near(mean(s), 4.0, 1e-9, 'mean [4,5,3] = 4.0 after second slide');
}

// ---------------------------------------------------------------------
// Junk samples are dropped — accessor returning null or NaN does not
// touch the buffer.
// ---------------------------------------------------------------------
{
  const s = makeSmoothState(3);
  ingest(s, 5);
  ingest(s, null);
  ingest(s, undefined);
  ingest(s, NaN);
  ingest(s, Infinity);
  eq(s.count, 1, 'junk samples (null/undefined/NaN/Infinity) ignored');
  near(mean(s), 5.0, 1e-9, 'mean unchanged by junk');
}

// ---------------------------------------------------------------------
// Clear: state returns to pending with no buffered samples.
// ---------------------------------------------------------------------
{
  const s = makeSmoothState(2);
  ingest(s, 10); ingest(s, 20);
  eq(pending(s), false, 'before clear: not pending');
  clear(s);
  eq(pending(s), true, 'after clear: pending');
  eq(mean(s),    null, 'after clear: mean is null');
  ingest(s, 30);
  near(mean(s), 30.0, 1e-9, 'after clear + 1 sample: mean = 30');
}

// ---------------------------------------------------------------------
// 40-sample default window matches the documented 2 s @ 20 Hz contract.
// Confirms the hook's default constant is what the ring helpers expect.
// ---------------------------------------------------------------------
{
  const s = makeSmoothState(40);
  for (let i = 0; i < 39; i++) ingest(s, i);
  eq(pending(s), true,  '39 samples in 40-slot window: pending');
  ingest(s, 39);
  eq(pending(s), false, '40 samples in 40-slot window: ready');
  near(mean(s), 19.5, 1e-9, 'mean of 0..39 = 19.5');
}

// ---------------------------------------------------------------------
// Report.
// ---------------------------------------------------------------------
console.log('useSmoothedField ring-buffer invariants:');
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
