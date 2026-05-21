// test_efis_garmin_g3x.cpp
//
// Unit tests for GarminG3XParser.
//
// Fixtures are SYNTHETIC.
// TODO(maintainer): replace with real captured frames from a live Garmin G3X.
//
// =11 attitude frame (59 bytes): same offsets as G5 but also has AOA% at [43..44]
// and OAT at [49..51].
//
// =31 EMS frame (221 bytes): engine data.

#include <unity.h>
#include <efis/GarminG3X.h>
#include <types/EfisFrame.h>

#include <cstring>
#include <cstdio>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::efis::GarminG3XParser;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void putField(char* buf, int pos, const char* tmp, int n)
{
    for (int i = 0; i < n; i++) buf[pos + i] = tmp[i];
}

static void buildG3XAttFrame(char buf[59],
                              float pitchDeg, float rollDeg, int headingDeg,
                              float iasKt, int paltFt,
                              float lateralG, float verticalG,
                              int aoaPercent, int vsiFpm, float oatC)
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
    snprintf(tmp, sizeof(tmp), "%02d", aoaPercent);   putField(buf, 43, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%04d", vsiFpm / 10);  putField(buf, 45, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%+03d", static_cast<int>(oatC));
    putField(buf, 49, tmp, 3);

    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);          putField(buf, 55, tmp, 2);
    buf[57] = '\r';
    buf[58] = '\n';
}

// Build a minimal =31 EMS frame (221 bytes).
static void buildG3XEmsFrame(char buf[221])
{
    char tmp[16];
    for (int i = 0; i < 219; i++) buf[i] = ' ';
    buf[0] = '='; buf[1] = '3'; buf[2] = '1';
    putField(buf, 18, "2400", 4);
    putField(buf, 26, "240",  3);
    putField(buf, 29, "120",  3);
    putField(buf, 44, "500",  3);
    int crc = 0;
    for (int i = 0; i <= 216; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);  putField(buf, 217, tmp, 2);
    buf[219] = '\r';
    buf[220] = '\n';
}

static std::optional<EfisFrame> feedAll(GarminG3XParser& p,
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

void test_g3x_att_valid_ias(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500, 0.1f, 1.0f, 62, 500, 8.0f);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
}

void test_g3x_att_has_aoa_field(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500, 0.1f, 1.0f, 62, 500, 8.0f);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 62.0f, frame->aoaPercent);
}

void test_g3x_att_has_oat_field(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 15.0f);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 15.0f, frame->oatCelsius);
}

void test_g3x_source_is_garmin(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Garmin),
                          static_cast<int>(frame->source));
}

void test_g3x_ems_frame_parses(void)
{
    char buf[221];
    buildG3XEmsFrame(buf);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 221);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Garmin),
                          static_cast<int>(frame->source));
}

void test_g3x_ems_frame_leaves_attitude_airspeed_absent(void)
{
    // EMS carries engine data only — it must not overwrite the attitude
    // or airspeed fields that the =21 attitude frame populated. Those
    // EfisFrame fields should be kEfisFieldAbsent (NaN) so applyFrame()
    // preserves the prior suEfis values for them.
    char buf[221];
    buildG3XEmsFrame(buf);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 221);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FALSE(std::isfinite(frame->iasKt));
    TEST_ASSERT_FALSE(std::isfinite(frame->paltFt));
    TEST_ASSERT_FALSE(std::isfinite(frame->vsiFpm));
    TEST_ASSERT_FALSE(std::isfinite(frame->pitchDeg));
    TEST_ASSERT_FALSE(std::isfinite(frame->rollDeg));
    TEST_ASSERT_FALSE(std::isfinite(frame->verticalG));
    TEST_ASSERT_FALSE(std::isfinite(frame->lateralG));
    TEST_ASSERT_FALSE(std::isfinite(frame->oatCelsius));
}

