// test_ds18b20_decode.cpp — native tests for the DS18B20 scratchpad
// decoder.
//
// DecodeDs18b20Celsius takes the raw 9-byte scratchpad the sensor
// returns over 1-Wire and produces a Celsius float.  It mirrors
// DallasTemperature's behavior for the single-sensor 12-bit DS18B20
// path our firmware uses:
//
//   * CRC mismatch  -> kDeviceDisconnectedC (-127.0f)
//   * All-zero pad  -> kDeviceDisconnectedC
//   * Valid pad     -> int16_t(msb<<8 | lsb) * 0.0625f
//
// DEVICE_FAULT_* sentinels from the MAX31850 path are NOT reproduced
// because OnSpeed only supports DS18B20.

#include <unity.h>
#include <sensors/Ds18b20Decode.h>

#include <cstdint>
#include <cmath>

using onspeed::sensors::DecodeDs18b20Celsius;
using onspeed::sensors::Ds18b20Crc8;
using onspeed::sensors::kDs18b20DisconnectedC;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helper: build a valid 9-byte DS18B20 scratchpad given temp bytes and
// fill the rest with the conventional defaults at 12-bit resolution:
//   byte 2 = TH (high alarm)   = user/default, use 0x4B (75C) by default
//   byte 3 = TL (low alarm)    = user/default, use 0x46 (70C) by default
//   byte 4 = CONFIG            = 0x7F for 12-bit
//   byte 5 = reserved          = 0xFF
//   byte 6 = reserved          = 0x0C (count-remain on DS18S20, but
//                                DS18B20 reserved-bit state is 0x0C)
//   byte 7 = reserved          = 0x10
//   byte 8 = CRC-8 over [0..7]
// ---------------------------------------------------------------------------
static void buildValidPad(uint8_t out[9], uint8_t lsb, uint8_t msb)
{
    out[0] = lsb;
    out[1] = msb;
    out[2] = 0x4B;
    out[3] = 0x46;
    out[4] = 0x7F;
    out[5] = 0xFF;
    out[6] = 0x0C;
    out[7] = 0x10;
    out[8] = Ds18b20Crc8(out, 8);
}

// ---------------------------------------------------------------------------
// Independent CRC-8 reference (MAXIM/Dallas reflected polynomial 0x8C,
// init 0). This is the bit-reflected form of x^8 + x^5 + x^4 + 1; it's
// what the DS18B20 scratchpad CRC uses and what OneWire::crc8 computes.
// Kept here as a test-local reference so we're comparing two
// independent implementations, not the decoder against itself.
// ---------------------------------------------------------------------------
static uint8_t referenceCrc8(const uint8_t* data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        uint8_t b = data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t mix = static_cast<uint8_t>(crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// CRC correctness: our Ds18b20Crc8 must match an independent reference
// for the 8-byte scratchpad prefix, on a set of known vectors.
// ---------------------------------------------------------------------------

void test_crc8_matches_reference_on_known_vectors()
{
    // Vector 1: +25.0625C @ 12-bit, default alarms
    const uint8_t v1[8] = {0x91, 0x01, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10};
    TEST_ASSERT_EQUAL_UINT8(referenceCrc8(v1, 8), Ds18b20Crc8(v1, 8));

    // Vector 2: all zero (CRC should also be 0)
    const uint8_t v2[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(0, Ds18b20Crc8(v2, 8));
    TEST_ASSERT_EQUAL_UINT8(referenceCrc8(v2, 8), Ds18b20Crc8(v2, 8));

    // Vector 3: -55C @ 12-bit
    const uint8_t v3[8] = {0x90, 0xFC, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10};
    TEST_ASSERT_EQUAL_UINT8(referenceCrc8(v3, 8), Ds18b20Crc8(v3, 8));

    // Vector 4: all-ones noise
    const uint8_t v4[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT8(referenceCrc8(v4, 8), Ds18b20Crc8(v4, 8));
}

// ---------------------------------------------------------------------------
// Valid scratchpad -> correct Celsius
// ---------------------------------------------------------------------------

void test_decode_positive_25_0625()
{
    // DS18B20 datasheet example: +25.0625C -> msb=0x01, lsb=0x91
    uint8_t pad[9];
    buildValidPad(pad, 0x91, 0x01);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 25.0625f, DecodeDs18b20Celsius(pad));
}

void test_decode_positive_125()
{
    // Max positive: +125C -> msb=0x07, lsb=0xD0
    uint8_t pad[9];
    buildValidPad(pad, 0xD0, 0x07);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 125.0f, DecodeDs18b20Celsius(pad));
}

void test_decode_por_value_85()
{
    // Power-on-reset sentinel: +85C. Decoder returns the raw temp;
    // FilterOat (separate function) rejects it.
    uint8_t pad[9];
    buildValidPad(pad, 0x50, 0x05);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 85.0f, DecodeDs18b20Celsius(pad));
}

void test_decode_zero()
{
    uint8_t pad[9];
    buildValidPad(pad, 0x00, 0x00);
    // Valid scratchpad at 0C: all-zero *value* bytes but non-zero
    // alarm/config bytes, so isAllZeros should be false and decoder
    // returns 0.0.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, DecodeDs18b20Celsius(pad));
}

void test_decode_half_degree()
{
    // +0.5C -> msb=0x00, lsb=0x08
    uint8_t pad[9];
    buildValidPad(pad, 0x08, 0x00);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, DecodeDs18b20Celsius(pad));
}

