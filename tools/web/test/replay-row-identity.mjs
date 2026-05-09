// replay-row-identity.mjs — verifies that the synth-path row reassembly
// in buildResultsFromWasm (ReplayPage.js) aligns results[i] with the
// correct input row i.
//
// Bug caught: the original reassembly stored immediates[i] at results[i],
// which placed each row's result 100 ticks late (lag period). The tail
// was then written to results[0..99], overwriting slots meant for rows
// N-100..N-1 with results for the start of the flight.
//
// The test uses a passthrough field (iasKt) that is read from the input
// row and echoed unchanged in the result. If results[i].iasKt does not
// match the input row i's iasKt, the reassembly is wrong.
//
// Run:
//   node tools/web/test/replay-row-identity.mjs
//
// Invoked by `npm test` in tools/web/package.json.

import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const wasmJsPath = path.resolve(
    __dirname,
    '../../../software/Libraries/onspeed_core/wasm/dist/onspeed_core.js'
);

let OnSpeedCoreModule;
try {
    const mod = await import(wasmJsPath);
    OnSpeedCoreModule = mod.default;
} catch (err) {
    if (process.env.ALLOW_MISSING_WASM === '1') {
        console.warn(`SKIP: onspeed_core.js not found at ${wasmJsPath}`);
        process.exit(0);
    }
    console.error(`FAIL: onspeed_core.js not found at ${wasmJsPath}`);
    console.error('      Build it first: bash software/Libraries/onspeed_core/wasm/build_wasm.sh');
    console.error(`      (${err.message})`);
    process.exit(1);
}

const Module = await OnSpeedCoreModule();

