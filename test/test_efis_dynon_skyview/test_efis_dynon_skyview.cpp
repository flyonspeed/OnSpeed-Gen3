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
                              int vsiFpm, float oatC, float tasKt,
                              int hh = 12, int mm = 34, int ss = 56, int cs = 78)
{
    char tmp[16];

    for (int i = 0; i < 72; i++) buf[i] = ' ';
    buf[0] = '!'; buf[1] = '1'; buf[2] = ' ';

    snprintf(tmp, sizeof(tmp), "%02d", hh);   putField(buf,  3, tmp, 2);  // HH
    snprintf(tmp, sizeof(tmp), "%02d", mm);   putField(buf,  5, tmp, 2);  // MM
    snprintf(tmp, sizeof(tmp), "%02d", ss);   putField(buf,  7, tmp, 2);  // SS
    snprintf(tmp, sizeof(tmp), "%02d", cs);   putField(buf,  9, tmp, 2);  // CS

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

// Overwrite the 8 time-field bytes at buf[3..10] with the given 8-char string.
// Lets tests exercise the all-dashes ('--------') sentinel without rebuilding
// the whole frame. Recomputes the CRC so the frame still parses.
static void patchTimeField(char buf[74], const char* eightChars)
{
    for (int i = 0; i < 8; i++) buf[3 + i] = eightChars[i];
    int crc = 0;
    for (int i = 0; i <= 69; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[3];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[70] = tmp[0];
    buf[71] = tmp[1];
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
// The parser validates the CRC, marks the source, and decodes the
// per-flight engine values (RPM, MAP, fuel flow, fuel remaining,
// percent power) into the EfisFrame engine fields. Covers
// DynonSkyview.cpp DecodeEms().
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

// Build an EMS frame with realistic engine values. Field offsets and
// scales come from the Dynon SkyView serial spec:
//   bytes 18..21 (4): RPM, ÷1
//   bytes 26..28 (3): MAP × 10 (inHg)
//   bytes 29..31 (3): FuelFlow × 10 (gph)
//   bytes 44..46 (3): FuelRemaining × 10 (gallons)
//   bytes 217..219 (3): PercentPower, ÷1
static void buildEmsFrameWithEngine(char buf[225],
                                    int rpm, float mapInHg, float ffGph,
                                    float frGal, int pctPower)
{
    for (int i = 0; i < 223; i++) buf[i] = ' ';
    buf[0] = '!'; buf[1] = '3';
    char tmp[8];
    snprintf(tmp, sizeof(tmp), "%04d", rpm);
    for (int i = 0; i < 4; i++) buf[18 + i] = tmp[i];
    snprintf(tmp, sizeof(tmp), "%03d", static_cast<int>(mapInHg * 10.0f + 0.5f));
    for (int i = 0; i < 3; i++) buf[26 + i] = tmp[i];
    snprintf(tmp, sizeof(tmp), "%03d", static_cast<int>(ffGph * 10.0f + 0.5f));
    for (int i = 0; i < 3; i++) buf[29 + i] = tmp[i];
    snprintf(tmp, sizeof(tmp), "%03d", static_cast<int>(frGal * 10.0f + 0.5f));
    for (int i = 0; i < 3; i++) buf[44 + i] = tmp[i];
    snprintf(tmp, sizeof(tmp), "%03d", pctPower);
    for (int i = 0; i < 3; i++) buf[217 + i] = tmp[i];
    int crc = 0;
    for (int i = 0; i <= 220; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[221] = tmp[0]; buf[222] = tmp[1];
    buf[223] = '\r'; buf[224] = '\n';
}

void test_ems_engine_fields_round_trip(void)
{
    char buf[225];
    buildEmsFrameWithEngine(buf,
                            /*rpm*/ 2400,
                            /*mapInHg*/ 24.6f,
                            /*ffGph*/ 9.8f,
                            /*frGal*/ 35.4f,
                            /*pctPower*/ 78);
    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 225);
    TEST_ASSERT_TRUE(frame.has_value());

    TEST_ASSERT_EQUAL_FLOAT(2400.0f, frame->rpm);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 24.6f, frame->mapInchHg);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 9.8f, frame->fuelFlowGph);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 35.4f, frame->fuelRemainingGal);
    TEST_ASSERT_EQUAL_FLOAT(78.0f, frame->percentPower);
}

