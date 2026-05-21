// test_efis_vn300.cpp
//
// Unit tests for Vn300Parser.
//
// Fixtures are SYNTHETIC — constructed to match the VN-300 binary packet
// structure from EfisSerial.cpp (EnVN300 branch).
//
// TODO(maintainer): replace with real captured frames from a live VN-300.
//
// Packet structure (127 bytes):
//   [0]     0xFA  sync 1
//   [1]     0x19  sync 2
//   [2..7]  0xE0, 0x01, 0x91, 0x00, 0x42, 0x01  (rest of 8-byte header)
//   [8..11]  AngularRateRoll (float, LE)
//   [12..15] AngularRatePitch
//   [16..19] AngularRateYaw
//   [20..27] GnssLat (double, LE)
//   [28..35] GnssLon (double, LE)
//   [36..43] EstAltMeters (double, LE) — INS-estimated altitude from
//                                         the Common.Position group
//   [44..47] VelNedNorth (float)
//   [48..51] VelNedEast
//   [52..55] VelNedDown
//   [56..59] AccelFwd
//   [60..63] AccelLat
//   [64..67] AccelVert
//   [68..75] TimeUTC (8 bytes; [68]=Year-2000,[69]=Month,[70]=Day,
//                              [71]=Hour,[72]=Min,[73]=Sec,
//                              [74..75]=Ms u16 LE per UM005 GpsGroup.UTC)
//   [76]    GPSFix
//   [77..80] GnssVelNedNorth
//   [81..84] GnssVelNedEast
//   [85..88] GnssVelNedDown
//   [89..92] Yaw (float)
//   [93..96] Pitch
//   [97..100] Roll
//   [101..104] LinAccFwd
//   [105..108] LinAccLat
//   [109..112] LinAccVert
//   [113..116] YawSigma
//   [117..120] PitchSigma
//   [121..124] RollSigma
//   [125..126] CRC-16

#include <unity.h>
#include <efis/Vn300.h>
#include <types/EfisFrame.h>

#include <cstring>
#include <cstdint>
#include <cstdio>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::efis::Vn300Parser;
using onspeed::efis::Vn300Data;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void writeFloat(uint8_t* buf, int offset, float value)
{
    memcpy(buf + offset, &value, sizeof(float));
}

static void writeDouble(uint8_t* buf, int offset, double value)
{
    memcpy(buf + offset, &value, sizeof(double));
}

// Compute VN-300 CRC-16 over bytes 1..(N-3) inclusive, append to [N-2..N-1].
// The CRC must be stored big-endian (MSB first) so that a full check over
// bytes 1..(N-1) yields 0.
static void appendVnCrc(uint8_t buf[], int n)
{
    uint16_t crc = 0;
    for (int i = 1; i < n - 2; i++)
    {
        crc  = static_cast<uint16_t>((crc >> 8) | (crc << 8));
        crc ^= buf[i];
        crc ^= static_cast<uint16_t>(static_cast<uint8_t>(crc & 0xFF) >> 4);
        crc ^= static_cast<uint16_t>((crc << 8) << 4);
        crc ^= static_cast<uint16_t>(((crc & 0xFF) << 4) << 1);
    }
    // Store big-endian so the full-message check gives 0.
    buf[n - 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    buf[n - 1] = static_cast<uint8_t>(crc & 0xFF);
}

static void buildVn300Packet(uint8_t buf[127],
                              float pitch, float roll, float yaw,
                              float iasPlaceholder = 0.0f)
{
    (void)iasPlaceholder;
    memset(buf, 0, 127);
    buf[0] = 0xFA; buf[1] = 0x19;
    buf[2] = 0xE0; buf[3] = 0x01;
    buf[4] = 0x91; buf[5] = 0x00;
    buf[6] = 0x42; buf[7] = 0x01;

    writeFloat(buf,  8, 0.01f);   // AngularRateRoll
    writeFloat(buf, 12, 0.02f);   // AngularRatePitch
    writeFloat(buf, 16, 0.03f);   // AngularRateYaw
    writeDouble(buf, 20, 37.7749);   // GnssLat
    writeDouble(buf, 28, -122.4194); // GnssLon
    writeDouble(buf, 36, 1378.265);  // EstAltMeters (~4521 ft)
    writeFloat(buf, 44, 50.0f);   // VelNedNorth
    writeFloat(buf, 48, 5.0f);    // VelNedEast
    writeFloat(buf, 52, -1.5f);   // VelNedDown
    writeFloat(buf, 56, 0.5f);    // AccelFwd
    writeFloat(buf, 60, 0.1f);    // AccelLat
    writeFloat(buf, 64, 9.8f);    // AccelVert
    // TimeUTC at [68..75]
    buf[71] = 12;   // Hour
    buf[72] = 34;   // Min
    buf[73] = 56;   // Sec
    // Ms at [74..75] as u16 little-endian. 789 = 0x0315.
    buf[74] = 0x15;
    buf[75] = 0x03;
    buf[76] = 3;    // GPSFix
    writeFloat(buf, 77, 50.0f);
    writeFloat(buf, 81, 5.0f);
    writeFloat(buf, 85, -1.5f);
    writeFloat(buf, 89, yaw);
    writeFloat(buf, 93, pitch);
    writeFloat(buf, 97, roll);
    writeFloat(buf, 101, 0.1f);
    writeFloat(buf, 105, 0.02f);
    writeFloat(buf, 109, 9.75f);
    writeFloat(buf, 113, 0.5f);   // YawSigma
    writeFloat(buf, 117, 0.3f);   // PitchSigma per UM005 §5.8.8
    writeFloat(buf, 121, 0.4f);   // RollSigma  per UM005 §5.8.8

    appendVnCrc(buf, 127);
}

static void feedAll(Vn300Parser& p, const uint8_t* buf, int len)
{
    for (int i = 0; i < len; i++)
        p.FeedByte(buf[i]);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_vn300_valid_packet_produces_frame(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_vn300_pitch(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, frame->pitchDeg);
}

void test_vn300_roll(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, frame->rollDeg);
}

void test_vn300_heading_from_yaw(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 270.0f, frame->headingDeg);
}