void test_decode_negative_half_degree()
{
    // -0.5C -> msb=0xFF, lsb=0xF8 (two's complement)
    uint8_t pad[9];
    buildValidPad(pad, 0xF8, 0xFF);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, DecodeDs18b20Celsius(pad));
}

void test_decode_negative_25_0625()
{
    // -25.0625C -> msb=0xFE, lsb=0x6F
    uint8_t pad[9];
    buildValidPad(pad, 0x6F, 0xFE);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -25.0625f, DecodeDs18b20Celsius(pad));
}

void test_decode_minus_55()
{
    // Minimum spec temp: -55C -> msb=0xFC, lsb=0x90
    uint8_t pad[9];
    buildValidPad(pad, 0x90, 0xFC);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -55.0f, DecodeDs18b20Celsius(pad));
}

void test_decode_least_significant_bit()
{
    // One LSB = 0.0625C
    uint8_t pad[9];
    buildValidPad(pad, 0x01, 0x00);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0625f, DecodeDs18b20Celsius(pad));
}

// ---------------------------------------------------------------------------
// CRC failure -> disconnect sentinel
// ---------------------------------------------------------------------------

void test_decode_bad_crc_returns_disconnect_sentinel()
{
    // Build a valid pad at +25C then flip a bit in the temp byte
    // WITHOUT updating CRC -> CRC check fails.
    uint8_t pad[9];
    buildValidPad(pad, 0x91, 0x01);
    pad[0] ^= 0x01;  // flip LSB of temp, CRC no longer matches
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, kDs18b20DisconnectedC,
                             DecodeDs18b20Celsius(pad));
}

void test_decode_corrupted_crc_byte_returns_sentinel()
{
    uint8_t pad[9];
    buildValidPad(pad, 0x91, 0x01);
    pad[8] ^= 0xAA;  // smash the CRC byte itself
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, kDs18b20DisconnectedC,
                             DecodeDs18b20Celsius(pad));
}

// ---------------------------------------------------------------------------
// All-zero scratchpad -> disconnect sentinel
// ---------------------------------------------------------------------------

void test_decode_all_zero_scratchpad_returns_disconnect_sentinel()
{
    // All zeros: the bus is pulled low by a missing sensor.  CRC of
    // 8 zero bytes happens to be 0, so the CRC check passes — the
    // library explicitly adds an all-zeros guard.  Our decoder must
    // too.
    uint8_t pad[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, kDs18b20DisconnectedC,
                             DecodeDs18b20Celsius(pad));
}

// ---------------------------------------------------------------------------
// Round-trip: every 12-bit temperature step should decode back to the
// exact raw value when re-encoded. Spot-check across the range.
// ---------------------------------------------------------------------------

void test_decode_every_lsb_step_in_normal_range()
{
    // Walk -40C through +85C in 1 LSB steps (~2000 steps).
    // Every raw 16-bit value r maps to r * 0.0625; re-splitting into
    // msb/lsb must decode back to the same float.
    const int16_t lo = static_cast<int16_t>(-40.0f / 0.0625f);  // -640
    const int16_t hi = static_cast<int16_t>( 85.0f / 0.0625f);  // 1360
    for (int16_t raw = lo; raw <= hi; ++raw) {
        uint16_t u = static_cast<uint16_t>(raw);
        uint8_t pad[9];
        buildValidPad(pad,
                      static_cast<uint8_t>(u & 0xFF),
                      static_cast<uint8_t>((u >> 8) & 0xFF));
        float expected = raw * 0.0625f;
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected, DecodeDs18b20Celsius(pad));
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc8_matches_reference_on_known_vectors);

    RUN_TEST(test_decode_positive_25_0625);
    RUN_TEST(test_decode_positive_125);
    RUN_TEST(test_decode_por_value_85);
    RUN_TEST(test_decode_zero);
    RUN_TEST(test_decode_half_degree);
    RUN_TEST(test_decode_negative_half_degree);
    RUN_TEST(test_decode_negative_25_0625);
    RUN_TEST(test_decode_minus_55);
    RUN_TEST(test_decode_least_significant_bit);

    RUN_TEST(test_decode_bad_crc_returns_disconnect_sentinel);
    RUN_TEST(test_decode_corrupted_crc_byte_returns_sentinel);
    RUN_TEST(test_decode_all_zero_scratchpad_returns_disconnect_sentinel);

    RUN_TEST(test_decode_every_lsb_step_in_normal_range);
    return UNITY_END();
}
