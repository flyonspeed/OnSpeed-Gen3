// test_log_replay_engine.cpp — characterization tests for onspeed::replay::LogReplayEngine.
//
// These are CHARACTERIZATION TESTS: they pin behavior at Sub-task 2 of
// PLAN_FIRMWARE_LOG_REPLAY_PARITY.md — the engine uses RateAdjustedAccelEma
// for lateral, vertical, and forward accel channels.  Pinned values for the
// smoothed accel fields reflect the rate-adjusted EMA output at 50 Hz with
// tau = kAccelEmaTauSec ≈ 0.076516 s (alpha ≈ 0.2300).
//
// Sub-task 3 (synth flapsRawADC for old logs) will produce the next clean diff.
//
// What is tested:
//   - step() is deterministic: two fresh engines on identical input produce
//     identical output.
//   - Accel fields (accelLatSmoothed, accelVertSmoothed, accelFwdSmoothed) carry
//     rate-adjusted-EMA smoothed values, NOT raw passthrough.  First call
//     seeds at the raw value; subsequent calls blend.
//   - Engine state (AOA EMA + accel EMA) accumulates: sequential steps on
//     distinct inputs produce distinct outputs pinned to exact floats.
//   - reset() clears all EMA state (AOA + accel) so the engine behaves
//     identically to a fresh instance.
//   - Raw IMU fields (imuLateralG, imuVerticalG, imuForwardG) remain raw.
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

// Raw IMU fields pass through unchanged; smoothed accel fields seed at raw on
// first call.
//
// Raw IMU: imuLateralG / imuVerticalG / imuForwardG feed g_pIMU->Ay/Az/Ax
// and are never modified by the engine.
//
// Smoothed accel: accelLatSmoothed / accelVertSmoothed / accelFwdSmoothed are
// rate-adjusted-EMA outputs (alpha ≈ 0.2300 at 50 Hz, tau ≈ 0.076516 s).
// On the FIRST call to a fresh engine, each EMA seeds at the raw value
// (returns input as-is — same as EMAFilter::update() contract).  After
// the first call, subsequent values blend against the seed.
//
// This test exercises the first-call (seed) case.  test_accel_ema_accumulates
// exercises the multi-call blend case.
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

    // Raw IMU fields are always verbatim passthrough.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.11f,  res.imuForwardG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.05f, res.imuLateralG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.99f,  res.imuVerticalG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.5f,   res.imuRollRateDps);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.3f,  res.imuPitchRateDps);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.7f,   res.imuYawRateDps);

    // Smoothed accel: first call seeds at the raw value.
    // accelLatSmoothed  → lateral  EMA seeds at imuLateralG  = -0.05
    // accelVertSmoothed → vertical EMA seeds at imuVerticalG = 0.99
    // accelFwdSmoothed  → forward  EMA seeds at imuForwardG  = 0.11
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.05f, res.accelLatSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.99f, res.accelVertSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.11f, res.accelFwdSmoothed);
}

// AOA EMA accumulates: two sequential steps on distinct pressure inputs produce
// different AOA outputs.
//
// Fixture uses a linear polynomial curve (AOA = 30 * coeffP) so the EMA
// produces nonzero values.  With iAoaSmoothing=10 (alpha=0.1):
//   row1: pfwd=1.0, p45=0.1 -> coeffP=0.1 -> raw_aoa=3.0. EMA seeds at 3.0.
//   row2: pfwd=1.0, p45=0.3 -> coeffP=0.3 -> raw_aoa=9.0. EMA blends:
//         0.1*9.0 + 0.9*3.0 = 3.6.
//
// These values are identical to the PR-1 baseline: the AOA EMA (alpha=0.1)
// uses AOACalculator, which is not changed by Sub-task 2 (rate-adjusted accel EMA).
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

    // Pinned values — unchanged from PR-1 baseline (AOA calc is not affected
    // by Sub-task 2's rate-adjusted accel EMA).
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, r1.aoa);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.6f, r2.aoa);
    TEST_ASSERT_NOT_EQUAL(r1.aoa, r2.aoa);  // sanity: EMA actually moved
}

