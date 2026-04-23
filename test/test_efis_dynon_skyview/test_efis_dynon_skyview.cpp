// test_efis_dynon_skyview.cpp
//
// Unit tests for DynonSkyviewParser.
//
// Fixtures are SYNTHETIC — constructed to match the field offsets and CRC
// algorithm documented in EfisSerial.cpp. A real captured frame from a live
// Dynon SkyView would look identical in structure.
//
// TODO(maintainer): add real captured frame fixtures from a live SkyView
// (serial tap on N720AK or bench-box with SkyView simulator). Replace the
// synthetic frames with real captures to validate exact field encoding.
//
// Frame format (!1 ADAHRS, 74 bytes including CR+LF):
//   [0]   '!'
//   [1]   '1'
//   [2]   ' '   (space padding)
//   [3..4]  HH  (hour)
//   [5..6]  MM  (minute)
//   [7..8]  SS  (second)
//   [9..10] CS  (centiseconds)
//   [11..14] Pitch ×10, signed 4 chars e.g. "+023" = +2.3°
//   [15..19] Roll ×10, signed 5 chars e.g. "+0050" = +5.0°
//   [20..22] Heading, 3 chars e.g. "270"
//   [23..26] IAS ×10, 4 chars e.g. "1100" = 110.0 kt
//   [27..32] Palt, 6 chars e.g. "005500" = 5500 ft
//   [33..36] reserved (4 chars)
//   [37..39] LateralG ×100, 3 chars e.g. "010" = 0.10 g
//   [40..42] VerticalG ×10, 3 chars e.g. "010" = 1.0 g
//   [43..44] PercentLift, 2 chars e.g. "62" = 62%
//   [45..48] VSI ×10, 4 chars e.g. "0500" = 5000 fpm
//   [49..51] OAT, 3 chars e.g. "+08" = 8°C
//   [52..55] TAS ×10, 4 chars e.g. "1180" = 118.0 kt
//   [56..69] reserved
//   [70..71] CRC hex (2 chars), sum of bytes 0..69 mod 256
//   [72]     0x0D (CR)
//   [73]     0x0A (LF)

#include <unity.h>
#include <efis/DynonSkyview.h>
#include <types/EfisFrame.h>

#include <cmath>
#include <cstring>
#include <cstdio>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::efis::DynonSkyviewParser;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers: frame construction
// ---------------------------------------------------------------------------

// Build a synthetic !1 ADAHRS frame.
// The format is (!1)(space)(HH)(MM)(SS)(CS)(PPPP)(RRRRR)(HHH)(IIII)(AAAAAA)
// (RRRR)(LLL)(VVV)(PL)(VVVV)(OOO)(TTTT)(reserved to 70)(XX)(CR)(LF)
//
// We encode each field into its exact positions then compute and write the CRC.
// Write n chars of tmp into buf starting at pos, without NUL terminator.
static void putField(char* buf, int pos, const char* tmp, int n)
{
    for (int i = 0; i < n; i++)
        buf[pos + i] = tmp[i];
}

static void buildAdahrsFrame(char buf[74],
                              float pitchDeg, float rollDeg, int headingDeg,
                              float iasKt, int paltFt,
                              float lateralG, float verticalG,
                              int percentLift,
                              int vsiFpm, float oatC, float tasKt)
{
    char tmp[16];

    for (int i = 0; i < 72; i++) buf[i] = ' ';
    buf[0] = '!'; buf[1] = '1'; buf[2] = ' ';

    snprintf(tmp, sizeof(tmp), "%02d", 12);   putField(buf,  3, tmp, 2);  // HH
    snprintf(tmp, sizeof(tmp), "%02d", 34);   putField(buf,  5, tmp, 2);  // MM
    snprintf(tmp, sizeof(tmp), "%02d", 56);   putField(buf,  7, tmp, 2);  // SS
    snprintf(tmp, sizeof(tmp), "%02d", 78);   putField(buf,  9, tmp, 2);  // CS

    int pitch10 = static_cast<int>(pitchDeg * 10.0f);
    snprintf(tmp, sizeof(tmp), "%+04d", pitch10);   putField(buf, 11, tmp, 4);

    int roll10  = static_cast<int>(rollDeg * 10.0f);
    snprintf(tmp, sizeof(tmp), "%+05d", roll10);    putField(buf, 15, tmp, 5);

    snprintf(tmp, sizeof(tmp), "%03d", headingDeg); putField(buf, 20, tmp, 3);

    int ias10   = static_cast<int>(iasKt * 10.0f);
    snprintf(tmp, sizeof(tmp), "%04d", ias10);      putField(buf, 23, tmp, 4);

    snprintf(tmp, sizeof(tmp), "%06d", paltFt);     putField(buf, 27, tmp, 6);

    int lat100  = static_cast<int>(lateralG * 100.0f);
    snprintf(tmp, sizeof(tmp), "%03d", lat100);     putField(buf, 37, tmp, 3);

    int vert10  = static_cast<int>(verticalG * 10.0f);
    snprintf(tmp, sizeof(tmp), "%03d", vert10);     putField(buf, 40, tmp, 3);

    snprintf(tmp, sizeof(tmp), "%02d", percentLift); putField(buf, 43, tmp, 2);

    int vsi10   = vsiFpm / 10;
    snprintf(tmp, sizeof(tmp), "%04d", vsi10);      putField(buf, 45, tmp, 4);

    int oat     = static_cast<int>(oatC);
    snprintf(tmp, sizeof(tmp), "%+03d", oat);       putField(buf, 49, tmp, 3);

    int tas10   = static_cast<int>(tasKt * 10.0f);
    snprintf(tmp, sizeof(tmp), "%04d", tas10);      putField(buf, 52, tmp, 4);

    // [56..69] remain spaces.

    // CRC: sum bytes 0..69 mod 256
    int crc = 0;
    for (int i = 0; i <= 69; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);        putField(buf, 70, tmp, 2);

    buf[72] = '\r';
    buf[73] = '\n';
}

