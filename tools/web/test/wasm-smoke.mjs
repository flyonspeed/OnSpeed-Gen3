// wasm-smoke.mjs — smoke test for the onspeed_core WASM build.
//
// Step 0 (PR #462): compute_percent_lift.
// Step 1 (this PR): compute_anchors, parse_config.
//
// Loads the compiled module and calls each export with known inputs,
// asserting outputs match the C++ formula within floating-point tolerance.
//
// Run:
//   node tools/web/test/wasm-smoke.mjs
//
// This file is also invoked by `npm test` in tools/web/package.json.

import { fileURLToPath } from 'url';
import path from 'path';
import fs from 'fs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const wasmJsPath = path.resolve(
    __dirname,
    '../../../software/Libraries/onspeed_core/wasm/dist/onspeed_core.js'
);

// Dynamic import so we get a clean error if the file doesn't exist.
let OnSpeedCoreModule;
try {
    // SINGLE_FILE build: the .js contains everything (WASM inlined as base64).
    const mod = await import(wasmJsPath);
    OnSpeedCoreModule = mod.default;
} catch (err) {
    if (process.env.ALLOW_MISSING_WASM === '1') {
        console.warn(`SKIP: onspeed_core.js not found at ${wasmJsPath}`);
        console.warn('      Set ALLOW_MISSING_WASM=1 suppressed this failure (local use only).');
        console.warn(`      (${err.message})`);
        process.exit(0);
    }
    console.error(`FAIL: onspeed_core.js not found at ${wasmJsPath}`);
    console.error('      Build it first: bash software/Libraries/onspeed_core/wasm/build_wasm.sh');
    console.error(`      (${err.message})`);
    process.exit(1);
}

const Module = await OnSpeedCoreModule();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function assertClose(label, actual, expected, tolerance) {
    if (Math.abs(actual - expected) > tolerance) {
        console.error(`FAIL: ${label}: expected ~${expected}, got ${actual}`);
        process.exit(1);
    }
    console.log(`OK: ${label}: ${typeof actual === 'number' ? actual.toFixed(3) : actual}`);
}

function assertEqual(label, actual, expected) {
    if (actual !== expected) {
        console.error(`FAIL: ${label}: expected ${expected}, got ${actual}`);
        process.exit(1);
    }
    console.log(`OK: ${label}: ${actual}`);
}

function assertDefined(label, val) {
    if (val === undefined || val === null) {
        console.error(`FAIL: ${label}: got ${val}`);
        process.exit(1);
    }
    console.log(`OK: ${label} defined (${val})`);
}

// ---------------------------------------------------------------------------
// Step 0: compute_percent_lift
//
// Known-correct answer:
//   aoa=3.24, alpha_0=-3.72, alpha_stall=10.31, stallwarn=8.24, ias=true
//   → (3.24 - (-3.72)) / (10.31 - (-3.72)) × 100 ≈ 49.608
// ---------------------------------------------------------------------------

console.log('\n--- compute_percent_lift ---');

assertClose(
    'compute_percent_lift(3.24, -3.72, 10.31, 8.24, true)',
    Module.compute_percent_lift(3.24, -3.72, 10.31, 8.24, true),
    49.6,
    0.1
);

// ias_valid=false must return 0.
assertEqual(
    'ias_valid=false → 0',
    Module.compute_percent_lift(3.24, -3.72, 10.31, 8.24, false),
    0.0
);

// Below alpha_0 clamps to 0.
assertEqual(
    'aoa below alpha_0 clamps to 0',
    Module.compute_percent_lift(-5.0, -3.72, 10.31, 8.24, true),
    0.0
);

// Above alpha_stall clamps to 99.9.
// Use a tolerant comparison: WASM returns C++ float32 (~99.9000015259...),
// which differs from the JS binary64 literal 99.9 at strict equality.
assertClose(
    'aoa above alpha_stall clamps to ~99.9',
    Module.compute_percent_lift(15.0, -3.72, 10.31, 8.24, true),
    99.9,
    0.01
);