void test_ems_engine_sentinels_become_nan(void)
{
    // SkyView emits "XXX"/"XXXX" placeholders when a sensor isn't
    // present or hasn't reported. Those must decode to NaN so the
    // applyFrame() consumer holds the previous suEfis value.
    char buf[225];
    for (int i = 0; i < 223; i++) buf[i] = ' ';
    buf[0] = '!'; buf[1] = '3';
    // Place sentinels at each engine field's offsets.
    buf[18] = 'X'; buf[19] = 'X'; buf[20] = 'X'; buf[21] = 'X';   // RPM
    buf[26] = 'X'; buf[27] = 'X'; buf[28] = 'X';                  // MAP
    buf[29] = 'X'; buf[30] = 'X'; buf[31] = 'X';                  // FF
    buf[44] = 'X'; buf[45] = 'X'; buf[46] = 'X';                  // FR
    buf[217] = 'X'; buf[218] = 'X'; buf[219] = 'X';               // %Power
    int crc = 0;
    for (int i = 0; i <= 220; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[221] = tmp[0]; buf[222] = tmp[1];
    buf[223] = '\r'; buf[224] = '\n';

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 225);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FALSE(std::isfinite(frame->rpm));
    TEST_ASSERT_FALSE(std::isfinite(frame->mapInchHg));
    TEST_ASSERT_FALSE(std::isfinite(frame->fuelFlowGph));
    TEST_ASSERT_FALSE(std::isfinite(frame->fuelRemainingGal));
    TEST_ASSERT_FALSE(std::isfinite(frame->percentPower));
}

// ---------------------------------------------------------------------------
// Buffer-overflow reset: noise on the serial line without a line terminator
// should not hang the parser. After overflow (bufLen_ > 230), a subsequent
// valid frame must still parse.
// ---------------------------------------------------------------------------

void test_buffer_overflow_resets_and_recovers(void)
{
    DynonSkyviewParser parser;
    // Start with '!' to enter collection mode, then feed 250 junk bytes with
    // no '\n' terminator.  The overflow guard at bufLen_ > 230 resets.
    parser.FeedByte(static_cast<uint8_t>('!'));
    for (int i = 0; i < 250; i++)
        parser.FeedByte(static_cast<uint8_t>('x'));
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());

    // A valid frame must still parse.
    char buf[74];
    buildAdahrsFrame(buf, 1.0f, 2.0f, 180, 100.0f, 3000, 0.0f, 1.0f, 40, 0, 10.0f, 105.0f);
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 100.0f, frame->iasKt);
}

// ---------------------------------------------------------------------------
// Sentinel-int parsing: the heading field is an int-typed field. When Dynon
// emits 'XXX' for an unavailable heading, the parser must leave the field
// at kEfisFieldAbsent (NaN). Covers parseFieldInt's sentinel-match branch
// (DynonSkyview.cpp:42, which parseFieldFloat's existing sentinel test does
// NOT cover).
// ---------------------------------------------------------------------------

void test_adahrs_sentinel_heading_leaves_field_absent(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    // Overwrite heading (bytes 20..22) with the sentinel.
    buf[20] = 'X'; buf[21] = 'X'; buf[22] = 'X';
    // Recompute CRC so the rest of the frame is valid.
    int crc = 0;
    for (int i = 0; i <= 69; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[70] = tmp[0]; buf[71] = tmp[1];

    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FALSE(std::isfinite(frame->headingDeg));
    // Other fields (parsed via parseFieldFloat) remain finite.
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 2.3f, frame->pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
}

static void test_adahrs_system_time_valid()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     14, 32, 47, 12);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("14:32:47", frame->timeOfDayHms);
}

static void test_adahrs_system_time_all_dashes()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f);
    patchTimeField(buf, "--------");
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

static void test_adahrs_system_time_partial_dashes()
{
    // Any dash in the HHMMSS portion = treat as absent.
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f);
    patchTimeField(buf, "14----99");
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

static void test_adahrs_system_time_out_of_range()
{
    // HH=25 is out of range (0..23). Treat as absent.
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     25, 30, 30, 0);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

static void test_adahrs_system_time_mm_out_of_range()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     14, 60, 30, 0);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

static void test_adahrs_system_time_ss_out_of_range()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     14, 30, 60, 0);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

static void test_adahrs_system_time_midnight()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     0, 0, 0, 0);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("00:00:00", frame->timeOfDayHms);
}

