// test_types_sensor_sample.cpp — structural tests for onspeed::SensorSample

#include <unity.h>
#include <types/SensorSample.h>

using onspeed::SensorSample;
using onspeed::kOatInvalid;

void setUp(void) {}
void tearDown(void) {}

void test_default_initializes_fields_to_expected_values(void)
{
    SensorSample s;
    TEST_ASSERT_EQUAL_FLOAT(0.0f,       s.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,       s.paltFt);
    TEST_ASSERT_EQUAL_FLOAT(kOatInvalid, s.oatCelsius);  // sentinel, not zero
    TEST_ASSERT_EQUAL_FLOAT(0.0f,       s.psPsi);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,       s.ptPsi);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,       s.p45Psi);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,       s.densityAltitudeFt);
    TEST_ASSERT_EQUAL_UINT32(0u,        s.timestampUs);
}

void test_fields_are_writable(void)
{
    SensorSample s;
    s.iasKt              = 95.0f;
    s.paltFt             = 3500.0f;
    s.oatCelsius         = 15.0f;
    s.psPsi              = 12.5f;
    s.ptPsi              = 12.7f;
    s.p45Psi             = 0.05f;
    s.densityAltitudeFt  = 4200.0f;
    s.timestampUs        = 123456789u;

    TEST_ASSERT_EQUAL_FLOAT(95.0f,      s.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(3500.0f,    s.paltFt);
    TEST_ASSERT_EQUAL_FLOAT(15.0f,      s.oatCelsius);
    TEST_ASSERT_EQUAL_UINT32(123456789u, s.timestampUs);
}

void test_size_is_reasonable(void)
{
    // 7 floats (28 bytes) + 1 uint32 (4 bytes) = 32 bytes. Allow padding to 48.
    TEST_ASSERT_LESS_OR_EQUAL(48u, sizeof(SensorSample));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_initializes_fields_to_expected_values);
    RUN_TEST(test_fields_are_writable);
    RUN_TEST(test_size_is_reasonable);
    return UNITY_END();
}
