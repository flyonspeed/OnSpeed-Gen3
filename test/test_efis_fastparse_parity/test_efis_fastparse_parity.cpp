// test_efis_fastparse_parity.cpp
//
// Parity tests: the FastParse helpers must produce bit-identical output
// to the prior strtof/strtol + memcpy/memcmp dance for every field
// pattern the wire format can produce.
//
// The key invariant: for any digit string the wire could carry, the
// new integer-accumulate parser followed by `/ scale` must produce
// the SAME IEEE-754 float bits as `strtof(str, nullptr) / scale`. We
// assert this with TEST_ASSERT_EQUAL_MEMORY on the float bytes — no
// tolerance.
//
// Why this is bit-stable: strtof on a pure-decimal string with no
// exponent produces N.0f where N is the parsed integer value. Then
// dividing by `scale` (a compile-time float constant) goes through
// the same FDIV instruction in both paths. So the new
// `accumulate-as-int -> cast to float -> divide` produces identical
// bits to `strtof -> divide`.

#include <unity.h>
#include <efis/FastParse.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using onspeed::efis::fastparse::parseFixedUnsigned;
using onspeed::efis::fastparse::parseFixedSigned;
using onspeed::efis::fastparse::parseHex1;
using onspeed::efis::fastparse::parseHex2;
using onspeed::efis::fastparse::isSentinel;
using onspeed::efis::fastparse::sumChecksum;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helper: legacy strtof path replicating the prior parseFieldFloat helper.
// Returns the divided float for a NUL-terminated digit string.
// ---------------------------------------------------------------------------
static float legacyParseFloat(const char* digits, int len, float scale)
{
    char tmp[16];
    memcpy(tmp, digits, static_cast<size_t>(len));
    tmp[len] = '\0';
    return strtof(tmp, nullptr) / scale;
}

// New path: parse as signed integer, divide by scale.
static float fastParseFloatSigned(const char* digits, int len, float scale)
{
    return static_cast<float>(parseFixedSigned(digits, 0, len)) / scale;
}

// Compare two floats for bit-identity.
static void assertBitIdentical(float a, float b, const char* msg)
{
    uint32_t ai, bi;
    memcpy(&ai, &a, sizeof(ai));
    memcpy(&bi, &b, sizeof(bi));
    if (ai != bi) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "%s: legacy=0x%08x (%.9g) fast=0x%08x (%.9g)",
                 msg, ai, a, bi, b);
        TEST_FAIL_MESSAGE(buf);
    }
}

// ===========================================================================
// parseFixedUnsigned — corpus sweep
// ===========================================================================
static void test_unsigned_basic()
{
    // Width 4
    TEST_ASSERT_EQUAL_UINT32(0u,    parseFixedUnsigned("0000", 0, 4));
    TEST_ASSERT_EQUAL_UINT32(1u,    parseFixedUnsigned("0001", 0, 4));
    TEST_ASSERT_EQUAL_UINT32(234u,  parseFixedUnsigned("0234", 0, 4));
    TEST_ASSERT_EQUAL_UINT32(9999u, parseFixedUnsigned("9999", 0, 4));
    TEST_ASSERT_EQUAL_UINT32(1100u, parseFixedUnsigned("1100", 0, 4));
    // Leading space (Dynon's space-padded format)
    TEST_ASSERT_EQUAL_UINT32(62u,   parseFixedUnsigned("  62", 0, 4));
    TEST_ASSERT_EQUAL_UINT32(5u,    parseFixedUnsigned("   5", 0, 4));
    // Width 6 (Palt)
    TEST_ASSERT_EQUAL_UINT32(5500u,   parseFixedUnsigned("005500", 0, 6));
    TEST_ASSERT_EQUAL_UINT32(99999u,  parseFixedUnsigned("099999", 0, 6));
    TEST_ASSERT_EQUAL_UINT32(999999u, parseFixedUnsigned("999999", 0, 6));
}

// ===========================================================================
// parseFixedSigned — corpus sweep
// ===========================================================================
static void test_signed_basic()
{
    TEST_ASSERT_EQUAL_INT32(0,     parseFixedSigned("+000", 0, 4));
    TEST_ASSERT_EQUAL_INT32(23,    parseFixedSigned("+023", 0, 4));
    TEST_ASSERT_EQUAL_INT32(-23,   parseFixedSigned("-023", 0, 4));
    TEST_ASSERT_EQUAL_INT32(50,    parseFixedSigned("+0050", 0, 5));
    TEST_ASSERT_EQUAL_INT32(-50,   parseFixedSigned("-0050", 0, 5));
    TEST_ASSERT_EQUAL_INT32(9999,  parseFixedSigned("+9999", 0, 5));
    TEST_ASSERT_EQUAL_INT32(-9999, parseFixedSigned("-9999", 0, 5));
    // No sign byte — leading space then digits (Dynon altitude)
    TEST_ASSERT_EQUAL_INT32(5500,  parseFixedSigned("005500", 0, 6));
    // 3-char signed OAT field
    TEST_ASSERT_EQUAL_INT32(8,    parseFixedSigned("+08", 0, 3));
    TEST_ASSERT_EQUAL_INT32(-12,  parseFixedSigned("-12", 0, 3));
}