// ---------------------------------------------------------------------------
// Step 1: compute_anchors
//
// RV-10 N720AK clean-flap detent data from the config fixture.
// Fixture: test/fixtures/config_xml/n720ak_2_11_26.cfg
//
// Flap 0 (clean): degrees=0, potPosition=3908,
//   ldmaxAoa=4.1, onSpeedFastAoa=4.08, onSpeedSlowAoa=4.95, stallWarnAoa=7.98
//   alpha0=0, alphaStall=0  (old-style config, pre-ALPHA0 fields)
//
// With only one flap entry, pipPctLift == tonesOnPctLift exactly.
// activeIndex=0, rawAdc=3908 (at the clean detent pot position).
//
// Expected percent values with alpha0=0:
//   tonesOnPctLift = ComputePercentLift(4.1, 0, 0, 7.98)
//     alpha_stall=0 is the uncalibrated sentinel, so PercentLift falls back
//     to (stallwarn + 1.5) as the effective stall reference:
//     effective_stall = 7.98 + 1.5 = 9.48
//     pct = (4.1 - 0) / (9.48 - 0) * 100 ≈ 43.25 → int 43
//
// Verify: result is a JS object with all six fields.
// Verify: all int fields are non-negative integers.
// ---------------------------------------------------------------------------

console.log('\n--- compute_anchors ---');

const singleFlap = [{
    degrees:        0,
    potPosition:    3908,
    ldmaxAoa:       4.1,
    onSpeedFastAoa: 4.08,
    onSpeedSlowAoa: 4.95,
    stallWarnAoa:   7.98,
    alpha0:         0.0,
    alphaStall:     0.0,
}];

const anchors = Module.compute_anchors(singleFlap, 0, 3908);

assertDefined('anchors.pipPctLift',         anchors.pipPctLift);
assertDefined('anchors.tonesOnPctLift',     anchors.tonesOnPctLift);
assertDefined('anchors.onSpeedFastPctLift', anchors.onSpeedFastPctLift);
assertDefined('anchors.onSpeedSlowPctLift', anchors.onSpeedSlowPctLift);
assertDefined('anchors.stallWarnPctLift',   anchors.stallWarnPctLift);
assertDefined('anchors.flapsDeg',           anchors.flapsDeg);

// Single detent: pip == tonesOn
if (anchors.pipPctLift !== anchors.tonesOnPctLift) {
    console.error(
        `FAIL: single detent: pipPctLift (${anchors.pipPctLift}) ` +
        `should equal tonesOnPctLift (${anchors.tonesOnPctLift})`);
    process.exit(1);
}
console.log(`OK: single detent: pipPctLift == tonesOnPctLift (${anchors.pipPctLift})`);

// All values in [0, 99] or 0 for flapsDeg (which is degrees, can be negative
// but this detent is 0 degrees).
for (const key of ['pipPctLift', 'tonesOnPctLift', 'onSpeedFastPctLift',
                   'onSpeedSlowPctLift', 'stallWarnPctLift']) {
    const v = anchors[key];
    if (!Number.isInteger(v) || v < 0 || v > 99) {
        console.error(`FAIL: anchors.${key} = ${v} not in [0,99] integer range`);
        process.exit(1);
    }
}
console.log(`OK: all pct anchors are integers in [0,99]`);

// flapsDeg for the single-entry case: equals iDegrees (0).
assertEqual('anchors.flapsDeg (single entry, at-detent)', anchors.flapsDeg, 0);

// Two-flap test: verify pip slides between detents.
// Use flap 0 and flap 33 from the N720AK config fixture.
const twoFlaps = [
    {
        degrees: 0, potPosition: 3908,
        ldmaxAoa: 4.1, onSpeedFastAoa: 4.08, onSpeedSlowAoa: 4.95, stallWarnAoa: 7.98,
        alpha0: 0.0, alphaStall: 0.0,
    },
    {
        degrees: 33, potPosition: 8,
        ldmaxAoa: -1.12, onSpeedFastAoa: 3.79, onSpeedSlowAoa: 5.23, stallWarnAoa: 9.24,
        alpha0: 0.0, alphaStall: 0.0,
    },
];

