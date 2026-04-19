// test_efis_dispatcher.cpp
//
// Unit tests for EfisParser dispatcher.
//
// Tests that the dispatcher routes bytes to the correct parser and exposes
// the correct interface. Does not re-test parser internals — those are
// covered in per-parser test suites.

#include <unity.h>
#include <efis/EfisParser.h>
#include <types/EfisFrame.h>

#include <cstring>
#include <cstdio>
#include <cstdint>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::efis::EfisParser;
using onspeed::efis::EfisType;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void putField(char* buf, int pos, const char* tmp, int n)
{
    for (int i = 0; i < n; i++) buf[pos + i] = tmp[i];
}

static void buildAdahrsFrame(char buf[74],
                              float pitchDeg, float rollDeg, int headingDeg,
                              float iasKt, int paltFt)
{
    for (int i = 0; i < 72; i++) buf[i] = ' ';
    char tmp[16];
    buf[0] = '!'; buf[1] = '1'; buf[2] = ' ';
    snprintf(tmp, sizeof(tmp), "%02d", 12);  putField(buf,  3, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 34);  putField(buf,  5, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 56);  putField(buf,  7, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 78);  putField(buf,  9, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%+04d", static_cast<int>(pitchDeg * 10.0f));
    putField(buf, 11, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%+05d", static_cast<int>(rollDeg * 10.0f));
    putField(buf, 15, tmp, 5);
    snprintf(tmp, sizeof(tmp), "%03d", headingDeg);  putField(buf, 20, tmp, 3);
    snprintf(tmp, sizeof(tmp), "%04d", static_cast<int>(iasKt * 10.0f));
    putField(buf, 23, tmp, 4);
    snprintf(tmp, sizeof(tmp), "%06d", paltFt);      putField(buf, 27, tmp, 6);
    int crc = 0;
    for (int i = 0; i <= 69; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);         putField(buf, 70, tmp, 2);
    buf[72] = '\r'; buf[73] = '\n';
}

static void buildG5Frame(char buf[59], float pitchDeg, float iasKt)
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
    putField(buf, 15, "+0000", 5);
    putField(buf, 20, "000",   3);
    snprintf(tmp, sizeof(tmp), "%04d", static_cast<int>(iasKt * 10.0f));
    putField(buf, 23, tmp, 4);
    putField(buf, 27, "003000", 6);
    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc);  putField(buf, 55, tmp, 2);
    buf[57] = '\r'; buf[58] = '\n';
}

static std::optional<EfisFrame> feedAll(EfisParser& p,
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

void test_dispatcher_none_type_returns_no_frame(void)
{
    EfisParser parser(EfisType::None);
    char buf[74];
    buildAdahrsFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500);
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_dispatcher_dynon_skyview_routes_correctly(void)
{
    EfisParser parser(EfisType::DynonSkyview);
    char buf[74];
    buildAdahrsFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500);
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 110.0f, frame->iasKt);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Dynon),
                          static_cast<int>(frame->source));
}

void test_dispatcher_garmin_g5_routes_correctly(void)
{
    EfisParser parser(EfisType::GarminG5);
    char buf[59];
    buildG5Frame(buf, 3.0f, 120.0f);
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 120.0f, frame->iasKt);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Garmin),
                          static_cast<int>(frame->source));
}

void test_dispatcher_active_type_accessor(void)
{
    EfisParser parser(EfisType::DynonD10);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisType::DynonD10),
                          static_cast<int>(parser.ActiveType()));
}

void test_dispatcher_change_type_resets_state(void)
{
    // Feed half a Dynon SkyView frame, then change to G5.
    // No frame should appear from either partial feed.
    char buf[74];
    buildAdahrsFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500);

    EfisParser parser(EfisType::DynonSkyview);
    for (int i = 0; i < 40; i++)
        parser.FeedByte(static_cast<uint8_t>(buf[i]));

    parser.ChangeType(EfisType::GarminG5);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisType::GarminG5),
                          static_cast<int>(parser.ActiveType()));

    // A valid G5 frame after the type change should parse correctly.
    char g5buf[59];
    buildG5Frame(g5buf, 1.5f, 100.0f);
    auto frame = feedAll(parser, g5buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Garmin),
                          static_cast<int>(frame->source));
}

void test_dispatcher_vn300_take_data_returns_nullopt_for_other_type(void)
{
    EfisParser parser(EfisType::DynonSkyview);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_FALSE(data.has_value());
}

void test_dispatcher_none_type_vn300_data_nullopt(void)
{
    EfisParser parser(EfisType::None);
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 100.0f, 3000);
    feedAll(parser, buf, 74);
    TEST_ASSERT_FALSE(parser.TakeVn300Data().has_value());
}

void test_dispatcher_dynon_frame_is_not_produced_with_garmin_parser(void)
{
    // Configure G5, feed a Dynon '!' frame — should NOT produce a frame.
    EfisParser parser(EfisType::GarminG5);
    char buf[74];
    buildAdahrsFrame(buf, 2.0f, 5.0f, 270, 110.0f, 5500);
    auto frame = feedAll(parser, buf, 74);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_dispatcher_garmin_frame_is_not_produced_with_dynon_parser(void)
{
    // Configure DynonSkyview, feed a G5 '=' frame — should NOT produce a frame.
    EfisParser parser(EfisType::DynonSkyview);
    char buf[59];
    buildG5Frame(buf, 3.0f, 120.0f);
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_FALSE(frame.has_value());
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_dispatcher_none_type_returns_no_frame);
    RUN_TEST(test_dispatcher_dynon_skyview_routes_correctly);
    RUN_TEST(test_dispatcher_garmin_g5_routes_correctly);
    RUN_TEST(test_dispatcher_active_type_accessor);
    RUN_TEST(test_dispatcher_change_type_resets_state);
    RUN_TEST(test_dispatcher_vn300_take_data_returns_nullopt_for_other_type);
    RUN_TEST(test_dispatcher_none_type_vn300_data_nullopt);
    RUN_TEST(test_dispatcher_dynon_frame_is_not_produced_with_garmin_parser);
    RUN_TEST(test_dispatcher_garmin_frame_is_not_produced_with_dynon_parser);
    return UNITY_END();
}
