// test_oat_select.cpp — unit tests for onspeed::efis::SelectDisplayOatC.
//
// Pins the OAT-source decision rule used by both the WebSocket JSON
// broadcast and the M5 display serial frame.  Both surfaces must
// agree on which source they're showing, and on the gates that lock
// the EFIS branch out when the EFIS feed is disabled or stale.

#include <unity.h>

#include <efis/OatSelect.h>

#include <cmath>
#include <limits>

using onspeed::efis::SelectDisplayOatC;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Happy path: EFIS branch
// ---------------------------------------------------------------------------

void test_efis_when_cal_source_efis_read_on_fresh_sensor_off(void)
{
    const float r = SelectDisplayOatC(
        /*calSourceEfis*/        true,
        /*readEfisDataEnabled*/  true,
        /*efisIsFresh*/          true,
        /*oatSensorEnabled*/     false,
        /*efisOatC*/             12.0f,
        /*internalOatC*/         -40.0f);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, r);
}

void test_efis_wins_when_both_efis_and_internal_available(void)
{
    // bCalSourceEfis overrides bOatSensor when the EFIS feed is healthy.
    const float r = SelectDisplayOatC(true, true, true, true, 15.0f, 99.0f);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, r);
}

// ---------------------------------------------------------------------------
// EFIS-branch lockouts → fall through
// ---------------------------------------------------------------------------

void test_efis_read_disabled_falls_through_to_internal(void)
{
    // Master EFIS read switch off.  Even with cal-source=EFIS and a
    // fresh frame in suEfis (stale data only, but the gate is the
    // config bit), the helper must not use the EFIS value.
    const float r = SelectDisplayOatC(true, false, true, true, 15.0f, -5.0f);
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, r);
}

void test_efis_stale_falls_through_to_internal(void)
{
    // No EFIS frame decoded in the last 2 s.
    const float r = SelectDisplayOatC(true, true, false, true, 15.0f, -5.0f);
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, r);
}

void test_efis_stale_falls_through_to_zero_when_internal_off(void)
{
    const float r = SelectDisplayOatC(true, true, false, false, 15.0f, -5.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r);
}

void test_efis_nan_falls_through_to_internal(void)
{
    const float r = SelectDisplayOatC(true, true, true, true,
                                      std::nanf(""), -5.0f);
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, r);
}

void test_efis_inf_falls_through_to_internal(void)
{
    const float r = SelectDisplayOatC(true, true, true, true,
                                      std::numeric_limits<float>::infinity(),
                                      -5.0f);
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, r);
}

// ---------------------------------------------------------------------------
// Internal-cal-source branch
// ---------------------------------------------------------------------------

void test_internal_source_returns_sensor_value(void)
{
    const float r = SelectDisplayOatC(false, true, true, true, 99.0f, 21.0f);
    TEST_ASSERT_EQUAL_FLOAT(21.0f, r);
}

void test_internal_source_no_sensor_returns_zero(void)
{
    const float r = SelectDisplayOatC(false, false, false, false, 99.0f, 21.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r);
}

void test_internal_sensor_nan_returns_zero(void)
{
    // Defensive: a NaN from the internal sensor reaches the M5 wire as
    // int(NaN), which is undefined behaviour.  The helper substitutes
    // 0.0f.  The JSON path already has SafeJsonFloat downstream; this
    // ensures both surfaces see the same value.
    const float r = SelectDisplayOatC(false, false, false, true,
                                      99.0f, std::nanf(""));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r);
}

// ---------------------------------------------------------------------------
// Realistic mixed-scenario regression cases for issue #361
// ---------------------------------------------------------------------------

void test_issue_361_efis_user_with_healthy_efis_sees_efis_oat(void)
{
    // Typical Dynon SkyView setup: cal-source=EFIS, EFIS feed enabled,
    // internal sensor not wired, EFIS streaming healthily.  Before the
    // fix the M5 wire reported 0; after, it reports the EFIS value.
    const float r = SelectDisplayOatC(true, true, true, false, 17.5f, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(17.5f, r);
}

void test_efis_unplugged_falls_back_to_internal(void)
{
    // cal-source=EFIS but the EFIS is unplugged or stopped sending.
    // IsDataFresh() goes false after 2 s.  With the internal sensor
    // wired, the helper falls back to it instead of returning a stale
    // (or zero-init) suEfis.OAT.
    const float r = SelectDisplayOatC(true, true, false, true, 0.0f, 22.0f);
    TEST_ASSERT_EQUAL_FLOAT(22.0f, r);
}

// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_efis_when_cal_source_efis_read_on_fresh_sensor_off);
    RUN_TEST(test_efis_wins_when_both_efis_and_internal_available);

    RUN_TEST(test_efis_read_disabled_falls_through_to_internal);
    RUN_TEST(test_efis_stale_falls_through_to_internal);
    RUN_TEST(test_efis_stale_falls_through_to_zero_when_internal_off);
    RUN_TEST(test_efis_nan_falls_through_to_internal);
    RUN_TEST(test_efis_inf_falls_through_to_internal);

    RUN_TEST(test_internal_source_returns_sensor_value);
    RUN_TEST(test_internal_source_no_sensor_returns_zero);
    RUN_TEST(test_internal_sensor_nan_returns_zero);

    RUN_TEST(test_issue_361_efis_user_with_healthy_efis_sees_efis_oat);
    RUN_TEST(test_efis_unplugged_falls_back_to_internal);

    return UNITY_END();
}
