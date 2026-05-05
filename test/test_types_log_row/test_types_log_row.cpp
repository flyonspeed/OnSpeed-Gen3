// test_types_log_row.cpp — structural tests for onspeed::LogRow

#include <unity.h>
#include <types/LogRow.h>

using onspeed::LogRow;

void setUp(void) {}
void tearDown(void) {}

void test_default_initializes_numeric_fields_to_zero(void)
{
    LogRow r;
    TEST_ASSERT_EQUAL_UINT32(0u,   r.timeStampMs);
    TEST_ASSERT_EQUAL_INT(0,       r.pfwdCounts);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.pfwdSmoothed);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.paltFt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.oatCelsius);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.pitchDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.rollDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.derivedAoaDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,  r.coeffP);
    TEST_ASSERT_FALSE(r.boomEnabled);
    TEST_ASSERT_FALSE(r.efisEnabled);
    TEST_ASSERT_FALSE(r.efisIsVn300);
    TEST_ASSERT_FALSE(r.flapsRawAdcPresent);
    TEST_ASSERT_EQUAL_UINT16(0u, r.flapsRawAdc);
}

void test_fields_are_writable(void)
{
    LogRow r;
    r.timeStampMs     = 5000u;
    r.iasKt           = 95.0f;
    r.paltFt          = 3500.0f;
    r.angleOfAttackDeg = 8.2f;
    r.pitchDeg        = 3.1f;
    r.derivedAoaDeg   = 7.9f;
    r.coeffP          = 0.3214f;
    r.boomEnabled     = true;
    r.efisEnabled     = true;
    r.efisIsVn300     = false;

    TEST_ASSERT_EQUAL_UINT32(5000u,  r.timeStampMs);
    TEST_ASSERT_EQUAL_FLOAT(95.0f,   r.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(7.9f,    r.derivedAoaDeg);
    TEST_ASSERT_TRUE(r.boomEnabled);
    TEST_ASSERT_TRUE(r.efisEnabled);
    TEST_ASSERT_FALSE(r.efisIsVn300);
}

void test_size_is_reasonable(void)
{
    // LogRow is the largest struct: ~40-60 fields. Generously bound at 512 bytes.
    TEST_ASSERT_LESS_OR_EQUAL(512u, sizeof(LogRow));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_initializes_numeric_fields_to_zero);
    RUN_TEST(test_fields_are_writable);
    RUN_TEST(test_size_is_reasonable);
    return UNITY_END();
}