const anchorsClean = Module.compute_anchors(twoFlaps, 0, 3908);   // at clean detent
const anchorsFullFlap = Module.compute_anchors(twoFlaps, 1, 8);   // at full-flap detent

// At the clean end, pip == clean L/Dmax pct.
// At full flap, pip != clean L/Dmax pct (it slides toward full-flap target).
if (anchorsClean.pipPctLift === anchorsFullFlap.pipPctLift) {
    console.error(
        `FAIL: pip should slide between clean (${anchorsClean.pipPctLift}) ` +
        `and full-flap (${anchorsFullFlap.pipPctLift}); values are equal`);
    process.exit(1);
}
console.log(
    `OK: pip slides: clean=${anchorsClean.pipPctLift}, ` +
    `full-flap=${anchorsFullFlap.pipPctLift}`);

// Body-angle convention: alpha_0 is typically NEGATIVE for the real airplane
// (RV-10 = -3.72°). The anchors path must preserve this — a regression that
// floored alpha_0 at 0 or took std::abs() would silently corrupt every
// percent-lift display in the Replay UI. Pin the contract here.
//
// Expected values computed from C++ with alpha_0=-3.72, alpha_stall=10.31:
//   stallWarnPctLift: (8.24 - (-3.72)) / (10.31 - (-3.72)) × 100 ≈ 85.2 → 85
{
    const anchorsRV10 = Module.compute_anchors([
        {
            potPosition:    0,
            degrees:        0,
            ldmaxAoa:       6.5,
            onSpeedFastAoa: 7.5,
            onSpeedSlowAoa: 8.5,
            stallWarnAoa:   8.24,
            alpha0:         -3.72,
            alphaStall:     10.31,
            kFit:           0.0,
        }
    ], 0, 0);

    console.log(`\n--- compute_anchors (RV-10 alpha_0=-3.72) ---`);
    console.log(`    ${JSON.stringify(anchorsRV10)}`);

    const checkInRange = (label, val) => {
        if (typeof val !== 'number' || val < 0 || val > 99) {
            console.error(`FAIL: anchors ${label} out of [0,99]: ${val}`);
            process.exit(1);
        }
    };
    checkInRange('tonesOnPctLift',     anchorsRV10.tonesOnPctLift);
    checkInRange('onSpeedFastPctLift', anchorsRV10.onSpeedFastPctLift);
    checkInRange('onSpeedSlowPctLift', anchorsRV10.onSpeedSlowPctLift);
    checkInRange('stallWarnPctLift',   anchorsRV10.stallWarnPctLift);

    // Pin exact integer values from C++ output. With alpha_0=-3.72,
    // alpha_stall=10.31: if alpha_0 were wrongly floored at 0, these
    // would all shift (e.g. stallWarnPctLift → 79 instead of 85).
    assertEqual('anchorsRV10.tonesOnPctLift',     anchorsRV10.tonesOnPctLift,     72);
    assertEqual('anchorsRV10.onSpeedFastPctLift', anchorsRV10.onSpeedFastPctLift, 79);
    assertEqual('anchorsRV10.onSpeedSlowPctLift', anchorsRV10.onSpeedSlowPctLift, 87);
    assertEqual('anchorsRV10.stallWarnPctLift',   anchorsRV10.stallWarnPctLift,   85);
    console.log(`OK: compute_anchors with RV-10 alpha_0=-3.72 returns sensible anchors`);
}

// Empty flaps array must return all zeros (uncalibrated sentinel).
const anchorsEmpty = Module.compute_anchors([], 0, 0);
for (const key of ['pipPctLift', 'tonesOnPctLift', 'onSpeedFastPctLift',
                   'onSpeedSlowPctLift', 'stallWarnPctLift', 'flapsDeg']) {
    if (anchorsEmpty[key] !== 0) {
        console.error(`FAIL: empty flaps: anchors.${key} = ${anchorsEmpty[key]}, expected 0`);
        process.exit(1);
    }
}
console.log(`OK: empty flaps → all-zero anchors (uncalibrated)`);

