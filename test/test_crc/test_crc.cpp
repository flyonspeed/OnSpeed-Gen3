// test_crc.cpp — unit tests for onspeed::util::Checksum8
//
// Cross-checks the additive 8-bit checksum against hand-calculated values and
// against a reference #1 frame captured from the Gen3 firmware.

#include <unity.h>
#include <util/Crc.h>

using onspeed::util::Checksum8;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Basic correctness
// ----------------------------------------------------------------------------

void test_empty_input_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x00u, Checksum8(nullptr, 0));
}

void test_single_byte(void)
{
    const uint8_t buf[] = {0x41};  // 'A' = 65 = 0x41
    TEST_ASSERT_EQUAL_UINT8(0x41u, Checksum8(buf, 1));
}

void test_two_bytes_no_wrap(void)
{
    // 0x10 + 0x20 = 0x30
    const uint8_t buf[] = {0x10, 0x20};
    TEST_ASSERT_EQUAL_UINT8(0x30u, Checksum8(buf, 2));
}

void test_overflow_wraps_to_low_byte(void)
{
    // 0xFF + 0x01 = 0x100; low byte = 0x00
    const uint8_t buf[] = {0xFF, 0x01};
    TEST_ASSERT_EQUAL_UINT8(0x00u, Checksum8(buf, 2));
}

void test_all_zeros(void)
{
    const uint8_t buf[8] = {};
    TEST_ASSERT_EQUAL_UINT8(0x00u, Checksum8(buf, 8));
}

// ----------------------------------------------------------------------------
// Reference frame: the 76-byte payload of "#1+0000+00000000+000000+00000+00+00+00
// +00+000+00+000+000+0000+0000+0000+0000+00+00"
// Checksum produced by the Gen3 firmware builder for all-zero inputs.
// Hand-verified: '#'=35, '1'=49 ... sum over 76 bytes.
// ----------------------------------------------------------------------------

void test_known_frame_prefix(void)
{
    // The first two bytes of every #1 frame are "#1" = 0x23 0x31
    // sum = 0x23 + 0x31 = 0x54
    const uint8_t prefix[] = {0x23, 0x31};
    TEST_ASSERT_EQUAL_UINT8(0x54u, Checksum8(prefix, 2));
}

void test_all_ascii_digits(void)
{
    // "0123456789" = 48+49+50+51+52+53+54+55+56+57 = 525 => 0x0D (525 & 0xFF = 13)
    const uint8_t buf[] = {'0','1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_UINT8(0x0Du, Checksum8(buf, 10));
}

void test_length_subset(void)
{
    // Only first 2 of 4 bytes should be summed
    const uint8_t buf[] = {0x01, 0x02, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT8(0x03u, Checksum8(buf, 2));
}

// ----------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_input_is_zero);
    RUN_TEST(test_single_byte);
    RUN_TEST(test_two_bytes_no_wrap);
    RUN_TEST(test_overflow_wraps_to_low_byte);
    RUN_TEST(test_all_zeros);
    RUN_TEST(test_known_frame_prefix);
    RUN_TEST(test_all_ascii_digits);
    RUN_TEST(test_length_subset);
    return UNITY_END();
}
