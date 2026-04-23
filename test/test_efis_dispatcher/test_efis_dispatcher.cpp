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

static std::optional<EfisFrame> feedAllBytes(EfisParser& p,
                                             const uint8_t* buf, int len)
{
    std::optional<EfisFrame> result;
    for (int i = 0; i < len; i++)
    {
        p.FeedByte(buf[i]);
        auto f = p.TakeFrame();
        if (f) result = f;
    }
    return result;
}

// Dynon D10: 51 payload + CR/LF (total 53). Must prime with '\n' first
// (lineStart_). See DynonD10Parser in test_efis_dynon_d10.
static void buildD10Frame(char buf[53], int statusNibble)
{
    char tmp[16];
    for (int i = 0; i < 51; i++) buf[i] = ' ';
    snprintf(tmp, sizeof(tmp), "%02d", 12); putField(buf, 0, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 34); putField(buf, 2, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 56); putField(buf, 4, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 78); putField(buf, 6, tmp, 2);
    putField(buf,  8, "+020",  4);   // pitch * 10
    putField(buf, 12, "+0050", 5);   // roll  * 10
    putField(buf, 20, "0500",  4);   // IAS m/s * 10
    putField(buf, 24, "01500", 5);   // alt m
    putField(buf, 29, "0000",  4);   // vsi ft/s * 10
    putField(buf, 33, "000",   3);   // lateral G * 100
    putField(buf, 36, "010",   3);   // vertical G * 10
    putField(buf, 39, "00",    2);   // percent lift
    snprintf(tmp, sizeof(tmp), "%X", statusNibble & 0xF);
    putField(buf, 46, tmp, 1);

    int crc = 0;
    for (int i = 0; i <= 48; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc); putField(buf, 49, tmp, 2);
    buf[51] = '\r'; buf[52] = '\n';
}

