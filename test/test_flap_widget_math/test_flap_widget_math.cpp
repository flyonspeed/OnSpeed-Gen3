// Unit tests for onspeed::gauges::FlapWidgetFrac.
//
// The function returns the fraction of configured flap travel a given
// FlapPos sits at, in [0, 1].  These tests pin the contract: endpoint
// values, in-band linearity, single-position fallback, misconfigured
// fallback, reflex-flap support, and out-of-range clamping.

#include <unity.h>

#include <gauges/FlapWidgetMath.h>

using onspeed::gauges::FlapWidgetFrac;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Endpoints
// ============================================================================

void test_at_min_returns_zero(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, FlapWidgetFrac(0, 0, 33));
}

void test_at_max_returns_one(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0f, FlapWidgetFrac(33, 0, 33));
}

void test_midpoint_returns_half(void)
{
    // RV-class half-flap: 16 of 33 deg.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.0f / 33.0f, FlapWidgetFrac(16, 0, 33));
}

void test_linearity(void)
{
    // Sample evenly through the range; verify monotone linear progression.
    for (int i = 0; i <= 10; ++i) {
        const int   pos      = i * 4;          // 0, 4, 8, ..., 40
        const float expected = static_cast<float>(pos) / 40.0f;
        TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, FlapWidgetFrac(pos, 0, 40));
    }
}

// ============================================================================
// Out-of-range clamping
// ============================================================================

void test_below_min_clamps_to_zero(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, FlapWidgetFrac(-5, 0, 33));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, FlapWidgetFrac(-99, 0, 33));
}

void test_above_max_clamps_to_one(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0f, FlapWidgetFrac(40, 0, 33));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, FlapWidgetFrac(99, 0, 33));
}

// ============================================================================
// Reflex flaps (FlapsMinDeg < 0)
// ============================================================================

void test_reflex_flaps_at_min_returns_zero(void)
{
    // Glider with reflex: travel from -5 (reflex) to +30 (full extend).
    TEST_ASSERT_EQUAL_FLOAT(0.0f, FlapWidgetFrac(-5, -5, 30));
}

void test_reflex_flaps_at_max_returns_one(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0f, FlapWidgetFrac(30, -5, 30));
}

void test_reflex_flaps_at_neutral_returns_known_fraction(void)
{
    // FlapPos = 0, range -5..30, span = 35, distance from min = 5.
    // Expected fraction = 5/35 = 0.1428...
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f / 35.0f,
                             FlapWidgetFrac(0, -5, 30));
}

void test_reflex_flaps_below_min_clamps(void)
{
    // Lever pushed below the configured reflex minimum.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, FlapWidgetFrac(-10, -5, 30));
}

// ============================================================================
// Degenerate configurations — must not divide by zero
// ============================================================================

void test_single_position_aircraft_parks_mid_arc(void)
{
    // Fixed-flap trainer: min == max.  Caller's flapPos is irrelevant;
    // there's no travel range to map onto.  Fallback to 0.5 so the
    // widget renders mid-arc rather than NaN-ing.
    TEST_ASSERT_EQUAL_FLOAT(0.5f, FlapWidgetFrac(0, 0, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, FlapWidgetFrac(15, 15, 15));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, FlapWidgetFrac(99, 15, 15));
}

void test_misconfigured_max_below_min_falls_back(void)
{
    // Misconfigured XML: max less than min.  Same fallback as the
    // single-position case (span <= 0 path).
    TEST_ASSERT_EQUAL_FLOAT(0.5f, FlapWidgetFrac(15, 30, 5));
}

// ============================================================================

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_at_min_returns_zero);
    RUN_TEST(test_at_max_returns_one);
    RUN_TEST(test_midpoint_returns_half);
    RUN_TEST(test_linearity);

    RUN_TEST(test_below_min_clamps_to_zero);
    RUN_TEST(test_above_max_clamps_to_one);

    RUN_TEST(test_reflex_flaps_at_min_returns_zero);
    RUN_TEST(test_reflex_flaps_at_max_returns_one);
    RUN_TEST(test_reflex_flaps_at_neutral_returns_known_fraction);
    RUN_TEST(test_reflex_flaps_below_min_clamps);

    RUN_TEST(test_single_position_aircraft_parks_mid_arc);
    RUN_TEST(test_misconfigured_max_below_min_falls_back);

    return UNITY_END();
}