// ---------------------------------------------------------------------------
// reassembleResults: the same logic used in ReplayPage.js::buildResultsFromWasm.
//
// immediates: Array of results from step() in order (may contain leading nulls
//   for the synth path).
// tail: Array from flush() (the final lag rows drained at EOF).
// N: total row count.
//
// Returns results[0..N-1] where results[i] is the ReplayStepResult for
// input row i.
// ---------------------------------------------------------------------------
function reassembleResults(immediates, tail, N) {
    // Find the lag: how many leading nulls did step() produce?
    let lag = immediates.findIndex(r => r !== null);
    const effectiveLag = lag < 0 ? 0 : lag;

    const results = new Array(N).fill(null);

    if (effectiveLag === 0) {
        // Fast path (flapsRawAdcAvailable=true or no lag): 1:1 mapping.
        for (let i = 0; i < N; i++) results[i] = immediates[i];
    } else {
        // Synth path: immediates[effectiveLag..N-1] → results[0..N-effectiveLag-1]
        for (let i = effectiveLag; i < N; i++) {
            results[i - effectiveLag] = immediates[i];
        }
        // tail[0..tail.length-1] → results[N-tail.length..N-1]
        for (let j = 0; j < tail.length; j++) {
            results[N - tail.length + j] = tail[j];
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// Minimal valid V2 config (one flap detent).
// ---------------------------------------------------------------------------
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
if (cfg.error !== undefined) {
    console.error(`FAIL: parse_config error: ${cfg.error}`);
    process.exit(1);
}

// ---------------------------------------------------------------------------
// Build a synthetic 300-row log where each row's iasKt = row index (0..299).
// This makes the passthrough field uniquely identify each row so misalignment
// is immediately visible: if results[42].iasKt === 42.0, row 42 is correct.
// ---------------------------------------------------------------------------
const N = 300;

function makeRow(i) {
    return {
        pfwdSmoothed:       0.0,
        p45Smoothed:        0.0,
        pStaticMbar:        0.0,
        paltFt:             5000.0,
        iasKt:              i,          // unique per row: passthrough identity field
        iasValid:           i > 0,
        flapsPos:           0,
        flapsRawAdc:        3908,
        flapsRawAdcPresent: true,       // overridden per test below
        imuVerticalG:       1.0,
        imuLateralG:        0.0,
        imuForwardG:        0.0,
        imuRollRateDps:     0.0,
        imuPitchRateDps:    0.0,
        imuYawRateDps:      0.0,
        pitchDeg:           0.0,
        rollDeg:            0.0,
        flightPathDeg:      0.0,
        vsiFpm:             0.0,
        dataMark:           0,
    };
}

// ---------------------------------------------------------------------------
// Test A: flapsRawAdcAvailable=true (fast path — no lag, no flush).
//
// step() returns a result for every row. results[i].iasKt must equal i.
// ---------------------------------------------------------------------------

console.log('\n--- row-identity: flapsRawAdcAvailable=true (fast path) ---');

{
    const engine = new Module.LogReplayEngine(cfg, 50, true);

    const immediates = [];
    for (let i = 0; i < N; i++) {
        const row = makeRow(i);
        immediates.push(engine.step(row));
    }
    const tail = engine.flush();
    engine.delete();

    if (tail.length !== 0) {
        console.error(`FAIL: fast path flush() returned ${tail.length} rows (expected 0)`);
        process.exit(1);
    }

    const results = reassembleResults(immediates, tail, N);

    let mismatch = 0;
    for (let i = 0; i < N; i++) {
        if (results[i] === null) {
            console.error(`FAIL: fast path results[${i}] is null`);
            process.exit(1);
        }
        if (Math.abs(results[i].iasKt - i) > 0.01) {
            console.error(`FAIL: fast path row ${i}: results[${i}].iasKt=${results[i].iasKt}, expected ${i}`);
            mismatch++;
            if (mismatch >= 5) { console.error('  (stopping at 5 mismatches)'); break; }
        }
    }
    if (mismatch > 0) process.exit(1);
    console.log(`OK: fast path: all ${N} rows align (results[i].iasKt === i)`);
}

// ---------------------------------------------------------------------------
// Test B: flapsRawAdcAvailable=false (synth path — 100-row lag at 50 Hz).
//
// The engine returns null for the first 100 rows, then emits the result for
// the row 100 ticks back. flush() drains the final 100 rows. After reassembly,
// results[i].iasKt must equal i.
//
// This test catches the original bug: the broken loop put results[100].iasKt
// at results[100] (should be results[0]), results[101].iasKt at results[101]
// (should be results[1]), etc. — a 100-tick lag across the whole array.
// ---------------------------------------------------------------------------

console.log('\n--- row-identity: flapsRawAdcAvailable=false (synth path) ---');

{
    const synthRows = [];
    for (let i = 0; i < N; i++) {
        const row = makeRow(i);
        row.flapsRawAdcPresent = false;   // synth path
        synthRows.push(row);
    }

    const engine = new Module.LogReplayEngine(cfg, 50, false);

    const immediates = [];
    for (let i = 0; i < N; i++) {
        immediates.push(engine.step(synthRows[i]));
    }
    const tail = engine.flush();
    engine.delete();

    // Verify lag period produced nulls.
    const firstNonNull = immediates.findIndex(r => r !== null);
    if (firstNonNull <= 0) {
        // If no lag at all the engine didn't buffer — this is a WASM mismatch.
        console.error(`FAIL: synth path: expected a lag period (null returns from step()); ` +
                      `firstNonNull=${firstNonNull}`);
        process.exit(1);
    }
    console.log(`OK: synth path: lag=${firstNonNull} rows (step() returned null during lag)`);

    if (tail.length === 0) {
        console.error('FAIL: synth path: flush() returned empty array (expected tail rows)');
        process.exit(1);
    }
    console.log(`OK: synth path: flush() returned ${tail.length} tail rows`);

    const results = reassembleResults(immediates, tail, N);

    // Every slot must be non-null and carry the expected iasKt.
    let mismatch = 0;
    for (let i = 0; i < N; i++) {
        if (results[i] === null) {
            console.error(`FAIL: synth path results[${i}] is null after reassembly`);
            process.exit(1);
        }
        if (Math.abs(results[i].iasKt - i) > 0.01) {
            console.error(`FAIL: synth path row ${i}: results[${i}].iasKt=${results[i].iasKt.toFixed(2)}, expected ${i}`);
            mismatch++;
            if (mismatch >= 5) { console.error('  (stopping at 5 mismatches)'); break; }
        }
    }
    if (mismatch > 0) process.exit(1);
    console.log(`OK: synth path: all ${N} rows align after reassembly (results[i].iasKt === i)`);
}

console.log('\nAll replay-row-identity checks passed.');
