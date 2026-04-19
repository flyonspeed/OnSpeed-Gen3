// test_types_flap_state.cpp — structural tests for onspeed::FlapState

#include <unity.h>
#include <types/FlapState.h>

using onspeed::FlapState;

void setUp(void) {}
void tearDown(void) {}

void test_default_initializes_all_fields_to_zero_or_false(void)
{
    FlapState f;
    TEST_ASSERT_EQUAL_UINT16(0u,    f.rawAdc);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   f.normalized);
    TEST_ASSERT_EQUAL_INT(0,        f.detectedIndex);
    TEST_ASSERT_FALSE(f.valid);
}

void test_fields_are_writable(void)
{
    FlapState f;
    f.rawAdc        = 2048u;
    f.normalized    = 0.5f;
    f.detectedIndex = 2;
    f.valid         = true;

    TEST_ASSERT_EQUAL_UINT16(2048u, f.rawAdc);
    TEST_ASSERT_EQUAL_FLOAT(0.5f,   f.normalized);
    TEST_ASSERT_EQUAL_INT(2,        f.detectedIndex);
    TEST_ASSERT_TRUE(f.valid);
}

void test_size_is_reasonable(void)
{
    // uint16 (2) + float (4) + int (4) + bool (1) = 11 bytes raw; allow padding to 16.
    TEST_ASSERT_LESS_OR_EQUAL(16u, sizeof(FlapState));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_initializes_all_fields_to_zero_or_false);
    RUN_TEST(test_fields_are_writable);
    RUN_TEST(test_size_is_reasonable);
    return UNITY_END();
}
