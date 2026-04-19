// test_flaps_detector.cpp — characterization tests for FlapsDetector.
//
// Covers: two-position (clean/full), three-position, ascending and
// descending pot wiring, readings near midpoints, and edge cases
// (zero positions, single position, equidistant reading).

#include <unity.h>
#include <sensors/FlapsDetector.h>

using onspeed::FlapState;
using onspeed::sensors::DetectFlaps;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Zero and single-position edge cases
// ============================================================================

void test_zero_positions_returns_invalid(void)
{
    FlapState s = DetectFlaps(1000u, nullptr, 0u);
    TEST_ASSERT_FALSE(s.valid);
}

void test_single_position_returns_index_zero(void)
{
    const uint16_t pos[] = { 2048u };
    FlapState s = DetectFlaps(1000u, pos, 1u);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0, s.detectedIndex);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.normalized);
}

// ============================================================================
// Two-position ascending: clean (low ADC) and full (high ADC)
// ============================================================================

// positions: clean=1000, full=3000 (ascending)

void test_two_pos_ascending_below_midpoint_detects_clean(void)
{
    // midpoint = (1000 + 3000) / 2 = 2000
    // reading 1500 < 2000 → index 0 (clean)
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(1500u, pos, 2u);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0, s.detectedIndex);
}

void test_two_pos_ascending_above_midpoint_detects_full(void)
{
    // reading 2500 > 2000 → index 1 (full flaps)
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(2500u, pos, 2u);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(1, s.detectedIndex);
}

void test_two_pos_ascending_at_midpoint_detects_clean(void)
{
    // At exactly 2000 the condition is rawAdc > midpoint (strict), so
    // it does NOT advance — stays at clean (index 0).
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(2000u, pos, 2u);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0, s.detectedIndex);
}

void test_two_pos_ascending_normalized_at_lower_endpoint(void)
{
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(1000u, pos, 2u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.normalized);
}

void test_two_pos_ascending_normalized_at_upper_endpoint(void)
{
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(3000u, pos, 2u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, s.normalized);
}

void test_two_pos_ascending_normalized_midpoint(void)
{
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(2000u, pos, 2u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, s.normalized);
}

// ============================================================================
// Two-position descending: some pots are wired "backwards"
// ============================================================================

// positions: clean=3000, full=1000 (descending)

void test_two_pos_descending_above_midpoint_detects_clean(void)
{
    // midpoint = (3000 + 1000) / 2 = 2000
    // descending: rawAdc < midpoint → advance. 2500 is NOT < 2000 → index 0.
    const uint16_t pos[] = { 3000u, 1000u };
    FlapState s = DetectFlaps(2500u, pos, 2u);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0, s.detectedIndex);
}

void test_two_pos_descending_below_midpoint_detects_full(void)
{
    // 1500 < 2000 → index 1 (full flaps)
    const uint16_t pos[] = { 3000u, 1000u };
    FlapState s = DetectFlaps(1500u, pos, 2u);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(1, s.detectedIndex);
}

void test_two_pos_descending_normalized_at_lower_endpoint(void)
{
    // positions[0] = 3000 (flaps up), so rawAdc = 3000 → normalized = 0
    const uint16_t pos[] = { 3000u, 1000u };
    FlapState s = DetectFlaps(3000u, pos, 2u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.normalized);
}

void test_two_pos_descending_normalized_at_upper_endpoint(void)
{
    // rawAdc = 1000 (full flaps) → normalized = 1
    const uint16_t pos[] = { 3000u, 1000u };
    FlapState s = DetectFlaps(1000u, pos, 2u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, s.normalized);
}

// ============================================================================
// Three-position ascending
// ============================================================================

// positions: clean=500, partial=2000, full=3500

void test_three_pos_ascending_first_zone(void)
{
    // midpoint(0,1) = (500+2000)/2 = 1250
    // midpoint(1,2) = (2000+3500)/2 = 2750
    // reading 800 < 1250 → index 0
    const uint16_t pos[] = { 500u, 2000u, 3500u };
    FlapState s = DetectFlaps(800u, pos, 3u);
    TEST_ASSERT_EQUAL_INT(0, s.detectedIndex);
}

void test_three_pos_ascending_middle_zone(void)
{
    // reading 1800: 1800 > 1250 → advance to 1; 1800 < 2750 → stay at 1
    const uint16_t pos[] = { 500u, 2000u, 3500u };
    FlapState s = DetectFlaps(1800u, pos, 3u);
    TEST_ASSERT_EQUAL_INT(1, s.detectedIndex);
}

void test_three_pos_ascending_last_zone(void)
{
    // reading 3000 > 1250 → advance to 1; 3000 > 2750 → advance to 2
    const uint16_t pos[] = { 500u, 2000u, 3500u };
    FlapState s = DetectFlaps(3000u, pos, 3u);
    TEST_ASSERT_EQUAL_INT(2, s.detectedIndex);
}

// ============================================================================
// Clamping: readings beyond the configured range
// ============================================================================

void test_normalized_clamped_below_zero(void)
{
    // ADC well below positions[0] — normalized must not go negative.
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(0u, pos, 2u);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.normalized);
}

void test_normalized_clamped_above_one(void)
{
    // ADC well above positions[1] — normalized must not exceed 1.
    const uint16_t pos[] = { 1000u, 3000u };
    FlapState s = DetectFlaps(4095u, pos, 2u);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.normalized);
}

int main(int, char**)
{
    UNITY_BEGIN();

    // Edge cases
    RUN_TEST(test_zero_positions_returns_invalid);
    RUN_TEST(test_single_position_returns_index_zero);

    // Two-position ascending
    RUN_TEST(test_two_pos_ascending_below_midpoint_detects_clean);
    RUN_TEST(test_two_pos_ascending_above_midpoint_detects_full);
    RUN_TEST(test_two_pos_ascending_at_midpoint_detects_clean);
    RUN_TEST(test_two_pos_ascending_normalized_at_lower_endpoint);
    RUN_TEST(test_two_pos_ascending_normalized_at_upper_endpoint);
    RUN_TEST(test_two_pos_ascending_normalized_midpoint);

    // Two-position descending
    RUN_TEST(test_two_pos_descending_above_midpoint_detects_clean);
    RUN_TEST(test_two_pos_descending_below_midpoint_detects_full);
    RUN_TEST(test_two_pos_descending_normalized_at_lower_endpoint);
    RUN_TEST(test_two_pos_descending_normalized_at_upper_endpoint);

    // Three-position ascending
    RUN_TEST(test_three_pos_ascending_first_zone);
    RUN_TEST(test_three_pos_ascending_middle_zone);
    RUN_TEST(test_three_pos_ascending_last_zone);

    // Clamping
    RUN_TEST(test_normalized_clamped_below_zero);
    RUN_TEST(test_normalized_clamped_above_one);

    return UNITY_END();
}
