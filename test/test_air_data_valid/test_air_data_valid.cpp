// test_air_data_valid.cpp — characterization tests for AirDataValid flags.
//
// Verifies bit constants are distinct, fit in the low 16 (the wire
// format slot), and that has/set/clear round-trip correctly.

#include <unity.h>
#include <types/AirDataValid.h>

using onspeed::types::AirDataValid;

void setUp(void) {}
void tearDown(void) {}

void test_default_constructed_has_zero_bits(void)
{
    AirDataValid v;
    TEST_ASSERT_EQUAL_UINT32(0u, v.bits);
}

void test_set_marks_bit(void)
{
    AirDataValid v;
    v.set(AirDataValid::kOatRaw);
    TEST_ASSERT_TRUE(v.has(AirDataValid::kOatRaw));
    TEST_ASSERT_FALSE(v.has(AirDataValid::kIas));
}

void test_clear_removes_bit(void)
{
    AirDataValid v;
    v.set(AirDataValid::kIas);
    v.set(AirDataValid::kTas);
    v.clear(AirDataValid::kIas);
    TEST_ASSERT_FALSE(v.has(AirDataValid::kIas));
    TEST_ASSERT_TRUE(v.has(AirDataValid::kTas));
}

void test_set_is_idempotent(void)
{
    AirDataValid v;
    v.set(AirDataValid::kPitch);
    v.set(AirDataValid::kPitch);
    TEST_ASSERT_TRUE(v.has(AirDataValid::kPitch));
    TEST_ASSERT_EQUAL_UINT32(AirDataValid::kPitch, v.bits);
}

void test_clear_on_unset_bit_is_noop(void)
{
    AirDataValid v;
    v.set(AirDataValid::kIas);
    v.clear(AirDataValid::kRoll);
    TEST_ASSERT_TRUE(v.has(AirDataValid::kIas));
    TEST_ASSERT_FALSE(v.has(AirDataValid::kRoll));
}

void test_bits_unique_and_in_low_16(void)
{
    // All wire-visible bits must fit in low 16 (the wire format slot).
    constexpr uint32_t kLow16Mask = 0x0000FFFFu;
    constexpr uint32_t kAllDefined =
        AirDataValid::kOatRaw | AirDataValid::kOatSat |
        AirDataValid::kIas | AirDataValid::kPalt |
        AirDataValid::kTas | AirDataValid::kDensityAlt |
        AirDataValid::kDerivedAoa | AirDataValid::kVsi |
        AirDataValid::kPitch | AirDataValid::kRoll |
        AirDataValid::kPercentLift | AirDataValid::kFlapsPos |
        AirDataValid::kFrameSelfConsistent;
    TEST_ASSERT_EQUAL_UINT32(kAllDefined, kAllDefined & kLow16Mask);
}

void test_bit_constants_are_distinct(void)
{
    // Sum of single bits == OR of single bits → no two share a position.
    constexpr uint32_t kSum =
        AirDataValid::kOatRaw + AirDataValid::kOatSat +
        AirDataValid::kIas + AirDataValid::kPalt +
        AirDataValid::kTas + AirDataValid::kDensityAlt +
        AirDataValid::kDerivedAoa + AirDataValid::kVsi +
        AirDataValid::kPitch + AirDataValid::kRoll +
        AirDataValid::kPercentLift + AirDataValid::kFlapsPos +
        AirDataValid::kFrameSelfConsistent;
    constexpr uint32_t kOr =
        AirDataValid::kOatRaw | AirDataValid::kOatSat |
        AirDataValid::kIas | AirDataValid::kPalt |
        AirDataValid::kTas | AirDataValid::kDensityAlt |
        AirDataValid::kDerivedAoa | AirDataValid::kVsi |
        AirDataValid::kPitch | AirDataValid::kRoll |
        AirDataValid::kPercentLift | AirDataValid::kFlapsPos |
        AirDataValid::kFrameSelfConsistent;
    TEST_ASSERT_EQUAL_UINT32(kOr, kSum);
}

void test_multiple_bits_or_together(void)
{
    AirDataValid v;
    v.set(AirDataValid::kIas);
    v.set(AirDataValid::kPalt);
    v.set(AirDataValid::kTas);
    TEST_ASSERT_TRUE(v.has(AirDataValid::kIas));
    TEST_ASSERT_TRUE(v.has(AirDataValid::kPalt));
    TEST_ASSERT_TRUE(v.has(AirDataValid::kTas));
    TEST_ASSERT_FALSE(v.has(AirDataValid::kOatRaw));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_constructed_has_zero_bits);
    RUN_TEST(test_set_marks_bit);
    RUN_TEST(test_clear_removes_bit);
    RUN_TEST(test_set_is_idempotent);
    RUN_TEST(test_clear_on_unset_bit_is_noop);
    RUN_TEST(test_bits_unique_and_in_low_16);
    RUN_TEST(test_bit_constants_are_distinct);
    RUN_TEST(test_multiple_bits_or_together);
    return UNITY_END();
}