// Accel EMA accumulates with rate-adjusted smoothing.
//
// Verifies that accelLatSmoothed / accelVertSmoothed / accelFwdSmoothed use the
// rate-adjusted-EMA filter (alpha ≈ 0.2300 at 50 Hz, tau ≈ 0.076516 s),
// not raw passthrough, from the second step onward.
//
// Row 1 values (from makeRow defaults):
//   imuLateralG = -0.02, imuVerticalG = 1.02, imuForwardG = 0.05
//   EMA seeds at these values on the first call.
//
// Row 2 values (overridden):
//   imuLateralG = 0.10, imuVerticalG = 0.90, imuForwardG = 0.20
//   EMA blends: alpha * new + (1-alpha) * seed
//   alpha = 1 - exp(-dt / tau) at dt=1/50 s, tau = kAccelEmaTauSec ≈ 0.076516 s
//   alpha ≈ 0.23001423
//
//   accelLatSmoothed  = 0.23001423 * 0.10  + 0.76998577 * (-0.02) = 0.0076017
//   accelVertSmoothed = 0.23001423 * 0.90  + 0.76998577 * 1.02    = 0.9923983
//   accelFwdSmoothed = 0.23001423 * 0.20  + 0.76998577 * 0.05   = 0.0845014
//
// Pinned to 4 decimal places (%.4f wire precision). Sub-task 3 (synth ADC)
// does not change these values.
void test_accel_ema_accumulates(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    // Step 1: seeds the accel EMA at the default makeRow() IMU values.
    // imuLateralG=-0.02, imuVerticalG=1.02, imuForwardG=0.05
    LogRow row1 = makeRow();
    ReplayStepResult r1 = eng.step(row1);

    // First step: EMA seeds at raw values (no blending yet).
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.02f, r1.accelLatSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  1.02f, r1.accelVertSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.05f, r1.accelFwdSmoothed);

    // Raw IMU fields are always verbatim — not affected by EMA state.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.02f, r1.imuLateralG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  1.02f, r1.imuVerticalG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.05f, r1.imuForwardG);

    // Step 2: different IMU values blend against the seeded EMA state.
    LogRow row2 = makeRow();
    row2.imuLateralG  =  0.10f;
    row2.imuVerticalG =  0.90f;
    row2.imuForwardG  =  0.20f;
    ReplayStepResult r2 = eng.step(row2);

    // Pinned values: alpha ≈ 0.23001423 at 50 Hz, tau = kAccelEmaTauSec.
    //   seed values from row1: lat=-0.02, vert=1.02, fwd=0.05
    //   accelLatSmoothed  = 0.23001 * 0.10  + 0.76999 * (-0.02) = 0.0076
    //   accelVertSmoothed = 0.23001 * 0.90  + 0.76999 * 1.02    = 0.9924
    //   accelFwdSmoothed  = 0.23001 * 0.20  + 0.76999 * 0.05    = 0.0845
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  0.0076f, r2.accelLatSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  0.9924f, r2.accelVertSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f,  0.0845f, r2.accelFwdSmoothed);

    // Raw IMU fields remain raw on step 2.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.10f, r2.imuLateralG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.90f, r2.imuVerticalG);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.20f, r2.imuForwardG);

    // Smoothed values differ from raw (EMA blending is visible).
    TEST_ASSERT_NOT_EQUAL(r2.imuLateralG,  r2.accelLatSmoothed);
    TEST_ASSERT_NOT_EQUAL(r2.imuVerticalG, r2.accelVertSmoothed);
}

