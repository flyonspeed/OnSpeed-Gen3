// test_types_imu_sample.cpp — structural tests for onspeed::ImuSample

#include <unity.h>
#include <types/ImuSample.h>

using onspeed::ImuSample;

void setUp(void) {}
void tearDown(void) {}

void test_default_initializes_all_fields_to_zero(void)
{
    ImuSample s;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.accelXG);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.accelYG);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.accelZG);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.gyroRollDps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.gyroPitchDps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.gyroYawDps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.tempCelsius);
    TEST_ASSERT_EQUAL_UINT32(0u,  s.timestampUs);
}

void test_fields_are_writable(void)
{
    ImuSample s;
    s.accelXG      =  0.02f;
    s.accelYG      = -0.01f;
    s.accelZG      = -1.00f;
    s.gyroRollDps  =  1.5f;
    s.gyroPitchDps =  0.5f;
    s.gyroYawDps   = -0.2f;
    s.tempCelsius  = 32.4f;
    s.timestampUs  = 0xDEADBEEFu;

    TEST_ASSERT_EQUAL_FLOAT(-1.00f, s.accelZG);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, s.timestampUs);
}

void test_size_is_reasonable(void)
{
    // 7 floats (28 bytes) + 1 uint32 (4 bytes) = 32 bytes. Allow padding to 40.
    TEST_ASSERT_LESS_OR_EQUAL(40u, sizeof(ImuSample));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_initializes_all_fields_to_zero);
    RUN_TEST(test_fields_are_writable);
    RUN_TEST(test_size_is_reasonable);
    return UNITY_END();
}
