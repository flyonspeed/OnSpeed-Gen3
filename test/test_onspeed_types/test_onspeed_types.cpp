// test_onspeed_types.cpp - Unit tests for OnSpeedTypes.h

#include <unity.h>
#include <OnSpeedTypes.h>
#include <cmath>

void setUp(void) {
}

void tearDown(void) {
}

// ============================================================================
// Unit Conversion Tests
// ============================================================================

void test_deg2rad() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, deg2rad(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5708f, deg2rad(90.0f));  // pi/2
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.1416f, deg2rad(180.0f)); // pi
}

void test_rad2deg() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, rad2deg(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, rad2deg(1.5708f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, rad2deg(3.1416f));
}

void test_ft2m_and_m2ft() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.3048f, ft2m(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 304.8f, ft2m(1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, m2ft(0.3048f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.0f, m2ft(304.8f));
}

void test_kts2mps_and_mps2kts() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.514444f, kts2mps(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 51.444f, kts2mps(100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, mps2kts(0.514444f));
}

void test_g2mps_and_mps2g() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.80665f, g2mps(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mps2g(9.80665f));
}

void test_psi2mb_and_mb2psi() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 68.948f, psi2mb(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1013.25f, psi2mb(14.696f));  // 1 atm
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, mb2psi(68.94757f));
}

// ============================================================================
// Attitude Functions Tests
// ============================================================================

void test_accelPitch_level() {
    // Level flight: only vertical acceleration (1g down)
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, accelPitch(0.0f, 0.0f, 1.0f));
}

void test_accelPitch_nose_up() {
    // Nose up 45 degrees: equal forward and vertical acceleration
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.0f, accelPitch(1.0f, 0.0f, 1.0f));
}

void test_accelRoll_level() {
    // Level flight
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, accelRoll(0.0f, 0.0f, 1.0f));
}

void test_accelRoll_banked() {
    // 45 degree bank: equal lateral and vertical acceleration
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.0f, accelRoll(0.0f, -1.0f, 1.0f));
}

// ============================================================================
// Pressure Coefficient Tests
// ============================================================================

void test_pressureCoeff_normal() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, pressureCoeff(100.0f, 50.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pressureCoeff(100.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, pressureCoeff(50.0f, 100.0f));
}

void test_pressureCoeff_zero_pfwd() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pressureCoeff(0.0f, 50.0f));
}

void test_pressureCoeff_negative_pfwd() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pressureCoeff(-10.0f, 50.0f));
}

// ============================================================================
// Constants Tests
// ============================================================================

void test_constants() {
    TEST_ASSERT_EQUAL_INT(5, MAX_AOA_CURVES);
    TEST_ASSERT_EQUAL_INT(4, MAX_CURVE_COEFF);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 40.0f, AOA_MAX_VALUE);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -20.0f, AOA_MIN_VALUE);
}

// ============================================================================
// clampAOA
// ============================================================================

void test_clampAOA_clamps_high() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, AOA_MAX_VALUE, clampAOA(100.0f));
}

void test_clampAOA_clamps_low() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, AOA_MIN_VALUE, clampAOA(-100.0f));
}

void test_clampAOA_handles_nan() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, AOA_MIN_VALUE, clampAOA(std::nanf("")));
}

// ============================================================================
// Legacy Macro Tests
// ============================================================================

void test_legacy_macros() {
    // Test that legacy macros still work
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5708f, DEG2RAD(90));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, RAD2DEG(1.5708));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, PCOEFF(100, 50));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Unit conversions
    RUN_TEST(test_deg2rad);
    RUN_TEST(test_rad2deg);
    RUN_TEST(test_ft2m_and_m2ft);
    RUN_TEST(test_kts2mps_and_mps2kts);
    RUN_TEST(test_g2mps_and_mps2g);
    RUN_TEST(test_psi2mb_and_mb2psi);

    // Attitude functions
    RUN_TEST(test_accelPitch_level);
    RUN_TEST(test_accelPitch_nose_up);
    RUN_TEST(test_accelRoll_level);
    RUN_TEST(test_accelRoll_banked);

    // Pressure coefficient
    RUN_TEST(test_pressureCoeff_normal);
    RUN_TEST(test_pressureCoeff_zero_pfwd);
    RUN_TEST(test_pressureCoeff_negative_pfwd);

    // Constants
    RUN_TEST(test_constants);

    // clampAOA
    RUN_TEST(test_clampAOA_clamps_high);
    RUN_TEST(test_clampAOA_clamps_low);
    RUN_TEST(test_clampAOA_handles_nan);

    // Legacy macros
    RUN_TEST(test_legacy_macros);

    return UNITY_END();
}