// ---------------------------------------------------------------------------
// Step 1: parse_config
//
// Smoke test uses the inline minimal V2 config — no filesystem dependency.
// We verify: field presence, flaps array shape, numeric types.
//
// The full N720AK fixture is exercised by the C++ unit tests (test_config_xml).
// Here we just verify the WASM binding compiles and returns the right shape.
// ---------------------------------------------------------------------------

console.log('\n--- parse_config ---');

// Minimal valid V2 config with one flap entry.
const minimalV2Xml = `<CONFIG2>
  <AOA_SMOOTHING>15</AOA_SMOOTHING>
  <PRESSURE_SMOOTHING>15</PRESSURE_SMOOTHING>
  <DATASOURCE>SENSORS</DATASOURCE>
  <FLAP_POSITION>
    <DEGREES>0</DEGREES>
    <POT_VALUE>3908</POT_VALUE>
    <LDMAXAOA>4.1</LDMAXAOA>
    <ONSPEEDFASTAOA>4.08</ONSPEEDFASTAOA>
    <ONSPEEDSLOWAOA>4.95</ONSPEEDSLOWAOA>
    <STALLWARNAOA>7.98</STALLWARNAOA>
    <STALLAOA>0</STALLAOA>
    <MANAOA>0</MANAOA>
    <ALPHA0>-3.72</ALPHA0>
    <ALPHASTALL>10.31</ALPHASTALL>
    <KFIT>0.0</KFIT>
    <AOA_CURVE>
      <TYPE>1</TYPE>
      <X3>0</X3>
      <X2>0</X2>
      <X1>1</X1>
      <X0>0</X0>
    </AOA_CURVE>
  </FLAP_POSITION>
</CONFIG2>`;

const cfg = Module.parse_config(minimalV2Xml);

// Must not have an error field.
if (cfg.error !== undefined) {
    console.error(`FAIL: parse_config returned error: ${cfg.error}`);
    process.exit(1);
}
console.log(`OK: parse_config returned no error`);

// Top-level fields.
assertEqual('cfg.aoaSmoothing', cfg.aoaSmoothing, 15);
assertEqual('cfg.pressureSmoothing', cfg.pressureSmoothing, 15);
assertEqual('cfg.dataSource', cfg.dataSource, 'SENSORS');

// flaps array.
assertDefined('cfg.flaps', cfg.flaps);
if (!Array.isArray(cfg.flaps)) {
    console.error(`FAIL: cfg.flaps is not an array (got ${typeof cfg.flaps})`);
    process.exit(1);
}
if (cfg.flaps.length !== 1) {
    console.error(`FAIL: expected 1 flap entry, got ${cfg.flaps.length}`);
    process.exit(1);
}
console.log(`OK: cfg.flaps.length = ${cfg.flaps.length}`);

const flap0 = cfg.flaps[0];
assertDefined('flap0.degrees',       flap0.degrees);
assertDefined('flap0.potPosition',   flap0.potPosition);
assertDefined('flap0.ldmaxAoa',      flap0.ldmaxAoa);
assertDefined('flap0.onSpeedFastAoa',flap0.onSpeedFastAoa);
assertDefined('flap0.onSpeedSlowAoa',flap0.onSpeedSlowAoa);
assertDefined('flap0.stallWarnAoa',  flap0.stallWarnAoa);
assertDefined('flap0.alpha0',        flap0.alpha0);
assertDefined('flap0.alphaStall',    flap0.alphaStall);

assertEqual('flap0.degrees',     flap0.degrees, 0);
assertEqual('flap0.potPosition', flap0.potPosition, 3908);