static void test_adahrs_system_time_one_second_before_midnight()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     23, 59, 59, 0);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("23:59:59", frame->timeOfDayHms);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// EfisFrame::fieldsPresent bit assertions. EfisSerialPort::applyFrame()
// branches on these bits instead of std::isfinite(); a parser that
// silently fails to set a bit would cause the consumer to hold-last
// rather than apply, with no other test catching it (the float-value
// tests would still pass because the field still got written).
// ---------------------------------------------------------------------------

void test_adahrs_presence_bits_set_on_valid_frame(void)
{
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    const uint32_t fp = frame->fieldsPresent;
    using namespace onspeed::EfisField;
    TEST_ASSERT_TRUE(fp & Ias);
    TEST_ASSERT_TRUE(fp & Pitch);
    TEST_ASSERT_TRUE(fp & Roll);
    TEST_ASSERT_TRUE(fp & Heading);
    TEST_ASSERT_TRUE(fp & Palt);
    TEST_ASSERT_TRUE(fp & AoaPercent);
    TEST_ASSERT_TRUE(fp & LateralG);
    TEST_ASSERT_TRUE(fp & VerticalG);
    TEST_ASSERT_TRUE(fp & Vsi);
    TEST_ASSERT_TRUE(fp & Tas);
    TEST_ASSERT_TRUE(fp & OatCelsius);
}

void test_ems_presence_bits_set_on_valid_frame(void)
{
    char buf[225];
    buildEmsFrame(buf);
    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 225);
    TEST_ASSERT_TRUE(frame.has_value());
    const uint32_t fp = frame->fieldsPresent;
    using namespace onspeed::EfisField;
    TEST_ASSERT_TRUE(fp & Rpm);
    TEST_ASSERT_TRUE(fp & MapInchHg);
    TEST_ASSERT_TRUE(fp & FuelFlowGph);
    TEST_ASSERT_TRUE(fp & FuelRemainingGal);
    TEST_ASSERT_TRUE(fp & PercentPower);
    // EMS frame carries no airspeed/attitude — those bits must NOT be set.
    TEST_ASSERT_FALSE(fp & Ias);
    TEST_ASSERT_FALSE(fp & Pitch);
}

void test_adahrs_sentinel_field_clears_presence_bit(void)
{
    // Build a valid frame, then overwrite the IAS field with the
    // sentinel and re-stamp the CRC. The IAS bit must NOT be set on
    // the decoded frame; all other bits should remain set.
    char buf[74];
    buildAdahrsFrame(buf, 2.3f, 5.0f, 270, 110.0f, 5500, 0.10f, 1.0f, 62, 5000, 8.0f, 118.0f);
    buf[23] = 'X'; buf[24] = 'X'; buf[25] = 'X'; buf[26] = 'X';
    // Recompute checksum over bytes 0..69.
    int crc = 0;
    for (int i = 0; i <= 69; i++) crc += (unsigned char)buf[i];
    crc &= 0xFF;
    char hex[3]; snprintf(hex, sizeof(hex), "%02X", crc);
    buf[70] = hex[0]; buf[71] = hex[1];
    DynonSkyviewParser parser;
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    using namespace onspeed::EfisField;
    TEST_ASSERT_FALSE(frame->fieldsPresent & Ias);
    TEST_ASSERT_TRUE(frame->fieldsPresent  & Pitch);   // unchanged
    TEST_ASSERT_TRUE(frame->fieldsPresent  & Roll);
}

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
    RUN_TEST(test_ems_engine_fields_round_trip);
    RUN_TEST(test_ems_engine_sentinels_become_nan);
    RUN_TEST(test_buffer_overflow_resets_and_recovers);
    RUN_TEST(test_adahrs_sentinel_heading_leaves_field_absent);
    RUN_TEST(test_adahrs_system_time_valid);
    RUN_TEST(test_adahrs_system_time_all_dashes);
    RUN_TEST(test_adahrs_system_time_partial_dashes);
    RUN_TEST(test_adahrs_system_time_out_of_range);
    RUN_TEST(test_adahrs_system_time_mm_out_of_range);
    RUN_TEST(test_adahrs_system_time_ss_out_of_range);
    RUN_TEST(test_adahrs_system_time_midnight);
    RUN_TEST(test_adahrs_system_time_one_second_before_midnight);
    RUN_TEST(test_adahrs_presence_bits_set_on_valid_frame);
    RUN_TEST(test_ems_presence_bits_set_on_valid_frame);
    RUN_TEST(test_adahrs_sentinel_field_clears_presence_bit);
    return UNITY_END();
}
