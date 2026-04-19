// test_types_ahrs_outputs.cpp — structural tests for onspeed::AhrsOutputs

#include <unity.h>
#include <types/AhrsOutputs.h>

using onspeed::AhrsOutputs;

void setUp(void) {}
void tearDown(void) {}

void test_default_initializes_all_fields_to_zero(void)
{
    AhrsOutputs a;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.pitchDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.rollDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.flightPathDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.derivedAoaDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.tasMps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.tasDotMps2);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.kalmanAltFt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.kalmanVsiFpm);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.earthVertG);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.gyroRollFiltDps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.gyroPitchFiltDps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.gyroYawFiltDps);
    TEST_ASSERT_EQUAL_UINT32(0u,  a.timestampUs);
}

void test_fields_are_writable(void)
{
    AhrsOutputs a;
    a.pitchDeg         =  3.5f;
    a.rollDeg          = -10.0f;
    a.flightPathDeg    =  1.2f;
    a.derivedAoaDeg    =  9.7f;
    a.tasMps           = 55.0f;
    a.tasDotMps2       = -0.5f;
    a.kalmanAltFt      = 3500.0f;
    a.kalmanVsiFpm     = -200.0f;
    a.earthVertG       = 1.02f;
    a.gyroRollFiltDps  =  0.3f;
    a.gyroPitchFiltDps =  0.1f;
    a.gyroYawFiltDps   = -0.05f;
    a.timestampUs      = 99999u;

    TEST_ASSERT_EQUAL_FLOAT(9.7f,    a.derivedAoaDeg);
    TEST_ASSERT_EQUAL_FLOAT(3500.0f, a.kalmanAltFt);
    TEST_ASSERT_EQUAL_UINT32(99999u, a.timestampUs);
}

void test_size_is_reasonable(void)
{
    // 12 floats (48 bytes) + 1 uint32 (4 bytes) = 52 bytes. Allow padding to 64.
    TEST_ASSERT_LESS_OR_EQUAL(64u, sizeof(AhrsOutputs));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_initializes_all_fields_to_zero);
    RUN_TEST(test_fields_are_writable);
    RUN_TEST(test_size_is_reasonable);
    return UNITY_END();
}
