// calwiz-fit.mjs — numerical regression tests for analyzeDecel.
//
// Pins the calibration wizard's stall detector, K/alpha_0 physics fit,
// 2nd-order CP→AOA polynomial fit, and six-setpoint derivation against
// known inputs and against the checked-in V1 cal-flight fixture.
//
// Why these tests exist: analyzeDecel runs once per flap per aircraft
// and writes six setpoints + one polynomial into config that the
// firmware reads at 50 Hz for every subsequent flight.  A regression in
// this function gets baked into pilot configs and there is no runtime
// signal that surfaces it.  This file is the only numerical test
// covering that math; the save-path differential test
// (test/test_calwiz_save_diff/) covers field plumbing, not the math.
//
// Test strategy:
//
//   1. Synthetic clean — IAS sweep 80→55 kt, realistic RV-class lift
//      equation (K=46887.5, alpha_0=-3.5, stall AOA ≈ 12°).  Strict
//      equality on every recovered scalar — analyzeDecel is fully
//      deterministic on noise-free input, and regression.js's
//      precision=6 rounding is below the values we pin.
//
//   2. Synthetic noisy — same K and alpha_0 with a deterministic LCG
//      adding ±0.1° AOA jitter and ±2.5 mU CP jitter.  Pinned to the
//      exact observed values (deterministic seed → deterministic
//      output), so any future change in the EMA smoothing, the
//      regression algorithm, or the fit ordering shows up immediately.
//
//   3. Synthetic mid-run stall — IAS bottoms at sample 130 and climbs
//      back (a stall-and-recovery shape).  Verifies the stall detector
//      truncates the fit at the CP peak rather than the last sample,
//      and that the post-peak recovery samples don't bias the K fit.
//
//   4. Synthetic flapped — flapIndex=1, exercises the vfeKt branch of
//      LDmax derivation (which the flapIndex=0 fixtures don't touch).
//
//   5. NaN/null air-data guard — pins the early-return when any sample
//      carries a non-finite iasKt or coeffP.
//
//   6. Real V1 decel slice (vac_decel_run.csv) with coeffP
//      reconstructed from Pfwd/P45 via pressureCoeff
//      (onspeed_core/util/OnSpeedTypes.h::pressureCoeff).
//      AngleofAttack is the V1-era DerivedAOA column.  Strict-equality
//      pinning to current observed values.
//
//   7. Cross-check: vac_config.cfg's SETPOINT_STALLWARNAOA (clean
//      flap) vs analyzeDecel's recovery from the matched decel slice.
//      0.5° tolerance.  Informational — agreement to ~0.35° is
//      evidence the wizard math and Vac's stored config came from the
//      same calibration session, independent of any K/alpha_0 ground
//      truth.
//
// All synthetic fits are constructed so analyzeDecel can recover the
// input parameters exactly under double-precision arithmetic.  Where a
// tolerance is used, the comment states why.

import { fileURLToPath } from 'url';
import path from 'path';
import fs from 'fs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------------
// Load regression.js the way the wizard does (UMD bundle that writes
// to `module.exports` when invoked with a `module` arg).  Can't
// `import` directly because it's a single-line UMD wrapper, not ESM.
// ---------------------------------------------------------------------
const regressionSrc = fs.readFileSync(
    path.resolve(__dirname, '../lib/vendor/regression.js'),
    'utf8');
const regressionModule = { exports: {} };
new Function('module', 'exports', regressionSrc)(regressionModule, regressionModule.exports);

// analyzeDecel reads window.regression at call time; install before import.
globalThis.window = globalThis;
globalThis.window.regression = regressionModule.exports;

const { analyzeDecel } = await import(
    new URL('../lib/pages/CalWizardPage.js', import.meta.url));

// ---------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------
let passed = 0, failed = 0;

function test(name, fn) {
    try {
        fn();
        console.log(`  ok ${name}`);
        passed++;
    } catch (err) {
        console.log(`  FAIL ${name}\n    ${err.message}`);
        failed++;
    }
}

// Strict equality on doubles.  All wizard outputs from noise-free or
// deterministic-seeded inputs land on bit-exact doubles.
function assertEqual(label, actual, expected) {
    if (!Number.isFinite(actual))
        throw new Error(`${label}: expected ${expected}, got non-finite ${actual}`);
    if (actual !== expected)
        throw new Error(`${label}: expected ${expected}, got ${actual}`);
}

