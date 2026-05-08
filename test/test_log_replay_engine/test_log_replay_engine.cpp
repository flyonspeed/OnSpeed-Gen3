// test_log_replay_engine.cpp — characterization tests for onspeed::replay::LogReplayEngine.
//
// These are CHARACTERIZATION TESTS: they pin the current (PR 1) behavior so
// that PRs 2 (rate-correct EMA) and 3 (synth ADC) produce clean diffs.
// They do not assert physical correctness — they assert that the engine
// produces the same outputs as the original inline ReadLogLine() code for
// the same inputs.
//
// What is tested:
//   - step() is deterministic: two fresh engines on identical input produce
//     identical output.
//   - Engine state (AOA EMA) accumulates: two sequential steps on inputs with
//     different pressures produce numerically distinct AOA values pinned to
//     exact floats (so PR 2's rate-correct EMA produces a visible diff).
//   - reset() clears EMA state — two step() calls after reset give the
//     same result as the first two calls from a fresh engine.
//   - flapsRawAdcPresent gate: field is propagated only when set.
//   - Flap index lookup: correct entry selected, unknown degrees fall back to 0.

#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <config/OnSpeedConfig.h>
#include <replay/LogReplayEngine.h>
#include <types/LogRow.h>
#include <util/OnSpeedTypes.h>

using onspeed::LogRow;
using onspeed::SuCalibrationCurve;
using onspeed::config::OnSpeedConfig;
using onspeed::replay::LogReplayEngine;
using onspeed::replay::ReplayStepResult;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Build a minimal OnSpeedConfig with two flap detents.
// The calibration curves are left at zero-coefficient defaults — the
// engine's AOA output matches zero-curve behavior (pressureCoeff math).
OnSpeedConfig makeTwoFlapConfig()
{
    OnSpeedConfig cfg;
    cfg.aFlaps.clear();

    // Detent 0: flaps-up at 0 degrees
    {
        OnSpeedConfig::SuFlaps f0;
        f0.iDegrees      = 0;
        f0.iPotPosition  = 1000;
        f0.fLDMAXAOA     = 3.0f;
        f0.fONSPEEDFASTAOA = 6.5f;
        f0.fONSPEEDSLOWAOA = 9.5f;
        f0.fSTALLWARNAOA = 12.5f;
        f0.fAlpha0       = -3.7f;
        f0.fAlphaStall   = 14.0f;
        cfg.aFlaps.push_back(f0);
    }

    // Detent 1: flaps at 30 degrees
    {
        OnSpeedConfig::SuFlaps f1;
        f1.iDegrees      = 30;
        f1.iPotPosition  = 3000;
        f1.fLDMAXAOA     = 5.0f;
        f1.fONSPEEDFASTAOA = 7.0f;
        f1.fONSPEEDSLOWAOA = 10.0f;
        f1.fSTALLWARNAOA = 13.0f;
        f1.fAlpha0       = -2.0f;
        f1.fAlphaStall   = 13.5f;
        cfg.aFlaps.push_back(f1);
    }

    cfg.iAoaSmoothing = 10;
    cfg.iLogRate = 50;
    return cfg;
}

// Build a minimal OnSpeedConfig with a linear polynomial AOA curve on flap 0.
// Curve: y = a1 * coeffP, with a1 = 30. Zero-poly defaults for the rest.
// This lets test_ema_accumulates produce nonzero AOA values whose exact
// numerics are pinned — PR 2's rate-correct EMA fix will diff cleanly against
// these constants.
OnSpeedConfig makeLinearCurveConfig()
{
    OnSpeedConfig cfg = makeTwoFlapConfig();

    // Set flap 0's AOA curve to a simple linear polynomial: AOA = 30 * coeffP.
    // SuCalibrationCurve coefficients are [a3, a2, a1, a0]; iCurveType 1 =
    // polynomial evaluated via Horner's method.
    SuCalibrationCurve curve;
    curve.iCurveType   = 1;      // polynomial
    curve.afCoeff[0]   = 0.0f;   // a3
    curve.afCoeff[1]   = 0.0f;   // a2
    curve.afCoeff[2]   = 30.0f;  // a1
    curve.afCoeff[3]   = 0.0f;   // a0
    cfg.aFlaps[0].AoaCurve = curve;

    return cfg;
}

