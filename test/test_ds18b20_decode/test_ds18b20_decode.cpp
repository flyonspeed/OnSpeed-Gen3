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
#include <cstdio>
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
// Table-driven CRC-8 reference (MAXIM/Dallas polynomial, reflected 0x8C).
//
// This reference uses a different algorithm from the production
// shift-register variant in Ds18b20Decode.h, so a bug in either path
// shows up as a disagreement on at least some input.  The table is
// built at static-init time from the polynomial, matching the one
// the AVR build of OneWire::crc8 baked into PROGMEM.
// ---------------------------------------------------------------------------
namespace {

struct Crc8Table {
    uint8_t t[256];
    constexpr Crc8Table() : t{} {
        for (int i = 0; i < 256; ++i) {
            uint8_t c = static_cast<uint8_t>(i);
            for (int b = 0; b < 8; ++b) {
                c = (c & 1) ? static_cast<uint8_t>((c >> 1) ^ 0x8C)
                            : static_cast<uint8_t>(c >> 1);
            }
            t[i] = c;
        }
    }
};

static const Crc8Table kCrc8Table;

uint8_t tableDrivenCrc8(const uint8_t* data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        crc = kCrc8Table.t[crc ^ data[i]];
    }
    return crc;
}

} // namespace

// ---------------------------------------------------------------------------
// CRC correctness — Part A: compare production Ds18b20Crc8 against
// hardcoded vectors computed outside the C++ toolchain.
//
// These constants were produced by two independent Python
// implementations (shift-register and table-driven, both agreeing).
// Hardcoding them means this test catches any bug in either of the
// C++ implementations without either implementation acting as its
// own reference.
// ---------------------------------------------------------------------------

void test_crc8_matches_hardcoded_vectors()
{
    struct V {
        const char* name;
        uint8_t data[8];
        int len;
        uint8_t expected;
    };

    const V vectors[] = {
        // 8-byte scratchpad prefixes (real DS18B20 readings)
        {"+25.0625C 12-bit",  {0x91, 0x01, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10}, 8, 0x70},
        {"all zeros",         {0,    0,    0,    0,    0,    0,    0,    0},    8, 0x00},
        {"-55C 12-bit",       {0x90, 0xFC, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10}, 8, 0x4F},
        {"all 0xFF",          {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 8, 0xC9},
        {"+85C POR",          {0x50, 0x05, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10}, 8, 0x1C},
        {"+0.5C 12-bit",      {0x08, 0x00, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10}, 8, 0xE2},
        {"-0.5C 12-bit",      {0xF8, 0xFF, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10}, 8, 0xC3},
        {"0x01..0x08",        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}, 8, 0x83},
    };

    for (const auto& v : vectors) {
        uint8_t got = Ds18b20Crc8(v.data, v.len);
        if (got != v.expected) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "CRC mismatch on vector '%s': expected 0x%02X, got 0x%02X",
                     v.name, v.expected, got);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// ---------------------------------------------------------------------------
// CRC correctness — Part B: cross-check production shift-register
// implementation against an algorithmically different table-driven
// reference across many inputs.
// ---------------------------------------------------------------------------

void test_crc8_matches_table_driven_reference_exhaustive_singles()
{
    // All 256 single-byte inputs.
    for (int i = 0; i < 256; ++i) {
        uint8_t b = static_cast<uint8_t>(i);
        TEST_ASSERT_EQUAL_UINT8(tableDrivenCrc8(&b, 1), Ds18b20Crc8(&b, 1));
    }
}

void test_crc8_matches_table_driven_reference_random_streams()
{
    // Deterministic PRNG so the test is reproducible.
    uint32_t rng = 0xDEADBEEF;
    auto next = [&rng]() -> uint8_t {
        // xorshift32 — cheap and completely independent from the CRC.
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<uint8_t>(rng & 0xFF);
    };

    // 500 random messages of lengths 1..64 bytes.
    for (int iter = 0; iter < 500; ++iter) {
        uint8_t buf[64];
        int len = 1 + (next() & 0x3F);  // 1..64
        for (int i = 0; i < len; ++i) buf[i] = next();
        TEST_ASSERT_EQUAL_UINT8(tableDrivenCrc8(buf, len),
                                Ds18b20Crc8(buf, len));
    }
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
    RUN_TEST(test_crc8_matches_hardcoded_vectors);
    RUN_TEST(test_crc8_matches_table_driven_reference_exhaustive_singles);
    RUN_TEST(test_crc8_matches_table_driven_reference_random_streams);

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
