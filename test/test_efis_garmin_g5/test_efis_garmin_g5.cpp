// test_efis_garmin_g5.cpp
//
// Unit tests for GarminG5Parser.
//
// Fixtures are SYNTHETIC.
// TODO(maintainer): replace with real captured frames from a live Garmin G5.
//
// Frame format (=11, 59 bytes including CR+LF):
//   [0]     '='
//   [1]     '1'
//   [2]     '1'
//   [3..4]  HH  hour
//   [5..6]  MM  minute
//   [7..8]  SS  second
//   [9..10] CS  centiseconds
//   [11..14] Pitch ×10 (4 chars, sentinel '____')
//   [15..19] Roll ×10 (5 chars, sentinel '_____')
//   [20..22] Heading (3 chars, sentinel '___')
//   [23..26] IAS ×10 (4 chars, sentinel '____')
//   [27..32] Palt (6 chars, sentinel '______')
//   [33..36] reserved (4 chars)
//   [37..39] LateralG ×100 (3 chars, sentinel '___')
//   [40..42] VerticalG ×10 (3 chars, sentinel '___')
//   [43..44] reserved (2 chars)    <- G5 has no AOA% field
//   [45..48] VSI ×10 (4 chars, sentinel '____')
//   [49..51] reserved (3 chars)
//   [52..55] reserved (4 chars)
//   [55..56] CRC hex (sum bytes 0..54 mod 256)
//   [57]     0x0D
//   [58]     0x0A

#include <unity.h>
#include <efis/GarminG5.h>
#include <types/EfisFrame.h>

#include <cmath>
#include <cstring>
#include <cstdio>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::efis::GarminG5Parser;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void putField(char* buf, int pos, const char* tmp, int n)
{
    for (int i = 0; i < n; i++) buf[pos + i] = tmp[i];
}

static void buildG5Frame(char buf[59],
                          float pitchDeg, float rollDeg, int headingDeg,
                          float iasKt, int paltFt,
                          float lateralG, float verticalG, int vsiFpm)
{
    char tmp[16];
    for (int i = 0; i < 57; i++) buf[i] = ' ';
    buf[0] = '='; buf[1] = '1'; buf[2] = '1';

    snprintf(tmp, sizeof(tmp), "%02d", 12);  putField(buf,  3, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 34);  putField(buf,  5, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 56);  putField(buf,  7, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 78);  putField(buf,  9, tmp, 2);

    snprintf(tmp, sizeof(tmp), "%+04d", static_cast<int>(pitchDeg * 10.0f));
    putField(buf, 11, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%+05d", static_cast<int>(rollDeg * 10.0f));
    putField(buf, 15, tmp, 5);
    snprintf(tmp, sizeof(tmp), "%03d", headingDeg);   putField(buf, 20, tmp, 3);
    snprintf(tmp, sizeof(tmp), "%04d", static_cast<int>(iasKt * 10.0f));
    putField(buf, 23, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%06d", paltFt);       putField(buf, 27, tmp, 6);
    snprintf(tmp, sizeof(tmp), "%03d", static_cast<int>(lateralG * 100.0f));
    putField(buf, 37, tmp, 3);
    snprintf(tmp, sizeof(tmp), "%03d", static_cast<int>(verticalG * 10.0f));
    putField(buf, 40, tmp, 3);
    snprintf(tmp, sizeof(tmp), "%04d", vsiFpm / 10);  putField(buf, 45, tmp, 4);

    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);          putField(buf, 55, tmp, 2);
    buf[57] = '\r';
    buf[58] = '\n';
}

static std::optional<EfisFrame> feedAll(GarminG5Parser& p,
                                        const char* buf, int len)
{
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

void test_g5_valid_frame_ias(void)
{
    char buf[59];
    buildG5Frame(buf, 2.0f, 5.0f, 270, 110.0f, 5500, 0.1f, 1.0f, 500);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
}

void test_g5_valid_frame_pitch(void)
{
    char buf[59];
    buildG5Frame(buf, 3.0f, 0.0f, 90, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 3.0f, frame->pitchDeg);
}

void test_g5_valid_frame_heading(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 180, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 180.0f, frame->headingDeg);
}

void test_g5_no_aoa_field(void)
{
    // G5 does not output AOA%; the field stays at kEfisFieldAbsent (NaN)
    // so applyFrame() holds the prior suEfis value instead of overwriting.
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FALSE(std::isfinite(frame->aoaPercent));
}

void test_g5_source_is_garmin(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Garmin),
                          static_cast<int>(frame->source));
}

void test_g5_truncated_no_output(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    for (int i = 0; i < 40; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_g5_corrupted_crc_no_output(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    buf[55] = 'Z';
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_g5_wrong_magic_discarded(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    buf[0] = '!';   // Dynon magic, not Garmin
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_g5_garbage_then_valid_parses(void)
{
    const uint8_t garbage[] = {0x00, 0xFF, 'A', 'B'};
    char buf[59];
    buildG5Frame(buf, 2.0f, 5.0f, 270, 110.0f, 5500, 0.1f, 1.0f, 500);

    GarminG5Parser parser;
    for (uint8_t b : garbage) parser.FeedByte(b);
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
}

void test_g5_reset_clears_state(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    for (int i = 0; i < 30; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));
    parser.Reset();
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_g5_take_frame_clears_pending(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    for (int i = 0; i < 59; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));
    auto first  = parser.TakeFrame();
    auto second = parser.TakeFrame();
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_FALSE(second.has_value());
}

void test_g5_palt_field(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 7500, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 7500.0f, frame->paltFt);
}

void test_g5_vsi_field(void)
{
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 2000);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    // VSI encoded as /10: 2000 fpm / 10 = 200, then * 10 = 2000 back
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 2000.0f, frame->vsiFpm);
}