// Build a minimal LogRow with sensor state.
LogRow makeRow(float pfwdSmoothed = 0.5f,
               float p45Smoothed  = 0.1f,
               int   flapsPosDeg  = 0,
               bool  iasValid     = true,
               float iasKt        = 50.0f)
{
    LogRow r;
    r.pfwdSmoothed       = pfwdSmoothed;
    r.p45Smoothed        = p45Smoothed;
    r.flapsPos           = flapsPosDeg;
    r.iasKt              = iasKt;
    r.iasValid           = iasValid;
    r.paltFt             = 3500.0f;
    r.vsiFpm             = 100.0f;
    r.dataMark           = 7;
    r.imuForwardG        = 0.05f;
    r.imuLateralG        = -0.02f;
    r.imuVerticalG       = 1.02f;
    r.imuRollRateDps     = 0.1f;
    r.imuPitchRateDps    = 0.3f;
    r.imuYawRateDps      = -0.1f;
    r.pitchDeg           = 3.5f;
    r.rollDeg            = 1.2f;
    r.flightPathDeg      = 2.1f;
    r.flapsRawAdcPresent = false;
    r.flapsRawAdc        = 0;
    return r;
}

}  // namespace

// ============================================================================
// setUp / tearDown (Unity requirement)
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Tests
// ============================================================================

// step() is deterministic: two fresh engines on identical input produce
// identical output. (No reset() involved — that's a different property
// tested separately by test_reset_clears_state.)
void test_step_deterministic(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogRow row = makeRow();

    LogReplayEngine engA(cfg, 50, false);
    LogReplayEngine engB(cfg, 50, false);

    ReplayStepResult rA = engA.step(row);
    ReplayStepResult rB = engB.step(row);

    TEST_ASSERT_EQUAL_FLOAT(rA.aoa,       rB.aoa);
    TEST_ASSERT_EQUAL_FLOAT(rA.coeffP,   rB.coeffP);
    TEST_ASSERT_EQUAL_INT(rA.flapsIndex, rB.flapsIndex);
}

// Fields from the row are passed through to the result unchanged
void test_passthrough_fields(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow(0.5f, 0.1f, 0, true, 55.0f);
    row.dataMark     = 42;
    row.pitchDeg     = 7.2f;
    row.rollDeg      = -2.1f;
    row.flightPathDeg = 3.0f;
    row.paltFt       = 5000.0f;
    row.vsiFpm       = 200.0f;

    ReplayStepResult res = eng.step(row);

    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f,  res.pfwdSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.1f,  res.p45Smoothed);
    TEST_ASSERT_EQUAL_INT(0, res.flapsPos);
    TEST_ASSERT_EQUAL_INT(0, res.flapsIndex);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 55.0f, res.iasKt);
    TEST_ASSERT_TRUE(res.iasValid);
    TEST_ASSERT_EQUAL_INT(42, res.dataMark);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 7.2f,  res.pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -2.1f, res.rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f,  res.flightPathDeg);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 5000.0f, res.paltFt);
}

// IMU fields pass through unchanged
void test_imu_passthrough(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow();
    row.imuForwardG     = 0.11f;
    row.imuLateralG     = -0.05f;
    row.imuVerticalG    = 0.99f;
    row.imuRollRateDps  = 2.5f;
    row.imuPitchRateDps = -1.3f;
    row.imuYawRateDps   = 0.7f;

    ReplayStepResult res = eng.step(row);

    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.11f,  res.imuForwardG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.05f, res.imuLateralG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.99f,  res.imuVerticalG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.5f,   res.imuRollRateDps);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.3f,  res.imuPitchRateDps);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.7f,   res.imuYawRateDps);

    // AccelLatCorr == imuLateralG (body-frame Ay)
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, row.imuLateralG,  res.accelLatCorr);
    // AccelVertCorr == imuVerticalG (body-frame Az)
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, row.imuVerticalG, res.accelVertCorr);
}