function assertClose(label, actual, expected, tolerance) {
    if (!Number.isFinite(actual))
        throw new Error(`${label}: expected ~${expected}, got non-finite ${actual}`);
    if (Math.abs(actual - expected) > tolerance)
        throw new Error(`${label}: expected ${expected} ± ${tolerance}, got ${actual} (Δ=${(actual - expected).toExponential(3)})`);
}

function assertOk(result) {
    if (!result.ok)
        throw new Error(`analyzeDecel returned ok=false: ${result.error}`);
}

function assertNotOk(result, expectedErrorSubstring) {
    if (result.ok)
        throw new Error(`expected ok=false; got ok=true`);
    if (expectedErrorSubstring && !result.error.includes(expectedErrorSubstring))
        throw new Error(`error message ${JSON.stringify(result.error)} did not contain ${JSON.stringify(expectedErrorSubstring)}`);
}

// ---------------------------------------------------------------------
// Realistic RV-class lift equation: AOA = K/IAS² + alpha_0 with K and
// alpha_0 chosen so the stall AOA at iasEnd lands near +12° — within
// the actual operating envelope of every aircraft OnSpeed targets.
// (K=400 from an earlier draft of this file produced a "stall" at
// alpha_0+0.13°, technically a stall by stall-IAS but not a stall by
// AOA — the wizard's setpoint derivation collapsed onto a 0.1°-wide
// band, which masked any percent-lift / monotonicity bug.)
// ---------------------------------------------------------------------
const REALISTIC_K       = 46887.5;
const REALISTIC_ALPHA_0 = -3.5;

const DEFAULT_PARAMS = {
    gLimit:          '4',
    grossWeightLb:   '2400',
    currentWeightLb: '2200',
    bestGlideKt:     '85',
    vfeKt:           '100',
};

// Construct a clean decel: IAS sweeps linearly from iasStart to iasEnd
// over N samples, AOA = K/IAS² + alpha_0, CP = a + b*AOA + c*AOA² so
// the wizard's 2nd-order CP→AOA polynomial fit has a true 2nd-order
// solution.  (Setting CP linear in sample index makes the polynomial
// fit lossy, which means r² < 1 on noise-free data — surprising and
// distracting.  Tying CP to AOA via a quadratic mirrors the physical
// relationship the polynomial is designed to invert.)
function cleanRun({
    N = 200, iasStart = 80, iasEnd = 55,
    K = REALISTIC_K, alpha_0 = REALISTIC_ALPHA_0,
    cpA = 0.20, cpB = 0.080, cpC = 0.001,
    flapsPosDeg = 0, flapIndex = 0,
}) {
    const samples = [];
    for (let i = 0; i < N; i++) {
        const ias = iasStart + (iasEnd - iasStart) * (i / (N - 1));
        const aoa = K / (ias * ias) + alpha_0;
        const cp  = cpA + cpB * aoa + cpC * aoa * aoa;
        samples.push({
            iasKt:         ias,
            coeffP:        cp,
            derivedAoaDeg: aoa,
            flapsPosDeg,
            flapIndex,
        });
    }
    return samples;
}

// Deterministic LCG so the noisy fixture's output is pin-stable across
// machines.  Math.random would re-seed per process and ruin the pin.
function makeLcg(seed = 0xC0FFEE) {
    let s = seed >>> 0;
    return () => {
        s = (s * 1664525 + 1013904223) >>> 0;
        return (s / 0x100000000) - 0.5;  // [-0.5, +0.5)
    };
}

// ---------------------------------------------------------------------
// Fixture 1 — synthetic clean fit, strict equality
// ---------------------------------------------------------------------
console.log('\n# analyzeDecel — synthetic clean fit (strict equality)');

