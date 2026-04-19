// test_efis_mgl_binary.cpp
//
// Unit tests for MglBinaryParser.
//
// Fixtures are SYNTHETIC — constructed to match the MGL binary message
// structure from EfisSerial.cpp (EnMglBinary branch).
//
// TODO(maintainer): replace with real captured frames from a live MGL EFIS.
//
// Message structure:
//   [0]  0x05  DLE
//   [1]  0x02  STX
//   [2]  MessageLength (N)
//   [3]  MessageLengthXOR = 0xFF ^ MessageLength
//   [4]  MessageType (1=primary, 3=attitude)
//   [5]  MessageRate
//   [6]  MessageCount
//   [7]  MessageVersion
//   [8..N+19]  body (varies by type)
//
// Total length = MessageLength + 20.
// Type 1 body (36 bytes): PAltitude, BAltitude, IAS, TAS, AOA, VSI, Baro,
//   Local, OAT, Humidity, SystemFlags, Hour, Minute, Second, Date, Month,
//   Year, FTHour, FTMin, Checksum (CRC32 — not validated by parser).
// Type 3 body (32 bytes): HeadingMag, PitchAngle, BankAngle, YawAngle,
//   TurnRate, Slip, GForce, LRForce, FRForce, BankRate, PitchRate, YawRate,
//   SensorFlags, 3×Padding, Checksum.

#include <unity.h>
#include <efis/MglBinary.h>
#include <types/EfisFrame.h>

#include <cstring>
#include <cstdint>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::efis::MglBinaryParser;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct MglMsg1Body {
    int32_t  PAltitude;
    int32_t  BAltitude;
    uint16_t IAS;
    uint16_t TAS;
    int16_t  AOA;
    int16_t  VSI;
    uint16_t Baro;
    uint16_t Local;
    int16_t  OAT;
    uint8_t  Humidity;
    uint8_t  SystemFlags;
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  Date;
    uint8_t  Month;
    uint8_t  Year;
    uint8_t  FTHour;
    uint8_t  FTMin;
    int32_t  Checksum;   // not validated
};

struct MglMsg3Body {
    uint16_t HeadingMag;
    int16_t  PitchAngle;
    int16_t  BankAngle;
    int16_t  YawAngle;
    int16_t  TurnRate;
    int16_t  Slip;
    int16_t  GForce;
    int16_t  LRForce;
    int16_t  FRForce;
    int16_t  BankRate;
    int16_t  PitchRate;
    int16_t  YawRate;
    uint8_t  SensorFlags;
    uint8_t  Pad1;
    uint8_t  Pad2;
    uint8_t  Pad3;
    int32_t  Checksum;
};
#pragma pack(pop)

// Build a Type 1 (primary) MGL frame in buf, size 44 bytes.
static void buildMglMsg1(uint8_t buf[44],
                          int32_t paltFt, uint16_t ias10kmh, uint16_t tas10kmh,
                          int16_t aoaRaw, int16_t vsiFpm, int16_t oat)
{
    memset(buf, 0, 44);
    buf[0] = 0x05; buf[1] = 0x02;
    // MessageLength for Msg1 body = sizeof(MglMsg1Body) = 36; total = 36 + 8 header = 44, so MessageLength = 44 - 20 = 24
    buf[2] = 24;
    buf[3] = 0xFF ^ 24;   // XOR
    buf[4] = 1;   // MessageType = 1
    buf[5] = 5;   // rate
    buf[6] = 0;   // count
    buf[7] = 1;   // version

    MglMsg1Body body;
    memset(&body, 0, sizeof(body));
    body.PAltitude = paltFt;
    body.IAS       = ias10kmh;
    body.TAS       = tas10kmh;
    body.AOA       = aoaRaw;
    body.VSI       = vsiFpm;
    body.OAT       = oat;
    body.Hour      = 12;
    body.Minute    = 34;
    body.Second    = 56;
    memcpy(buf + 8, &body, sizeof(body));
}

// Build a Type 3 (attitude) MGL frame in buf, size 40 bytes.
static void buildMglMsg3(uint8_t buf[40],
                          uint16_t headingMag10, int16_t pitch10, int16_t bank10,
                          int16_t gForce100, int16_t lrForce100)
{
    memset(buf, 0, 40);
    buf[0] = 0x05; buf[1] = 0x02;
    // Msg3 body = sizeof(MglMsg3Body) = 32; total = 32 + 8 = 40, MessageLength = 40 - 20 = 20
    buf[2] = 20;
    buf[3] = 0xFF ^ 20;
    buf[4] = 3;   // MessageType = 3
    buf[5] = 5;
    buf[6] = 0;
    buf[7] = 1;

    MglMsg3Body body;
    memset(&body, 0, sizeof(body));
    body.HeadingMag = headingMag10;
    body.PitchAngle = pitch10;
    body.BankAngle  = bank10;
    body.GForce     = gForce100;
    body.LRForce    = lrForce100;
    memcpy(buf + 8, &body, sizeof(body));
}