// alpha_0 round-trips correctly (typically negative).
assertClose('flap0.alpha0',    flap0.alpha0,    -3.72,  0.001);
assertClose('flap0.alphaStall',flap0.alphaStall, 10.31, 0.001);
assertClose('flap0.ldmaxAoa',  flap0.ldmaxAoa,   4.1,  0.001);

// parse_config on invalid XML must return an error, not throw.
const badCfg = Module.parse_config('this is not xml');
if (badCfg.error === undefined) {
    console.error(`FAIL: parse_config on garbage should return {error:...}`);
    process.exit(1);
}
console.log(`OK: parse_config(garbage) → error: "${badCfg.error}"`);

// ---------------------------------------------------------------------------
// Step 2: LogReplayEngine round-trip — flapsRawAdcAvailable=true
//
// When flapsRawAdcAvailable is true (log has flapsRawADC column), step()
// returns a result immediately for every row — no lag period, no buffering.
// flush() returns an empty array.
//
// The C++ engine tests (test_aoa_calculator, regression golden) cover
// numeric correctness.  This smoke test verifies only that the WASM
// binding compiles, the class is constructible, and the data moves
// across the boundary without WASM memory errors.
//
// We use the same minimal V2 config constructed above (cfg / minimalV2Xml).
// The config has alpha_stall=10.31 and iAoaSmoothing=15; with pfwdSmoothed
// and p45Smoothed both at zero, the engine produces aoa=0 and coeffP=0.
// ---------------------------------------------------------------------------

console.log('\n--- LogReplayEngine (flapsRawAdcAvailable=true) ---');

// Construct engine with flapsRawAdcAvailable=true so step() returns
// immediately (no lag period, no synth circular buffer).
const engine = new Module.LogReplayEngine(cfg, 50, true);
assertDefined('LogReplayEngine instance', engine);

// Minimal log row — all zero / default.  The engine doesn't crash on
// all-zero inputs (pressure sensors returning 0 in ground test).
const minimalRow = {
    pfwdSmoothed:       0.0,
    p45Smoothed:        0.0,
    pStaticMbar:        0.0,
    paltFt:             5000.0,
    iasKt:              10.0,
    iasValid:           true,
    flapsPos:           0,
    flapsRawAdc:        2048,
    flapsRawAdcPresent: true,
    imuVerticalG:       1.0,
    imuLateralG:        0.05,
    imuForwardG:       -0.02,
    imuRollRateDps:     0.0,
    imuPitchRateDps:    0.0,
    imuYawRateDps:      0.0,
    pitchDeg:          -3.0,
    rollDeg:            0.5,
    flightPathDeg:      0.0,
    vsiFpm:             0.0,
    dataMark:           0,
};

const stepResult = engine.step(minimalRow);
assertDefined('step() returns a result (flapsRawAdcAvailable=true)', stepResult);

// step() must return a non-null object when flapsRawAdcAvailable=true.
if (stepResult === null) {
    console.error('FAIL: step() returned null with flapsRawAdcAvailable=true (no lag expected)');
    process.exit(1);
}

// Verify field presence.
for (const field of [
    'iasKt', 'paltFt', 'iasValid', 'aoaDeg', 'coeffP',
    'flapsPos', 'flapsIndex', 'flapsRawAdc', 'flapsRawAdcPresent',
    'pitchDeg', 'rollDeg', 'flightPathDeg', 'kalmanVsiMps',
    'imuForwardG', 'imuLateralG', 'imuVerticalG',
    'imuRollRateDps', 'imuPitchRateDps', 'imuYawRateDps',
    'accelLatSmoothed', 'accelVertSmoothed', 'accelFwdSmoothed',
    'dataMark',
]) {
    if (stepResult[field] === undefined) {
        console.error(`FAIL: step() result missing field: ${field}`);
        process.exit(1);
    }
}
console.log('OK: step() result has all expected fields');

// Spot-check passthrough fields.
assertClose('step().iasKt',    stepResult.iasKt,    10.0, 0.001);
assertClose('step().paltFt',   stepResult.paltFt,  5000.0, 0.5);
assertEqual('step().iasValid', stepResult.iasValid, true);
assertEqual('step().flapsPos', stepResult.flapsPos, 0);