// Feed a buffer into the parser byte by byte.
static std::optional<EfisFrame> feedAll(DynonSkyviewParser& p,
                                        const char* buf, int len)
{
    std::optional<EfisFrame> result;
    for (int i = 0; i < len; i++)
    {
        p.FeedByte(static_cast<uint8_t>(buf[i]));
        auto f = p.TakeFrame();
        if (f)
            result = f;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_adahrs_valid_frame_ias(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
}

void test_adahrs_valid_frame_pitch(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 2.3f, frame->pitchDeg);
}

void test_adahrs_valid_frame_roll(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 5.0f, frame->rollDeg);
}

void test_adahrs_valid_frame_heading(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 270.0f, frame->headingDeg);
}

void test_adahrs_valid_frame_palt(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 5500.0f, frame->paltFt);
}

void test_adahrs_valid_frame_aoa_percent(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 62.0f, frame->aoaPercent);
}

void test_adahrs_valid_frame_source_is_dynon(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Dynon),
                          static_cast<int>(frame->source));
}

void test_truncated_frame_no_output(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    // Feed only first 40 bytes (no terminating LF yet)
    auto frame = feedAll(parser, buf, 40);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_corrupted_crc_no_output(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    // Corrupt the CRC bytes
    buf[70] = 'F';
    buf[71] = 'F';

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_wrong_magic_byte_discarded(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    // Change '!' to '@'
    buf[0] = '@';

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_garbage_then_valid_frame_parses(void)
{
    // Some garbage bytes, then a valid frame.
    uint8_t garbage[] = { 0x00, 0x01, 'X', 'Y', 0xFF };
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    feedAll(parser, reinterpret_cast<const char*>(garbage), 5);   // garbage
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
}

void test_wrong_length_no_output(void)
{
    // Build a frame but only 73 bytes (missing final LF)
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    buf[73] = 'X';   // replace LF — frame never terminates cleanly

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 73);   // 73 bytes, no LF
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_take_frame_clears_pending(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    // Feed bytes without consuming TakeFrame so the pending is still set.
    for (int i = 0; i < 74; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));

    auto first  = parser.TakeFrame();
    auto second = parser.TakeFrame();

    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_FALSE(second.has_value());
}

void test_reset_clears_state(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);

    DynonSkyviewParser parser;
    // Feed half the frame
    for (int i = 0; i < 40; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));

    parser.Reset();

    // Now feed the full valid frame — should parse cleanly.
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_two_adahrs_frames_both_parse(void)
{
    char buf1[74], buf2[74];
    buildAdahrsFrame(buf1, 2.3f,  5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    buildAdahrsFrame(buf2, 4.0f, -10.0f, 90, 120.0f, 6000, 0.05f, 1.2f, 70, 3000, 10.0f, 125.0f);

    DynonSkyviewParser parser;
    auto f1 = feedAll(parser, buf1, 74);
    auto f2 = feedAll(parser, buf2, 74);

    TEST_ASSERT_TRUE(f1.has_value());
    TEST_ASSERT_TRUE(f2.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, f1->iasKt);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 120.0f, f2->iasKt);
}

void test_vsi_field(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 80.0f, 3000, 0.0f, 1.0f, 0, 5000, 0.0f, 80.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    // VSI encoded as ×10: 5000 fpm / 10 = 500 stored, then * 10 = 5000 back
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 5000.0f, frame->vsiFpm);
}

void test_oat_field(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 80.0f, 3000, 0.0f, 1.0f, 0, 0, 15.0f, 80.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 15.0f, frame->oatCelsius);
}

void test_tas_field(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 80.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f, 95.0f);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);

    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 95.0f, frame->tasKt);
}

