// Tests for lib/ws/wsClient.js — frame-to-record mapping.
//
// Issues #358 / #455: the producer emits JSON null for AOA, DerivedAOA,
// IAS, and percentLift when air data is invalid (bIasAlive=false on the
// firmware side).  The -100 case below is a belt-and-suspenders guard
// against any future numeric-sentinel drift.  This test pins the
// consumer-side handling so a future drift in either contract (producer
// reverting to a numeric sentinel, or wsClient losing its typeof guard)
// breaks loud rather than silently mis-classifying invalid frames.
//
// Run with:  node tools/web/test/wsclient.mjs
//
// Exit code 0 = all pass.

import { frameToRecord } from '../lib/ws/wsClient.js';

let failed = 0;
let passed = 0;
const results = [];

function eq(actual, expected, msg) {
  if (actual === expected) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)}`]); }
}

// ---- aoaIsValid: numeric sentinel and explicit null/undefined guards ----

// Live AOA above the -100 sentinel → valid.
eq(frameToRecord({ AOA: 5.5 }).aoaIsValid, true,
   'aoaIsValid: numeric AOA above -20 → true');

// AOA below the -20 cutoff (legacy `> AOA_NA_SENTINEL` boundary) → invalid.
eq(frameToRecord({ AOA: -100 }).aoaIsValid, false,
   'aoaIsValid: -100 sentinel → false');
eq(frameToRecord({ AOA: -50 }).aoaIsValid, false,
   'aoaIsValid: anything ≤ -20 → false');

// JS `null > -20` is `true` (null coerces to 0).  The wsClient must
// reject null explicitly so a future producer drift toward null-for-AOA
// can't silently mark invalid frames as valid.
eq(frameToRecord({ AOA: null }).aoaIsValid, false,
   'aoaIsValid: null AOA → false (guards JS null > -20 coercion)');

// Missing key → undefined → also rejected.
eq(frameToRecord({}).aoaIsValid, false,
   'aoaIsValid: missing AOA key → false');

// String "5.5" — also rejected; aoaIsValid wants a real number.
eq(frameToRecord({ AOA: '5.5' }).aoaIsValid, false,
   'aoaIsValid: string AOA → false');

// ---- iasKt: null passes through unchanged for fmt() to dash ----

// Live numeric IAS passes through unchanged.
eq(frameToRecord({ IAS: 75.5 }).iasKt, 75.5,
   'iasKt: numeric value passes through unchanged');

// Null IAS (producer's "not alive") passes through as null — so the
// downstream fmt() helper renders as '—'.  Consumer must NOT coerce
// null to 0, since 0 kt is a valid taxi reading.
eq(frameToRecord({ IAS: null }).iasKt, null,
   'iasKt: null passes through unchanged (fmt collapses to —)');

// ---- percentLift: parseFloatOr0 collapses null to 0 (intentional) ----
//
// The chevron / index-bar consumer compares percentLift against integer
// band anchors.  When AOA is invalid, those consumers gate on
// `aoaIsValid` (not on percentLift directly), so the bar / chevron are
// hidden regardless of percentLift's value.  parseFloatOr0 returning 0
// for null is therefore harmless — but pin the contract so a future
// reader doesn't misinterpret the 0 as a real reading.
eq(frameToRecord({ percentLift: null }).percentLift, 0,
   'percentLift: parseFloatOr0(null) → 0 (consumers gate on aoaIsValid)');
eq(frameToRecord({ percentLift: 47.3 }).percentLift, 47.3,
   'percentLift: numeric value preserved');

// ---- Report ---------------------------------------------------------

console.log('wsClient frameToRecord:');
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