void test_g3x_ems_engine_fields_round_trip(void)
{
    // buildG3XEmsFrame writes RPM=2400, MAP raw=240 (24.0 inHg),
    // FuelFlow raw=120 (12.0 gph), FuelRemaining raw=500 (50.0 gal).
    // G3X EMS does not carry PercentPower; that field stays absent.
    char buf[221];
    buildG3XEmsFrame(buf);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 221);
    TEST_ASSERT_TRUE(frame.has_value());

    TEST_ASSERT_EQUAL_FLOAT(2400.0f, frame->rpm);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 24.0f, frame->mapInchHg);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.0f, frame->fuelFlowGph);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 50.0f, frame->fuelRemainingGal);
    TEST_ASSERT_FALSE(std::isfinite(frame->percentPower));
}

void test_g3x_ems_engine_sentinels_become_nan(void)
{
    // G3X uses '_' as the absent marker. With sentinels in place each
    // engine field should decode to NaN so applyFrame() holds prior.
    char buf[221];
    for (int i = 0; i < 219; i++) buf[i] = ' ';
    buf[0] = '='; buf[1] = '3'; buf[2] = '1';
    putField(buf, 18, "____", 4);
    putField(buf, 26, "___",  3);
    putField(buf, 29, "___",  3);
    putField(buf, 44, "___",  3);
    int crc = 0;
    for (int i = 0; i <= 216; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    putField(buf, 217, tmp, 2);
    buf[219] = '\r';
    buf[220] = '\n';

    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 221);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FALSE(std::isfinite(frame->rpm));
    TEST_ASSERT_FALSE(std::isfinite(frame->mapInchHg));
    TEST_ASSERT_FALSE(std::isfinite(frame->fuelFlowGph));
    TEST_ASSERT_FALSE(std::isfinite(frame->fuelRemainingGal));
}

void test_g3x_att_time_of_day_round_trip(void)
{
    // buildG3XAttFrame writes "12345678" at offsets 3..10 — HH=12 MM=34
    // SS=56 CS=78. Parser populates timeOfDayHms with HH:MM:SS.FF for
    // the sidecar metadata stamp.
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("12:34:56.78", frame->timeOfDayHms);
}

void test_g3x_att_time_of_day_non_digits_leave_empty(void)
{
    // Garbled time bytes leave timeOfDayHms empty.
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    buf[3] = '-'; buf[4] = '-';
    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    putField(buf, 55, tmp, 2);

    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

void test_g3x_att_time_of_day_out_of_range_leaves_empty(void)
{
    // ASCII-digit bytes that decode out-of-range (HH > 23 / MM > 59 /
    // SS > 59 / FF > 99) leave timeOfDayHms empty rather than write a
    // nonsensical wall-clock stamp.  HH=99 here.
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    buf[3] = '9'; buf[4] = '9';   // HH=99 (invalid)
    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[4];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    putField(buf, 55, tmp, 2);

    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

void test_g3x_interleaved_att_and_ems(void)
{
    char att[59];
    char ems[221];
    buildG3XAttFrame(att, 2.0f, 0.0f, 90, 100.0f, 3000, 0.0f, 1.0f, 55, 0, 5.0f);
    buildG3XEmsFrame(ems);

    GarminG3XParser parser;
    auto f1 = feedAll(parser, att, 59);
    auto f2 = feedAll(parser, ems, 221);

    TEST_ASSERT_TRUE(f1.has_value());
    TEST_ASSERT_TRUE(f2.has_value());
}

void test_g3x_truncated_no_output(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    GarminG3XParser parser;
    for (int i = 0; i < 40; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_g3x_corrupted_crc_no_output(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    buf[55] = 'Z';
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_g3x_wrong_magic_discarded(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    buf[0] = '!';
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_g3x_reset_clears_state(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000, 0.0f, 1.0f, 0, 0, 0.0f);
    GarminG3XParser parser;
    for (int i = 0; i < 30; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));
    parser.Reset();
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_g3x_garbage_then_valid_parses(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500, 0.1f, 1.0f, 62, 500, 8.0f);
    GarminG3XParser parser;
    const uint8_t garbage[] = {0x00, 0xFF, 'X'};
    for (uint8_t b : garbage) parser.FeedByte(b);
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
}

void test_g3x_ems_corrupted_crc_no_output(void)
{
    char buf[221];
    buildG3XEmsFrame(buf);
    buf[217] = 'Z';
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 221);
    TEST_ASSERT_FALSE(frame.has_value());
}

// Noise on the serial line without a frame terminator: the parser must reset
// once bufLen_ > 230 so a subsequent valid frame still parses.
void test_g3x_buffer_overflow_resets_and_recovers(void)
{
    GarminG3XParser parser;
    // Start with '=' to enter collection mode, then feed 250 junk bytes with
    // no '\n' terminator.
    parser.FeedByte(static_cast<uint8_t>('='));
    for (int i = 0; i < 250; i++)
        parser.FeedByte(static_cast<uint8_t>('x'));
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());

    // A valid 59-byte attitude frame must still parse.
    char buf[59];
    buildG3XAttFrame(buf, 1.5f, 3.0f, 120, 95.0f, 2500, 0.0f, 1.0f, 30, 0, 12.0f);
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 95.0f, frame->iasKt);
}

// EfisFrame::fieldsPresent bit assertions — see DynonSkyview test for rationale.
void test_g3x_att_presence_bits_set_on_valid_frame(void)
{
    char buf[59];
    buildG3XAttFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500, 0.1f, 1.0f, 62, 500, 8.0f);
    GarminG3XParser parser;
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
    TEST_ASSERT_TRUE(fp & AoaPercent);
    TEST_ASSERT_TRUE(fp & Palt);
    TEST_ASSERT_TRUE(fp & OatCelsius);
    TEST_ASSERT_TRUE(fp & Vsi);
}

