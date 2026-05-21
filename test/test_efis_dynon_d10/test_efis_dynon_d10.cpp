// test_efis_dynon_d10.cpp
//
// Unit tests for DynonD10Parser.
//
// Fixtures are SYNTHETIC — constructed to match the field offsets and CRC
// algorithm documented in EfisSerial.cpp (EnDynonD10 branch).
//
// TODO(maintainer): add real captured frame fixtures from a live Dynon D10/D100.
//
// Frame format (53 bytes including CR+LF):
//   [0..1]  HH  hour
//   [2..3]  MM  minute
//   [4..5]  SS  second
//   [6..7]  CS  centiseconds
//   [8..11] Pitch ×10 (4 chars)
//   [12..16] Roll ×10 (5 chars)
//   [17..19] reserved (3 chars, spaces)
//   [20..23] IAS in m/s ×10 (4 chars)
//   [24..28] Palt in metres (5 chars)
//   [29..32] VSI in feet/sec ×10 (4 chars)
//   [33..35] LateralG ×100 (3 chars)
//   [36..38] VerticalG ×10 (3 chars)
//   [39..40] PercentLift (2 chars)
//   [41..44] reserved (4 chars)
//   [45..47] status (3 chars, hex nibble at [46])
//   [48]     reserved
//   [49..50] CRC hex (2 chars), sum bytes 0..48 mod 256
//   [51]     0x0D
//   [52]     0x0A

#include <unity.h>
#include <efis/DynonD10.h>
#include <types/EfisFrame.h>

#include <cstring>
#include <cstdio>
#include <cmath>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::efis::DynonD10Parser;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a 53-byte D10 frame.
// iasMs: IAS in m/s; altM: altitude in metres; vsiFtS: VSI in feet/sec;
// statusNibble: the hex nibble at position 46 (bit 0 = alt/vsi valid).
static void putField(char* buf, int pos, const char* tmp, int n)
{
    for (int i = 0; i < n; i++) buf[pos + i] = tmp[i];
}

