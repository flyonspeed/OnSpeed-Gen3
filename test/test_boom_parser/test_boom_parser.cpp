// test_boom_parser.cpp
//
// Unit tests for onspeed::boom::Decode against:
//   1. Real captured $AIRDAQ frames from a live boom (Sam's bench
//      capture at ~/Downloads/boomchecksum.txt — a subset is embedded
//      here so the test is hermetic).
//   2. The synth-builder $BOOM frame format used by the perf-synth env
//      (separator '*' rather than ',' before the CRC).
//   3. Adversarial inputs (bad CRC, short frame, missing fields,
//      malformed digits).

#include <unity.h>
#include <boom/BoomParser.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using onspeed::boom::BoomFrame;
using onspeed::boom::Decode;

void setUp(void) {}
void tearDown(void) {}

// ===========================================================================
// 1. Real-world $AIRDAQ frames from boomchecksum.txt
//    Format: $AIRDAQ,64.12.B8,ADC,SSSS,DDDD,AAAA,BBBB,XX
//    Where the trailing XX is the sum-mod-256 CRC of everything before it.
// ===========================================================================

static void test_real_airdaq_frame_decodes()
{
    const char* line = "$AIRDAQ,64.12.B8,ADC,9842,8152,3942,4006,8C";
    const int len = static_cast<int>(std::strlen(line));
    BoomFrame f = Decode(line, len, /*checkCrc=*/true);

    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_EQUAL_INT(9842, f.staticCounts);
    TEST_ASSERT_EQUAL_INT(8152, f.dynamicCounts);
    TEST_ASSERT_EQUAL_INT(3942, f.alphaCounts);
    TEST_ASSERT_EQUAL_INT(4006, f.betaCounts);
}

static void test_real_airdaq_crc_failure_rejected()
{
    // Same frame, CRC corrupted from 8C → 00.
    const char* line = "$AIRDAQ,64.12.B8,ADC,9842,8152,3942,4006,00";
    const int len = static_cast<int>(std::strlen(line));
    BoomFrame f = Decode(line, len, /*checkCrc=*/true);
    TEST_ASSERT_FALSE(f.valid);
}

static void test_real_airdaq_crc_disabled_passes_corrupt_crc()
{
    const char* line = "$AIRDAQ,64.12.B8,ADC,9842,8152,3942,4006,00";
    const int len = static_cast<int>(std::strlen(line));
    BoomFrame f = Decode(line, len, /*checkCrc=*/false);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_EQUAL_INT(9842, f.staticCounts);
}

static void test_real_airdaq_second_sample()
{
    // From boomchecksum.txt line 13: '$AIRDAQ,64.12.B8,ADC,9834,8154,3942,4006,8F'
    const char* line = "$AIRDAQ,64.12.B8,ADC,9834,8154,3942,4006,8F";
    const int len = static_cast<int>(std::strlen(line));
    BoomFrame f = Decode(line, len, /*checkCrc=*/true);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_EQUAL_INT(9834, f.staticCounts);
    TEST_ASSERT_EQUAL_INT(8154, f.dynamicCounts);
    TEST_ASSERT_EQUAL_INT(3942, f.alphaCounts);
    TEST_ASSERT_EQUAL_INT(4006, f.betaCounts);
}

// ===========================================================================
// 2. Legacy $BOOM,...,*XX synth format. The current perf-synth env emits
//    $AIRDAQ now (matches real wire format); this test keeps the older
//    '*'-separator form to lock in the parser's separator-agnosticism —
//    if anyone ever changes the parser to require ',' specifically,
//    this test fails. No live producer emits this form.
// ===========================================================================