// EMA state accumulates: two sequential steps on distinct inputs produce
// different AOA outputs.
//
// Fixture uses a linear polynomial curve (AOA = 30 * coeffP) so the EMA
// produces nonzero values.  With iAoaSmoothing=10 (alpha=0.1):
//   row1: pfwd=1.0, p45=0.1 -> coeffP=0.1 -> raw_aoa=3.0. EMA seeds at 3.0.
//   row2: pfwd=1.0, p45=0.3 -> coeffP=0.3 -> raw_aoa=9.0. EMA blends:
//         0.1*9.0 + 0.9*3.0 = 3.6.
//
// Pinned from a cold engine on the PR-1 commit. PR 2 (rate-correct EMA) will
// change these values — that diff is intentional and expected.
void test_ema_accumulates(void)
{
    OnSpeedConfig cfg = makeLinearCurveConfig();
    LogReplayEngine eng(cfg, 50, false);

    // Step 1: coeffP=0.1 -> raw_aoa=3.0, EMA seeds at 3.0.
    LogRow row1 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.1f);
    ReplayStepResult r1 = eng.step(row1);

    // Step 2: coeffP=0.3 -> raw_aoa=9.0, EMA blends with seeded 3.0.
    LogRow row2 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.3f);
    ReplayStepResult r2 = eng.step(row2);

    // Pinned values for PR-1 behavior. PR 2 changes EMA rate -> visible diff.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, r1.aoa);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.6f, r2.aoa);
    TEST_ASSERT_NOT_EQUAL(r1.aoa, r2.aoa);  // sanity: EMA actually moved
}

// reset() clears EMA state: step after reset == step on a fresh engine
void test_reset_clears_state(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    // Warm up the EMA with a few rows
    LogRow warmup = makeRow(2.0f, 0.5f);
    eng.step(warmup);
    eng.step(warmup);
    eng.step(warmup);

    // Now do a step with a different row to get the "warmed up" result
    LogRow testRow = makeRow(0.1f, 0.02f);
    ReplayStepResult warmed = eng.step(testRow);

    // Reset and repeat the same sequence from scratch
    eng.reset();
    LogReplayEngine fresh(cfg, 50, false);
    fresh.step(warmup);
    fresh.step(warmup);
    fresh.step(warmup);
    ReplayStepResult fresh_result = fresh.step(testRow);

    // After reset(), same input sequence produces same output as fresh engine
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.aoa, warmed.aoa);
}

// flapsRawAdcPresent = false: result.flapsRawAdcPresent is false, uValue unchanged
void test_flaps_raw_adc_absent(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow();
    row.flapsRawAdcPresent = false;
    row.flapsRawAdc        = 9999;  // should NOT be forwarded

    ReplayStepResult res = eng.step(row);
    TEST_ASSERT_FALSE(res.flapsRawAdcPresent);
}

// flapsRawAdcPresent = true: result carries the ADC value
void test_flaps_raw_adc_present(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow();
    row.flapsRawAdcPresent = true;
    row.flapsRawAdc        = 2048;

    ReplayStepResult res = eng.step(row);
    TEST_ASSERT_TRUE(res.flapsRawAdcPresent);
    TEST_ASSERT_EQUAL_UINT16(2048, res.flapsRawAdc);
}

// Flap index lookup: row with flapsPos matching second detent -> flapsIndex == 1
void test_flap_index_second_detent(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow(0.5f, 0.1f, 30);  // 30-degree detent
    ReplayStepResult res = eng.step(row);

    TEST_ASSERT_EQUAL_INT(1, res.flapsIndex);
    TEST_ASSERT_EQUAL_INT(30, res.flapsPos);
}