// Smoothed accel must be a finite number (first call: seeded to input).
if (!Number.isFinite(stepResult.accelLatSmoothed)) {
    console.error(`FAIL: accelLatSmoothed is not finite: ${stepResult.accelLatSmoothed}`);
    process.exit(1);
}
console.log(`OK: accelLatSmoothed is finite (${stepResult.accelLatSmoothed.toFixed(4)})`);

// Second step: smoothed lateral must drift toward the new input.
const row2 = Object.assign({}, minimalRow, { imuLateralG: 0.5 });
const step2 = engine.step(row2);
if (step2 === null) {
    console.error('FAIL: second step() returned null with flapsRawAdcAvailable=true');
    process.exit(1);
}
if (Math.abs(step2.accelLatSmoothed - stepResult.accelLatSmoothed) < 0.001) {
    console.error('FAIL: accelLatSmoothed did not update on second step');
    process.exit(1);
}
console.log(`OK: accelLatSmoothed updates across steps ` +
    `(${stepResult.accelLatSmoothed.toFixed(4)} → ${step2.accelLatSmoothed.toFixed(4)})`);

// flush() must return an empty array when flapsRawAdcAvailable=true
// (no synth buffer; nothing was held back).
const flushed = engine.flush();
if (!Array.isArray(flushed)) {
    console.error(`FAIL: flush() did not return an Array (got ${typeof flushed})`);
    process.exit(1);
}
if (flushed.length !== 0) {
    console.error(`FAIL: flush() returned ${flushed.length} items with flapsRawAdcAvailable=true (expected 0)`);
    process.exit(1);
}
console.log(`OK: flush() returns empty Array when flapsRawAdcAvailable=true (length=${flushed.length})`);

// reset() should not throw and should produce the same first-step result
// as the original construction.
engine.reset();
const resetResult = engine.step(minimalRow);
if (resetResult === null) {
    console.error('FAIL: step() after reset() returned null with flapsRawAdcAvailable=true');
    process.exit(1);
}
assertClose('after reset: accelLatSmoothed == first step (seeded to input)',
    resetResult.accelLatSmoothed,
    stepResult.accelLatSmoothed,
    0.001);
console.log('OK: reset() restores initial EMA state');

// delete() must free WASM memory without crashing.
engine.delete();
console.log('OK: delete() completed without error');

// ---------------------------------------------------------------------------
// Step 2: LogReplayEngine synth path — flapsRawAdcAvailable=false
//
// When flapsRawAdcAvailable is false (pre-PR-#221 log, no flapsRawADC column),
// the engine holds rows in a circular buffer of synthHalfWindowTicks_ rows
// (kSynthHalfWindowSec=2s × 50Hz = 100 ticks at 50 Hz).
//
// Behaviour:
//   - step() returns null for the first 100 rows (lag period, buffer filling).
//   - After 100 rows, step() returns the synth result for the row 100 ticks back.
//   - After the last row, flush() returns the remaining ~100 tail rows.
//
// This smoke verifies: null during lag, non-null after lag, flush() returns
// the tail, and flush() is a JS Array of result objects.
// ---------------------------------------------------------------------------

console.log('\n--- LogReplayEngine (flapsRawAdcAvailable=false, synth path) ---');

