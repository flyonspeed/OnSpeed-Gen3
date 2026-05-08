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
// Done
// ---------------------------------------------------------------------------

console.log('\nAll wasm-smoke checks passed. (compute_percent_lift, compute_anchors, parse_config)');