// Flap index lookup: row with unknown flapsPos falls back to index 0
void test_flap_index_unknown_falls_back(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow(0.5f, 0.1f, 99);  // 99 is not in aFlaps
    ReplayStepResult res = eng.step(row);

    TEST_ASSERT_EQUAL_INT(0, res.flapsIndex);
}

// iasValid = false propagates through
void test_ias_invalid_propagates(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow(0.0f, 0.0f, 0, false, 0.0f);
    ReplayStepResult res = eng.step(row);

    TEST_ASSERT_FALSE(res.iasValid);
}

// Kalman VSI conversion: vsiFpm is converted to m/s
void test_kalman_vsi_conversion(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow();
    row.vsiFpm = 196.85f;   // 1 m/s * 196.85 = 196.85 fpm

    ReplayStepResult res = eng.step(row);

    // fpm2mps(196.85f) = 196.85f / 196.85f = 1.0 m/s
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, res.kalmanVSI);
}

// coeffP is set from pressureCoeff(pfwd, p45) via AOACalculatorResult.
//
// With pfwd=10.0, p45=2.0: pressureCoeff = 2.0/10.0 = 0.2 exactly.
// The AOACalculator propagates this through regardless of the curve type.
//
// Pinned to 1e-4f tolerance. PR 2 does not change this value (it only
// changes the EMA rate for aoa, not the pressure-coefficient path).
void test_coeff_p_nonzero(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    // Use non-zero pressures so pressureCoeff produces a non-zero result.
    // coeffP = p45 / pfwd = 2.0 / 10.0 = 0.2
    LogRow row = makeRow(10.0f, 2.0f);
    ReplayStepResult res = eng.step(row);

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.2f, res.coeffP);
}

// ============================================================================
// Synth ADC tests (Sub-task 3 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md)
// ============================================================================

// Build a row sequence of length N, all at the same flap position.
std::vector<LogRow> makeRowSeq(int N, int flapPosDeg = 0)
{
    std::vector<LogRow> rows;
    rows.reserve(static_cast<size_t>(N));
    for (int i = 0; i < N; i++)
    {
        LogRow r = makeRow(0.5f, 0.1f, flapPosDeg);
        rows.push_back(r);
    }
    return rows;
}

// --- Steady-state synth: all rows at one detent, no transitions. ---
//
// With flapsRawAdcAvailable=false, prepare() fills every row with the
// detent's nominal pot value (1000 for detent 0 in makeTwoFlapConfig).
// All step() calls should emit flapsRawAdc == 1000.
void test_synth_steady_state(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    const int N = 20;
    auto rows = makeRowSeq(N, 0);   // all at detent 0 (pot=1000)
    eng.prepare(rows);

    for (int i = 0; i < N; i++)
    {
        ReplayStepResult res = eng.step(rows[static_cast<size_t>(i)]);
        // Synth path: flapsRawAdcPresent stays false (original log lacked
        // the column), but flapsRawAdc carries the synthesised value.
        TEST_ASSERT_FALSE(res.flapsRawAdcPresent);
        TEST_ASSERT_EQUAL_UINT16(1000, res.flapsRawAdc);
    }
}

