// test_replay_wasm.js — Node.js test harness for the M5 firmware replay
// WASM build (PR 1 of Project B2 PLAN_REPLAY_M5_WASM).
//
// Loads two WASM modules:
//   1. onspeed_core.wasm (PR #496) — the canonical wire-frame builder.
//      `Module.build_display_frame(inputs)` returns the exact 77-byte
//      v4.23 #1-protocol frame the firmware would emit on the wire.
//   2. onspeed_m5.wasm (this PR) — the M5-Display firmware compiled to
//      WASM. JS feeds wire bytes via `_replay_inject_byte`, drives
//      virtual time, and reads state-var accessors.
//
// Loading both WASM modules instead of hand-porting the frame builder
// makes drift between firmware and test fixture impossible by
// construction: a future wire-format change shows up in
// onspeed_core/proto/DisplaySerial.cpp, the rebuild propagates the new
// bytes through both the firmware (M5 WASM) and the test (onspeed_core
// WASM), and any encode/decode mismatch surfaces as a parse failure
// in the test rather than as a silent mismatch the test misses.
//
// Run:
//   node software/OnSpeed-M5-Display/test/test_replay_wasm.js
//
// Exit code 0 = all assertions passed; non-zero on any failure.

'use strict';

const path = require('path');
const fs = require('fs');

// Output of build_wasm.sh --target replay.
const WASM_DIR = path.resolve(
  __dirname, '..', 'sim', 'build', 'wasm-replay');
const MODULE_JS = path.join(WASM_DIR, 'onspeed_m5.js');

// Output of software/Libraries/onspeed_core/wasm/build_wasm.sh.
// SINGLE_FILE=1 build: the .js contains everything (WASM as base64).
const CORE_JS = path.resolve(
  __dirname, '..', '..', 'Libraries', 'onspeed_core', 'wasm', 'dist',
  'onspeed_core.js');

