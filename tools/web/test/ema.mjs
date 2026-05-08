// Unit tests for the pure EMA helpers in `lib/core/ema.js`.  Mirrors
// the test pattern used by smoothedField.mjs — node-runnable, no
// framework, exit code 0 = all pass.
//
// Run with:  node tools/web/test/ema.mjs

import { makeEmaState, updateEma, setAlpha, resetEma }
  from '../lib/core/ema.js';

let passed = 0;
let failed = 0;
const results = [];

function eq(actual, expected, msg) {
  if (actual === expected) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)}`]); }
}
function near(actual, expected, eps, msg) {
  if (Math.abs(actual - expected) <= eps) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${actual}, want ${expected} (±${eps})`]); }
}

// ---------------------------------------------------------------------
// Fresh state has no smoothed value until the first valid sample lands.
// ---------------------------------------------------------------------
{
  const s = makeEmaState(0.04);
  eq(s.smoothed, null, 'fresh state has null smoothed value');
  eq(s.alpha, 0.04, 'alpha stored on construction');
}

// ---------------------------------------------------------------------
// First valid sample seeds the state directly (no warm-up — otherwise
// a real flight starting at e.g. 100 kt/s decel would slowly walk in
// from 0 and lie to the pilot for ~τ seconds).
// ---------------------------------------------------------------------
{
  const s = makeEmaState(0.04);
  const out = updateEma(s, 5.0);
  near(out, 5.0, 1e-9, 'first sample seeds the smoothed value');
  near(s.smoothed, 5.0, 1e-9, 'state reflects the seed');
}

// ---------------------------------------------------------------------
// One-pole step response: α·x + (1−α)·prev each tick.  At α=0.5 the
// halving is easy to verify by hand.
// ---------------------------------------------------------------------
{
  const s = makeEmaState(0.5);
  updateEma(s, 0);    // seed at 0
  near(updateEma(s, 10), 5.0,    1e-9, '0.5·10 + 0.5·0  = 5.0');
  near(updateEma(s, 10), 7.5,    1e-9, '0.5·10 + 0.5·5  = 7.5');
  near(updateEma(s, 10), 8.75,   1e-9, '0.5·10 + 0.5·7.5 = 8.75');
}

// ---------------------------------------------------------------------
// Heavy smoothing at α=0.04 reaches ~63% (one τ) of the step in
// 1/α = 25 samples (1.25 s at 20 Hz).
// ---------------------------------------------------------------------
{
  const s = makeEmaState(0.04);
  updateEma(s, 0);
  for (let i = 0; i < 25; i++) updateEma(s, 1.0);
  // 1 − (1 − 0.04)^25 = 1 − 0.96^25 ≈ 1 − 0.3604 = 0.6396
  near(s.smoothed, 0.6396, 0.005, 'α=0.04: 25 unit steps → ≈63% of target');
}

// ---------------------------------------------------------------------
// Junk samples (null / undefined / NaN / ±Infinity) are dropped — the
// filter holds its current value rather than ingesting nonsense.
// Important: the JSON contract emits null for AOA-derived fields when
// air-data isn't valid (issue #358 / #431); the EMA must not blow up
// when the wire goes "silent".
// ---------------------------------------------------------------------
{
  const s = makeEmaState(0.5);
  updateEma(s, 4.0);
  eq(updateEma(s, null),       4.0, 'null sample held');
  eq(updateEma(s, undefined),  4.0, 'undefined sample held');
  eq(updateEma(s, NaN),        4.0, 'NaN sample held');
  eq(updateEma(s, Infinity),   4.0, '+Inf sample held');
  eq(updateEma(s, -Infinity),  4.0, '-Inf sample held');
  near(s.smoothed, 4.0, 1e-9, 'state unchanged after junk burst');
}

// ---------------------------------------------------------------------
// setAlpha mutates live without re-seeding — Gen2's wizard slider
// changes the filter α as the pilot drags it; the smoothed value
// continues from where it was, the next update applies the new α.
// ---------------------------------------------------------------------
{
  const s = makeEmaState(0.1);
  updateEma(s, 0);
  for (let i = 0; i < 5; i++) updateEma(s, 1.0);
  const before = s.smoothed;
  setAlpha(s, 0.9);
  eq(s.alpha, 0.9, 'alpha updated');
  near(s.smoothed, before, 1e-9, 'smoothed value unchanged by setAlpha');
  // Next update should be very responsive (α=0.9):
  const out = updateEma(s, 10.0);
  near(out, 0.9 * 10 + 0.1 * before, 1e-9, 'next update uses new alpha');
}

// ---------------------------------------------------------------------
// resetEma returns to the fresh-state contract: next sample seeds.
// ---------------------------------------------------------------------
{
  const s = makeEmaState(0.04);
  updateEma(s, 5.0);
  resetEma(s);
  eq(s.smoothed, null, 'after reset: smoothed is null');
  near(updateEma(s, 99), 99, 1e-9, 'after reset: next sample seeds');
}

// ---------------------------------------------------------------------
// Report.
// ---------------------------------------------------------------------
console.log('EMA filter invariants:');
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