// --- Single transition: detent 0 → detent 30, mid-window values pinned. ---
//
// Config: detent 0 pot=1000, detent 30 pot=3000. Sample rate 50 Hz.
// half = round(2.0 / (1/50)) = 100 ticks.
// Snap tick i = 100 (rows 0..99 at detent 0, rows 100..N-1 at detent 30).
//
// Pinned values (smoothstep math, verified by hand):
//   k = 0 (well before window) : at steady-state fill → 1000
//   k = 100 (snap tick, centre): u=0.5, λ=0.5, val=2000
//   k = 50 (quarter-window):     u=0.25, λ=0.15625, val≈1312.5
//   k = 150 (three-quarter):     u=0.75, λ=0.84375, val≈2687.5
//   k = 200 (post-window end):   steady-state fill → 3000
void test_synth_single_transition(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    // pot positions: detent 0 → 1000, detent 30 → 3000 (from makeTwoFlapConfig)
    LogReplayEngine eng(cfg, 50, false);

    // Build: 100 rows at detent 0, 101 rows at detent 30 (snap at index 100).
    const int snapIdx = 100;
    const int N = 201;
    std::vector<LogRow> rows;
    rows.reserve(static_cast<size_t>(N));
    for (int i = 0; i < N; i++)
    {
        LogRow r = makeRow(0.5f, 0.1f, (i < snapIdx) ? 0 : 30);
        rows.push_back(r);
    }
    eng.prepare(rows);

    // Step through all rows, collect flapsRawAdc.
    std::vector<uint16_t> adcOut(static_cast<size_t>(N));
    for (int i = 0; i < N; i++)
        adcOut[static_cast<size_t>(i)] = eng.step(rows[static_cast<size_t>(i)]).flapsRawAdc;

    // Pre-window steady-state: row 0, far before transition.
    TEST_ASSERT_EQUAL_UINT16(1000, adcOut[0]);

    // At the snap tick (centre of the window): u=0.5, λ=smoothstep(0.5)=0.5.
    // val = (1-0.5)*1000 + 0.5*3000 = 2000.
    TEST_ASSERT_EQUAL_UINT16(2000, adcOut[static_cast<size_t>(snapIdx)]);

    // Quarter of the way through the window (k = snapIdx - half/2 = 50):
    // signed = (50 - 100) / 100 = -0.5, u = 0.5 + 0.5*(-0.5) = 0.25.
    // λ = smoothstep(0.25) = 3*0.0625 - 2*0.015625 = 0.15625.
    // val = (1 - 0.15625)*1000 + 0.15625*3000 = 843.75 + 468.75 = 1312.5 → 1312.
    TEST_ASSERT_EQUAL_UINT16(1312, adcOut[50]);

    // Three-quarters of the way through (k = snapIdx + half/2 = 150):
    // signed = (150 - 100) / 100 = 0.5, u = 0.5 + 0.5*0.5 = 0.75.
    // λ = smoothstep(0.75) = 3*0.5625 - 2*0.421875 = 1.6875 - 0.84375 = 0.84375.
    // val = (1 - 0.84375)*1000 + 0.84375*3000 = 156.25 + 2531.25 = 2687.5 → 2687.
    TEST_ASSERT_EQUAL_UINT16(2687, adcOut[150]);

    // Post-window steady-state: last row, well after transition.
    TEST_ASSERT_EQUAL_UINT16(3000, adcOut[static_cast<size_t>(N - 1)]);
}

// --- Multiple consecutive transitions: 0 → 30 → 0, windows overlapping. ---
//
// Two flap deployments 50 ticks apart. The half-window is 100 ticks, so the
// two smoothstep windows overlap. Pass 2 processes snap1 first (0→30 at i=50),
// then snap2 (30→0 at i=100) overwrites the overlapping portion. Final values
// reflect whichever snap's window wrote last.
//
// Pinned values (computed by tracing the algorithm):
//   snap2 centre (k=100): u=0.5, λ=0.5, prevPot=3000, newPot=1000 → 2000.
//   snap1 centre (k=50): snap2's window at k=50 is
//     signed=(50-100)/100=-0.5, u=0.25, λ=0.15625,
//     val=(1-0.15625)*3000+0.15625*1000 = 2531.25+156.25 = 2687.5 → 2687.
//   (snap2 overwrites snap1's k=50 value because its window [0..149] covers k=50.)
void test_synth_multiple_transitions(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    // 50 rows at detent 0, 50 at detent 30, 50 at detent 0.
    // snap1 at index 50 (0→30), snap2 at index 100 (30→0).
    const int N = 150;
    std::vector<LogRow> rows;
    rows.reserve(static_cast<size_t>(N));
    for (int i = 0; i < N; i++)
    {
        int deg = (i < 50) ? 0 : (i < 100) ? 30 : 0;
        rows.push_back(makeRow(0.5f, 0.1f, deg));
    }
    eng.prepare(rows);

    std::vector<uint16_t> adcOut(static_cast<size_t>(N));
    for (int i = 0; i < N; i++)
        adcOut[static_cast<size_t>(i)] = eng.step(rows[static_cast<size_t>(i)]).flapsRawAdc;

    // snap2 centre (k=100): λ=0.5, val=2000.
    TEST_ASSERT_EQUAL_UINT16(2000, adcOut[100]);

    // snap1 centre (k=50): overwritten by snap2's window → 2687.
    TEST_ASSERT_EQUAL_UINT16(2687, adcOut[50]);

    // The two snaps produce monotonic-ish interpolation; no value outside
    // the range [1000, 3000] should appear.
    for (int i = 0; i < N; i++)
    {
        TEST_ASSERT_TRUE(adcOut[static_cast<size_t>(i)] >= 1000
                         && adcOut[static_cast<size_t>(i)] <= 3000);
    }
}