static std::optional<EfisFrame> feedAll(MglBinaryParser& p,
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_mgl_msg1_ias(void)
{
    uint8_t buf[44];
    // IAS: 556 * 0.05399565 = ~30.0 kt
    buildMglMsg1(buf, 5500, 556, 600, 0, 0, 20);
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 30.0f, frame->iasKt);
}

void test_mgl_msg1_palt(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 20);
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 5500.0f, frame->paltFt);
}

void test_mgl_msg1_oat(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 15);
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 15.0f, frame->oatCelsius);
}

void test_mgl_msg1_source_is_mgl(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 20);
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Mgl),
                          static_cast<int>(frame->source));
}

void test_mgl_msg3_pitch(void)
{
    uint8_t buf[40];
    // pitch: +25 (tenths → 2.5°)
    buildMglMsg3(buf, 2700, 25, 50, 100, 5);
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 40);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 2.5f, frame->pitchDeg);
}

void test_mgl_msg3_roll(void)
{
    uint8_t buf[40];
    buildMglMsg3(buf, 2700, 0, 150, 100, 0);  // 15.0° roll
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 40);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 15.0f, frame->rollDeg);
}

void test_mgl_msg3_heading(void)
{
    uint8_t buf[40];
    buildMglMsg3(buf, 2700, 0, 0, 100, 0);  // heading = 270.0°
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 40);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 270.0f, frame->headingDeg);
}

void test_mgl_msg3_source_is_mgl(void)
{
    uint8_t buf[40];
    buildMglMsg3(buf, 0, 0, 0, 100, 0);
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 40);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Mgl),
                          static_cast<int>(frame->source));
}

void test_mgl_truncated_no_output(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 20);
    MglBinaryParser parser;
    for (int i = 0; i < 30; i++) parser.FeedByte(buf[i]);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_mgl_bad_sync_byte2_resets(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 20);
    buf[1] = 0x03;   // wrong STX
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_mgl_bad_length_xor_resets(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 20);
    buf[3] = 0x00;   // corrupt XOR
    MglBinaryParser parser;
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_FALSE(frame.has_value());
}

void test_mgl_reset_clears_state(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 20);
    MglBinaryParser parser;
    for (int i = 0; i < 20; i++) parser.FeedByte(buf[i]);
    parser.Reset();
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_mgl_interleaved_msg1_msg3(void)
{
    uint8_t buf1[44], buf3[40];
    buildMglMsg1(buf1, 5500, 500, 520, 0, 0, 20);
    buildMglMsg3(buf3, 2700, 25, 50, 100, 5);

    MglBinaryParser parser;
    auto f1 = feedAll(parser, buf1, 44);
    auto f3 = feedAll(parser, buf3, 40);

    TEST_ASSERT_TRUE(f1.has_value());
    TEST_ASSERT_TRUE(f3.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Mgl),
                          static_cast<int>(f1->source));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Mgl),
                          static_cast<int>(f3->source));
}

void test_mgl_garbage_then_valid_msg1(void)
{
    uint8_t buf[44];
    buildMglMsg1(buf, 5500, 500, 520, 0, 0, 20);

    MglBinaryParser parser;
    const uint8_t garbage[] = {0x00, 0xFF, 0xAB};
    for (uint8_t b : garbage) parser.FeedByte(b);
    auto frame = feedAll(parser, buf, 44);
    TEST_ASSERT_TRUE(frame.has_value());
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_mgl_msg1_ias);
    RUN_TEST(test_mgl_msg1_palt);
    RUN_TEST(test_mgl_msg1_oat);
    RUN_TEST(test_mgl_msg1_source_is_mgl);
    RUN_TEST(test_mgl_msg3_pitch);
    RUN_TEST(test_mgl_msg3_roll);
    RUN_TEST(test_mgl_msg3_heading);
    RUN_TEST(test_mgl_msg3_source_is_mgl);
    RUN_TEST(test_mgl_truncated_no_output);
    RUN_TEST(test_mgl_bad_sync_byte2_resets);
    RUN_TEST(test_mgl_bad_length_xor_resets);
    RUN_TEST(test_mgl_reset_clears_state);
    RUN_TEST(test_mgl_interleaved_msg1_msg3);
    RUN_TEST(test_mgl_garbage_then_valid_msg1);
    return UNITY_END();
}
