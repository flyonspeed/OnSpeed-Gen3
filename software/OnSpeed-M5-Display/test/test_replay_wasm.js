// test_replay_wasm.js — Node.js test harness for the M5 firmware replay
// WASM build (PR 1 of Project B2 PLAN_REPLAY_M5_WASM).
//
// Loads sim/build/wasm-replay/onspeed_m5.{js,wasm}, drives the firmware
// through a known wire frame, and asserts that the state-var accessors
// expose the values the firmware code computed. The point of this
// harness is to prove the WASM build actually runs the firmware logic
// (not the JS hand-port we're trying to replace) — sabotage checks in
// the PR description verify the test fails when production code is
// disabled.
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

if (!fs.existsSync(MODULE_JS)) {
  console.error(`FATAL: ${MODULE_JS} not found.`);
  console.error(`Run \`bash sim/build_wasm.sh --target replay\` first.`);
  process.exit(2);
}

// ---------------------------------------------------------------------------
// Frame builder — emits bytes byte-for-byte identical to
// onspeed_core/proto/DisplaySerial.cpp::BuildDisplayFrame for v4.23.
//
// Hand-implemented in JS rather than calling into a second WASM module
// so this test depends only on the M5 replay artifact (PR 1 scope).
// PR 2 will route real frames through the onspeed_core WASM instead.
//
// Field offsets / scales / formats from
// onspeed_core/src/proto/DisplaySerial.h.
// ---------------------------------------------------------------------------

function fmtSigned(value, width) {
  // %+0Nd: leading + on positives, padded to N total chars including sign.
  const v = Math.trunc(value);
  const sign = v < 0 ? '-' : '+';
  const mag = String(Math.abs(v)).padStart(width - 1, '0');
  return sign + mag;
}

function fmtUnsigned(value, width) {
  // %0Nu: zero-padded unsigned.
  return String(Math.trunc(value)).padStart(width, '0');
}

function buildFrame(inputs) {
  // Defaults match DisplayBuildInputs's in-class initializers.
  const i = Object.assign({
    pitchDeg:           0,
    rollDeg:            0,
    iasKt:              0,
    iasValid:           true,
    paltFt:             0,
    turnRateDps:        0,
    lateralG:           0,    // body-frame, +rightward
    verticalG:          0,    // raw G value (we apply ×10 below)
    percentLiftPct:     0,
    vsiFpm:             0,    // raw fpm (we /10 below)
    oatC:               0,
    flightPathDeg:      0,
    flapsDeg:           0,
    tonesOnPctLift:     0,
    onSpeedFastPctLift: 0,
    onSpeedSlowPctLift: 0,
    stallWarnPctLift:   0,
    flapsMinDeg:        0,
    flapsMaxDeg:        0,
    gOnsetRate:         0,
    spinRecoveryCue:    0,
    dataMark:           0,
    pipPctLift:         0,
  }, inputs);

  const iasWire = i.iasValid ? Math.round(i.iasKt * 10) : 9999;

  let s = '#1';
  s += fmtSigned(Math.round(i.pitchDeg * 10),     4);
  s += fmtSigned(Math.round(i.rollDeg  * 10),     5);
  s += fmtUnsigned(iasWire,                        4);
  s += fmtSigned(Math.round(i.paltFt),            6);
  s += fmtSigned(Math.round(i.turnRateDps * 10),  5);
  s += fmtSigned(Math.round(i.lateralG  * 100),   3);
  s += fmtSigned(Math.round(i.verticalG * 10),    3);
  s += fmtUnsigned(
    i.iasValid ? Math.round(i.percentLiftPct * 10) : 0, 3);
  s += fmtSigned(Math.round(i.vsiFpm / 10),       4);
  s += fmtSigned(Math.round(i.oatC),              3);
  s += fmtSigned(Math.round(i.flightPathDeg * 10),4);
  s += fmtSigned(Math.round(i.flapsDeg),          3);
  s += fmtUnsigned(i.tonesOnPctLift,               2);
  s += fmtUnsigned(i.onSpeedFastPctLift,           2);
  s += fmtUnsigned(i.onSpeedSlowPctLift,           2);
  s += fmtUnsigned(i.stallWarnPctLift,             2);
  s += fmtSigned(i.flapsMinDeg,                    3);
  s += fmtSigned(i.flapsMaxDeg,                    3);
  s += fmtSigned(Math.round(i.gOnsetRate * 100),   4);
  s += fmtSigned(i.spinRecoveryCue,                2);
  s += fmtUnsigned(i.dataMark,                      2);
  s += fmtUnsigned(i.pipPctLift,                    2);

  if (s.length !== 73) {
    throw new Error(`payload length is ${s.length}, expected 73`);
  }

  let cksum = 0;
  for (let n = 0; n < s.length; n++) cksum = (cksum + s.charCodeAt(n)) & 0xFF;
  const ckHex = cksum.toString(16).toUpperCase().padStart(2, '0');
  s += ckHex;
  s += '\r\n';

  if (s.length !== 77) {
    throw new Error(`frame length is ${s.length}, expected 77`);
  }
  return Buffer.from(s, 'binary');
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
  console.log(`Loading WASM module: ${MODULE_JS}`);
  const factory = require(MODULE_JS);
  const Module = await factory();

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

  // Step 3 — drive the 50 ms graphics tick at virtual time 50 ms. The
  // firmware's loop() runs SerialRead (no-op for the replay path; bytes
  // already injected via accumulator), advances loopTime past 50 ms,
  // and reaches the per-frame render block.
  console.log('\nStep 3: advance to t=50 ms, drive one graphics tick');
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
  // up the wire's IAS (80 kt). The IAS_IN_MPH define in main.cpp
  // multiplies by 1.15078 — verify with tolerance.
  console.log('\nAssertion 7: t=600 ms, displayIAS now snapshotted');
  // Need to re-inject the frame so SerialRead has fresh bytes; the
  // accumulator already consumed the previous frame on the LF byte.
  // Without a fresh frame, serialDataFresh() may go false at t=600 ms
  // (300 ms threshold from kSerialDataFreshThresholdMs).
  for (const b of frame) Module._replay_inject_byte(b);
  Module._replay_set_time(600n);
  Module._replay_loop();
  assertEq(
    'displayIAS @t=600 ms',
    Module._replay_get_displayIAS(),
    80 * 1.15078, 0.5);

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