{
    const samples = cleanRun({});
    const r = analyzeDecel(samples, DEFAULT_PARAMS);

    test('analyzeDecel succeeds on clean fixture', () => assertOk(r));

    test('recovers K and alpha_0 exactly', () => {
        // Noise-free, IAS sweep covers a wide 1/IAS² range, regression.js
        // precision=6 cannot perturb integer-ish inputs.  No tolerance.
        assertEqual('kFit',         r.fit.kFit,         REALISTIC_K);
        assertEqual('alpha0Deg',    r.fit.alpha0Deg,    REALISTIC_ALPHA_0);
        // alphaStall = K/stallIas² + alpha_0; stallIas comes from the EMA
        // (α=0.98 IAS smoothing) so it sits just above iasEnd.  That makes
        // alphaStall slightly below the geometric K/iasEnd² + alpha_0=12.
        assertClose('alphaStallDeg', r.fit.alphaStallDeg, 11.998555027804844, 1e-12);
    });

    test('CP polynomial fits perfectly (r²=1) on quadratic CP/AOA', () => {
        // CP is constructed as a quadratic in AOA, so the wizard's 2nd-
        // order polynomial fit recovers it exactly.  rounded to precision=6.
        assertEqual('cpToAoaR2',  r.fit.cpToAoaR2,  1);
        // 1/IAS² physics fit is exact on noise-free data.
        assertEqual('iasToAoaR2', r.fit.iasToAoaR2, 1);
    });

    test('CP polynomial evaluated at stallCP equals alphaStall (internal consistency)', () => {
        // The wizard saves [curve0, curve1, curve2] as the polynomial
        // [a₂, a₁, a₀] in y = a₂·cp² + a₁·cp + a₀.  Firmware uses this at
        // 50 Hz to map measured CP → AOA.  At the stall point the two
        // fits MUST agree (both are the AOA at the same CP).  Drift here
        // means the runtime AOA at stall would disagree with what the
        // wizard saved as alphaStall.
        const { curve0, curve1, curve2 } = r.fit;
        // stallCP isn't returned directly; reconstruct from the smoothed
        // sequence at stallIdx (this matches the wizard's CP→AOA fit input).
        const stallCP = r.smoothed.sCP[r.smoothed.stallIdx];
        const polyAtStall = curve0 * stallCP * stallCP + curve1 * stallCP + curve2;
        // regression.js rounds polynomial coefficients to precision=4.
        // At stallCP ≈ 1.3, those rounding errors compound to ~1e-2 on
        // the evaluated polynomial.  This is a sanity check, not a
        // precision check — anything within 0.02° at stall means the
        // saved polynomial would produce essentially the same AOA at
        // stall as the saved alphaStall scalar.  Catches gross errors
        // (wrong coefficient ordering, wrong x variable) without being
        // fooled by precision=4 rounding noise.
        assertClose('poly(stallCP) ≈ alphaStall', polyAtStall, r.fit.alphaStallDeg, 0.02);
    });

    test('stallIas is at the IAS-EMA-smoothed value at iasEnd', () => {
        // With α=0.98 (IAS smoothing) over 200 samples sweeping 80→55
        // linearly, the EMA lags the raw IAS by ~step/(1-α) ≈ 0.125/0.02
        // = 6.25 kt initially, converging fast.  At the last sample the
        // lag is well under 0.01 kt.
        assertEqual('stallIas', r.stallIas, 55.002563839606196);
    });

    test('all six setpoints land at their canonical values', () => {
        // Pinning the entire setpoint vector — these are what the firmware
        // would fly with if this calibration were saved.  Any change to
        // multipliers, weight scaling, Va factor, or stall-warn margin
        // surfaces here.
        const s = r.setpoints;
        assertEqual('ldMaxAoaDeg',       s.ldMaxAoaDeg,        3.58);
        assertEqual('onSpeedFastAoaDeg', s.onSpeedFastAoaDeg,  5);
        assertEqual('onSpeedSlowAoaDeg', s.onSpeedSlowAoaDeg,  6.42);
        assertEqual('stallWarnAoaDeg',   s.stallWarnAoaDeg,    9.52);
        assertEqual('stallAoaDeg',       s.stallAoaDeg,        12);
        // maneuvering at Va = stallIas*√G = 55*2 = 110 kt; AOA there is
        // K/110² + α₀ = 46887.5/12100 − 3.5 = 0.375... → rounds to 0.37
        assertEqual('maneuveringAoaDeg', s.maneuveringAoaDeg,  0.37);
    });

    test('setpoint ordering matches physical reality', () => {
        // Body angle ordering for a normal RV-class lift curve.
        // maneuvering sits below LDmax (Va is faster than Vbg, so AOA at
        // Va < AOA at Vbg).  Stall is the ceiling.
        const s = r.setpoints;
        const sequence = [
            ['maneuvering', s.maneuveringAoaDeg],
            ['LDmax',       s.ldMaxAoaDeg],
            ['OnSpeedFast', s.onSpeedFastAoaDeg],
            ['OnSpeedSlow', s.onSpeedSlowAoaDeg],
            ['StallWarn',   s.stallWarnAoaDeg],
            ['Stall',       s.stallAoaDeg],
        ];
        for (let i = 1; i < sequence.length; i++) {
            if (sequence[i][1] <= sequence[i - 1][1])
                throw new Error(
                    `out of order at index ${i}: ` +
                    `${sequence[i - 1][0]}=${sequence[i - 1][1]} >= ` +
                    `${sequence[i][0]}=${sequence[i][1]}`);
        }
    });

    test('every setpoint sits in [alpha_0, alphaStall] (modulo 0.01° rounding)', () => {
        // Setpoints are stored as Number((x).toFixed(2)), so they can
        // exceed unrounded alphaStall by up to 0.005°.  Use a 0.01°
        // slop window above hi; below lo is strict.
        const s = r.setpoints;
        const lo = r.fit.alpha0Deg, hi = r.fit.alphaStallDeg;
        for (const [name, v] of Object.entries(s)) {
            if (v < lo || v > hi + 0.01)
                throw new Error(
                    `${name}=${v} outside [alpha_0=${lo}, alphaStall=${hi}+0.01]`);
        }
    });
}

