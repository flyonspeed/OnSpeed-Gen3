// test_log_replay_engine.cpp — characterization and streaming tests for
// onspeed::replay::LogReplayEngine.
//
// CHARACTERIZATION TESTS (Sub-task 2 behavior — accel EMA):
//   These pin the baseline behavior with RateAdjustedAccelEma for lateral,
//   vertical, and forward accel channels. Pinned values for the smoothed accel
//   fields reflect the rate-adjusted EMA output at 50 Hz with
//   tau = kAccelEmaTauSec ≈ 0.076516 s (alpha ≈ 0.2300). Tests use
//   flapsRawAdcAvailable=true so step() returns immediately (no lag).
//
// STREAMING SYNTH TESTS (Sub-task 3 — new):
//   These exercise the circular buffer, lag contract, flush(), bounded
//   memory, and streaming-vs-batch equivalence.

#include <unity.h>

#include <cmath>
#include <cstring>
#include <optional>
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
using onspeed::replay::kSynthHalfWindowSec;

// kSynthHalfWindow50: expected half-window tick count at 50 Hz.
// = kSynthHalfWindowSec (2.0 s) × 50 Hz = 100 ticks.
// All 50 Hz streaming tests use this to verify the lag contract,
// flush size, and smoothstep window at the standard log rate.
static constexpr int kSynthHalfWindow50 = static_cast<int>(kSynthHalfWindowSec * 50.0f);
static_assert(kSynthHalfWindow50 == 100, "unexpected 50 Hz half-window tick count");

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

// Helper: step() and assert the result is non-empty (fast path / ADC present).
ReplayStepResult stepExpectResult(LogReplayEngine& eng, const LogRow& row)
{
    auto opt = eng.step(row);
    TEST_ASSERT_TRUE_MESSAGE(opt.has_value(), "step() returned empty when a result was expected");
    return opt.value();
}

// Batch reference: compute the smoothstep-blended synth ADC for a given tick
// relative to a transition at snapTick, with prevPot and nextPot.
// halfWindow is the per-engine synthHalfWindowTicks_ (rate-dependent).
// Returns the nominal steady-state pot value when tick is outside the window.
uint16_t batchSynthAdc(int tick, int snapTick, int prevPot, int nextPot,
                        int steadyPot, int halfWindow)
{
    const int winStart = snapTick - halfWindow;
    const int winEnd   = snapTick + halfWindow;
    if (tick < winStart || tick > winEnd)
        return static_cast<uint16_t>(steadyPot);

    const float span = static_cast<float>(2 * halfWindow);
    float t = static_cast<float>(tick - winStart) / span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float s = t * t * (3.0f - 2.0f * t);
    const float result = static_cast<float>(prevPot) + s * static_cast<float>(nextPot - prevPot);
    if (result < 0.0f)    return 0u;
    if (result > 65535.0f) return 65535u;
    return static_cast<uint16_t>(result + 0.5f);
}

}  // namespace

// ============================================================================
// setUp / tearDown (Unity requirement)
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Characterization tests (unchanged from PR 1, now using optional unwrap)
// ============================================================================

// step() is deterministic: two fresh engines on identical input produce
// identical output. Uses flapsRawAdcAvailable=true so step() returns
// immediately with no lag.
void test_step_deterministic(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogRow row = makeRow();
    row.flapsRawAdcPresent = true;
    row.flapsRawAdc = 1234;

    LogReplayEngine engA(cfg, 50, true);
    LogReplayEngine engB(cfg, 50, true);

    ReplayStepResult rA = stepExpectResult(engA, row);
    ReplayStepResult rB = stepExpectResult(engB, row);

    TEST_ASSERT_EQUAL_FLOAT(rA.aoa,       rB.aoa);
    TEST_ASSERT_EQUAL_FLOAT(rA.coeffP,   rB.coeffP);
    TEST_ASSERT_EQUAL_INT(rA.flapsIndex, rB.flapsIndex);
}

