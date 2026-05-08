// wasm-smoke.mjs — smoke test for the onspeed_core WASM build.
//
// Loads the compiled module, calls compute_percent_lift with a known
// input, and asserts the output matches the C++ formula within 0.1.
//
// Known-correct answer:
//   aoa=3.24, alpha_0=-3.72, alpha_stall=10.31, stallwarn=8.24, ias=true
//   → (3.24 - (-3.72)) / (10.31 - (-3.72)) × 100 ≈ 49.608
//
// Run:
//   node tools/web/test/wasm-smoke.mjs
//
// This file is also invoked by `npm test` in tools/web/package.json.

import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const wasmJsPath = path.resolve(
    __dirname,
    '../../software/Libraries/onspeed_core/wasm/dist/onspeed_core.js'
);

// Dynamic import so we get a clean error if the file doesn't exist.
let OnSpeedCoreModule;
try {
    // SINGLE_FILE build: the .js contains everything (WASM inlined as base64).
    const mod = await import(wasmJsPath);
    OnSpeedCoreModule = mod.default;
} catch (err) {
    console.error(`SKIP: onspeed_core.js not found at ${wasmJsPath}`);
    console.error('      Run bash software/Libraries/onspeed_core/wasm/build_wasm.sh first.');
    console.error(`      (${err.message})`);
    process.exit(0);  // exit 0 — skip rather than fail on a missing artifact
}

const Module = await OnSpeedCoreModule();

// Step 0: one function to prove the pipeline.
// Expected: (3.24 - (-3.72)) / (10.31 - (-3.72)) * 100 ≈ 49.608
const EXPECTED = 49.6;
const TOLERANCE = 0.1;

const result = Module.compute_percent_lift(
    3.24,   // aoaDeg
    -3.72,  // alpha_0  (fAlpha0)
    10.31,  // alpha_stall (fAlphaStall)
    8.24,   // stallwarn (fSTALLWARNAOA)
    true    // ias_valid
);

if (Math.abs(result - EXPECTED) > TOLERANCE) {
    console.error(`FAIL: compute_percent_lift expected ~${EXPECTED}, got ${result}`);
    process.exit(1);
}

console.log(`OK: compute_percent_lift(3.24, -3.72, 10.31, 8.24) = ${result.toFixed(3)}`);

// Sanity-check: ias_valid=false must return 0.
const groundResult = Module.compute_percent_lift(3.24, -3.72, 10.31, 8.24, false);
if (groundResult !== 0.0) {
    console.error(`FAIL: ias_valid=false should return 0.0, got ${groundResult}`);
    process.exit(1);
}
console.log(`OK: ias_valid=false → 0.0`);

// Sanity-check: below alpha_0 clamps to 0.
const belowAlpha0 = Module.compute_percent_lift(-5.0, -3.72, 10.31, 8.24, true);
if (belowAlpha0 !== 0.0) {
    console.error(`FAIL: aoa below alpha_0 should clamp to 0.0, got ${belowAlpha0}`);
    process.exit(1);
}
console.log(`OK: aoa below alpha_0 clamps to 0.0`);

// Sanity-check: above alpha_stall clamps to 99.9.
// Use a tolerant comparison: WASM returns C++ float32 (~99.9000015259...),
// which differs from the JS binary64 literal 99.9 at strict equality.
const aboveStall = Module.compute_percent_lift(15.0, -3.72, 10.31, 8.24, true);
if (Math.abs(aboveStall - 99.9) > 0.01) {
    console.error(`FAIL: aoa above alpha_stall should clamp to ~99.9, got ${aboveStall}`);
    process.exit(1);
}
console.log(`OK: aoa above alpha_stall clamps to ~99.9 (got ${aboveStall})`);

console.log('\nAll wasm-smoke checks passed.');
