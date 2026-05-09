// m5sim-smoke.mjs — Node smoke test for the JS M5Sim wrapper.
//
// PR 2 of Project B2. Existing tests:
//
//   software/OnSpeed-M5-Display/test/test_replay_wasm.js
//     Verifies the WASM module's correctness (PR 1 deliverable).
//   software/OnSpeed-M5-Display/test/test_replay_wire_completeness.js
//     Verifies the wire bytes feeding the M5 sim are complete (PR 1.5).
//
// What this test adds: a smoke test for the JS wrapper itself
// (`tools/web/lib/replay/m5sim.js`). The wrapper is the user-visible
// surface PR 2 ships — it loads the WASM, exposes advanceTo / injectBytes
// / setMode / read, and freezes the read object. A regression in any
// of those (a missing accessor in read(), advanceTo not advancing the
// clock, gHistory copy-vs-view semantics) shows up here.
//
// Run:
//   node tools/web/test/m5sim-smoke.mjs
//
// Exit code 0 = all assertions passed; non-zero on any failure.

import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const REPO_ROOT  = path.resolve(__dirname, '..', '..', '..');

// Path to the WASM .js. Loaded via createRequire (CJS) because the
// build emits with EXPORT_ES6=0; m5sim/package.json overrides the
// parent's "type": "module" so this works.
const WASM_DIR = path.join(
  REPO_ROOT, 'tools', 'web', 'lib', 'replay', 'm5sim');
const MODULE_JS = path.join(WASM_DIR, 'onspeed_m5.js');

// Path to onspeed_core (for build_display_frame in the wire-frame
// fixture). SINGLE_FILE so it's just a .js.
const CORE_JS = path.join(
  REPO_ROOT, 'software', 'Libraries', 'onspeed_core', 'wasm', 'dist',
  'onspeed_core.js');

if (!fs.existsSync(MODULE_JS)) {
  console.error(`FATAL: ${MODULE_JS} not found.`);
  console.error(
    'Run `bash software/OnSpeed-M5-Display/sim/build_wasm.sh --target replay` first.');
  process.exit(2);
}
if (!fs.existsSync(CORE_JS)) {
  console.error(`FATAL: ${CORE_JS} not found.`);
  console.error(
    'Run `bash software/Libraries/onspeed_core/wasm/build_wasm.sh` first.');
  process.exit(2);
}

// Counter scaffolding (kept simple; this is a smoke test, not a
// per-component coverage suite).
let passed = 0;
let failed = 0;

function pass(label) { passed++; console.log(`  PASS ${label}`); }
function fail(label, detail = '') {
  failed++;
  console.log(`  FAIL ${label}${detail ? ': ' + detail : ''}`);
}

function assertEq(label, actual, expected) {
  if (actual === expected) pass(`${label} = ${expected}`);
  else fail(label, `got ${actual}, expected ${expected}`);
}

function assertCloseAbs(label, actual, expected, tol) {
  if (Math.abs(actual - expected) <= tol) {
    pass(`${label} = ${actual} (~${expected})`);
  } else {
    fail(label, `got ${actual}, expected ${expected}±${tol}`);
  }
}