static void buildD10Frame(char buf[53],
                           float pitchDeg, float rollDeg,
                           float iasMs, float altM, float vsiFtS,
                           float lateralG, float verticalG, int percentLift,
                           int statusNibble)
{
    char tmp[16];
    for (int i = 0; i < 51; i++) buf[i] = ' ';

    snprintf(tmp, sizeof(tmp), "%02d", 12);  putField(buf,  0, tmp, 2);  // HH
    snprintf(tmp, sizeof(tmp), "%02d", 34);  putField(buf,  2, tmp, 2);  // MM
    snprintf(tmp, sizeof(tmp), "%02d", 56);  putField(buf,  4, tmp, 2);  // SS
    snprintf(tmp, sizeof(tmp), "%02d", 78);  putField(buf,  6, tmp, 2);  // CS

    snprintf(tmp, sizeof(tmp), "%+04d", static_cast<int>(pitchDeg * 10.0f));
    putField(buf, 8, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%+05d", static_cast<int>(rollDeg * 10.0f));
    putField(buf, 12, tmp, 5);

    snprintf(tmp, sizeof(tmp), "%04d", static_cast<int>(iasMs * 10.0f));
    putField(buf, 20, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%05d", static_cast<int>(altM));
    putField(buf, 24, tmp, 5);
    snprintf(tmp, sizeof(tmp), "%04d", static_cast<int>(vsiFtS * 10.0f));
    putField(buf, 29, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%03d", static_cast<int>(lateralG  * 100.0f));
    putField(buf, 33, tmp, 3);
    snprintf(tmp, sizeof(tmp), "%03d", static_cast<int>(verticalG * 10.0f));
    putField(buf, 36, tmp, 3);
    snprintf(tmp, sizeof(tmp), "%02d", percentLift);
    putField(buf, 39, tmp, 2);
    // status byte: space at [45], nibble at [46], space at [47], space at [48]
    snprintf(tmp, sizeof(tmp), "%X", statusNibble & 0xF);
    putField(buf, 46, tmp, 1);

    int crc = 0;
    for (int i = 0; i <= 48; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);  putField(buf, 49, tmp, 2);
    buf[51] = '\r';
    buf[52] = '\n';
}

// Feed a '\n' first (to prime lineStart_), then the frame.
static std::optional<EfisFrame> primeAndFeed(DynonD10Parser& p,
                                             const char* buf, int len)
{
    p.FeedByte('\n');   // prime lineStart_
    std::optional<EfisFrame> result;
    for (int i = 0; i < len; i++)
    {
        p.FeedByte(static_cast<uint8_t>(buf[i]));
        auto f = p.TakeFrame();
        if (f) result = f;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_d10_valid_frame_ias(void)
{
    char buf[53];
    // IAS = 56.0 m/s = 108.9 kt
    buildD10Frame(buf, 2.0f, 5.0f, 56.0f, 1676.4f, 0.0f, 0.0f, 1.0f, 55, 0x3);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    // 56.0 m/s * 1.94384 = 108.95 kt
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 108.95f, frame->iasKt);
}

void test_d10_valid_frame_pitch(void)
{
    char buf[53];
    buildD10Frame(buf, 3.5f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 3.5f, frame->pitchDeg);
}

void test_d10_valid_frame_roll(void)
{
    char buf[53];
    buildD10Frame(buf, 0.0f, -15.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, -15.0f, frame->rollDeg);
}

void test_d10_status_bit0_enables_palt_vsi(void)
{
    char buf[53];
    // 1676.4 m = 5499.8 ft; vsi 5.0 ft/s * 60 = 300 fpm
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1676.4f, 5.0f, 0.0f, 1.0f, 0, 0x1);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 5500.0f, frame->paltFt);
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 300.0f, frame->vsiFpm);
}

void test_d10_status_bit0_zero_leaves_palt_absent(void)
{
    // When status bit 0 = 0 the D10 is signalling "Palt/VSI not valid this
    // frame — hold your last values". The parser marks both fields absent
    // (NaN) so applyFrame() will preserve the prior suEfis values rather
    // than overwrite them with 0.
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1676.4f, 5.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FALSE(std::isfinite(frame->paltFt));
    TEST_ASSERT_FALSE(std::isfinite(frame->vsiFpm));
}

void test_d10_source_is_dynon(void)
{
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Dynon),
                          static_cast<int>(frame->source));
}