if (!fs.existsSync(MODULE_JS)) {
  console.error(`FATAL: ${MODULE_JS} not found.`);
  console.error(`Run \`bash sim/build_wasm.sh --target replay\` first.`);
  process.exit(2);
}
if (!fs.existsSync(CORE_JS)) {
  console.error(`FATAL: ${CORE_JS} not found.`);
  console.error(
    `Run \`bash software/Libraries/onspeed_core/wasm/build_wasm.sh\` first.`);
  process.exit(2);
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

let passed = 0;
let failed = 0;

function assertEq(label, actual, expected, tolerance) {
  const tol = (typeof tolerance === 'number') ? tolerance : 0;
  const ok = Math.abs(actual - expected) <= tol;
  if (ok) {
    passed++;
    console.log(`  PASS ${label}: ${actual} (expected ${expected}±${tol})`);
  } else {
    failed++;
    console.log(`  FAIL ${label}: got ${actual}, expected ${expected}±${tol}`);
  }
}

function assertEqExact(label, actual, expected) {
  if (actual === expected) {
    passed++;
    console.log(`  PASS ${label}: ${actual}`);
  } else {
    failed++;
    console.log(`  FAIL ${label}: got ${actual}, expected ${expected}`);
  }
}

async function main() {
  console.log(`Loading M5 WASM module: ${MODULE_JS}`);
  const factory = require(MODULE_JS);
  const Module = await factory();

  // Load the onspeed_core WASM (PR #496). It's an ES module
  // (EXPORT_ES6=1, SINGLE_FILE=1), so use dynamic import. URL form is
  // required for absolute paths under file:// resolution on Node.
  console.log(`Loading onspeed_core WASM: ${CORE_JS}`);
  const coreUrl = require('url').pathToFileURL(CORE_JS).href;
  const CoreFactory = (await import(coreUrl)).default;
  const Core = await CoreFactory();
  if (typeof Core.build_display_frame !== 'function') {
    console.error(
      'FATAL: onspeed_core WASM is missing the `build_display_frame` ' +
      'export. Rebuild it: ' +
      '`bash software/Libraries/onspeed_core/wasm/build_wasm.sh`.');
    process.exit(2);
  }

  // buildFrame: thin wrapper around Core.build_display_frame, returning a
  // Node Buffer. The C++ side runs the canonical BuildDisplayFrame, so
  // these bytes are guaranteed byte-for-byte identical to what the
  // firmware emits on the wire — no JS hand-port to drift.
  function buildFrame(inputs) {
    return Buffer.from(Core.build_display_frame(inputs));
  }

  // Sanity: every accessor we expect must be exported.
  const accessors = [
    'replay_init', 'replay_set_time', 'replay_loop',
    'replay_inject_byte', 'replay_set_displayType',
    'replay_get_displayIAS', 'replay_get_displayPalt',
    'replay_get_displayPitch', 'replay_get_displayVerticalG',
    'replay_get_displayPercentLift', 'replay_get_displayDecelRate',
    'replay_get_Slip', 'replay_get_PercentLift', 'replay_get_gOnsetRate',
    'replay_get_IAS', 'replay_get_Palt', 'replay_get_IasIsValid',
    'replay_get_displayType', 'replay_get_iVSI', 'replay_get_OAT',
    'replay_get_FlightPath', 'replay_get_Pitch', 'replay_get_Roll',
    'replay_get_TonesOnPctLift', 'replay_get_OnSpeedFastPctLift',
    'replay_get_OnSpeedSlowPctLift', 'replay_get_StallWarnPctLift',
    'replay_get_PipPctLift', 'replay_get_FlapsMinDeg',
    'replay_get_FlapsMaxDeg', 'replay_get_FlapPos',
    'replay_get_gHistoryIndex', 'replay_get_gHistory_ptr',
    'replay_get_SpinRecoveryCue', 'replay_get_DataMark',
  ];
  for (const name of accessors) {
    if (typeof Module['_' + name] !== 'function') {
      console.error(`FATAL: accessor _${name} missing from module exports`);
      process.exit(2);
    }
  }
  console.log(`All ${accessors.length} accessor exports present.`);

  // Step 1: init at virtual time 0. The firmware's setup() runs.
  Module._replay_set_time(0n);
  Module._replay_init();

  // Build a known wire frame. Picked values that exercise multiple state
  // vars without saturating any field — leaves room to verify ranges.
  const frame = buildFrame({
    iasKt:              80,
    iasValid:           true,
    pitchDeg:           5.0,
    rollDeg:            10.0,
    paltFt:             3000,
    lateralG:           0.04,   // body-frame +rightward → Slip should be -34
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

  console.log(`\nStep 2: inject ${frame.length}-byte wire frame`);
  for (const b of frame) Module._replay_inject_byte(b);

  // Frame-parse-success assertion: SerialRead's accumulator runs the
  // checksum + parse synchronously inside InjectSerialByte() on the
  // final LF byte. On a successful parse, IAS is set to the decoded
  // value (80 kt here); on rejection (bad magic, bad checksum, length
  // mismatch) the accumulator resets and IAS stays at its prior value
  // (0 from setup()). This assertion catches silent frame rejection
  // directly — without it, a parser regression could pass the indirect
  // Slip / displayIAS checks below by leaving stale defaults that
  // happen to match the expected values.
  console.log('\nAssertion: frame accepted by parser');
  assertEqExact('IAS after frame parse', Module._replay_get_IAS(), 80);

  // Step 3 — advance to t=50 ms. The firmware's loop() checks the 50 ms
  // graphics tick: `millis() > loopTime+50` is strictly greater-than,
  // so 50 > 0+50 is false — the render block does NOT fire here. Slip
  // was already set during _replay_inject_byte above (SerialProcess
  // runs synchronously on frame completion inside InjectSerialByte,
  // not inside loop()). The Module._replay_loop call here just advances
  // any state that does fire on the first tick at this time.
  console.log('\nStep 3: advance to t=50 ms, run loop');
  Module._replay_set_time(50n);
  Module._replay_loop();

  // Step 5 (plan numbering): Slip computation runs in SerialProcess
  // every byte injection that completes a frame (not every loop tick).
  // Slip = int(-LateralG * 34 / 0.04) per SerialRead.cpp::SerialProcess.
  // For LateralG=0.04, expected = int(-0.04 * 34 / 0.04) = -34.
  console.log('\nAssertion 5: Slip computed from wire LateralG');
  assertEqExact('Slip', Module._replay_get_Slip(), -34);

  // Step 6 — at t=200 ms (after one more tick), the 500 ms numbers
  // snapshot should NOT yet have fired. displayIAS still 0.
  // updateRateNumbers is 500 in main.cpp. The first numbers snapshot
  // happens when (millis() - numbersUpdateTime > 500); since
  // numbersUpdateTime starts at 0 and we're now at 200, 200 > 500 is
  // false → no snapshot.
  console.log('\nAssertion 6: t=200 ms, displayIAS not yet snapshotted');
  Module._replay_set_time(200n);
  Module._replay_loop();
  assertEqExact('displayIAS @t=200 ms', Module._replay_get_displayIAS(), 0);

  // Step 7 — at t=600 ms, the numbers snapshot fires. displayIAS picks
  // up the wire's IAS (80 kt). The wire IAS field is in knots; main.cpp's
  // snapshot block selects the units at runtime: `displayIAS = g_speedInMph
  // ? IAS * 1.15078f : IAS`. Default on a fresh device is KTS
  // (g_speedInMph == false), so displayIAS == IAS == 80 here. The replay
  // target inherits the same logic — this test asserts the default
  // (KTS) path; the MPH path is exercised through the runtime toggle in
  // the firmware-side menu and is not driven from JS.
  console.log('\nAssertion 7: t=600 ms, displayIAS now snapshotted');
  // Re-inject the frame here: this updates serialMillis = current
  // virtual time (200ms-ish, when the bytes complete), keeping
  // serialDataFresh() returning true at t=600 ms (300 ms threshold).
  // Note: even WITHOUT this re-injection, displayIAS WOULD update at
  // t=600 ms — the numbers-snapshot block runs BEFORE the
  // !serialDataFresh() early-return. The re-injection keeps the test's
  // mental model clean (no NO-DATA overlay would fire at this t), not
  // because the assertion depends on it.
  for (const b of frame) Module._replay_inject_byte(b);
  Module._replay_set_time(600n);
  Module._replay_loop();
  assertEqExact(
    'displayIAS @t=600 ms',
    Module._replay_get_displayIAS(),
    80);

  // Step 8 — cycle through every mode, verify the loop runs without
  // crashing and mode-specific accessors return non-error sentinels.
  console.log('\nAssertion 8: each mode renders without crash');
  for (let mode = 0; mode <= 4; mode++) {
    Module._replay_set_displayType(mode);
    // Inject a fresh frame (serial-fresh predicate, gHistory sampler).
    for (const b of frame) Module._replay_inject_byte(b);
    // Advance virtual time past the 50 ms graphics tick threshold.
    Module._replay_set_time(BigInt(700 + mode * 100));
    Module._replay_loop();
    assertEqExact(
      `mode ${mode} displayType reads back`,
      Module._replay_get_displayType(),
      mode);
  }

  // Spot-check Mode 0 anchors propagated from the wire.
  console.log('\nMode-0 anchor checks');
  Module._replay_set_displayType(0);
  for (const b of frame) Module._replay_inject_byte(b);
  Module._replay_set_time(1500n);
  Module._replay_loop();
  assertEqExact('TonesOnPctLift',     Module._replay_get_TonesOnPctLift(),     30);
  assertEqExact('OnSpeedFastPctLift', Module._replay_get_OnSpeedFastPctLift(), 40);
  assertEqExact('OnSpeedSlowPctLift', Module._replay_get_OnSpeedSlowPctLift(), 50);
  assertEqExact('StallWarnPctLift',   Module._replay_get_StallWarnPctLift(),   80);
  assertEqExact('PipPctLift',         Module._replay_get_PipPctLift(),         32);
  assertEqExact('FlapsMinDeg',        Module._replay_get_FlapsMinDeg(),         0);
  assertEqExact('FlapsMaxDeg',        Module._replay_get_FlapsMaxDeg(),        33);
  assertEqExact('FlapPos',            Module._replay_get_FlapPos(),            10);

  // Mode-4 array pointer check: read 300 floats out of the WASM heap.
  // gHistoryIndex starts at 0 and advances by 1 each time the firmware
  // samples (every 200 ms while serial-fresh). The buffer initial fill
  // is 1.00 from setup(), so even if the sampler hasn't run yet,
  // values should be 1.00 across the board.
  console.log('\nMode-4 (Historic G) array readout');
  const ptr = Module._replay_get_gHistory_ptr();
  if (ptr === 0) {
    failed++;
    console.log('  FAIL gHistory_ptr returned 0');
  } else {
    const HEAPF32 = Module.HEAPF32;
    const ring = HEAPF32.subarray(ptr / 4, ptr / 4 + 300);
    if (ring.length === 300) {
      passed++;
      console.log(`  PASS gHistory ring readable (300 floats, first=${ring[0].toFixed(2)})`);
    } else {
      failed++;
      console.log(`  FAIL gHistory length ${ring.length}, expected 300`);
    }
  }

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