// Noise on the serial line without a frame terminator: the parser must reset
// once bufLen_ > 230 so a subsequent valid frame still parses.
void test_g5_buffer_overflow_resets_and_recovers(void)
{
    GarminG5Parser parser;
    parser.FeedByte(static_cast<uint8_t>('='));
    for (int i = 0; i < 250; i++)
        parser.FeedByte(static_cast<uint8_t>('x'));
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());

    char buf[59];
    buildG5Frame(buf, 1.0f, 2.0f, 90, 95.0f, 3000, 0.0f, 1.0f, 0);
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 95.0f, frame->iasKt);
}

// 59-byte frame with leading '=' but wrong inner magic ('=XX...') exercises
// the Decode()-time magic check at GarminG5.cpp:108-109 (previously uncovered).
void test_g5_wrong_inner_magic_discarded_at_decode(void)
{
    GarminG5Parser parser;
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    buf[1] = 'X';   // was '1'
    buf[2] = 'X';   // was '1'
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_g5_time_of_day_round_trip(void)
{
    // buildG5Frame writes "12345678" at offsets 3..10 — HH=12 MM=34
    // SS=56 CS=78. The parser must populate timeOfDayHms with the
    // canonical HH:MM:SS.FF form for the sidecar metadata.
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("12:34:56.78", frame->timeOfDayHms);
}

void test_g5_time_of_day_non_digits_leave_empty(void)
{
    // Garbled time bytes should leave timeOfDayHms empty so the
    // sidecar does not record a junk stamp.
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    buf[3] = '-'; buf[4] = '-';   // overwrite HH digits

    // Recompute CRC after corruption so the frame still validates and
    // exercises the time-validity guard, not the CRC failure branch.
    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[55] = tmp[0]; buf[56] = tmp[1];

    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

void test_g5_time_of_day_out_of_range_leaves_empty(void)
{
    // ASCII-digit bytes that decode out-of-range (HH > 23 / MM > 59 /
    // SS > 59 / FF > 99) leave timeOfDayHms empty rather than write a
    // nonsensical wall-clock stamp.  HH=99 here.
    char buf[59];
    buildG5Frame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0);
    buf[3] = '9'; buf[4] = '9';   // HH=99 (invalid)

    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[55] = tmp[0]; buf[56] = tmp[1];

    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

// EfisFrame::fieldsPresent bit assertions — see DynonSkyview test for rationale.
void test_g5_presence_bits_set_on_valid_frame(void)
{
    char buf[59];
    buildG5Frame(buf, 2.0f, 5.0f, 270, 110.0f, 5500, 0.1f, 1.0f, 500);
    GarminG5Parser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    const uint32_t fp = frame->fieldsPresent;
    using namespace onspeed::EfisField;
    TEST_ASSERT_TRUE(fp & Ias);
    TEST_ASSERT_TRUE(fp & Pitch);
    TEST_ASSERT_TRUE(fp & Roll);
    TEST_ASSERT_TRUE(fp & Heading);
    TEST_ASSERT_TRUE(fp & LateralG);
    TEST_ASSERT_TRUE(fp & VerticalG);
    TEST_ASSERT_TRUE(fp & Palt);
    TEST_ASSERT_TRUE(fp & Vsi);
    // G5 does NOT output AOA percent — the bit must stay unset.
    TEST_ASSERT_FALSE(fp & AoaPercent);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_g5_valid_frame_ias);
    RUN_TEST(test_g5_valid_frame_pitch);
    RUN_TEST(test_g5_valid_frame_heading);
    RUN_TEST(test_g5_no_aoa_field);
    RUN_TEST(test_g5_source_is_garmin);
    RUN_TEST(test_g5_truncated_no_output);
    RUN_TEST(test_g5_corrupted_crc_no_output);
    RUN_TEST(test_g5_wrong_magic_discarded);
    RUN_TEST(test_g5_garbage_then_valid_parses);
    RUN_TEST(test_g5_reset_clears_state);
    RUN_TEST(test_g5_take_frame_clears_pending);
    RUN_TEST(test_g5_palt_field);
    RUN_TEST(test_g5_vsi_field);
    RUN_TEST(test_g5_buffer_overflow_resets_and_recovers);
    RUN_TEST(test_g5_wrong_inner_magic_discarded_at_decode);
    RUN_TEST(test_g5_time_of_day_round_trip);
    RUN_TEST(test_g5_time_of_day_non_digits_leave_empty);
    RUN_TEST(test_g5_time_of_day_out_of_range_leaves_empty);
    RUN_TEST(test_g5_presence_bits_set_on_valid_frame);
    return UNITY_END();
}
