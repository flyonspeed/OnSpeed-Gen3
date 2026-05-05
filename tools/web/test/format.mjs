// Tests for lib/core/format.js — the central number-formatter helper
// shared by the IndexerPage data fields, the SVG components, and the
// mode renderers.  Guards against the -0.0 regression: any value that
// rounds to zero must render unsigned.
//
// Run with:  node tools/web/test/format.mjs
//
// Exit code 0 = all pass, non-zero = some failed.

import { fmt, fmtSigned } from '../lib/core/format.js';

let failed = 0;
let passed = 0;
const results = [];

function eq(actual, expected, msg) {
  if (actual === expected) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)}`]); }
}

// ---- fmt: negative-zero guard --------------------------------------------
eq(fmt(-0.001, 2), '0.00',  'fmt(-0.001, 2) → 0.00 (would print "-0.00" without guard)');
eq(fmt(-0.04,  1), '0.0',   'fmt(-0.04, 1) → 0.0 (would print "-0.0" without guard)');
eq(fmt(0,      2), '0.00',  'fmt(0, 2) → 0.00');
eq(fmt(-0,     2), '0.00',  'fmt(-0, 2) → 0.00 (literal negative zero)');

// ---- fmt: real negatives stay negative -----------------------------------
eq(fmt(-0.5, 1),   '-0.5',  'fmt(-0.5, 1) preserves sign');
eq(fmt(-1.234, 2), '-1.23', 'fmt(-1.234, 2) preserves sign');
eq(fmt(2.5, 1),    '2.5',   'fmt(2.5, 1) → 2.5');

// ---- fmt: non-finite inputs collapse to em-dash --------------------------
eq(fmt(NaN, 2),       '—', 'fmt(NaN, 2) → —');
eq(fmt(undefined, 2), '—', 'fmt(undefined, 2) → —');
eq(fmt(null, 2),      '—', 'fmt(null, 2) → —');
eq(fmt(Infinity, 2),  '—', 'fmt(Infinity, 2) → —');
eq(fmt(-Infinity, 2), '—', 'fmt(-Infinity, 2) → —');

// ---- fmtSigned: signs and zero handling ----------------------------------
eq(fmtSigned(1.5, 1),    '+1.5',  'fmtSigned(1.5, 1) → +1.5');
eq(fmtSigned(-1.5, 1),   '-1.5',  'fmtSigned(-1.5, 1) preserves sign');
eq(fmtSigned(0, 1),      '+0.0',  'fmtSigned(0, 1) → +0.0');
eq(fmtSigned(-0.001, 1), '+0.0',  'fmtSigned(-0.001, 1) → +0.0 (zero rounds to +0)');
eq(fmtSigned(NaN, 1),    '—',     'fmtSigned(NaN, 1) → —');

// ---- Report --------------------------------------------------------------

console.log('Format helper:');
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