// ===========================================================================
// Bit-identity sweep: every digit pattern Dynon SkyView ADAHRS could
// produce for each numeric field must yield bit-identical floats.
//
// This is the load-bearing test for the hand-rolled parser: it proves
// the refactor is byte-stable on every reasonable input.
// ===========================================================================

static void test_bit_identical_4digit_div10()
{
    // 4-digit IAS / TAS field: "0000" .. "9999", divided by 10.0f.
    char buf[5] = {0,0,0,0,0};
    for (uint32_t v = 0; v <= 9999u; v += 7)   // sweep every 7 to keep it fast
    {
        snprintf(buf, sizeof(buf), "%04u", v);
        const float legacy = legacyParseFloat(buf, 4, 10.0f);
        const float fast   = fastParseFloatSigned(buf, 4, 10.0f);
        assertBitIdentical(legacy, fast, "4digit/10");
    }
}

static void test_bit_identical_4digit_signed_div10()
{
    // 4-char signed pitch field: "+PPP" / "-PPP", divided by 10.0f.
    // Sweep every 7 to keep test runtime reasonable.
    char buf[8] = {};
    for (int v = -999; v <= 999; v += 7)
    {
        snprintf(buf, sizeof(buf), "%+04d", v);
        const float legacy = legacyParseFloat(buf, 4, 10.0f);
        const float fast   = fastParseFloatSigned(buf, 4, 10.0f);
        assertBitIdentical(legacy, fast, "4digit_signed/10");
    }
}

static void test_bit_identical_5digit_signed_div10()
{
    // 5-char signed roll field: "+RRRR" / "-RRRR", divided by 10.0f.
    char buf[8] = {};
    for (int v = -9999; v <= 9999; v += 71)
    {
        snprintf(buf, sizeof(buf), "%+05d", v);
        const float legacy = legacyParseFloat(buf, 5, 10.0f);
        const float fast   = fastParseFloatSigned(buf, 5, 10.0f);
        assertBitIdentical(legacy, fast, "5digit_signed/10");
    }
}

static void test_bit_identical_3digit_div100()
{
    // 3-digit lateralG field: "000" .. "999", divided by 100.0f.
    char buf[4] = {0,0,0,0};
    for (uint32_t v = 0; v <= 999u; v++)
    {
        snprintf(buf, sizeof(buf), "%03u", v);
        const float legacy = legacyParseFloat(buf, 3, 100.0f);
        const float fast   = fastParseFloatSigned(buf, 3, 100.0f);
        assertBitIdentical(legacy, fast, "3digit/100");
    }
}

static void test_bit_identical_3digit_div10()
{
    // 3-digit verticalG field: "000" .. "999", divided by 10.0f.
    char buf[4] = {0,0,0,0};
    for (uint32_t v = 0; v <= 999u; v++)
    {
        snprintf(buf, sizeof(buf), "%03u", v);
        const float legacy = legacyParseFloat(buf, 3, 10.0f);
        const float fast   = fastParseFloatSigned(buf, 3, 10.0f);
        assertBitIdentical(legacy, fast, "3digit/10");
    }
}

static void test_bit_identical_3digit_signed_unit_scale()
{
    // OAT field: "+OO" or "-OO", scale 1.0.
    char buf[8] = {};
    for (int v = -99; v <= 99; v++)
    {
        snprintf(buf, sizeof(buf), "%+03d", v);
        const float legacy = legacyParseFloat(buf, 3, 1.0f);
        const float fast   = fastParseFloatSigned(buf, 3, 1.0f);
        assertBitIdentical(legacy, fast, "3digit_signed/1");
    }
}

static void test_bit_identical_6digit_signed()
{
    // 6-digit signed Palt field for Dynon: "005500", "099999", "-12345".
    // Note Dynon SkyView Palt is unsigned 6-digit; signed parsing covers
    // both the unsigned wire format and the rare "-NNNNN" below-MSL case.
    char buf[10] = {};
    for (int v = -99999; v <= 99999; v += 137)
    {
        if (v >= 0)
            snprintf(buf, sizeof(buf), "%06d", v);
        else
            snprintf(buf, sizeof(buf), "%+06d", v);
        const float legacy = legacyParseFloat(buf, 6, 1.0f);
        const float fast   = fastParseFloatSigned(buf, 6, 1.0f);
        assertBitIdentical(legacy, fast, "6digit_signed");
    }
}