// Fields from the row are passed through to the result unchanged
void test_passthrough_fields(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow(0.5f, 0.1f, 0, true, 55.0f);
    row.dataMark     = 42;
    row.pitchDeg     = 7.2f;
    row.rollDeg      = -2.1f;
    row.flightPathDeg = 3.0f;
    row.paltFt       = 5000.0f;
    row.vsiFpm       = 200.0f;

    ReplayStepResult res = stepExpectResult(eng, row);

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
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow();
    row.imuForwardG     = 0.11f;
    row.imuLateralG     = -0.05f;
    row.imuVerticalG    = 0.99f;
    row.imuRollRateDps  = 2.5f;
    row.imuPitchRateDps = -1.3f;
    row.imuYawRateDps   = 0.7f;

    ReplayStepResult res = stepExpectResult(eng, row);

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
    LogReplayEngine eng(cfg, 50, true);

    // Step 1: coeffP=0.1 -> raw_aoa=3.0, EMA seeds at 3.0.
    LogRow row1 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.1f);
    ReplayStepResult r1 = stepExpectResult(eng, row1);

    // Step 2: coeffP=0.3 -> raw_aoa=9.0, EMA blends with seeded 3.0.
    LogRow row2 = makeRow(/*pfwd=*/1.0f, /*p45=*/0.3f);
    ReplayStepResult r2 = stepExpectResult(eng, row2);

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
    // flapsRawAdcAvailable=true: column present in log, step() returns immediately
    // (no synth lag). The EMA smoothing under test is independent of the synth path.
    LogReplayEngine eng(cfg, 50, true);

    // Step 1: seeds the accel EMA at the default makeRow() IMU values.
    // imuLateralG=-0.02, imuVerticalG=1.02, imuForwardG=0.05
    LogRow row1 = makeRow();
    ReplayStepResult r1 = stepExpectResult(eng, row1);

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
    ReplayStepResult r2 = stepExpectResult(eng, row2);

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
    LogReplayEngine eng(cfg, 50, true);

    // Warm up the AOA + accel EMA with a few rows.
    LogRow warmup = makeRow(2.0f, 0.5f);
    stepExpectResult(eng, warmup);
    stepExpectResult(eng, warmup);
    stepExpectResult(eng, warmup);

    // Step with a different row to get the "warmed up" result.
    LogRow testRow = makeRow(0.1f, 0.02f);
    ReplayStepResult warmed = stepExpectResult(eng, testRow);

    // Reset and repeat the same sequence from scratch.
    eng.reset();
    LogReplayEngine fresh(cfg, 50, true);
    stepExpectResult(fresh, warmup);
    stepExpectResult(fresh, warmup);
    stepExpectResult(fresh, warmup);
    ReplayStepResult fresh_result = stepExpectResult(fresh, testRow);

    // After reset(), same input sequence produces same output as fresh engine.
    // Checks both AOA EMA and accel EMA state are cleared.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.aoa,             warmed.aoa);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.accelLatSmoothed,  warmed.accelLatSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.accelVertSmoothed, warmed.accelVertSmoothed);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, fresh_result.accelFwdSmoothed, warmed.accelFwdSmoothed);
}

// flapsRawAdcPresent = false in row + flapsRawAdcAvailable=true in engine:
// result.flapsRawAdcPresent is false (column was in log but this row didn't set it).
void test_flaps_raw_adc_absent(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow();
    row.flapsRawAdcPresent = false;
    row.flapsRawAdc        = 9999;  // should NOT be forwarded

    ReplayStepResult res = stepExpectResult(eng, row);
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

    ReplayStepResult res = stepExpectResult(eng, row);
    TEST_ASSERT_TRUE(res.flapsRawAdcPresent);
    TEST_ASSERT_EQUAL_UINT16(2048, res.flapsRawAdc);
}

// Flap index lookup: row with flapsPos matching second detent -> flapsIndex == 1
void test_flap_index_second_detent(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow(0.5f, 0.1f, 30);  // 30-degree detent
    ReplayStepResult res = stepExpectResult(eng, row);

    TEST_ASSERT_EQUAL_INT(1, res.flapsIndex);
    TEST_ASSERT_EQUAL_INT(30, res.flapsPos);
}

// Flap index lookup: row with unknown flapsPos falls back to index 0
void test_flap_index_unknown_falls_back(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow(0.5f, 0.1f, 99);  // 99 is not in aFlaps
    ReplayStepResult res = stepExpectResult(eng, row);

    TEST_ASSERT_EQUAL_INT(0, res.flapsIndex);
}

// iasValid = false propagates through
void test_ias_invalid_propagates(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow(0.0f, 0.0f, 0, false, 0.0f);
    ReplayStepResult res = stepExpectResult(eng, row);

    TEST_ASSERT_FALSE(res.iasValid);
}

// Kalman VSI conversion: vsiFpm is converted to m/s
void test_kalman_vsi_conversion(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, true);

    LogRow row = makeRow();
    row.vsiFpm = 196.85f;   // 1 m/s * 196.85 = 196.85 fpm

    ReplayStepResult res = stepExpectResult(eng, row);

    // fpm2mps(196.85f) = 196.85f / 196.85f = 1.0 m/s
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, res.vsiMps);
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
    LogReplayEngine eng(cfg, 50, true);

    // Use non-zero pressures so pressureCoeff produces a non-zero result.
    // coeffP = p45 / pfwd = 2.0 / 10.0 = 0.2
    LogRow row = makeRow(10.0f, 2.0f);
    ReplayStepResult res = stepExpectResult(eng, row);

    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.2f, res.coeffP);
}

// ============================================================================
// Streaming synth tests (PR 3 — new)
// ============================================================================

// Lag contract: when flapsRawAdcAvailable is false, step() returns empty
// for the first kSynthHalfWindow50 calls, then starts returning results.
void test_streaming_lag_contract(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, /*flapsRawAdcAvailable=*/false);

    LogRow row = makeRow(0.5f, 0.1f, 0);

    // First kSynthHalfWindow50 rows should return empty.
    for (int i = 0; i < kSynthHalfWindow50; i++) {
        auto opt = eng.step(row);
        TEST_ASSERT_FALSE_MESSAGE(opt.has_value(),
            "step() should return empty during lag period");
    }

    // (kSynthHalfWindow50 + 1)-th row should return a result.
    auto opt = eng.step(row);
    TEST_ASSERT_TRUE_MESSAGE(opt.has_value(),
        "step() should return a result once buffer is full");

    // Total rows fed should be kSynthHalfWindow50 + 1.
    TEST_ASSERT_EQUAL_INT(kSynthHalfWindow50 + 1, eng.rowsFed());
}

