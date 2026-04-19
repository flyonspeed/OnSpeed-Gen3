// test_volume_curve.cpp — Unit tests for VolumeCurve::MapPotToGain
//
// Covers: min/max/midpoint mapping, out-of-range clamping, zero-range
// degenerate config, and custom endpoint configurations.

#include <unity.h>
#include <audio/VolumeCurve.h>

using namespace onspeed::audio;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Test 1: minimum ADC (at lowAnalog) → 0.0 gain
// ---------------------------------------------------------------------------

void test_min_adc_returns_zero_gain()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  =    0;
    cfg.highAnalog = 4095;

    float gain = MapPotToGain(0, cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, gain);
}

// ---------------------------------------------------------------------------
// Test 2: maximum ADC (at highAnalog) → 1.0 gain
// ---------------------------------------------------------------------------

void test_max_adc_returns_full_gain()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  =    0;
    cfg.highAnalog = 4095;

    float gain = MapPotToGain(4095, cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, gain);
}

// ---------------------------------------------------------------------------
// Test 3: midpoint ADC → 0.5 gain
// ---------------------------------------------------------------------------

void test_midpoint_adc_returns_half_gain()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  =    0;
    cfg.highAnalog = 4095;

    // midpoint is 2047 for int range [0, 4095]
    // exact midpoint of the range: (0 + 4095) / 2 = 2047.5 → use 2047 (int)
    // gain = (2047 - 0) / (4095 - 0) ≈ 0.4999
    float gain = MapPotToGain(2047, cfg);
    // Allow a little slack for the integer approximation of the midpoint
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, gain);
}

// ---------------------------------------------------------------------------
// Test 4: ADC below lowAnalog → clamps to 0.0
// ---------------------------------------------------------------------------

void test_below_low_analog_clamps_to_zero()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  =  100;
    cfg.highAnalog = 4000;

    float gain = MapPotToGain(50, cfg);   // below lowAnalog
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, gain);
}

// ---------------------------------------------------------------------------
// Test 5: ADC above highAnalog → clamps to 1.0
// ---------------------------------------------------------------------------

void test_above_high_analog_clamps_to_one()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  =  100;
    cfg.highAnalog = 4000;

    float gain = MapPotToGain(4095, cfg);   // above highAnalog
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, gain);
}

// ---------------------------------------------------------------------------
// Test 6: zero-range config (degenerate) → returns 0.0, no divide-by-zero
// ---------------------------------------------------------------------------

void test_zero_range_config_returns_zero()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  = 2000;
    cfg.highAnalog = 2000;   // same value → zero range

    float gain = MapPotToGain(2000, cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, gain);
}

// ---------------------------------------------------------------------------
// Test 7: custom non-zero offset (lowAnalog > 0)
// ---------------------------------------------------------------------------

void test_custom_offset_mapping()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  = 500;
    cfg.highAnalog = 3500;

    // At lowAnalog: gain should be 0
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, MapPotToGain(500, cfg));

    // At highAnalog: gain should be 1
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, MapPotToGain(3500, cfg));

    // At midpoint (2000): gain should be 0.5
    // (2000 - 500) / (3500 - 500) = 1500/3000 = 0.5
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, MapPotToGain(2000, cfg));
}

// ---------------------------------------------------------------------------
// Test 8: gain is strictly between 0 and 1 for interior ADC values
// ---------------------------------------------------------------------------

void test_interior_adc_produces_fractional_gain()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  =    0;
    cfg.highAnalog = 4095;

    // 25% of range
    float gain25 = MapPotToGain(1023, cfg);
    TEST_ASSERT_TRUE(gain25 > 0.0f && gain25 < 1.0f);

    // 75% of range
    float gain75 = MapPotToGain(3071, cfg);
    TEST_ASSERT_TRUE(gain75 > 0.0f && gain75 < 1.0f);

    // 75% gain should be larger than 25% gain
    TEST_ASSERT_TRUE(gain75 > gain25);
}

// ---------------------------------------------------------------------------
// Test 9: negative ADC reading clamps to 0 (defensive — ADC shouldn't go negative)
// ---------------------------------------------------------------------------

void test_negative_adc_clamps_to_zero()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  =    0;
    cfg.highAnalog = 4095;

    float gain = MapPotToGain(-100, cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, gain);
}

// ---------------------------------------------------------------------------
// Test 10: inverted config (lowAnalog > highAnalog) → negative range
//          At the "low" end the gain goes to 1; at the "high" end it goes to 0.
//          Clamps protect against >1 and <0.
// ---------------------------------------------------------------------------

void test_inverted_config_clamps_correctly()
{
    VolumeCurveConfig cfg;
    cfg.lowAnalog  = 4095;   // inverted: low > high
    cfg.highAnalog =    0;

    // rawAdc at 4095 (lowAnalog): (4095-4095)/(0-4095) = 0/(−4095) = 0
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, MapPotToGain(4095, cfg));

    // rawAdc at 0 (highAnalog): (0-4095)/(0-4095) = (-4095)/(-4095) = 1.0
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, MapPotToGain(0, cfg));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int /*argc*/, char** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_min_adc_returns_zero_gain);
    RUN_TEST(test_max_adc_returns_full_gain);
    RUN_TEST(test_midpoint_adc_returns_half_gain);
    RUN_TEST(test_below_low_analog_clamps_to_zero);
    RUN_TEST(test_above_high_analog_clamps_to_one);
    RUN_TEST(test_zero_range_config_returns_zero);
    RUN_TEST(test_custom_offset_mapping);
    RUN_TEST(test_interior_adc_produces_fractional_gain);
    RUN_TEST(test_negative_adc_clamps_to_zero);
    RUN_TEST(test_inverted_config_clamps_correctly);

    return UNITY_END();
}