// ---------------------------------------------------------------------
// Fixture 2 — synthetic noisy fit, pinned to deterministic outputs
// ---------------------------------------------------------------------
console.log('\n# analyzeDecel — synthetic noisy fit (deterministic pin)');

{
    // ±0.1° AOA noise reflects realistic IMU/pressure-fusion jitter at
    // 50 Hz; ±2.5 mU CP noise reflects the noise floor on the pressure
    // ratio after EMA smoothing.  Same LCG seed every run, so outputs
    // are pin-stable.
    const noisy = cleanRun({});  // start from the clean fixture
    const rng = makeLcg();
    for (const s of noisy) {
        s.derivedAoaDeg += rng() * 0.2;   // ±0.1°
        s.coeffP        += rng() * 0.005; // ±2.5 mU
    }
    const r = analyzeDecel(noisy, DEFAULT_PARAMS);

    test('analyzeDecel succeeds on noisy fixture', () => assertOk(r));

    test('deterministic noise produces deterministic fit', () => {
        // Strict equality.  If any of these drift, either:
        //   - the LCG implementation changed (it shouldn't; it's local)
        //   - regression.js's polynomial implementation changed
        //   - analyzeDecel's preprocessing changed
        //   - JS double-arithmetic semantics changed (won't happen)
        // Any of those is a real change worth surfacing.
        assertEqual('kFit',          r.fit.kFit,          46784.997852);
        assertEqual('alpha0Deg',     r.fit.alpha0Deg,     -3.474234);
        assertEqual('alphaStallDeg', r.fit.alphaStallDeg, 11.990439179097915);
        assertEqual('stallIas',      r.stallIas,          55.002563839606196);
    });

    test('noisy fit recovers K and alpha_0 within physical tolerance', () => {
        // Sanity ceiling on the deterministic pin: even with noise the
        // fit must land near the truth.  These tolerances are loose
        // enough that they don't catch deterministic regressions (the
        // strict-equality test above does that) — they catch the
        // separate failure mode of "noise model produces a sensible
        // fit, not garbage".
        assertClose('kFit ~ truth',    r.fit.kFit,        REALISTIC_K,       500);
        assertClose('alpha0 ~ truth',  r.fit.alpha0Deg,   REALISTIC_ALPHA_0, 0.1);
    });

    test('noisy fit r² stays above 0.95 for both fits', () => {
        // Below 0.95 on this noise level would indicate the fit broke,
        // not that noise increased.
        if (r.fit.cpToAoaR2  < 0.95) throw new Error(`cpToAoaR2=${r.fit.cpToAoaR2} below 0.95`);
        if (r.fit.iasToAoaR2 < 0.95) throw new Error(`iasToAoaR2=${r.fit.iasToAoaR2} below 0.95`);
    });
}