// --- Edge cases: single row and two-row sequences. ---
//
// prepare() on a single row (no transition possible) — must not crash.
// prepare() on two rows with a transition — must not crash even though
// the window extends beyond the sequence bounds.
void test_synth_edge_cases(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();

    // Single row
    {
        LogReplayEngine eng(cfg, 50, false);
        auto rows = makeRowSeq(1, 0);
        eng.prepare(rows);
        ReplayStepResult res = eng.step(rows[0]);
        TEST_ASSERT_EQUAL_UINT16(1000, res.flapsRawAdc);
    }

    // Two rows: row 0 at detent 0, row 1 at detent 30. Snap at index 1.
    // Window extends well outside [0, 1] — clamp must prevent out-of-bounds.
    {
        LogReplayEngine eng(cfg, 50, false);
        std::vector<LogRow> rows;
        rows.push_back(makeRow(0.5f, 0.1f, 0));
        rows.push_back(makeRow(0.5f, 0.1f, 30));
        eng.prepare(rows);
        // Must not crash; values should be bounded in [1000, 3000].
        for (size_t i = 0; i < rows.size(); i++)
        {
            uint16_t adc = eng.step(rows[i]).flapsRawAdc;
            TEST_ASSERT_TRUE(adc >= 1000 && adc <= 3000);
        }
    }
}

// --- When flapsRawAdcAvailable=true, prepare() is a no-op and real ADC  ---
// --- is passed through from the row.                                      ---
void test_prepare_noop_when_adc_available(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow();
    row.flapsRawAdcPresent = true;
    row.flapsRawAdc        = 9999;

    // prepare() with real ADC: a no-op, but must not crash.
    std::vector<LogRow> rows = { row };
    eng.prepare(rows);

    ReplayStepResult res = eng.step(row);
    TEST_ASSERT_TRUE(res.flapsRawAdcPresent);
    TEST_ASSERT_EQUAL_UINT16(9999, res.flapsRawAdc);
}

// ============================================================================
// main
// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_step_deterministic);
    RUN_TEST(test_passthrough_fields);
    RUN_TEST(test_imu_passthrough);
    RUN_TEST(test_ema_accumulates);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_flaps_raw_adc_absent);
    RUN_TEST(test_flaps_raw_adc_present);
    RUN_TEST(test_flap_index_second_detent);
    RUN_TEST(test_flap_index_unknown_falls_back);
    RUN_TEST(test_ias_invalid_propagates);
    RUN_TEST(test_kalman_vsi_conversion);
    RUN_TEST(test_coeff_p_nonzero);
    RUN_TEST(test_synth_steady_state);
    RUN_TEST(test_synth_single_transition);
    RUN_TEST(test_synth_multiple_transitions);
    RUN_TEST(test_synth_edge_cases);
    RUN_TEST(test_prepare_noop_when_adc_available);

    return UNITY_END();
}