// Flush contract: after feeding N rows, flush() emits exactly kSynthHalfWindow50
// results (the tail rows that were buffered).
void test_streaming_flush_emits_tail(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow(0.5f, 0.1f, 0);

    // Feed exactly kSynthHalfWindow50 rows (still in lag — no output yet).
    int stepResults = 0;
    for (int i = 0; i < kSynthHalfWindow50; i++) {
        if (eng.step(row).has_value())
            ++stepResults;
    }
    // Nothing should have been emitted yet.
    TEST_ASSERT_EQUAL_INT(0, stepResults);

    // flush() should emit exactly kSynthHalfWindow50 rows.
    auto tail = eng.flush();
    TEST_ASSERT_EQUAL_INT(kSynthHalfWindow50, (int)tail.size());
}

// Total rows in = rows from step() + rows from flush()
// For N input rows, we should get exactly N output rows total.
void test_streaming_total_output_equals_input(void)
{
    const int N = kSynthHalfWindow50 * 3 + 17;  // not a round multiple

    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow(0.5f, 0.1f, 0);

    int stepCount = 0;
    for (int i = 0; i < N; i++) {
        if (eng.step(row).has_value())
            ++stepCount;
    }

    auto tail = eng.flush();
    const int totalOut = stepCount + (int)tail.size();

    TEST_ASSERT_EQUAL_INT(N, totalOut);
}

// Bounded memory: feeding 1000 rows should not grow the engine's static
// footprint. The buffer is sized at construction from logSampleRateHz;
// the transitions_ array is a fixed-size C array. Neither grows after
// construction.
void test_streaming_bounded_memory(void)
{
    // Static size check: catches inline-array regressions only. circBuf_ is a
    // std::vector (24 bytes on 64-bit), so sizeof(LogReplayEngine) stays small
    // regardless of how much heap the vector has reserved. Heap growth (e.g.,
    // a push_back that escapes the constructor) is policed by the runtime check
    // below.
    static_assert(sizeof(onspeed::replay::LogReplayEngine) < 64 * 1024,
                  "LogReplayEngine struct size growing unexpectedly; check for "
                  "unbounded growing-vector storage. circBuf_ should be a "
                  "std::vector (24 bytes), not a growing array.");

    OnSpeedConfig cfg = makeTwoFlapConfig();

    // Runtime capacity check: circBuf_ is sized at construction to
    // synthHalfWindowTicks_ + 1 entries and must never grow. At 50 Hz,
    // synthHalfWindowTicks_ = 100, so the expected capacity is 101.
    LogReplayEngine eng(cfg, 50, false);
    const size_t initialCapacity = eng.bufferCapacity();
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        static_cast<size_t>(kSynthHalfWindow50 + 1), initialCapacity,
        "circBuf_ capacity at 50 Hz should be synthHalfWindowTicks_+1 == 101");

    // Feed 1000 rows; switch flap detent every 200 rows to exercise transitions.
    for (int i = 0; i < 1000; i++) {
        int flapDeg = ((i / 200) % 2 == 0) ? 0 : 30;
        LogRow row = makeRow(0.5f, 0.1f, flapDeg);
        eng.step(row);
    }

    // Capacity must be unchanged — no heap growth permitted.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(initialCapacity, eng.bufferCapacity(),
        "circBuf_ capacity grew after construction; streaming bounded-memory guarantee violated");

    // flush() must return at most kSynthHalfWindow50 rows, regardless of input length.
    auto tail = eng.flush();
    TEST_ASSERT_TRUE_MESSAGE((int)tail.size() <= kSynthHalfWindow50,
        "flush() returned more than kSynthHalfWindow50 rows");

    // rowsFed() should equal the total rows fed (1000).
    TEST_ASSERT_EQUAL_INT(1000, eng.rowsFed());
}

// Steady-state synth: when there are no transitions, every emitted row's
// flapsRawAdc should equal the nominal pot position for that flap detent.
void test_synth_steady_state_no_transition(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    // Detent 0 pot = 1000, detent 1 pot = 3000.

    LogReplayEngine eng(cfg, 50, false);
    LogRow row = makeRow(0.5f, 0.1f, 0);  // always flaps=0

    // Feed enough rows to fill the buffer and get output.
    std::vector<ReplayStepResult> results;
    for (int i = 0; i < kSynthHalfWindow50 * 2; i++) {
        auto opt = eng.step(row);
        if (opt.has_value())
            results.push_back(opt.value());
    }
    auto tail = eng.flush();
    results.insert(results.end(), tail.begin(), tail.end());

    // All emitted rows should have flapsRawAdc == 1000 (detent 0 pot).
    for (size_t i = 0; i < results.size(); i++) {
        TEST_ASSERT_TRUE(results[i].flapsRawAdcPresent);
        TEST_ASSERT_EQUAL_UINT16(1000, results[i].flapsRawAdc);
    }
}

