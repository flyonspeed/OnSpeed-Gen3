// test_aoa_calculator.cpp - Unit tests for CalcAOA and AOACalculator

#include <unity.h>
#include <aoa/AOACalculator.h>
#include <cmath>

using namespace onspeed;

void setUp(void) {}
void tearDown(void) {}

// Helper: Linear curve where AOA = coeffP * scale + offset
static SuCalibrationCurve makeLinearCurve(float scale, float offset)
{
    return {{0.0f, 0.0f, scale, offset}, 1};
}

// ============================================================================
// CalcAOA
// ============================================================================

void test_CalcAOA_basic()
{
    // AOA = 10 * coeffP + 5
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    // Pfwd=100, P45=50 => coeffP=0.5 => AOA=10
    AOAResult result = CalcAOA(100.0f, 50.0f, curve);

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, result.coeffP);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, result.aoa);
}

void test_CalcAOA_zero_pfwd_invalid()
{
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    AOAResult result = CalcAOA(0.0f, 50.0f, curve);

    TEST_ASSERT_FALSE(result.valid);
}

void test_CalcAOA_negative_p45_valid()
{
    // Negative P45 happens in some flight conditions
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    AOAResult result = CalcAOA(100.0f, -50.0f, curve);

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, result.coeffP);
}

void test_CalcAOA_not_clamped()
{
    // Raw output is NOT clamped - clamping is caller's job
    SuCalibrationCurve curve = makeLinearCurve(100.0f, 0.0f);

    AOAResult result = CalcAOA(100.0f, 100.0f, curve);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, result.aoa);  // > AOA_MAX_VALUE
}

// ============================================================================
// AOACalculator (stateful, with smoothing)
// ============================================================================

void test_AOACalculator_no_smoothing()
{
    AOACalculator calc(0);  // No smoothing
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 5.0f);

    // Pfwd=100, P45=50 => coeffP=0.5 => AOA=10
    AOACalculatorResult r1 = calc.calculate(100.0f, 50.0f, curve);
    TEST_ASSERT_TRUE(r1.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, r1.aoa);

    // Immediate change with no smoothing
    AOACalculatorResult r2 = calc.calculate(100.0f, 0.0f, curve);  // coeffP=0 => AOA=5
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, r2.aoa);
}

void test_AOACalculator_with_smoothing()
{
    AOACalculator calc(2);  // alpha = 0.5
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 0.0f);

    // First value seeds: AOA = 10
    calc.calculate(100.0f, 100.0f, curve);

    // Second value: raw AOA = 0, smoothed = 0.5*0 + 0.5*10 = 5
    AOACalculatorResult r = calc.calculate(100.0f, 0.0f, curve);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, r.aoa);
}

void test_AOACalculator_clamps_output()
{
    AOACalculator calc(0);
    SuCalibrationCurve curve = makeLinearCurve(100.0f, 0.0f);

    // Raw AOA would be 100, but should be clamped
    AOACalculatorResult r = calc.calculate(100.0f, 100.0f, curve);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, AOA_MAX_VALUE, r.aoa);
}

void test_AOACalculator_reset()
{
    AOACalculator calc(10);  // Heavy smoothing
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 0.0f);

    // Build up state
    for (int i = 0; i < 20; i++) {
        calc.calculate(100.0f, 100.0f, curve);  // AOA = 10
    }

    calc.reset();

    // After reset, next value should seed fresh
    AOACalculatorResult r = calc.calculate(100.0f, 0.0f, curve);  // AOA = 0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.aoa);
}

// ============================================================================
// EMA poisoning regression tests
// ============================================================================

void test_AOACalculator_invalid_samples_preserve_ema_state()
{
    AOACalculator calc(4);  // alpha = 0.25 — moderate smoothing
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 0.0f);

    // Establish steady-state around 5° (coeffP=0.5 => AOA=5)
    for (int i = 0; i < 20; i++) {
        calc.calculate(100.0f, 50.0f, curve);
    }

    // Confirm we're at ~5°
    AOACalculatorResult baseline = calc.calculate(100.0f, 50.0f, curve);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, baseline.aoa);

    // Feed 10 invalid samples (negative pfwd => invalid)
    for (int i = 0; i < 10; i++) {
        AOACalculatorResult inv = calc.calculate(-1.0f, 50.0f, curve);
        TEST_ASSERT_FALSE(inv.valid);
        // Smoothed AOA should hold near 5°, NOT drag toward -20°
        TEST_ASSERT_FLOAT_WITHIN(1.0f, 5.0f, inv.aoa);
    }

    // Resume with valid samples — should still be near 5°
    AOACalculatorResult resumed = calc.calculate(100.0f, 50.0f, curve);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 5.0f, resumed.aoa);
}

void test_AOACalculator_first_sample_invalid_returns_floor()
{
    // If the very first sample is invalid (before EMA is seeded),
    // return AOA_MIN_VALUE (safe floor), not 0.0 degrees.
    AOACalculator calc(4);
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 0.0f);

    AOACalculatorResult r = calc.calculate(-100.0f, 500.0f, curve);  // invalid pfwd
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, AOA_MIN_VALUE, r.aoa);
}

void test_AOACalculator_zero_pfwd_does_not_drag_ema()
{
    AOACalculator calc(2);  // alpha = 0.5
    SuCalibrationCurve curve = makeLinearCurve(10.0f, 0.0f);

    // Seed with AOA = 10°
    calc.calculate(100.0f, 100.0f, curve);

    // Zero pfwd is invalid
    AOACalculatorResult inv = calc.calculate(0.0f, 100.0f, curve);
    TEST_ASSERT_FALSE(inv.valid);

    // With EMA poisoning fixed, smoothed value should still be 10°
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, inv.aoa);
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    UNITY_BEGIN();

    // CalcAOA
    RUN_TEST(test_CalcAOA_basic);
    RUN_TEST(test_CalcAOA_zero_pfwd_invalid);
    RUN_TEST(test_CalcAOA_negative_p45_valid);
    RUN_TEST(test_CalcAOA_not_clamped);

    // AOACalculator
    RUN_TEST(test_AOACalculator_no_smoothing);
    RUN_TEST(test_AOACalculator_with_smoothing);
    RUN_TEST(test_AOACalculator_clamps_output);
    RUN_TEST(test_AOACalculator_reset);

    // EMA poisoning regression
    RUN_TEST(test_AOACalculator_first_sample_invalid_returns_floor);
    RUN_TEST(test_AOACalculator_invalid_samples_preserve_ema_state);
    RUN_TEST(test_AOACalculator_zero_pfwd_does_not_drag_ema);

    return UNITY_END();
}