static void test_synth_boom_frame_decodes()
{
    // Build the synth frame inline (matches SynthFrames.cpp::BuildBoom output).
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf),
        "$BOOM,260520120000FF,10000,05000,08200,08100*");
    int crc = 0;
    for (int i = 0; i < n - 1; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    std::snprintf(buf + n, sizeof(buf) - n, "%02X", crc);
    const int len = n + 2;   // n includes the '*', plus 2 hex digits

    BoomFrame f = Decode(buf, len, /*checkCrc=*/true);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_EQUAL_INT(10000, f.staticCounts);
    TEST_ASSERT_EQUAL_INT( 5000, f.dynamicCounts);
    TEST_ASSERT_EQUAL_INT( 8200, f.alphaCounts);
    TEST_ASSERT_EQUAL_INT( 8100, f.betaCounts);
}

// ===========================================================================
// 3. Adversarial inputs
// ===========================================================================

static void test_too_short_rejected()
{
    const char* line = "$AIRDAQ";
    BoomFrame f = Decode(line, 7, /*checkCrc=*/true);
    TEST_ASSERT_FALSE(f.valid);
}

static void test_missing_dollar_rejected()
{
    const char* line = "AIRDAQ,64.12.B8,ADC,9842,8152,3942,4006,8C";
    const int len = static_cast<int>(std::strlen(line));
    BoomFrame f = Decode(line, len, /*checkCrc=*/false);
    TEST_ASSERT_FALSE(f.valid);
}

static void test_missing_fields_rejected()
{
    // Three integer fields, not four. Without CRC enforcement so we
    // test the field-count guard rather than checksum.
    const char* line = "$AIRDAQ,64.12.B8,ADC,9842,8152,3942,XX";
    const int len = static_cast<int>(std::strlen(line));
    BoomFrame f = Decode(line, len, /*checkCrc=*/false);
    TEST_ASSERT_FALSE(f.valid);
}

static void test_empty_field_rejected()
{
    // Two commas in a row → empty field in position 2 (dynamic).
    // Field-walk should fail on the empty range.
    const char* line = "$AIRDAQ,64.12.B8,ADC,9842,,3942,4006,8C";
    const int len = static_cast<int>(std::strlen(line));
    // CRC check off — we only care about the field-walk guard.
    BoomFrame f = Decode(line, len, /*checkCrc=*/false);
    TEST_ASSERT_FALSE(f.valid);
}

static void test_negative_values_supported()
{
    // Boom alpha/beta can be negative on the wire.
    // Build the frame body WITHOUT the trailing separator, then compute
    // CRC over that body (the parser sums [0..len-3) — i.e. up to but
    // NOT including the separator). Then append "," + "XX".
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf),
        "$AIRDAQ,XX.XX.XX,ADC,9842,8152,-100,-200");   // no trailing ','
    int crc = 0;
    for (int i = 0; i < n; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    std::snprintf(buf + n, sizeof(buf) - n, ",%02X", crc);
    const int len = static_cast<int>(std::strlen(buf));
    BoomFrame f = Decode(buf, len, /*checkCrc=*/true);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_EQUAL_INT(9842, f.staticCounts);
    TEST_ASSERT_EQUAL_INT(8152, f.dynamicCounts);
    TEST_ASSERT_EQUAL_INT(-100, f.alphaCounts);
    TEST_ASSERT_EQUAL_INT(-200, f.betaCounts);
}

// ===========================================================================
// 4. Parity with legacy atoi/strtol path: compare the new parser against
//    a synthetic legacy implementation on a corpus.
// ===========================================================================