// ---------------------------------------------------------------------
// Fixture 3 — mid-run stall (CP peaks at sample 130, then recovery)
// ---------------------------------------------------------------------
console.log('\n# analyzeDecel — mid-run stall + recovery');

{
    // IAS drops 80→55 kt over samples [0..130], then climbs back to
    // 70 kt over [130..199] (pilot recovers).  CP rises through the
    // stall, then drops on recovery (AOA falls as IAS climbs).  The
    // stall detector should pick the CP peak (≈ sample 130) and
    // truncate the fit there — recovery samples must NOT pollute K.
    const stallIdx = 130;
    const N = 200;
    const K = REALISTIC_K, alpha_0 = REALISTIC_ALPHA_0;
    const samples = [];
    for (let i = 0; i < N; i++) {
        const ias = i <= stallIdx
            ? 80 - (80 - 55) * (i / stallIdx)
            : 55 + (70 - 55) * ((i - stallIdx) / (N - 1 - stallIdx));
        const aoa = K / (ias * ias) + alpha_0;
        const cp  = 0.20 + 0.080 * aoa + 0.001 * aoa * aoa;  // same CP/AOA mapping
        samples.push({iasKt: ias, coeffP: cp, derivedAoaDeg: aoa, flapsPosDeg: 0, flapIndex: 0});
    }
    const r = analyzeDecel(samples, DEFAULT_PARAMS);

    test('mid-run stall detector picks the CP peak, not the last sample', () => {
        assertOk(r);
        // EMA-smoothed peak should land at or very close to the
        // pre-recovery sample 130.  Acceptable window: ±3 samples
        // (EMA(α=0.90) on CP introduces a small lag).
        if (Math.abs(r.smoothed.stallIdx - stallIdx) > 3)
            throw new Error(`stallIdx=${r.smoothed.stallIdx} not within ±3 of ${stallIdx}`);
    });

    test('recovery samples do not bias the K fit', () => {
        // If recovery samples (where IAS is climbing and AOA dropping)
        // leaked into the 1/IAS² regression, K would be pulled toward
        // a much smaller value (because the post-stall AOA is far from
        // the lift-equation predicted AOA at recovering IAS).  Strict
        // recovery to the input K means the truncation worked.
        assertEqual('kFit (mid-stall)',    r.fit.kFit,      K);
        assertEqual('alpha0 (mid-stall)',  r.fit.alpha0Deg, alpha_0);
    });
}

// ---------------------------------------------------------------------
// Fixture 4 — flapped (flapIndex=1, LDmax uses vfeKt branch)
// ---------------------------------------------------------------------
console.log('\n# analyzeDecel — flapped fixture (vfeKt branch of LDmax)');

{
    // Half-flap stall around 48 kt with alphaStall ≈ 11°.
    // K = (11 + 2.5) * 48² = 31104; alpha_0 = -2.5.
    const K_FLAP = 31104, ALPHA0_FLAP = -2.5;
    const samples = cleanRun({
        N: 200, iasStart: 70, iasEnd: 48,
        K: K_FLAP, alpha_0: ALPHA0_FLAP,
        cpA: 0.30, cpB: 0.060, cpC: 0.001,
        flapsPosDeg: 15, flapIndex: 1,
    });
    const params = { ...DEFAULT_PARAMS, vfeKt: '90' };  // half-flap Vfe
    const r = analyzeDecel(samples, params);

    test('flapped fit succeeds with vfeKt parameter', () => assertOk(r));

    test('flapped K and alpha_0 recovered exactly', () => {
        assertEqual('kFit (flap)',    r.fit.kFit,      K_FLAP);
        assertEqual('alpha0 (flap)',  r.fit.alpha0Deg, ALPHA0_FLAP);
    });

    test('LDmax derived from vfeKt, not the weight-scaled bestGlide', () => {
        // For flapIndex != 0, ldmaxIAS = vfeKt (when vfeKt > 0).
        // Hand-compute: AOA at vfe = K_FLAP/90² + alpha_0 = 1.34.
        // (For flapIndex==0 the code would use sqrt(currentWt/grossWt) *
        // bestGlideKt = sqrt(2200/2400) * 85 = 81.39 kt → AOA at 81.39
        // would be 31104/6624 − 2.5 = 4.70 + 2.07 = ~2.20°, very
        // different from 1.34.)
        assertEqual('ldMaxAoaDeg (flap)', r.setpoints.ldMaxAoaDeg, 1.34);
    });

    test('flapped result preserves flapIndex/flapsPos in the output', () => {
        assertEqual('flapIndex',  r.flapIndex, 1);
        assertEqual('flapsPos',   r.flapsPos,  15);
    });
}