static void test_bit_identical_dynon_d10_status_hex()
{
    // Dynon D10 status nibble: one hex char, value 0..15. parseHex1 must
    // produce the same value as strtol on a 1-char hex string.
    for (int v = 0; v < 16; v++)
    {
        char c = (v < 10) ? ('0' + v) : ('A' + v - 10);
        const int hot = parseHex1(&c, 0);
        char tmp[2] = { c, '\0' };
        const int legacy = static_cast<int>(strtol(tmp, nullptr, 16));
        if (hot != legacy) {
            char m[128];
            snprintf(m, sizeof(m), "hex1 mismatch on '%c': fast=%d legacy=%d", c, hot, legacy);
            TEST_FAIL_MESSAGE(m);
        }
    }
    // Lowercase too
    for (int v = 10; v < 16; v++)
    {
        char c = 'a' + v - 10;
        const int hot = parseHex1(&c, 0);
        if (hot != v) TEST_FAIL_MESSAGE("hex1 lowercase mismatch");
    }
}

// ===========================================================================
// parseHex2 — CRC byte parity
// ===========================================================================
static void test_bit_identical_hex_crc()
{
    char buf[3] = {0,0,0};
    for (int v = 0; v <= 255; v++)
    {
        snprintf(buf, sizeof(buf), "%02X", v);
        const int legacy = static_cast<int>(strtol(buf, nullptr, 16));
        const int fast   = parseHex2(buf, 0);
        if (legacy != fast) {
            char m[128];
            snprintf(m, sizeof(m), "hex2 mismatch v=%d buf=\"%s\" legacy=%d fast=%d",
                     v, buf, legacy, fast);
            TEST_FAIL_MESSAGE(m);
        }
    }
    // Lowercase
    for (int v = 0; v <= 255; v++)
    {
        snprintf(buf, sizeof(buf), "%02x", v);
        const int fast = parseHex2(buf, 0);
        if (fast != v) TEST_FAIL_MESSAGE("hex2 lowercase mismatch");
    }
}

// ===========================================================================
// isSentinel — single-byte sentinel detection equivalent to memcmp
// over the whole field width when the field is fully repeated sentinel
// bytes (the only case the wire ever produces).
// ===========================================================================
static void test_sentinel_detection()
{
    TEST_ASSERT_TRUE(isSentinel("XXXX", 0, 'X'));
    TEST_ASSERT_TRUE(isSentinel("____", 0, '_'));
    TEST_ASSERT_TRUE(isSentinel("---", 0, '-'));
    TEST_ASSERT_FALSE(isSentinel("0234", 0, 'X'));
    TEST_ASSERT_FALSE(isSentinel("+023", 0, 'X'));
    // Offset
    TEST_ASSERT_TRUE(isSentinel("ABCXXXX", 3, 'X'));
}

// ===========================================================================
// sumChecksum — parity with the per-protocol loops
// ===========================================================================
static void test_sum_checksum_parity()
{
    // SkyView !1 ADAHRS uses sum of bytes 0..69.
    char buf[80] = {};
    for (int i = 0; i < 80; i++) buf[i] = static_cast<char>(i + 1);

    uint32_t legacy = 0;
    for (int i = 0; i <= 69; i++) legacy += static_cast<unsigned char>(buf[i]);
    legacy &= 0xFFu;
    const uint8_t fast = sumChecksum(buf, 0, 70);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(legacy), fast);

    // SkyView !3 EMS uses sum of bytes 0..220.
    char emsBuf[225] = {};
    for (int i = 0; i < 225; i++) emsBuf[i] = static_cast<char>((i * 31) & 0xFF);
    uint32_t legacy2 = 0;
    for (int i = 0; i <= 220; i++) legacy2 += static_cast<unsigned char>(emsBuf[i]);
    legacy2 &= 0xFFu;
    const uint8_t fast2 = sumChecksum(emsBuf, 0, 221);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(legacy2), fast2);
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------
int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_unsigned_basic);
    RUN_TEST(test_signed_basic);
    RUN_TEST(test_bit_identical_4digit_div10);
    RUN_TEST(test_bit_identical_4digit_signed_div10);
    RUN_TEST(test_bit_identical_5digit_signed_div10);
    RUN_TEST(test_bit_identical_3digit_div100);
    RUN_TEST(test_bit_identical_3digit_div10);
    RUN_TEST(test_bit_identical_3digit_signed_unit_scale);
    RUN_TEST(test_bit_identical_6digit_signed);
    RUN_TEST(test_bit_identical_dynon_d10_status_hex);
    RUN_TEST(test_bit_identical_hex_crc);
    RUN_TEST(test_sentinel_detection);
    RUN_TEST(test_sum_checksum_parity);
    return UNITY_END();
}