// Use a two-flap config so the engine has real pot positions for both detents.
// Detent 0 (clean):    potPosition=3908
// Detent 33 (full):    potPosition=8
// The smoothstep sweeps between these two values across the transition window.
const synthCfgXml = `<CONFIG2>
  <AOA_SMOOTHING>15</AOA_SMOOTHING>
  <PRESSURE_SMOOTHING>15</PRESSURE_SMOOTHING>
  <DATASOURCE>SENSORS</DATASOURCE>
  <FLAP_POSITION>
    <DEGREES>0</DEGREES>
    <POT_VALUE>3908</POT_VALUE>
    <LDMAXAOA>4.1</LDMAXAOA>
    <ONSPEEDFASTAOA>4.08</ONSPEEDFASTAOA>
    <ONSPEEDSLOWAOA>4.95</ONSPEEDSLOWAOA>
    <STALLWARNAOA>7.98</STALLWARNAOA>
    <STALLAOA>0</STALLAOA>
    <MANAOA>0</MANAOA>
    <ALPHA0>-3.72</ALPHA0>
    <ALPHASTALL>10.31</ALPHASTALL>
    <KFIT>0.0</KFIT>
    <AOA_CURVE>
      <TYPE>1</TYPE>
      <X3>0</X3>
      <X2>0</X2>
      <X1>1</X1>
      <X0>0</X0>
    </AOA_CURVE>
  </FLAP_POSITION>
  <FLAP_POSITION>
    <DEGREES>33</DEGREES>
    <POT_VALUE>8</POT_VALUE>
    <LDMAXAOA>-1.12</LDMAXAOA>
    <ONSPEEDFASTAOA>3.79</ONSPEEDFASTAOA>
    <ONSPEEDSLOWAOA>5.23</ONSPEEDSLOWAOA>
    <STALLWARNAOA>9.24</STALLWARNAOA>
    <STALLAOA>0</STALLAOA>
    <MANAOA>0</MANAOA>
    <ALPHA0>-3.72</ALPHA0>
    <ALPHASTALL>10.31</ALPHASTALL>
    <KFIT>0.0</KFIT>
    <AOA_CURVE>
      <TYPE>1</TYPE>
      <X3>0</X3>
      <X2>0</X2>
      <X1>1</X1>
      <X0>0</X0>
    </AOA_CURVE>
  </FLAP_POSITION>
</CONFIG2>`;
const synthCfg = Module.parse_config(synthCfgXml);
if (synthCfg.error !== undefined) {
    console.error(`FAIL: synth test parse_config error: ${synthCfg.error}`);
    process.exit(1);
}

// Known pot values from the config above.
const SYNTH_POT_DETENT0  = 3908;   // clean flap (flapsPos=0)
const SYNTH_POT_DETENT33 = 8;      // full flap  (flapsPos=33)

const synthEngine = new Module.LogReplayEngine(synthCfg, 50, false);
assertDefined('synth engine instance', synthEngine);

// Base row template — reused for all synth steps.
const synthRowBase = {
    pfwdSmoothed:       0.0,
    p45Smoothed:        0.0,
    pStaticMbar:        0.0,
    paltFt:             5000.0,
    iasKt:              10.0,
    iasValid:           true,
    flapsRawAdc:        0,
    flapsRawAdcPresent: false,
    imuVerticalG:       1.0,
    imuLateralG:        0.05,
    imuForwardG:       -0.02,
    imuRollRateDps:     0.0,
    imuPitchRateDps:    0.0,
    imuYawRateDps:      0.0,
    pitchDeg:          -3.0,
    rollDeg:            0.5,
    flightPathDeg:      0.0,
    vsiFpm:             0.0,
    dataMark:           0,
};

// Feed 50 rows at flapsPos=0 (lag period, buffer filling — all return null).
// synthHalfWindowTicks_=100 at 50 Hz; we need ≥100 rows before emission starts.
let nullCount = 0;
for (let i = 0; i < 50; i++) {
    const r = synthEngine.step(Object.assign({}, synthRowBase, { flapsPos: 0 }));
    if (r !== null) {
        console.error(`FAIL: step() row ${i + 1} returned non-null during lag period (expected null)`);
        process.exit(1);
    }
    nullCount++;
}
console.log(`OK: first 50 step() calls (flapsPos=0) returned null (lag period; ${nullCount} nulls)`);