// ---------------------------------------------------------------------
// Fixture 5 — input-validation guards
// ---------------------------------------------------------------------
console.log('\n# analyzeDecel — input validation');

test('rejects samples with NaN iasKt', () => {
    const samples = cleanRun({});
    samples[50].iasKt = NaN;
    assertNotOk(analyzeDecel(samples, DEFAULT_PARAMS), 'Invalid air data');
});

test('rejects samples with null coeffP', () => {
    const samples = cleanRun({});
    samples[50].coeffP = null;
    assertNotOk(analyzeDecel(samples, DEFAULT_PARAMS), 'Invalid air data');
});

test('rejects fewer than 50 samples', () => {
    const samples = cleanRun({ N: 49 });
    assertNotOk(analyzeDecel(samples, DEFAULT_PARAMS), 'Not enough samples');
});

test('rejects an IAS-increasing run (climbing accel by mistake)', () => {
    const samples = cleanRun({ N: 200, iasStart: 55, iasEnd: 80 });  // reversed
    assertNotOk(analyzeDecel(samples, DEFAULT_PARAMS), 'Airspeed is increasing');
});

// ---------------------------------------------------------------------
// Fixture 6 — real V1 decel slice (vac_decel_run.csv)
// ---------------------------------------------------------------------
console.log('\n# analyzeDecel — vac_decel_run.csv (real V1 cal flight)');

function loadVacDecel() {
    const csvPath = path.resolve(
        __dirname,
        '../../onspeed_py/tests/fixtures/vac_decel_run.csv');
    const text  = fs.readFileSync(csvPath, 'utf8');
    const lines = text.trim().split('\n');
    const header = lines[0].split(',').map(s => s.trim());
    const colIdx = (name) => {
        const i = header.indexOf(name);
        if (i < 0) throw new Error(`vac_decel_run.csv missing column ${name}`);
        return i;
    };
    const iIAS   = colIdx('IAS');
    const iAOA   = colIdx('AngleofAttack');   // V1-era DerivedAOA
    const iFlaps = colIdx('flapsPos');
    const iPfwd  = colIdx('Pfwd');
    const iP45   = colIdx('P45');

    const samples = [];
    for (let i = 1; i < lines.length; i++) {
        const f = lines[i].split(',');
        const Pfwd = Number(f[iPfwd]);
        const P45  = Number(f[iP45]);
        // pressureCoeff (onspeed_core/util/OnSpeedTypes.h:133):
        // (pfwd > 0) ? p45/pfwd : 0.
        const coeffP = (Pfwd > 0) ? P45 / Pfwd : 0;
        samples.push({
            iasKt:         Number(f[iIAS]),
            coeffP,
            derivedAoaDeg: Number(f[iAOA]),
            flapsPosDeg:   Number(f[iFlaps]),
            flapIndex:     0,
        });
    }
    return samples;
}