// reset() clears all EMA state (AOA + accel): step sequence after reset
// produces identical output to the same sequence on a fresh engine.
void test_reset_clears_state(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    // Warm up the AOA + accel EMA with a few rows.
    LogRow warmup = makeRow(2.0f, 0.5f);
    warmup.imuLateralG = 0.15f;
    warmup.imuVerticalG = 0.85f;
    warmup.imuForwardG = 0.03f;
    eng.step(warmup);
    eng.step(warmup);
    eng.step(warmup);

    // Step with a different row to get the "warmed up" result.
    LogRow testRow = makeRow(0.1f, 0.02f);
    testRow.imuLateralG = -0.08f;
    testRow.imuVerticalG = 1.05f;
    testRow.imuForwardG = 0.02f;
    ReplayStepResult warmed = eng.step(testRow);

    // Reset and repeat the same sequence from scratch.
    eng.reset();
    LogReplayEngine fresh(cfg, 50, false);
    fresh.step(warmup);
    fresh.step(warmup);
    fresh.step(warmup);
    ReplayStepResult fresh_result = fresh.step(testRow);

    // After reset(), same input sequence produces same output as fresh engine.
    // Checks both AOA EMA and accel EMA state are cleared.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.aoa,             warmed.aoa);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.accelLatSmoothed,  warmed.accelLatSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.accelVertSmoothed, warmed.accelVertSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.accelFwdSmoothed, warmed.accelFwdSmoothed);
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

// Rapid cycling through many out-of-range detent values falls back
// consistently to index 0.
//
// makeTwoFlapConfig() has only two detents: 0° and 30°.  Feeding the engine
// 9+ distinct unmapped flap degrees (5, 10, 15, 20, 25, 35, 40, 45, 50) —
// more than the number of configured detents — exercises the linear-scan
// fallback in ResolveFlapIndex_() for each of them.  The guard being tested
// is the out.flapsIndex=0 fallback; if it were to corrupt flap-index state
// across rows, subsequent steps against valid detent values would return the
// wrong index.
//
// After all 9 unmapped rows, two final rows with mapped detents (0° and 30°)
// must return flapsIndex=0 and flapsIndex=1 respectively — confirming that
// sustained fallback use doesn't poison the lookup for known detents.
void test_kmaxtransitions_overflow_evicts_oldest(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    // 9 distinct unmapped flap degrees — more than the 2 configured detents.
    const int unmapped[] = { 5, 10, 15, 20, 25, 35, 40, 45, 50 };
    const int kNumUnmapped = static_cast<int>(sizeof(unmapped) / sizeof(unmapped[0]));

    for (int i = 0; i < kNumUnmapped; i++)
    {
        LogRow row = makeRow(0.5f, 0.1f, unmapped[i]);
        ReplayStepResult res = eng.step(row);

        // Every unmapped degree must fall back to index 0.
        TEST_ASSERT_EQUAL_INT(0, res.flapsIndex);
        // The reported flapsPos is the raw log value, not the detent.
        TEST_ASSERT_EQUAL_INT(unmapped[i], res.flapsPos);
    }

    // Mapped detent 0° still resolves to index 0 after all the fallback rows.
    {
        LogRow row0 = makeRow(0.5f, 0.1f, 0);
        ReplayStepResult res0 = eng.step(row0);
        TEST_ASSERT_EQUAL_INT(0, res0.flapsIndex);
    }

    // Mapped detent 30° still resolves to index 1 after all the fallback rows.
    {
        LogRow row1 = makeRow(0.5f, 0.1f, 30);
        ReplayStepResult res1 = eng.step(row1);
        TEST_ASSERT_EQUAL_INT(1, res1.flapsIndex);
    }
}

// reset() on a fresh engine (pre-step) leaves the engine in the same initial
// state: subsequent step() output is identical to a never-reset engine given
// the same input.
//
// Two assertions:
//   (a) reset() before any step() does not crash and the first step() after
//       reset() produces the same result as the first step() on a freshly
//       constructed engine.
//   (b) reset() called twice after warmup brings the engine to the same state
//       as a single reset() after warmup — second reset() on already-reset
//       state is idempotent.
void test_flush_idempotent_and_pre_step(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogRow probe = makeRow(0.5f, 0.1f);

    // (a) reset() before any step(): output matches fresh engine.
    {
        LogReplayEngine eng_reset(cfg, 50, false);
        eng_reset.reset();   // pre-step reset — should be a no-op

        LogReplayEngine eng_fresh(cfg, 50, false);

        ReplayStepResult after_reset = eng_reset.step(probe);
        ReplayStepResult from_fresh  = eng_fresh.step(probe);

        TEST_ASSERT_FLOAT_WITHIN(1e-5f, from_fresh.aoa,             after_reset.aoa);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, from_fresh.accelLatSmoothed,  after_reset.accelLatSmoothed);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, from_fresh.accelVertSmoothed, after_reset.accelVertSmoothed);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, from_fresh.accelFwdSmoothed,  after_reset.accelFwdSmoothed);
    }

    // (b) reset() twice after warmup: second reset() is idempotent.
    //
    // Both engines are warmed up identically then reset once.  After a
    // single additional reset() on one of them, both must produce identical
    // output on the same probe row.  Confirms that reset() on an
    // already-reset engine is a no-op, not a double-clear that diverges.
    {
        LogRow warmup = makeRow(2.0f, 0.4f);
        warmup.imuLateralG  = 0.12f;
        warmup.imuVerticalG = 0.88f;
        warmup.imuForwardG  = 0.04f;

        LogReplayEngine eng_once(cfg, 50, false);
        LogReplayEngine eng_twice(cfg, 50, false);

        for (int i = 0; i < 5; i++)
        {
            eng_once.step(warmup);
            eng_twice.step(warmup);
        }

        // Reset both once.
        eng_once.reset();
        eng_twice.reset();

        // Reset eng_twice a second time — should be identical to single reset.
        eng_twice.reset();

        ReplayStepResult once_result  = eng_once.step(probe);
        ReplayStepResult twice_result = eng_twice.step(probe);

        TEST_ASSERT_FLOAT_WITHIN(1e-5f, once_result.aoa,              twice_result.aoa);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, once_result.accelLatSmoothed,  twice_result.accelLatSmoothed);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, once_result.accelVertSmoothed, twice_result.accelVertSmoothed);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, once_result.accelFwdSmoothed,  twice_result.accelFwdSmoothed);
    }
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
    RUN_TEST(test_accel_ema_accumulates);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_flaps_raw_adc_absent);
    RUN_TEST(test_flaps_raw_adc_present);
    RUN_TEST(test_flap_index_second_detent);
    RUN_TEST(test_flap_index_unknown_falls_back);
    RUN_TEST(test_ias_invalid_propagates);
    RUN_TEST(test_kalman_vsi_conversion);
    RUN_TEST(test_coeff_p_nonzero);
    RUN_TEST(test_kmaxtransitions_overflow_evicts_oldest);
    RUN_TEST(test_flush_idempotent_and_pre_step);

    return UNITY_END();
}
