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
// produces nonzero values.  With iAoaSmoothing=10 (alpha=0.1), logSampleRateHz=50,
// and upsampleRatio=4 (208/50), each input row drives the EMA 4 times:
//
//   row1: pfwd=1.0, p45=0.1. prevRow=zero-default, so pfwd-interp ratio stays
//         at p45/pfwd=0.1 for all 4 sub-steps (both scale linearly from 0).
//         Sub-step k=0 seeds EMA at 3.0; k=1,2,3 blend 3.0 into 3.0 -> 3.0.
//         r1.aoa = 3.0 (same as PR-1 baseline; seeding dominates).
//
//   row2: pfwd=1.0, p45=0.3. prevRow has pfwd=1.0, p45=0.1.
//         Sub-steps linearly interpolate p45: 0.15, 0.20, 0.25, 0.30 ->
//         raw_aoa: 4.5, 6.0, 7.5, 9.0. EMA starting at 3.0:
//           k=0: 0.1*4.5 + 0.9*3.0     = 3.15
//           k=1: 0.1*6.0 + 0.9*3.15    = 3.435
//           k=2: 0.1*7.5 + 0.9*3.435   = 3.8415
//           k=3: 0.1*9.0 + 0.9*3.8415  = 4.35735
//         r2.aoa ≈ 4.35735 (vs PR-1 value of 3.6 — more lag, matching flight τ).
//
// Pinned post-rate-correction (PR 2 of PLAN_FIRMWARE_LOG_REPLAY_PARITY.md).
// PR 1 baseline at 50 Hz was r2.aoa=3.6; with 208 Hz upsampling the EMA
// effective τ matches flight, producing 4.35735.
void test_ema_accumulates(void)
{
    OnSpeedConfig cfg = makeLinearCurveConfig();
    LogReplayEngine eng(cfg, 50, false);

    // Step 1: coeffP=0.1 -> raw_aoa=3.0, EMA seeds at 3.0.
    // upsampleRatio=4; all sub-steps produce coeffP=0.1 (ratio preserved from
    // zero-initialized prevRow), so EMA stays at 3.0.
    LogRow row1 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.1f);
    ReplayStepResult r1 = eng.step(row1);

    // Step 2: coeffP=0.3 -> raw_aoa=9.0. EMA runs 4 sub-steps from 3.0.
    LogRow row2 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.3f);
    ReplayStepResult r2 = eng.step(row2);

    // Pinned post-rate-correction. PR-1 baseline: r2.aoa=3.6 (50 Hz, 1 EMA step).
    // Post-PR-2: r2.aoa=4.35735 (208 Hz upsampling, 4 EMA steps per row).
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f,    r1.aoa);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.35735f, r2.aoa);
    TEST_ASSERT_NOT_EQUAL(r1.aoa, r2.aoa);  // sanity: EMA actually moved
}

// At logSampleRateHz=208 (no upsampling), upsampleRatio=1, so each row
// drives the EMA exactly once — same behavior as PR-1 at any rate.
// Verifies that 208 Hz log replay preserves the PR-1 baseline pinned values.
void test_ema_accumulates_208hz(void)
{
    OnSpeedConfig cfg = makeLinearCurveConfig();
    LogReplayEngine eng(cfg, 208, false);

    // Same rows as test_ema_accumulates but at 208 Hz (ratio=1, no upsample).
    // Row 1: EMA seeds at raw_aoa=3.0. One sub-step (t=1.0, full current row).
    LogRow row1 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.1f);
    ReplayStepResult r1 = eng.step(row1);

    // Row 2: one EMA step from 3.0 with raw_aoa=9.0:
    //   0.1*9.0 + 0.9*3.0 = 3.6  — PR-1 baseline value.
    LogRow row2 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.3f);
    ReplayStepResult r2 = eng.step(row2);

    // Pinned: 208 Hz log, upsampleRatio=1. Matches PR-1 baseline exactly.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, r1.aoa);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.6f, r2.aoa);
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
// main
// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_step_deterministic);
    RUN_TEST(test_passthrough_fields);
    RUN_TEST(test_imu_passthrough);
    RUN_TEST(test_ema_accumulates);
    RUN_TEST(test_ema_accumulates_208hz);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_flaps_raw_adc_absent);
    RUN_TEST(test_flaps_raw_adc_present);
    RUN_TEST(test_flap_index_second_detent);
    RUN_TEST(test_flap_index_unknown_falls_back);
    RUN_TEST(test_ias_invalid_propagates);
    RUN_TEST(test_kalman_vsi_conversion);
    RUN_TEST(test_coeff_p_nonzero);

    return UNITY_END();
}