{
    const samples = loadVacDecel();
    const r = analyzeDecel(samples, DEFAULT_PARAMS);

    test('vac_decel_run.csv produces a successful analysis', () => assertOk(r));

    test('vac fit values are pinned (strict equality)', () => {
        // analyzeDecel is fully deterministic on a given byte-identical
        // input.  Strict equality on every recovered scalar.  If any of
        // these drift, the change is either a real algorithm change
        // (update the pin and document in the PR) or an accidental
        // regression.
        assertEqual('stallIas',          r.stallIas,                47.270016324929976);
        assertEqual('kFit',              r.fit.kFit,                42783.755486);
        assertEqual('alpha0Deg',         r.fit.alpha0Deg,           0.467095);
        assertEqual('alphaStallDeg',     r.fit.alphaStallDeg,       19.614388699880244);
        assertEqual('cpToAoaR2',         r.fit.cpToAoaR2,           0.9906);
        assertEqual('iasToAoaR2',        r.fit.iasToAoaR2,          0.991179);
        assertEqual('curve0',            r.fit.curve0,              0.7033);
        assertEqual('curve1',            r.fit.curve1,              27.5917);
        assertEqual('curve2',            r.fit.curve2,              4.0629);
    });

    test('vac setpoint vector is pinned (strict equality)', () => {
        const s = r.setpoints;
        assertEqual('ldMaxAoaDeg',       s.ldMaxAoaDeg,       6.93);
        assertEqual('onSpeedFastAoaDeg', s.onSpeedFastAoaDeg, 10.97);
        assertEqual('onSpeedSlowAoaDeg', s.onSpeedSlowAoaDeg, 12.72);
        assertEqual('stallWarnAoaDeg',   s.stallWarnAoaDeg,   16.13);
        assertEqual('stallAoaDeg',       s.stallAoaDeg,       19.61);
        assertEqual('maneuveringAoaDeg', s.maneuveringAoaDeg, 5.25);
    });

    test('vac CP polynomial evaluated at stallCP matches alphaStall', () => {
        const { curve0, curve1, curve2 } = r.fit;
        const stallCP = r.smoothed.sCP[r.smoothed.stallIdx];
        const polyAtStall = curve0 * stallCP * stallCP + curve1 * stallCP + curve2;
        // Real-flight data isn't a perfect quadratic, so the two fits
        // (CP→AOA via polynomial vs. 1/IAS² → AOA via linear) won't
        // agree exactly at the stall point.  Tolerance reflects the
        // observed residual.
        assertClose('vac poly(stallCP) ~ alphaStall',
                    polyAtStall, r.fit.alphaStallDeg, 0.5);
    });

    test('vac setpoints in [alpha_0, alphaStall] and monotonically ordered', () => {
        const s = r.setpoints;
        const lo = r.fit.alpha0Deg, hi = r.fit.alphaStallDeg;
        for (const [name, v] of Object.entries(s)) {
            if (v < lo - 1e-9 || v > hi + 1e-9)
                throw new Error(`${name}=${v} outside [${lo}, ${hi}]`);
        }
        // Five-element chain (maneuvering goes its own way for clean flap).
        const chain = [s.ldMaxAoaDeg, s.onSpeedFastAoaDeg, s.onSpeedSlowAoaDeg,
                       s.stallWarnAoaDeg, s.stallAoaDeg];
        for (let i = 1; i < chain.length; i++)
            if (chain[i] <= chain[i - 1])
                throw new Error(`vac setpoint chain not monotonic at index ${i}`);
    });
}

// ---------------------------------------------------------------------
// Fixture 7 — cross-check against vac_config.cfg's saved setpoints
// ---------------------------------------------------------------------
console.log('\n# analyzeDecel — vac_decel_run.csv vs vac_config.cfg cross-check');

test('recovered stallWarn matches vac_config.cfg to within 0.5°', () => {
    // vac_config.cfg (tools/onspeed_py/tests/fixtures/vac_config.cfg)
    // saves SETPOINT_STALLWARNAOA = "16.4800,..." (clean flap value).
    // Independent evidence that analyzeDecel produces what Vac's
    // calibration session actually produced.  This is informational —
    // a drift here doesn't necessarily indicate a wizard bug (could be
    // any drift in either side), but it's worth surfacing because the
    // two values currently agree to 0.35°.
    const STALLWARN_VAC_CONFIG = 16.48;
    const r = analyzeDecel(loadVacDecel(), DEFAULT_PARAMS);
    assertOk(r);
    const delta = Math.abs(r.setpoints.stallWarnAoaDeg - STALLWARN_VAC_CONFIG);
    if (delta > 0.5)
        throw new Error(
            `stallWarn drift: analyzeDecel=${r.setpoints.stallWarnAoaDeg} ` +
            `vs vac_config.cfg=${STALLWARN_VAC_CONFIG}, Δ=${delta.toFixed(3)}°`);
});

// ---------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------
console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
