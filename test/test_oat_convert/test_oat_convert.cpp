// test_oat_convert.cpp — characterization tests for OatConvert.
//
// These tests enforce the sentinel filtering that prevents the DS18B20
// disconnect value (-127°C) from corrupting AHRS TAS computation (audit #006).

#include <unity.h>
#include <sensors/OatConvert.h>

using namespace onspeed::sensors;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Disconnect sentinel
// ============================================================================

void test_disconnect_sentinel_returns_nullopt(void)
{
    // -127.0 is the DS18B20 error code for "no sensor present".
    TEST_ASSERT_FALSE(FilterOat(-127.0f).has_value());
}

void test_below_disconnect_sentinel_returns_nullopt(void)
{
    // Anything <= sentinel is treated as disconnected.
    TEST_ASSERT_FALSE(FilterOat(-128.0f).has_value());
    TEST_ASSERT_FALSE(FilterOat(-200.0f).has_value());
}

// ============================================================================
// Power-on-reset sentinel
// ============================================================================

void test_por_sentinel_returns_nullopt(void)
{
    // 85.0°C is the DS18B20 power-on-reset value.
    TEST_ASSERT_FALSE(FilterOat(85.0f).has_value());
}

void test_above_por_sentinel_returns_nullopt(void)
{
    // Values >= 85°C are above the valid range and the POR sentinel.
    TEST_ASSERT_FALSE(FilterOat(86.0f).has_value());
    TEST_ASSERT_FALSE(FilterOat(100.0f).has_value());
}

// ============================================================================
// Range limits
// ============================================================================

void test_at_min_valid_temp_returns_value(void)
{
    // Exactly at the lower edge of the valid range.
    auto v = FilterOat(-80.0f);
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_EQUAL_FLOAT(-80.0f, *v);
}

void test_at_max_valid_temp_returns_value(void)
{
    // Exactly at the upper edge of the valid range (just below POR).
    auto v = FilterOat(80.0f);
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_EQUAL_FLOAT(80.0f, *v);
}

void test_below_min_range_returns_nullopt(void)
{
    // One degree below the minimum — implausibly cold.
    TEST_ASSERT_FALSE(FilterOat(-81.0f).has_value());
}

void test_above_max_range_returns_nullopt(void)
{
    // 81°C is above 80°C max but below POR — still rejected.
    TEST_ASSERT_FALSE(FilterOat(81.0f).has_value());
}

// ============================================================================
// Valid readings pass through unchanged
// ============================================================================

void test_standard_day_temp_passes_through(void)
{
    auto v = FilterOat(15.0f);
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_EQUAL_FLOAT(15.0f, *v);
}

void test_zero_celsius_passes_through(void)
{
    auto v = FilterOat(0.0f);
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, *v);
}

void test_negative_temp_in_range_passes_through(void)
{
    auto v = FilterOat(-40.0f);
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, *v);
}

void test_hot_desert_temp_passes_through(void)
{
    // 50°C: plausible at desert elevations.
    auto v = FilterOat(50.0f);
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_EQUAL_FLOAT(50.0f, *v);
}

int main(int, char**)
{
    UNITY_BEGIN();

    // Disconnect sentinel
    RUN_TEST(test_disconnect_sentinel_returns_nullopt);
    RUN_TEST(test_below_disconnect_sentinel_returns_nullopt);

    // Power-on-reset sentinel
    RUN_TEST(test_por_sentinel_returns_nullopt);
    RUN_TEST(test_above_por_sentinel_returns_nullopt);

    // Range limits
    RUN_TEST(test_at_min_valid_temp_returns_value);
    RUN_TEST(test_at_max_valid_temp_returns_value);
    RUN_TEST(test_below_min_range_returns_nullopt);
    RUN_TEST(test_above_max_range_returns_nullopt);

    // Valid pass-throughs
    RUN_TEST(test_standard_day_temp_passes_through);
    RUN_TEST(test_zero_celsius_passes_through);
    RUN_TEST(test_negative_temp_in_range_passes_through);
    RUN_TEST(test_hot_desert_temp_passes_through);

    return UNITY_END();
}
