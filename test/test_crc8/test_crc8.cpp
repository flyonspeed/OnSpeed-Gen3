// test_crc8.cpp — CRC-8 with poly 0x07, init 0x00 (SMBus).
//
// Reference vectors verified via Python (see commit history); 0xF4 is
// the SMBus standard check value for "123456789".

#include <unity.h>
#include <cstring>
#include <proto/Crc8.h>

using onspeed::proto::Crc8;

void setUp(void) {}
void tearDown(void) {}

void test_empty_input_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x00, Crc8(nullptr, 0));
}

void test_single_zero_byte(void)
{
    const uint8_t zero = 0x00;
    TEST_ASSERT_EQUAL_UINT8(0x00, Crc8(&zero, 1));
}

void test_smbus_standard_check_vector(void)
{
    // CRC-8 of "123456789" == 0xF4 (standard SMBus check value).
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_UINT8(0xF4,
        Crc8(reinterpret_cast<const uint8_t*>(s), 9));
}

void test_v424_magic_known_vector(void)
{
    // CRC-8 of "#124" == 0x21.  Documents the v4.24 wire-format
    // header byte sequence under the new checksum algorithm.
    const char* s = "#124";
    TEST_ASSERT_EQUAL_UINT8(0x21,
        Crc8(reinterpret_cast<const uint8_t*>(s), 4));
}

void test_single_bit_flip_changes_crc(void)
{
    const uint8_t buf1[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t buf2[4] = {0x00, 0x00, 0x00, 0x00};
    buf2[1] = 0x01;
    const uint8_t c1 = Crc8(buf1, 4);
    const uint8_t c2 = Crc8(buf2, 4);
    TEST_ASSERT_NOT_EQUAL_UINT8(c1, c2);
}

void test_order_matters(void)
{
    // CRC is order-sensitive, unlike additive sum.
    const uint8_t bufAB[2] = {0xAA, 0xBB};
    const uint8_t bufBA[2] = {0xBB, 0xAA};
    TEST_ASSERT_NOT_EQUAL_UINT8(Crc8(bufAB, 2), Crc8(bufBA, 2));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_input_is_zero);
    RUN_TEST(test_single_zero_byte);
    RUN_TEST(test_smbus_standard_check_vector);
    RUN_TEST(test_v424_magic_known_vector);
    RUN_TEST(test_single_bit_flip_changes_crc);
    RUN_TEST(test_order_matters);
    return UNITY_END();
}