void test_vn300_source_is_vn300(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Vn300),
                          static_cast<int>(frame->source));
}

void test_vn300_extended_data_available(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, data->pitch);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, data->roll);
}

void test_vn300_truncated_no_output(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    for (int i = 0; i < 100; i++) parser.FeedByte(buf[i]);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_vn300_corrupted_crc_no_output(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    buf[125] ^= 0xFF;   // corrupt CRC
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_vn300_wrong_header_no_output(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    buf[2] = 0x00;   // corrupt header byte 2
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_vn300_garbage_then_valid_parses(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    const uint8_t garbage[] = {0x00, 0x01, 0xAB, 0xCD};
    for (uint8_t b : garbage) parser.FeedByte(b);
    feedAll(parser, buf, 127);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_vn300_reset_clears_state(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    for (int i = 0; i < 60; i++) parser.FeedByte(buf[i]);
    parser.Reset();
    feedAll(parser, buf, 127);
    TEST_ASSERT_TRUE(parser.TakeFrame().has_value());
}

void test_vn300_take_frame_clears_pending(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    // Feed without consuming TakeFrame so pending stays set.
    for (int i = 0; i < 127; i++) parser.FeedByte(buf[i]);
    auto first  = parser.TakeFrame();
    auto second = parser.TakeFrame();
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_FALSE(second.has_value());
}

void test_vn300_take_data_clears_pending(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    for (int i = 0; i < 127; i++) parser.FeedByte(buf[i]);
    auto first  = parser.TakeVn300Data();
    auto second = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_FALSE(second.has_value());
}

void test_vn300_gps_fix_in_data(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_UINT8(3, data->gpsFix);
}

// Regression for issue #454: YprU is laid out (yaw, pitch, roll) per
// VectorNav UM005 §5.8.8. The fixture writes pitch-sigma=0.3 at offset
// 117 and roll-sigma=0.4 at offset 121; the parser must decode them
// into data.pitchSigma and data.rollSigma respectively.
void test_vn300_yprU_axis_assignment(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, data->yawSigma);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f, data->pitchSigma);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.4f, data->rollSigma);
}

void test_vn300_est_alt_meters_in_data(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    // buildVn300Packet writes 1378.265 m at offset 36; the parser reads that
    // double via array2double. Tolerance is loose to absorb the round-trip
    // through a double constant in the synthetic frame.
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 1378.265, data->estAltMeters);
}

// TimeUTC formatting: parser reads bytes 74-75 as u16 LE ms and emits
// "HH:MM:SS.mmm" with zero-padded fields. Buffer fits the formatted string
// with room to spare (24-byte szTimeUTC vs 12-byte payload + null).
void test_vn300_time_utc_includes_ms(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);   // sets hour/min/sec/ms above
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_STRING("12:34:56.789", data->szTimeUTC);
}

void test_vn300_time_utc_zero_pads_single_digits(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    buf[71] = 1;     // Hour
    buf[72] = 2;     // Min
    buf[73] = 3;     // Sec
    buf[74] = 0x04;  // Ms = 4
    buf[75] = 0x00;
    appendVnCrc(buf, 127);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_STRING("01:02:03.004", data->szTimeUTC);
}

// EfisFrame::fieldsPresent bit assertions — VN-300 populates only the
// attitude triple on the EfisFrame; the full IMU/GPS dataset is on
// Vn300Data via TakeVn300Data().
void test_vn300_presence_bits_attitude_only(void)
{
    uint8_t buf[127];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, 127);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    const uint32_t fp = frame->fieldsPresent;
    using namespace onspeed::EfisField;
    TEST_ASSERT_TRUE(fp & Pitch);
    TEST_ASSERT_TRUE(fp & Roll);
    TEST_ASSERT_TRUE(fp & Heading);
    // VN-300 does not populate airspeed/altitude on the EfisFrame.
    TEST_ASSERT_FALSE(fp & Ias);
    TEST_ASSERT_FALSE(fp & Palt);
    TEST_ASSERT_FALSE(fp & Vsi);
    TEST_ASSERT_FALSE(fp & OatCelsius);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_vn300_valid_packet_produces_frame);
    RUN_TEST(test_vn300_pitch);
    RUN_TEST(test_vn300_roll);
    RUN_TEST(test_vn300_heading_from_yaw);
    RUN_TEST(test_vn300_source_is_vn300);
    RUN_TEST(test_vn300_extended_data_available);
    RUN_TEST(test_vn300_truncated_no_output);
    RUN_TEST(test_vn300_corrupted_crc_no_output);
    RUN_TEST(test_vn300_wrong_header_no_output);
    RUN_TEST(test_vn300_garbage_then_valid_parses);
    RUN_TEST(test_vn300_reset_clears_state);
    RUN_TEST(test_vn300_take_frame_clears_pending);
    RUN_TEST(test_vn300_take_data_clears_pending);
    RUN_TEST(test_vn300_gps_fix_in_data);
    RUN_TEST(test_vn300_yprU_axis_assignment);
    RUN_TEST(test_vn300_est_alt_meters_in_data);
    RUN_TEST(test_vn300_time_utc_includes_ms);
    RUN_TEST(test_vn300_time_utc_zero_pads_single_digits);
    RUN_TEST(test_vn300_presence_bits_attitude_only);
    return UNITY_END();
}