void test_adahrs_sentinel_ias_leaves_field_absent(void)
{
    // When the Dynon cannot compute a field it emits 'XXXX' (or the
    // per-width variant). The parser must leave the field at
    // kEfisFieldAbsent (NaN) so applyFrame() holds the prior suEfis value.
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    // Overwrite IAS with the 4-char sentinel.
    buf[23] = 'X'; buf[24] = 'X'; buf[25] = 'X'; buf[26] = 'X';
    // Recompute CRC so the frame is otherwise valid.
    int crc = 0;
    for (int i = 0; i <= 69; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[70] = tmp[0]; buf[71] = tmp[1];

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FALSE(std::isfinite(frame->iasKt));
    // Pitch was not sentineled so it remains finite.
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 2.3f, frame->pitchDeg);
}

// ---------------------------------------------------------------------------
// EMS frame (!3, 225 bytes) — engine/monitoring data.
// The OnSpeed firmware doesn't consume the EMS numeric fields (EfisFrame
// has no engine members), but the parser still validates the CRC and
// marks the source. Covers DynonSkyview.cpp:118-124, 170-186.
// ---------------------------------------------------------------------------

// Build a minimal valid !3 EMS frame. All body bytes zero-padded; only
// the magic '!3' prefix and checksum matter for parser branches we want
// to exercise.
static void buildEmsFrame(char buf[225])
{
    for (int i = 0; i < 223; i++) buf[i] = ' ';
    buf[0] = '!'; buf[1] = '3';
    // Optional: fill a realistic-shaped timestamp so the bytes aren't
    // all-spaces (no parser branch depends on it, but closer to real).
    char tmp[8];
    snprintf(tmp, sizeof(tmp), "%02d", 12); buf[3] = tmp[0]; buf[4] = tmp[1];
    snprintf(tmp, sizeof(tmp), "%02d", 34); buf[5] = tmp[0]; buf[6] = tmp[1];
    snprintf(tmp, sizeof(tmp), "%02d", 56); buf[7] = tmp[0]; buf[8] = tmp[1];
    snprintf(tmp, sizeof(tmp), "%02d", 78); buf[9] = tmp[0]; buf[10] = tmp[1];

    int crc = 0;
    for (int i = 0; i <= 220; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[221] = tmp[0]; buf[222] = tmp[1];
    buf[223] = '\r'; buf[224] = '\n';
}

void test_ems_valid_frame_produces_frame(void)
{
    char buf[225];
    buildEmsFrame(buf);

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 225);
    TEST_ASSERT_TRUE(frame.has_value());
    // Source should be tagged as Dynon.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Dynon),
                          static_cast<int>(frame->source));
    // EMS has no attitude/airspeed content — all numeric fields must be NaN
    // so the AHRS consumer holds prior values.
    TEST_ASSERT_FALSE(std::isfinite(frame->iasKt));
    TEST_ASSERT_FALSE(std::isfinite(frame->pitchDeg));
    TEST_ASSERT_FALSE(std::isfinite(frame->rollDeg));
    TEST_ASSERT_FALSE(std::isfinite(frame->paltFt));
    TEST_ASSERT_FALSE(std::isfinite(frame->oatCelsius));
}

void test_ems_bad_crc_discards_frame(void)
{
    char buf[225];
    buildEmsFrame(buf);
    // Corrupt the 2-char CRC at [221..222].
    buf[221] = '0'; buf[222] = '0';

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 225);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_ems_wrong_magic_ignored(void)
{
    // 225-byte buffer that isn't '!3' at the start — should be silently
    // discarded by the length+magic check in FlushLine().
    char buf[225];
    buildEmsFrame(buf);
    buf[1] = '9';   // now '!9' — neither '!1' nor '!3'
    // The line-length guard rejects this before CRC is even checked; no
    // frame should pop out.
    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 225);
    TEST_ASSERT_FALSE(frame.has_value());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_adahrs_valid_frame_ias);
    RUN_TEST(test_adahrs_valid_frame_pitch);
    RUN_TEST(test_adahrs_valid_frame_roll);
    RUN_TEST(test_adahrs_valid_frame_heading);
    RUN_TEST(test_adahrs_valid_frame_palt);
    RUN_TEST(test_adahrs_valid_frame_aoa_percent);
    RUN_TEST(test_adahrs_valid_frame_source_is_dynon);
    RUN_TEST(test_truncated_frame_no_output);
    RUN_TEST(test_corrupted_crc_no_output);
    RUN_TEST(test_wrong_magic_byte_discarded);
    RUN_TEST(test_garbage_then_valid_frame_parses);
    RUN_TEST(test_wrong_length_no_output);
    RUN_TEST(test_take_frame_clears_pending);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_two_adahrs_frames_both_parse);
    RUN_TEST(test_vsi_field);
    RUN_TEST(test_oat_field);
    RUN_TEST(test_tas_field);
    RUN_TEST(test_adahrs_sentinel_ias_leaves_field_absent);
    RUN_TEST(test_ems_valid_frame_produces_frame);
    RUN_TEST(test_ems_bad_crc_discards_frame);
    RUN_TEST(test_ems_wrong_magic_ignored);
    return UNITY_END();
}