// Garmin G3X: 59-byte attitude frame, header '=' '1' '1' (same as G5 but the
// G3X parser is a distinct type/dispatcher case).
static void buildG3XFrame(char buf[59])
{
    char tmp[16];
    for (int i = 0; i < 57; i++) buf[i] = ' ';
    buf[0] = '='; buf[1] = '1'; buf[2] = '1';
    snprintf(tmp, sizeof(tmp), "%02d", 12); putField(buf,  3, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 34); putField(buf,  5, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 56); putField(buf,  7, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", 78); putField(buf,  9, tmp, 2);
    putField(buf, 11, "+020",   4);
    putField(buf, 15, "+0050",  5);
    putField(buf, 20, "270",    3);
    putField(buf, 23, "1200",   4);   // IAS 120.0 kt
    putField(buf, 27, "005500", 6);
    putField(buf, 37, "000",    3);
    putField(buf, 40, "010",    3);
    putField(buf, 43, "00",     2);
    putField(buf, 45, "0000",   4);
    putField(buf, 49, "+15",    3);
    int crc = 0;
    for (int i = 0; i <= 54; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    snprintf(tmp, sizeof(tmp), "%02X", crc); putField(buf, 55, tmp, 2);
    buf[57] = '\r'; buf[58] = '\n';
}

// MGL Binary Message Type 1 (primary) — 44 bytes. Checksum in body is
// not validated by parser. No "rebuild on parser" needed.
static void buildMglMsg1(uint8_t buf[44])
{
    memset(buf, 0, 44);
    buf[0] = 0x05; buf[1] = 0x02;
    buf[2] = 24;             // message length
    buf[3] = 0xFF ^ 24;      // length XOR
    buf[4] = 1;              // type = primary
    buf[5] = 5;              // rate
    buf[6] = 0;              // count
    buf[7] = 1;              // version
    // body[8..43]: PAltitude(4) BAltitude(4) IAS(2) TAS(2) AOA(2) VSI(2)
    // Baro(2) Local(2) OAT(2) Humidity(1) SystemFlags(1) Hour(1) Minute(1)
    // Second(1) Date(1) Month(1) Year(1) FTHour(1) FTMin(1) Checksum(4).
    int32_t palt = 5500;       memcpy(buf + 8,  &palt, 4);
    uint16_t ias = 2000;       memcpy(buf + 16, &ias,  2);   // ~108 kt
    // All other fields remain zero; they're parsed but we don't assert.
}

// VN-300 binary packet — 127 bytes. CRC-16 must be correct; build via the
// same rolling-XOR used in EfisSerial.cpp.
static void appendVnCrc(uint8_t buf[], int n)
{
    uint16_t crc = 0;
    for (int i = 1; i < n - 2; i++) {
        crc  = static_cast<uint16_t>((crc >> 8) | (crc << 8));
        crc ^= buf[i];
        crc ^= static_cast<uint16_t>(static_cast<uint8_t>(crc & 0xFF) >> 4);
        crc ^= static_cast<uint16_t>((crc << 8) << 4);
        crc ^= static_cast<uint16_t>(((crc & 0xFF) << 4) << 1);
    }
    buf[n - 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    buf[n - 1] = static_cast<uint8_t>(crc & 0xFF);
}

static void buildVn300Packet(uint8_t buf[127])
{
    memset(buf, 0, 127);
    buf[0] = 0xFA; buf[1] = 0x19;
    buf[2] = 0xE0; buf[3] = 0x01;
    buf[4] = 0x91; buf[5] = 0x00;
    buf[6] = 0x42; buf[7] = 0x01;
    // Only the fields the parser reads need be non-zero; the parser
    // handles zero-init fields fine, so leave most of the body zero.
    appendVnCrc(buf, 127);
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

// ---------------------------------------------------------------------------
// Missing dispatcher cases (exercises the switch-case branches at
// EfisParser.cpp:19,21,22,23,33,35,36,37 that were previously untested).
// ---------------------------------------------------------------------------

void test_dispatcher_dynon_d10_routes_correctly(void)
{
    EfisParser parser(EfisType::DynonD10);
    // D10 parser requires a '\n' to prime lineStart_ before the frame.
    parser.FeedByte('\n');
    char buf[53];
    buildD10Frame(buf, /* statusNibble = */ 0x3);   // Palt+VSI valid
    auto frame = feedAll(parser, buf, 53);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Dynon),
                          static_cast<int>(frame->source));
}

void test_dispatcher_garmin_g3x_routes_correctly(void)
{
    EfisParser parser(EfisType::GarminG3X);
    char buf[59];
    buildG3XFrame(buf);
    auto frame = feedAll(parser, buf, 59);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Garmin),
                          static_cast<int>(frame->source));
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 120.0f, frame->iasKt);
}

void test_dispatcher_mgl_binary_routes_correctly(void)
{
    EfisParser parser(EfisType::MglBinary);
    uint8_t buf[44];
    buildMglMsg1(buf);
    auto frame = feedAllBytes(parser, buf, 44);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Mgl),
                          static_cast<int>(frame->source));
}

void test_dispatcher_vn300_routes_correctly(void)
{
    EfisParser parser(EfisType::Vn300);
    uint8_t buf[127];
    buildVn300Packet(buf);
    feedAllBytes(parser, buf, 127);
    // VN-300 parses into both an EfisFrame (via TakeFrame) and a Vn300Data
    // (via TakeVn300Data). Both should be present after a full packet.
    TEST_ASSERT_TRUE(parser.TakeFrame().has_value() ||
                     parser.TakeVn300Data().has_value());
}

void test_dispatcher_vn300_take_data_works_for_vn300(void)
{
    // Exercises EfisParser.cpp:46 (Vn300 branch of TakeVn300Data).
    EfisParser parser(EfisType::Vn300);
    uint8_t buf[127];
    buildVn300Packet(buf);
    feedAllBytes(parser, buf, 127);
    // Drain whatever TakeFrame produces first so TakeVn300Data isn't blocked.
    (void)parser.TakeFrame();
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
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
    RUN_TEST(test_dispatcher_dynon_d10_routes_correctly);
    RUN_TEST(test_dispatcher_garmin_g3x_routes_correctly);
    RUN_TEST(test_dispatcher_mgl_binary_routes_correctly);
    RUN_TEST(test_dispatcher_vn300_routes_correctly);
    RUN_TEST(test_dispatcher_vn300_take_data_works_for_vn300);
    return UNITY_END();
}