// Transition: after feeding 50 rows at flaps=0 then switching to flaps=30,
// the emitted rows near the transition should show a smoothstep blend.
// Specifically, the row AT the transition should be mid-blend (not the full
// pot for either detent). Rows far from the transition should be steady-state.
void test_synth_single_transition(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    // Detent 0 pot = 1000, detent 1 pot = 3000.

    LogReplayEngine eng(cfg, 50, false);

    // Feed 50 rows at flaps=0.
    const int preTrans  = 50;
    const int postTrans = kSynthHalfWindow50 * 2;

    LogRow row0 = makeRow(0.5f, 0.1f, 0);
    LogRow row30 = makeRow(0.5f, 0.1f, 30);

    std::vector<ReplayStepResult> results;
    for (int i = 0; i < preTrans; i++) {
        auto opt = eng.step(row0);
        if (opt.has_value()) results.push_back(opt.value());
    }
    // Transition on tick (preTrans + 1) — engine sees flaps=30 for first time.
    for (int i = 0; i < postTrans; i++) {
        auto opt = eng.step(row30);
        if (opt.has_value()) results.push_back(opt.value());
    }
    auto tail = eng.flush();
    results.insert(results.end(), tail.begin(), tail.end());

    // Total output should equal total input.
    TEST_ASSERT_EQUAL_INT(preTrans + postTrans, (int)results.size());

    // Pre-transition: rows well before the window should have steady pot=1000.
    // The first emitted row is at absolute tick kSynthHalfWindow50 (because of lag).
    // The transition occurs at tick preTrans+1 (1-indexed). The smoothstep window
    // around the transition spans [preTrans+1 - kSynthHalfWindow50, preTrans+1 + kSynthHalfWindow50].
    // Emitted row index 0 corresponds to absolute tick kSynthHalfWindow50.
    // If kSynthHalfWindow50 > preTrans the first emitted row is already inside the window.
    // If kSynthHalfWindow50 <= preTrans the first few emitted rows are steady-state.
    if (kSynthHalfWindow50 <= preTrans) {
        TEST_ASSERT_EQUAL_UINT16(1000, results[0].flapsRawAdc);
    }

    // Post-transition: the last emitted row is far after the window; should be
    // steady at pot=3000.
    TEST_ASSERT_EQUAL_UINT16(3000, results.back().flapsRawAdc);

    // At the transition point: the row emitted at the snap tick itself should
    // be at exactly s=0.5 (t=0.5 in the smoothstep) and value = lerp(1000,3000,0.5)=2000.
    // But since the emitted tick lags the input tick by kSynthHalfWindow50, the
    // snap tick row is emitted kSynthHalfWindow50 rows after the transition row enters.
    // That is: results[(preTrans+1) - kSynthHalfWindow50 + kSynthHalfWindow50 - kSynthHalfWindow50]
    //        = results[preTrans + 1 - kSynthHalfWindow50]
    // if preTrans+1 >= kSynthHalfWindow50; otherwise it's inside the buffer still.
    if (preTrans + 1 >= kSynthHalfWindow50) {
        const int snapEmitIdx = preTrans + 1 - kSynthHalfWindow50;
        if (snapEmitIdx >= 0 && snapEmitIdx < (int)results.size()) {
            // At the snap tick (t=0.5): smoothstep s = 3*(0.5)^2 - 2*(0.5)^3 = 0.5
            // Lerp(1000, 3000, 0.5) = 2000
            const uint16_t atSnap = results[snapEmitIdx].flapsRawAdc;
            TEST_ASSERT_UINT16_WITHIN(2, 2000, atSnap);
        }
    }
}

