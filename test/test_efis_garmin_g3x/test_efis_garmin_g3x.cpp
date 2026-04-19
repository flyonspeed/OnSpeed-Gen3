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
    // or airspeed fields that the =21 attitude frame populated. Every
    // EfisFrame numeric field should be kEfisFieldAbsent (NaN) so
    // applyFrame() preserves the prior suEfis values.
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

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_g3x_att_valid_ias);
    RUN_TEST(test_g3x_att_has_aoa_field);
    RUN_TEST(test_g3x_att_has_oat_field);
    RUN_TEST(test_g3x_source_is_garmin);
    RUN_TEST(test_g3x_ems_frame_parses);
    RUN_TEST(test_g3x_ems_frame_leaves_attitude_airspeed_absent);
    RUN_TEST(test_g3x_interleaved_att_and_ems);
    RUN_TEST(test_g3x_truncated_no_output);
    RUN_TEST(test_g3x_corrupted_crc_no_output);
    RUN_TEST(test_g3x_wrong_magic_discarded);
    RUN_TEST(test_g3x_reset_clears_state);
    RUN_TEST(test_g3x_garbage_then_valid_parses);
    RUN_TEST(test_g3x_ems_corrupted_crc_no_output);
    return UNITY_END();
}