// Legacy reference: mirrors BoomSerial.cpp's strtok/atoi/strtol logic.
static BoomFrame legacyDecode(const char* in, int len, bool checkCrc)
{
    BoomFrame out;
    if (len < 21) return out;
    if (in[0] != '$') return out;

    // Need an in-place writable buffer for strtok.
    char tmp[160];
    if (len + 1 > static_cast<int>(sizeof(tmp))) return out;
    std::memcpy(tmp, in, len);
    tmp[len] = '\0';

    if (checkCrc)
    {
        // Test-corpus convention: len = strlen(line) after CR/LF strip, so
        // the CRC bytes are buf[len-2..len-1] and the separator (',' for
        // $AIRDAQ, '*' for the synthetic $BOOM form) is at buf[len-3]. Sum
        // covers everything before the separator: bytes [0..len-3).
        //
        // (The original BoomSerial.cpp kept the trailing CR in its buffer
        // and used offsets relative to that, so its in-buffer index math
        // looked different — but the bytes summed and the bytes compared
        // are the same; we just express the convention directly here.)
        int crc = 0;
        for (int i = 0; i < len - 3; i++)
            crc += static_cast<unsigned char>(tmp[i]);
        crc &= 0xFF;
        char hexCrc[3] = {tmp[len-2], tmp[len-1], '\0'};
        int wire = static_cast<int>(std::strtol(hexCrc, nullptr, 16));
        if (crc != wire) return out;
    }

    int parsed[4] = {0, 0, 0, 0};
    char* tok = std::strtok(tmp + 21, ",");
    int idx = 0;
    while (tok && idx < 4)
    {
        parsed[idx++] = std::atoi(tok);
        tok = std::strtok(nullptr, ",");
    }
    if (idx < 4) return out;

    out.staticCounts  = parsed[0];
    out.dynamicCounts = parsed[1];
    out.alphaCounts   = parsed[2];
    out.betaCounts    = parsed[3];
    out.valid         = true;
    return out;
}

static void test_parity_against_legacy_on_real_corpus()
{
    // Sample of real frames from boomchecksum.txt (selected unique values).
    const char* corpus[] = {
        "$AIRDAQ,64.12.B8,ADC,9842,8152,3942,4006,8C",
        "$AIRDAQ,64.12.B8,ADC,9840,8152,3942,4006,8A",
        "$AIRDAQ,64.12.B8,ADC,9836,8152,3942,4006,8F",
        "$AIRDAQ,64.12.B8,ADC,9840,8150,3942,4006,88",
        "$AIRDAQ,64.12.B8,ADC,9838,8154,3942,4006,93",
        "$AIRDAQ,64.12.B8,ADC,9842,8154,3942,4006,8E",
        "$AIRDAQ,64.12.B8,ADC,9834,8154,3942,4006,8F",
        "$AIRDAQ,64.12.B8,ADC,9838,8152,3942,4006,91",
        "$AIRDAQ,64.12.B8,ADC,9840,8154,3942,4006,8C",
    };

    for (const char* line : corpus)
    {
        const int len = static_cast<int>(std::strlen(line));
        BoomFrame legacy = legacyDecode(line, len, /*checkCrc=*/true);
        BoomFrame fast   = Decode(      line, len, /*checkCrc=*/true);
        TEST_ASSERT_EQUAL_INT(legacy.valid ? 1 : 0, fast.valid ? 1 : 0);
        if (legacy.valid)
        {
            TEST_ASSERT_EQUAL_INT(legacy.staticCounts,  fast.staticCounts);
            TEST_ASSERT_EQUAL_INT(legacy.dynamicCounts, fast.dynamicCounts);
            TEST_ASSERT_EQUAL_INT(legacy.alphaCounts,   fast.alphaCounts);
            TEST_ASSERT_EQUAL_INT(legacy.betaCounts,    fast.betaCounts);
        }
    }
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_real_airdaq_frame_decodes);
    RUN_TEST(test_real_airdaq_crc_failure_rejected);
    RUN_TEST(test_real_airdaq_crc_disabled_passes_corrupt_crc);
    RUN_TEST(test_real_airdaq_second_sample);
    RUN_TEST(test_synth_boom_frame_decodes);
    RUN_TEST(test_too_short_rejected);
    RUN_TEST(test_missing_dollar_rejected);
    RUN_TEST(test_missing_fields_rejected);
    RUN_TEST(test_empty_field_rejected);
    RUN_TEST(test_negative_values_supported);
    RUN_TEST(test_parity_against_legacy_on_real_corpus);
    return UNITY_END();
}