// run_streaming_vs_batch_equivalence_at — shared helper exercised at both
// 50 Hz and 208 Hz by the two test functions below.
//
// Proves that the streaming engine produces the same output as a reference
// batch computation for a known input sequence at the given sample rate.
//
// Input: halfWindow rows at flaps=0, then halfWindow rows at flaps=30.
// Batch reference: compute the expected synth ADC for each tick directly using
// the smoothstep formula and the known transition tick, parameterised by
// halfWindow so the math is rate-independent.
static void run_streaming_vs_batch_equivalence_at(int logSampleRateHz)
{
    const int halfWindow = static_cast<int>(kSynthHalfWindowSec * static_cast<float>(logSampleRateHz));

    OnSpeedConfig cfg = makeTwoFlapConfig();
    // Detent 0 pot = 1000, detent 1 pot = 3000.
    const int prevPot = 1000;
    const int nextPot = 3000;

    const int phase1 = halfWindow;  // rows at flaps=0
    const int phase2 = halfWindow;  // rows at flaps=30

    LogReplayEngine eng(cfg, logSampleRateHz, false);
    LogRow row0  = makeRow(0.5f, 0.1f, 0);
    LogRow row30 = makeRow(0.5f, 0.1f, 30);

    // Collect all emitted ADC values.
    std::vector<uint16_t> streamingAdc;
    for (int i = 0; i < phase1; i++) {
        auto opt = eng.step(row0);
        if (opt.has_value())
            streamingAdc.push_back(opt.value().flapsRawAdc);
    }
    // The transition occurs when flaps first changes to 30.
    // rowsFed() at that point = phase1 + 1 (first flaps=30 row).
    const int snapTick = phase1 + 1;  // 1-indexed rowsFed value at transition
    for (int i = 0; i < phase2; i++) {
        auto opt = eng.step(row30);
        if (opt.has_value())
            streamingAdc.push_back(opt.value().flapsRawAdc);
    }
    auto tail = eng.flush();
    for (const auto& r : tail)
        streamingAdc.push_back(r.flapsRawAdc);

    // Total output rows.
    TEST_ASSERT_EQUAL_INT(phase1 + phase2, (int)streamingAdc.size());

    // Batch reference: the k-th emitted row (0-indexed) was fed at absolute
    // tick (k + 1). The first emitted row (k=0) is tick 1; the last (k=N-1)
    // is tick N. step() lags by halfWindow, so only ticks [1..N-halfWindow]
    // come from step() and ticks [N-halfWindow+1..N] come from flush().
    //
    // The transition snapTick = phase1 + 1 (first row at flaps=30, 1-indexed).
    // Smoothstep window: [snapTick - halfWindow, snapTick + halfWindow].
    for (int k = 0; k < (int)streamingAdc.size(); k++) {
        const int emitTick = k + 1;  // 1-indexed
        // Steady-state pot: prevPot before snap, nextPot after.
        const int steadyPot = (emitTick < snapTick) ? prevPot : nextPot;
        const uint16_t expected = batchSynthAdc(emitTick, snapTick,
                                                 prevPot, nextPot, steadyPot,
                                                 halfWindow);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected, streamingAdc[k],
            "streaming output does not match batch reference");
    }
}

void test_streaming_vs_batch_equivalence_50hz(void)
{
    run_streaming_vs_batch_equivalence_at(50);
}

void test_streaming_vs_batch_equivalence_208hz(void)
{
    run_streaming_vs_batch_equivalence_at(208);
}

// flush() on the fast path (flapsRawAdcAvailable=true) returns an empty vector.
void test_flush_fast_path_empty(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, /*flapsRawAdcAvailable=*/true);

    LogRow row = makeRow();
    row.flapsRawAdcPresent = true;
    row.flapsRawAdc = 999;

    for (int i = 0; i < 10; i++)
        stepExpectResult(eng, row);

    auto tail = eng.flush();
    TEST_ASSERT_EQUAL_INT(0, (int)tail.size());
}

// reset() clears the synth buffer: after reset(), rowsFed() is 0 and the
// lag starts again.
void test_reset_clears_synth_buffer(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    LogReplayEngine eng(cfg, 50, false);

    LogRow row = makeRow(0.5f, 0.1f, 0);

    // Fill the buffer past the lag point.
    for (int i = 0; i < kSynthHalfWindow50 + 5; i++)
        eng.step(row);

    TEST_ASSERT_TRUE(eng.rowsFed() > kSynthHalfWindow50);

    // After reset, rowsFed() is 0 and the lag starts again.
    eng.reset();
    TEST_ASSERT_EQUAL_INT(0, eng.rowsFed());

    // First kSynthHalfWindow50 rows should return empty again.
    for (int i = 0; i < kSynthHalfWindow50; i++) {
        auto opt = eng.step(row);
        TEST_ASSERT_FALSE(opt.has_value());
    }
    // (kSynthHalfWindow50 + 1)-th row should return a result.
    auto opt = eng.step(row);
    TEST_ASSERT_TRUE(opt.has_value());
}

// ============================================================================
// 208 Hz characterization tests
//
// These verify that the rate-aware synth window (kSynthHalfWindowSec ×
// logSampleRateHz) produces the same wall-clock smoothstep semantics at
// 208 Hz as it does at 50 Hz.  The tick count differs (416 vs 100) but
// fractional positions within the window are identical.
// ============================================================================

// kSynthHalfWindow208: expected half-window tick count at 208 Hz.
// = kSynthHalfWindowSec (2.0 s) × 208 Hz = 416 ticks.
static constexpr int kSynthHalfWindow208 = static_cast<int>(kSynthHalfWindowSec * 208.0f);
static_assert(kSynthHalfWindow208 == 416, "unexpected 208 Hz half-window tick count");