// Switch to flapsPos=33 for the next 100 rows.
// The transition occurs at row 51 (absolute tick 51). Both edges of the
// smoothstep window [51-100, 51+100] must be in the buffer when emission starts.
// Rows 51-100 return null (still in lag period); rows 101+ emit.
let postLagResult = null;
for (let i = 0; i < 100; i++) {
    const r = synthEngine.step(Object.assign({}, synthRowBase, { flapsPos: 33 }));
    // Rows 51-100 still in lag (nullCount already 50, lag needs 100 total):
    if (nullCount < 100) {
        if (r !== null) {
            console.error(`FAIL: step() row ${nullCount + 1} returned non-null during lag (expected null)`);
            process.exit(1);
        }
        nullCount++;
    } else {
        // Row 101+ should be non-null.
        if (postLagResult === null && r !== null) {
            postLagResult = r;
        }
    }
}

if (nullCount !== 100) {
    console.error(`FAIL: expected 100 nulls during lag, got ${nullCount}`);
    process.exit(1);
}
console.log(`OK: first 100 step() calls returned null (lag period; ${nullCount} nulls)`);

if (postLagResult === null) {
    console.error('FAIL: step() returned null for all 100 post-lag rows (expected non-null after lag)');
    process.exit(1);
}
assertDefined('postLagResult.iasKt', postLagResult.iasKt);
console.log(`OK: step() returns a non-null result after lag period (iasKt=${postLagResult.iasKt.toFixed(1)})`);

// flush() must return a JS Array of result objects (the remaining tail rows).
const synthFlushed = synthEngine.flush();
if (!Array.isArray(synthFlushed)) {
    console.error(`FAIL: flush() (synth path) did not return an Array (got ${typeof synthFlushed})`);
    process.exit(1);
}
if (synthFlushed.length === 0) {
    console.error('FAIL: flush() returned empty array on synth path (expected tail rows)');
    process.exit(1);
}
console.log(`OK: flush() returns ${synthFlushed.length} tail rows on synth path`);

// Each flushed result must be a non-null object with the expected field shape.
const firstFlushed = synthFlushed[0];
if (firstFlushed === null || typeof firstFlushed !== 'object') {
    console.error(`FAIL: flushed result[0] is not an object (got ${typeof firstFlushed})`);
    process.exit(1);
}
if (firstFlushed.iasKt === undefined) {
    console.error('FAIL: flushed result[0] missing iasKt field');
    process.exit(1);
}
console.log(`OK: flushed results are well-formed objects (result[0].iasKt=${firstFlushed.iasKt.toFixed(1)})`);

// Verify smoothstep paint: the transition (flapsPos 0→33) occurred at tick 51.
// The smoothstep window spans ticks [51-100, 51+100] = [1..151].
// The tail rows come from around tick ~101 onward, which is still inside the
// transition window [1..151]. At least one tail row's flapsRawAdc must land
// strictly between the two detent pot values (SYNTH_POT_DETENT33 < x < SYNTH_POT_DETENT0).
const anyMidTransition = synthFlushed.some(r =>
    r.flapsRawAdc > SYNTH_POT_DETENT33 &&
    r.flapsRawAdc < SYNTH_POT_DETENT0
);
if (!anyMidTransition) {
    const adcValues = synthFlushed.slice(0, 5).map(r => r.flapsRawAdc).join(', ');
    console.error(
        `FAIL: synth path did not paint any mid-transition flapsRawAdc values ` +
        `(expected strictly between ${SYNTH_POT_DETENT33} and ${SYNTH_POT_DETENT0}); ` +
        `first 5 tail values: [${adcValues}]`
    );
    process.exit(1);
}
console.log(
    `OK: synth path painted at least one mid-transition flapsRawAdc value ` +
    `(strictly between ${SYNTH_POT_DETENT33} and ${SYNTH_POT_DETENT0})`
);

synthEngine.delete();
console.log('OK: synth engine delete() completed without error');

// ---------------------------------------------------------------------------
// Done
// ---------------------------------------------------------------------------

console.log('\nAll wasm-smoke checks passed. (compute_percent_lift, compute_anchors, parse_config, LogReplayEngine)');