void test_d10_truncated_no_output(void)
{
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    parser.FeedByte('\n');
    for (int i = 0; i < 40; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_d10_corrupted_crc_no_output(void)
{
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    buf[49] = 'Z';   // corrupt CRC
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_d10_wrong_length_no_output(void)
{
    // 52-byte frame (one short) should be ignored.
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 52);   // no trailing LF
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_d10_reset_clears_state(void)
{
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    parser.FeedByte('\n');
    for (int i = 0; i < 30; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));
    parser.Reset();
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_d10_two_frames_both_parse(void)
{
    char buf1[53], buf2[53];
    buildD10Frame(buf1, 2.0f, 5.0f,  50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 55, 0x0);
    buildD10Frame(buf2, 4.0f, -8.0f, 55.0f, 1600.0f, 0.0f, 0.0f, 1.0f, 60, 0x0);
    DynonD10Parser parser;
    auto f1 = primeAndFeed(parser, buf1, 53);
    // Prime again: parser expects '\n' before next line
    parser.FeedByte('\n');
    std::optional<EfisFrame> f2;
    for (int i = 0; i < 53; i++)
    {
        parser.FeedByte(static_cast<uint8_t>(buf2[i]));
        auto f = parser.TakeFrame();
        if (f) f2 = f;
    }
    TEST_ASSERT_TRUE(f1.has_value());
    TEST_ASSERT_TRUE(f2.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 2.0f, f1->pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 4.0f, f2->pitchDeg);
}

// Noise on the serial line: arbitrary bytes without a line terminator should
// not hang the parser. The overflow guard at FeedByte resets bufLen_ = 0 when
// it exceeds 230. After overflow, a subsequent valid '\n'-primed frame must
// parse cleanly.
void test_d10_buffer_overflow_resets_and_recovers(void)
{
    DynonD10Parser parser;
    parser.FeedByte('\n');   // prime lineStart_
    // Feed 250 junk bytes (no '\n' terminator) — triggers the reset at > 230.
    for (int i = 0; i < 250; i++)
        parser.FeedByte(static_cast<uint8_t>('x'));
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());

    // Now a valid frame should still parse.
    char buf[53];
    buildD10Frame(buf, 3.0f, 7.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 42, 0x0);
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 3.0f, frame->pitchDeg);
}

void test_d10_time_of_day_round_trip(void)
{
    // buildD10Frame writes "12345678" at offsets 0..7 — HH=12 MM=34
    // SS=56 CS=78. The parser must populate timeOfDayHms with the
    // canonical "HH:MM:SS.FF" form so the sidecar metadata receives
    // a non-empty wall-clock stamp.
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("12:34:56.78", frame->timeOfDayHms);
}

void test_d10_time_of_day_non_digits_leave_empty(void)
{
    // Garbled time bytes (e.g., -- placeholders during boot) should
    // leave timeOfDayHms empty so the sidecar does not record a junk
    // stamp.
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    buf[0] = '-'; buf[1] = '-';   // overwrite HH digits

    // Recompute CRC after corruption so the frame still validates and
    // exercises the time-validity guard, not the CRC failure branch.
    int crc = 0;
    for (int i = 0; i <= 48; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[49] = tmp[0]; buf[50] = tmp[1];

    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

void test_d10_time_of_day_out_of_range_leaves_empty(void)
{
    // ASCII-digit bytes that decode to HH > 23 / MM > 59 / SS > 59 /
    // FF > 99 should leave timeOfDayHms empty rather than write a
    // nonsensical wall-clock stamp.  HH=99 here.
    char buf[53];
    buildD10Frame(buf, 0.0f, 0.0f, 50.0f, 1500.0f, 0.0f, 0.0f, 1.0f, 0, 0x0);
    buf[0] = '9'; buf[1] = '9';   // HH=99 (invalid)

    int crc = 0;
    for (int i = 0; i <= 48; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[49] = tmp[0]; buf[50] = tmp[1];

    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

// EfisFrame::fieldsPresent bit assertions — see DynonSkyview test for rationale.
void test_d10_presence_bits_set_on_valid_frame(void)
{
    char buf[53];
    buildD10Frame(buf, 2.0f, 5.0f, 56.0f, 1676.4f, 0.0f, 0.0f, 1.0f, 55, 0x3);
    DynonD10Parser parser;
    auto frame = primeAndFeed(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    const uint32_t fp = frame->fieldsPresent;
    using namespace onspeed::EfisField;
    TEST_ASSERT_TRUE(fp & Ias);
    TEST_ASSERT_TRUE(fp & Pitch);
    TEST_ASSERT_TRUE(fp & Roll);
    TEST_ASSERT_TRUE(fp & LateralG);
    TEST_ASSERT_TRUE(fp & VerticalG);
    TEST_ASSERT_TRUE(fp & AoaPercent);
    TEST_ASSERT_TRUE(fp & Palt);   // status bit set
    TEST_ASSERT_TRUE(fp & Vsi);    // status bit set
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_d10_valid_frame_ias);
    RUN_TEST(test_d10_valid_frame_pitch);
    RUN_TEST(test_d10_valid_frame_roll);
    RUN_TEST(test_d10_status_bit0_enables_palt_vsi);
    RUN_TEST(test_d10_status_bit0_zero_leaves_palt_absent);
    RUN_TEST(test_d10_source_is_dynon);
    RUN_TEST(test_d10_truncated_no_output);
    RUN_TEST(test_d10_corrupted_crc_no_output);
    RUN_TEST(test_d10_wrong_length_no_output);
    RUN_TEST(test_d10_reset_clears_state);
    RUN_TEST(test_d10_two_frames_both_parse);
    RUN_TEST(test_d10_buffer_overflow_resets_and_recovers);
    RUN_TEST(test_d10_time_of_day_round_trip);
    RUN_TEST(test_d10_time_of_day_non_digits_leave_empty);
    RUN_TEST(test_d10_time_of_day_out_of_range_leaves_empty);
    RUN_TEST(test_d10_presence_bits_set_on_valid_frame);
    return UNITY_END();
}