// The engine at 208 Hz uses a half-window of 416 ticks (= kSynthHalfWindowSec × 208).
// At the transition midpoint (t = 0.5), smoothstep s = 0.5 regardless of rate.
// The lerp'd pot value at the midpoint is therefore lerp(1000, 3000, 0.5) = 2000
// at both 50 Hz and 208 Hz — the wall-clock semantic is preserved.
void test_synth_208hz_half_window_ticks(void)
{
    // Engine at 208 Hz: synthHalfWindowTicks_ should be 416.
    // We verify this indirectly: the lag contract holds for 416 ticks.
    OnSpeedConfig cfg = makeTwoFlapConfig();
    // Detent 0 pot = 1000, detent 1 pot = 3000.

    LogReplayEngine eng(cfg, 208, false);
    LogRow row0  = makeRow(0.5f, 0.1f, 0);

    // Feed exactly kSynthHalfWindow208 rows — still in lag (no output yet).
    int stepResults = 0;
    for (int i = 0; i < kSynthHalfWindow208; i++) {
        if (eng.step(row0).has_value())
            ++stepResults;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stepResults,
        "at 208 Hz, first kSynthHalfWindow208 rows should be lag (no output)");

    // (kSynthHalfWindow208 + 1)-th row should return a result.
    auto opt = eng.step(row0);
    TEST_ASSERT_TRUE_MESSAGE(opt.has_value(),
        "at 208 Hz, row kSynthHalfWindow208+1 should produce output");

    // Total rows fed: kSynthHalfWindow208 + 1 = 417.
    TEST_ASSERT_EQUAL_INT(kSynthHalfWindow208 + 1, eng.rowsFed());
}

// At 208 Hz, the smoothstep transition spans 832 rows (2 × 416 = 4 seconds,
// same wall-clock duration as 50 Hz spanning 200 rows = 4 seconds).
// At the midpoint of the transition (t = 0.5), s = 0.5 → lerp(1000,3000,0.5) = 2000.
// This midpoint value is rate-independent: same result at 50 Hz or 208 Hz.
void test_synth_208hz_midpoint_value(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();
    // Detent 0 pot = 1000, detent 1 pot = 3000.

    const int halfWindow = kSynthHalfWindow208;  // 416

    LogReplayEngine eng(cfg, 208, false);
    LogRow row0  = makeRow(0.5f, 0.1f, 0);
    LogRow row30 = makeRow(0.5f, 0.1f, 30);

    // Feed halfWindow rows at flaps=0 before the transition.
    // Transition occurs on row halfWindow+1 (1-indexed rowsFed_ at transition).
    const int preTrans = halfWindow;  // exactly halfWindow rows before snap
    for (int i = 0; i < preTrans; i++)
        eng.step(row0);

    // First row at flaps=30: snap tick = preTrans + 1.
    const int snapTick = preTrans + 1;

    // Feed halfWindow rows at flaps=30 to cover the post-transition window.
    std::vector<ReplayStepResult> results;
    for (int i = 0; i < halfWindow; i++) {
        auto opt = eng.step(row30);
        if (opt.has_value()) results.push_back(opt.value());
    }
    auto tail = eng.flush();
    results.insert(results.end(), tail.begin(), tail.end());

    // Total output rows = preTrans + halfWindow (same as total input rows).
    TEST_ASSERT_EQUAL_INT(preTrans + halfWindow, (int)results.size());

    // Index math: the k-th output (0-based) has emitTick = k + 1.
    // So emitTick T → output index T - 1.
    //
    // snapTick = halfWindow + 1 = 417.
    // snapTick row is at output index = snapTick - 1 = 416.
    // At snap tick (t=0.5 through the smoothstep window centred on snapTick):
    //   winStart = snapTick - halfWindow = 1, winEnd = snapTick + halfWindow = 833
    //   t = (snapTick - winStart) / (2*halfWindow) = halfWindow / (2*halfWindow) = 0.5
    //   s = 3*(0.5)^2 - 2*(0.5)^3 = 0.5, lerp(1000,3000,0.5) = 2000
    const int snapEmitIdx = snapTick - 1;  // = 416
    TEST_ASSERT_TRUE_MESSAGE(snapEmitIdx >= 0 && snapEmitIdx < (int)results.size(),
        "snap emit index out of range");
    TEST_ASSERT_UINT16_WITHIN_MESSAGE(2, 2000, results[snapEmitIdx].flapsRawAdc,
        "at 208 Hz, midpoint (t=0.5) should lerp to ~2000");

    // Verify a point at 75% of the window (3/4 through the smoothstep):
    //   winStart = 1, winEnd = 833 → span = 832
    //   emitTick at 75%: 1 + 0.75 * 832 = 625  → output index 624
    // t = 0.75 → s = 3*(0.75)^2 - 2*(0.75)^3 = 1.6875 - 0.84375 = 0.84375
    // lerp(1000, 3000, 0.84375) = 1000 + 0.84375*2000 = 2687.5 → rounds to 2688
    const int threeQuarterEmitTick = 1 + (3 * (2 * halfWindow)) / 4;  // = 625
    const int threeQuarterIdx = threeQuarterEmitTick - 1;              // = 624
    if (threeQuarterIdx >= 0 && threeQuarterIdx < (int)results.size()) {
        TEST_ASSERT_UINT16_WITHIN_MESSAGE(2, 2688, results[threeQuarterIdx].flapsRawAdc,
            "at 208 Hz, 75% point (t=0.75) should lerp to ~2688");
    }

    // Final row (emitTick=832) is well past the transition window
    // (winEnd=833) so it should be at steady-state flaps=30, pot=3000.
    TEST_ASSERT_EQUAL_UINT16(3000, results.back().flapsRawAdc);
}