void test_g3x_ems_presence_bits_set_on_valid_frame(void)
{
    char buf[221];
    buildG3XEmsFrame(buf);
    GarminG3XParser parser;
    auto frame = feedAll(parser, buf, 221);
    TEST_ASSERT_TRUE(frame.has_value());
    const uint32_t fp = frame->fieldsPresent;
    using namespace onspeed::EfisField;
    TEST_ASSERT_TRUE(fp & Rpm);
    TEST_ASSERT_TRUE(fp & MapInchHg);
    TEST_ASSERT_TRUE(fp & FuelFlowGph);
    TEST_ASSERT_TRUE(fp & FuelRemainingGal);
    // G3X EMS carries no PercentPower (Dynon SkyView's wire does); leave absent.
    TEST_ASSERT_FALSE(fp & PercentPower);
    // EMS frame has no attitude/airspeed.
    TEST_ASSERT_FALSE(fp & Ias);
    TEST_ASSERT_FALSE(fp & Pitch);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_g3x_att_valid_ias);
    RUN_TEST(test_g3x_att_has_aoa_field);
    RUN_TEST(test_g3x_att_has_oat_field);
    RUN_TEST(test_g3x_source_is_garmin);
    RUN_TEST(test_g3x_ems_frame_parses);
    RUN_TEST(test_g3x_ems_frame_leaves_attitude_airspeed_absent);
    RUN_TEST(test_g3x_ems_engine_fields_round_trip);
    RUN_TEST(test_g3x_ems_engine_sentinels_become_nan);
    RUN_TEST(test_g3x_att_time_of_day_round_trip);
    RUN_TEST(test_g3x_att_time_of_day_non_digits_leave_empty);
    RUN_TEST(test_g3x_att_time_of_day_out_of_range_leaves_empty);
    RUN_TEST(test_g3x_interleaved_att_and_ems);
    RUN_TEST(test_g3x_truncated_no_output);
    RUN_TEST(test_g3x_corrupted_crc_no_output);
    RUN_TEST(test_g3x_wrong_magic_discarded);
    RUN_TEST(test_g3x_reset_clears_state);
    RUN_TEST(test_g3x_garbage_then_valid_parses);
    RUN_TEST(test_g3x_ems_corrupted_crc_no_output);
    RUN_TEST(test_g3x_buffer_overflow_resets_and_recovers);
    RUN_TEST(test_g3x_att_presence_bits_set_on_valid_frame);
    RUN_TEST(test_g3x_ems_presence_bits_set_on_valid_frame);
    return UNITY_END();
}