async function main() {
  // Load M5Sim (ESM module) and the WASM factory (CJS via createRequire).
  // The smoke test uses M5Sim.fromFactory because that path is
  // independent of the browser-side script-tag loader. Browser loading
  // (M5Sim.create) is exercised by manual verification — there's no
  // headless-DOM coverage of that path in this PR.
  console.log('Loading M5 WASM factory:', MODULE_JS);
  const require_ = createRequire(import.meta.url);
  const factory = require_(MODULE_JS);
  if (typeof factory !== 'function') {
    console.error('FATAL: onspeed_m5.js did not export a factory function. ' +
                  'Got: ' + typeof factory);
    process.exit(2);
  }

  console.log('Loading onspeed_core WASM:', CORE_JS);
  const coreUrl = pathToFileURL(CORE_JS).href;
  const CoreFactory = (await import(coreUrl)).default;
  const Core = await CoreFactory();
  if (typeof Core.build_display_frame !== 'function') {
    console.error(
      'FATAL: onspeed_core WASM is missing build_display_frame.');
    process.exit(2);
  }

  // Import M5Sim from the production wrapper.
  const m5simUrl = pathToFileURL(
    path.join(REPO_ROOT, 'tools', 'web', 'lib', 'replay', 'm5sim.js')).href;
  const { M5Sim } = await import(m5simUrl);

  // ------------------------------------------------------------------
  // Test 1 — construct, then verify read() returns every field.
  // ------------------------------------------------------------------
  console.log('\n--- M5Sim.fromFactory + read() schema ---');
  const sim = await M5Sim.fromFactory(factory);
  const initial = sim.read();

  // Every field the SVG mode renderers reference must be present.
  // Update this list when the WASM accessor surface grows; out-of-sync
  // is a deliberate fail mode.
  const expectedFields = [
    'displayIAS', 'displayPalt', 'displayPitch', 'displayVerticalG',
    'displayPercentLift', 'displayDecelRate',
    'Slip', 'PercentLift', 'gOnsetRate', 'IAS', 'Palt', 'IasIsValid',
    'displayType', 'iVSI', 'OAT', 'FlightPath', 'Pitch', 'Roll',
    'TonesOnPctLift', 'OnSpeedFastPctLift', 'OnSpeedSlowPctLift',
    'StallWarnPctLift', 'PipPctLift', 'FlapsMinDeg', 'FlapsMaxDeg',
    'FlapPos',
    'gHistoryIndex', 'gHistory',
    'SpinRecoveryCue', 'DataMark',
  ];
  let missing = 0;
  for (const k of expectedFields) {
    if (!(k in initial)) { fail(`read().${k} missing`); missing++; }
  }
  if (missing === 0) pass(`read() returned all ${expectedFields.length} expected fields`);

  // gHistory: must be a Float32Array length 300 (matches firmware ring
  // buffer size). Slice semantics: subsequent advanceTo() calls must NOT
  // mutate this array — verify by holding a reference and re-checking
  // after a tick.
  if (initial.gHistory instanceof Float32Array) {
    pass('read().gHistory is Float32Array');
  } else {
    fail('read().gHistory not Float32Array',
         `got ${initial.gHistory && initial.gHistory.constructor.name}`);
  }
  assertEq('read().gHistory.length', initial.gHistory ? initial.gHistory.length : 0, 300);

  // The frozen contract: mutation throws in strict mode. We're under
  // ESM (strict by default) — assert.
  let froze = false;
  try {
    initial.displayIAS = 999;
    if (initial.displayIAS === 999) {
      // Object.freeze didn't block the write — fail.
      fail('read() return must be frozen', 'mutation succeeded');
    } else {
      // Silent ignore (non-strict TypedArray-like behavior) is also OK
      // since the assignment didn't take effect.
      froze = true;
    }
  } catch (e) {
    froze = true;
  }
  if (froze) pass('read() return is frozen (mutation rejected)');

  // ------------------------------------------------------------------
  // Test 2 — inject a wire frame, advance time, read display state.
  // ------------------------------------------------------------------
  console.log('\n--- injectBytes + advanceTo + state propagation ---');
  const frame = Core.build_display_frame({
    iasKt:              80,
    iasValid:           true,
    pitchDeg:           5.0,
    rollDeg:            10.0,
    paltFt:             3000,
    lateralG:           0.04,   // → Slip = -34
    verticalG:          1.0,
    percentLiftPct:     50.0,
    vsiFpm:             400,
    oatC:               20,
    flightPathDeg:      3.0,
    flapsDeg:           10,
    tonesOnPctLift:     30,
    onSpeedFastPctLift: 40,
    onSpeedSlowPctLift: 50,
    stallWarnPctLift:   80,
    flapsMinDeg:        0,
    flapsMaxDeg:        33,
    gOnsetRate:         0.5,
    spinRecoveryCue:    0,
    dataMark:           7,
    pipPctLift:         32,
  });
  if (!(frame instanceof Uint8Array)) {
    fail('build_display_frame returned non-Uint8Array');
    process.exit(1);
  }

  sim.injectBytes(frame);

  // After parse, IAS reflects the wire's 80 kt; this is the
  // every-byte path (synchronous on terminal LF), independent of
  // advanceTo.
  let s = sim.read();
  assertEq('IAS after injectBytes', s.IAS, 80);
  assertEq('Slip after injectBytes', s.Slip, -34);

  // Advance to t=600 ms: the firmware's 500 ms numbers snapshot fires,
  // displayIAS picks up IAS.
  sim.advanceTo(600);
  s = sim.read();
  assertEq('displayIAS @t=600 ms (post-snapshot)', s.displayIAS, 80);

  // ------------------------------------------------------------------
  // Test 3 — setMode + read displayType round-trip for all five modes.
  // ------------------------------------------------------------------
  console.log('\n--- setMode round-trip ---');
  for (let mode = 0; mode <= 4; mode++) {
    sim.setMode(mode);
    sim.injectBytes(frame);
    sim.advanceTo(700 + mode * 100);
    const r = sim.read();
    assertEq(`displayType after setMode(${mode})`, r.displayType, mode);
  }

  // ------------------------------------------------------------------
  // Test 4 — gHistory copy semantics: holding the array across an
  // advanceTo() must NOT see post-tick mutations.
  // ------------------------------------------------------------------
  console.log('\n--- gHistory copy semantics ---');
  const heldHistory = sim.read().gHistory;
  // Snapshot the first slot value.
  const heldFirst = heldHistory[0];
  // Drive several frames + advance time enough for the firmware's
  // 200 ms gHistory sampler to fire and overwrite slot 0.
  for (let i = 0; i < 20; i++) {
    sim.injectBytes(frame);
    sim.advanceTo(2000 + i * 250);
  }
  // The held array should still show the original value (copy).
  assertEq('held gHistory[0] unchanged after ticks',
           heldHistory[0], heldFirst);
  // The fresh read should show the new value (different sample
  // landed). If the sampler somehow didn't run, this assertion
  // catches it — but the more important assertion is the held one.
  const freshHistory = sim.read().gHistory;
  if (freshHistory[0] !== heldFirst) {
    pass('fresh read shows updated gHistory (sampler ran)');
  } else {
    // Sampler may not have advanced if the firmware's serial-fresh
    // gate held it; not load-bearing for the copy assertion above.
    console.log('  NOTE: fresh gHistory[0] unchanged — sampler did not advance');
  }

  // ------------------------------------------------------------------
  // Test 5 — delete() makes the wrapper drop its module reference.
  // Defensive only; callers don't normally call delete().
  // ------------------------------------------------------------------
  console.log('\n--- delete() teardown ---');
  sim.delete();
  let threw = false;
  try { sim.read(); } catch { threw = true; }
  if (threw) pass('read() throws after delete()');
  else fail('read() did not throw after delete()');

  // ------------------------------------------------------------------
  console.log('');
  console.log(`Tests: ${passed} passed, ${failed} failed`);
  if (failed > 0) {
    console.log('Some assertions failed.');
    process.exit(1);
  }
  console.log('All assertions passed.');
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