// ============================================================================
// Regression-insurance tests (PR #491 round-4 bulldog review)
// ============================================================================

// Build a minimal OnSpeedConfig with three flap detents (0°, 16°, 33°).
// Used by the kMaxTransitions overflow test to generate 9+ rapid transitions.
namespace {
OnSpeedConfig makeThreeFlapConfig()
{
    OnSpeedConfig cfg;
    cfg.aFlaps.clear();

    auto makeFlap = [](int deg, int pot) {
        OnSpeedConfig::SuFlaps f;
        f.iDegrees        = deg;
        f.iPotPosition    = pot;
        f.fLDMAXAOA       = 3.0f;
        f.fONSPEEDFASTAOA = 6.5f;
        f.fONSPEEDSLOWAOA = 9.5f;
        f.fSTALLWARNAOA   = 12.5f;
        f.fAlpha0         = -3.7f;
        f.fAlphaStall     = 14.0f;
        return f;
    };

    cfg.aFlaps.push_back(makeFlap(0,  1000));   // detent A: pot=1000
    cfg.aFlaps.push_back(makeFlap(16, 2000));   // detent B: pot=2000
    cfg.aFlaps.push_back(makeFlap(33, 3000));   // detent C: pot=3000

    cfg.iAoaSmoothing = 10;
    cfg.iLogRate = 50;
    return cfg;
}
}  // namespace

// test_kmaxtransitions_overflow_evicts_oldest
//
// Exercises the transitions_[8] eviction path (LogReplayEngine.cpp lines ~333-345).
// kMaxTransitions=8: when a 9th rapid transition arrives while the table is full,
// the oldest entry (index 0) is evicted by a shift-left before the new one is
// recorded. No existing test reaches this code path.
//
// Why the previous test was coverage theater:
//   All 9 transitions were crammed into ticks 2-10. Every transition window ends
//   at snapTick+100 ≤ 110. EmitOldest_() prunes expired transitions before each
//   emit, so by emitTick 111 the table is empty and ComputeSynthAdc_ returns
//   steadyPot regardless of whether eviction was correct. Any shift-loop
//   corruption was invisible.
//
// Fixed approach — assert numTransitions_ count after overflow:
//
//   Feeding 9 rapid transitions with kMaxTransitions=8 must trigger eviction
//   on the 9th. After eviction: shift left, decrement, then add new entry →
//   numTransitions_ stays at 8. A no-op sabotage (shift loop removed) leaves
//   numTransitions_ at 9 (and writes one entry out of bounds).
//
//   numTransitionsForTest() exposes numTransitions_ so the count can be pinned.
//   This catches the "eviction is a no-op" sabotage that the bulldog confirmed
//   passes the original test.
//
// Pattern:
//   Row 1  : flaps=A → sets lastFlapPosDeg_, no transition
//   Row 2  : flaps=B → transition 1  (numTransitions=1)
//   Row 3  : flaps=A → transition 2  (numTransitions=2)
//   ...
//   Row 9  : flaps=B → transition 8  (numTransitions=8 == kMaxTransitions)
//   Row 10 : flaps=A → eviction fires: shift left, decrement, add transition 9
//                       → numTransitions stays 8.
//
// After the count pin, the existing steady-state tail check is retained as a
// sanity assertion that the eviction path leaves ComputeSynthAdc_ functional.
void test_kmaxtransitions_overflow_evicts_oldest(void)
{
    OnSpeedConfig cfg = makeThreeFlapConfig();
    // Detent A=0°/pot=1000, B=16°/pot=2000, C=33°/pot=3000.
    // kMaxTransitions=8; synthHalfWindowTicks_=100 at 50 Hz.

    LogReplayEngine eng(cfg, 50, /*flapsRawAdcAvailable=*/false);

    // Phase 1: 10 rows that generate 9 rapid transitions (ticks 1-10).
    // Pattern: A, B, A, B, A, B, A, B, A, B → 9 A↔B transitions.
    // All within the lag period (buffer not yet full), so no output yet.
    for (int i = 0; i < 10; i++) {
        int flapDeg = ((i % 2) == 0) ? 0 : 16;  // A=0, B=16, A, B, ...
        auto opt = eng.step(makeRow(0.5f, 0.1f, flapDeg));
        TEST_ASSERT_FALSE_MESSAGE(opt.has_value(),
            "still in lag: first 10 rows should not produce output");
    }

    // After 9 transitions with kMaxTransitions=8, the eviction path fires on
    // the 9th transition. numTransitions_ must still be 8 — not 9.
    // Sabotage: commenting out the shift loop + --numTransitions_ leaves the
    // count at 9 (and writes one slot out of bounds). This assertion catches it.
    //
    // 8 == kMaxTransitions (private static constexpr in LogReplayEngine).
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, eng.numTransitionsForTest(),
        "after 9th transition with kMaxTransitions=8, eviction must fire: "
        "numTransitions_ must remain 8, not 9");

    // Phase 2: feed kSynthHalfWindow50 - 10 more rows at steady-state flaps=A=0°.
    // This fills the buffer up to the full lag point without emitting yet.
    for (int i = 0; i < kSynthHalfWindow50 - 10; i++) {
        auto opt = eng.step(makeRow(0.5f, 0.1f, 0));
        (void)opt;  // still filling buffer
    }

    // Phase 3: feed kSynthHalfWindow50 + 50 more steady-state rows at flaps=A.
    // These rows are well past all transition windows (last transition was at
    // tick 10; all transition windows end at tick 10+100=110 at the latest).
    // After emitTick 110, all transitions are pruned and ComputeSynthAdc_
    // returns steadyPot = pot A = 1000.
    std::vector<ReplayStepResult> results;
    for (int i = 0; i < kSynthHalfWindow50 + 50; i++) {
        auto opt = eng.step(makeRow(0.5f, 0.1f, 0));
        if (opt.has_value())
            results.push_back(opt.value());
    }
    auto tail = eng.flush();
    results.insert(results.end(), tail.begin(), tail.end());

    // All results must be present (synth path always sets flapsRawAdcPresent).
    for (size_t i = 0; i < results.size(); i++) {
        TEST_ASSERT_TRUE_MESSAGE(results[i].flapsRawAdcPresent,
            "synth path must set flapsRawAdcPresent on every emitted row");
    }

    // Sanity: the final 50 emitted rows are well past all transition windows.
    // At steady-state flaps=A=0°, synth ADC must be pot A = 1000.
    const int checkFromIdx = (int)results.size() - 50;
    TEST_ASSERT_TRUE_MESSAGE(checkFromIdx >= 0,
        "not enough output rows to check steady-state tail");
    for (int i = checkFromIdx; i < (int)results.size(); i++) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(1000, results[i].flapsRawAdc,
            "steady-state rows (flaps=A, past all transition windows) "
            "must emit pot A = 1000");
    }
}

// test_flush_idempotent_and_pre_step
//
// Pins two flush() contracts:
//   (a) flush() before any step() on a fresh synth-path engine returns empty.
//   (b) flush() called twice — second call returns empty (drain is idempotent).
//
// Both are one-liners in the implementation but are not exercised by any
// existing test. A future refactor that accidentally leaves state in the
// buffer after flush() would break (b).
void test_flush_idempotent_and_pre_step(void)
{
    OnSpeedConfig cfg = makeTwoFlapConfig();

    // (a) flush() before any step() returns empty.
    {
        LogReplayEngine eng(cfg, 50, /*flapsRawAdcAvailable=*/false);
        auto pre_step_flush = eng.flush();
        TEST_ASSERT_EQUAL_size_t(0, pre_step_flush.size());
    }

    // (b) flush() twice: second call returns empty.
    {
        LogReplayEngine eng(cfg, 50, false);
        LogRow row = makeRow(0.5f, 0.1f, 0);

        // Feed 200 rows to fill the buffer and generate step()-emitted results.
        for (int i = 0; i < 200; i++) {
            row.timeStampMs = static_cast<uint32_t>(i * 20);
            (void)eng.step(row);
        }

        // First flush drains exactly synthHalfWindowTicks_ tail rows.
        // At 50 Hz: kSynthHalfWindowSec (2 s) × 50 = 100 ticks.
        // After 200 step()s the buffer is full and has emitted 100 rows
        // via step(); the remaining 100 buffered rows are drained by flush().
        auto first_flush = eng.flush();
        TEST_ASSERT_EQUAL_size_t(
            static_cast<size_t>(kSynthHalfWindow50), first_flush.size());

        // Second flush: buffer is empty; must return zero rows.
        auto second_flush = eng.flush();
        TEST_ASSERT_EQUAL_size_t(0, second_flush.size());
    }
}

// ============================================================================
// main
// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();

    // --- Characterization tests (fast path, ADC present) ---
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

    // --- Streaming synth tests (new, PR 3) ---
    RUN_TEST(test_streaming_lag_contract);
    RUN_TEST(test_streaming_flush_emits_tail);
    RUN_TEST(test_streaming_total_output_equals_input);
    RUN_TEST(test_streaming_bounded_memory);
    RUN_TEST(test_synth_steady_state_no_transition);
    RUN_TEST(test_synth_single_transition);
    RUN_TEST(test_streaming_vs_batch_equivalence_50hz);
    RUN_TEST(test_streaming_vs_batch_equivalence_208hz);
    RUN_TEST(test_flush_fast_path_empty);
    RUN_TEST(test_reset_clears_synth_buffer);

    // --- 208 Hz rate-aware tests ---
    RUN_TEST(test_synth_208hz_half_window_ticks);
    RUN_TEST(test_synth_208hz_midpoint_value);

    // --- Regression-insurance tests (PR #491 bulldog review) ---
    RUN_TEST(test_kmaxtransitions_overflow_evicts_oldest);
    RUN_TEST(test_flush_idempotent_and_pre_step);

    return UNITY_END();
}
