// test_efis_vn300.cpp
//
// Unit tests for Vn300Parser.
//
// Fixtures are SYNTHETIC — constructed to match the VN-300 binary packet
// structure documented in software/Libraries/onspeed_core/src/efis/Vn300.h.
//
// Packet structure (138 bytes, four active groups):
//   [0..1]     0xFA 0x1B           sync + Groups byte (Common+Time+GNSS1+AHRS)
//   [2..3]     E3 01               Common mask 0x01E3 (LE)
//   [4..5]     00 02               Time mask   0x0200 (LE)
//   [6..7]     90 00               GNSS1 mask  0x0090 (LE)
//   [8..9]     42 01               AHRS mask   0x0142 (LE)
//   [10..17]   TimeStartup         u64 LE  (ns since VN-300 boot)
//   [18..25]   TimeGps             u64 LE  (ns since GPS epoch 1980)
//   [26..29]   AngularRateRoll     f32
//   [30..33]   AngularRatePitch    f32
//   [34..37]   AngularRateYaw      f32
//   [38..45]   GnssLat             f64
//   [46..53]   GnssLon             f64
//   [54..61]   EstAltMeters        f64
//   [62..65]   VelNedNorth         f32
//   [66..69]   VelNedEast          f32
//   [70..73]   VelNedDown          f32
//   [74..77]   AccelFwd            f32
//   [78..81]   AccelLat            f32
//   [82..85]   AccelVert           f32
//   [86]       TimeStatus          u8
//   [87]       GpsFix              u8
//   [88..91]   GnssVelNedNorth     f32
//   [92..95]   GnssVelNedEast      f32
//   [96..99]   GnssVelNedDown      f32
//   [100..103] Yaw                 f32
//   [104..107] Pitch               f32
//   [108..111] Roll                f32
//   [112..115] LinAccFwd           f32
//   [116..119] LinAccLat           f32
//   [120..123] LinAccVert          f32
//   [124..127] YawSigma            f32
//   [128..131] PitchSigma          f32
//   [132..135] RollSigma           f32
//   [136..137] CRC-16              big-endian

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

static constexpr int kFrameSize = Vn300Parser::kPacketSize;   // 138

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

static void writeU64(uint8_t* buf, int offset, uint64_t value)
{
    memcpy(buf + offset, &value, sizeof(uint64_t));
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
    buf[n - 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    buf[n - 1] = static_cast<uint8_t>(crc & 0xFF);
}

static void buildVn300Packet(uint8_t buf[kFrameSize],
                              float pitch, float roll, float yaw)
{
    memset(buf, 0, kFrameSize);
    // 10-byte header: sync(1) + Groups(1) + 4 group masks(8).
    buf[0] = 0xFA; buf[1] = 0x1B;
    buf[2] = 0xE3; buf[3] = 0x01;   // Common 0x01E3 LE
    buf[4] = 0x00; buf[5] = 0x02;   // Time   0x0200 LE
    buf[6] = 0x90; buf[7] = 0x00;   // GNSS1  0x0090 LE
    buf[8] = 0x42; buf[9] = 0x01;   // AHRS   0x0142 LE

    // Time fields (Common group, leading the payload).
    writeU64(buf, 10, 1'234'567'890ULL);          // TimeStartup
    writeU64(buf, 18, 1'400'123'456'789'000ULL);  // TimeGps

    writeFloat(buf, 26, 0.01f);   // AngularRateRoll
    writeFloat(buf, 30, 0.02f);   // AngularRatePitch
    writeFloat(buf, 34, 0.03f);   // AngularRateYaw

    writeDouble(buf, 38, 37.7749);    // GnssLat
    writeDouble(buf, 46, -122.4194);  // GnssLon
    writeDouble(buf, 54, 1378.265);   // EstAltMeters

    writeFloat(buf, 62, 50.0f);   // VelNedNorth
    writeFloat(buf, 66, 5.0f);    // VelNedEast
    writeFloat(buf, 70, -1.5f);   // VelNedDown

    writeFloat(buf, 74, 0.5f);    // AccelFwd
    writeFloat(buf, 78, 0.1f);    // AccelLat
    writeFloat(buf, 82, 9.8f);    // AccelVert

    buf[86] = 0x07;               // TimeStatus: timeOk + dateOk + utcTimeValid
    buf[87] = 3;                  // GpsFix

    writeFloat(buf, 88, 50.0f);   // GnssVelNedNorth
    writeFloat(buf, 92, 5.0f);    // GnssVelNedEast
    writeFloat(buf, 96, -1.5f);   // GnssVelNedDown

    writeFloat(buf, 100, yaw);
    writeFloat(buf, 104, pitch);
    writeFloat(buf, 108, roll);

    writeFloat(buf, 112, 0.1f);   // LinAccFwd
    writeFloat(buf, 116, 0.02f);  // LinAccLat
    writeFloat(buf, 120, 9.75f);  // LinAccVert

    writeFloat(buf, 124, 0.5f);   // YawSigma
    writeFloat(buf, 128, 0.3f);   // PitchSigma  (YprU.c1 per UM005 §5.8.8)
    writeFloat(buf, 132, 0.4f);   // RollSigma   (YprU.c2)

    appendVnCrc(buf, kFrameSize);
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
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_vn300_pitch(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, frame->pitchDeg);
}

void test_vn300_roll(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, frame->rollDeg);
}

void test_vn300_heading_from_yaw(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 270.0f, frame->headingDeg);
}

void test_vn300_source_is_vn300(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Vn300),
                          static_cast<int>(frame->source));
}

void test_vn300_extended_data_available(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, data->pitch);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, data->roll);
}

void test_vn300_truncated_no_output(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    for (int i = 0; i < 100; i++) parser.FeedByte(buf[i]);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_vn300_corrupted_crc_no_output(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    buf[kFrameSize - 3] ^= 0xFF;   // corrupt last payload byte
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_vn300_wrong_header_no_output(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    buf[2] = 0x00;   // corrupt Common-mask byte
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

// Old 3-group VN-300 frames (sync 0xFA 0x19, 127 bytes) must be rejected.
// The new sync detection is on 0xFA 0x1B; an old-format frame's second byte
// (0x19) never starts a packet, so nothing decodes.
void test_vn300_old_format_rejected(void)
{
    uint8_t buf[127] = {};
    buf[0] = 0xFA; buf[1] = 0x19;       // old sync + old Groups byte
    buf[2] = 0xE0; buf[3] = 0x01;
    buf[4] = 0x91; buf[5] = 0x00;
    buf[6] = 0x42; buf[7] = 0x01;
    // Even if the old CRC were correct, the parser never starts collecting
    // because sync detection requires 0xFA 0x1B.
    Vn300Parser parser;
    for (int i = 0; i < 127; i++) parser.FeedByte(buf[i]);
    TEST_ASSERT_FALSE(parser.TakeFrame().has_value());
}

void test_vn300_garbage_then_valid_parses(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    const uint8_t garbage[] = {0x00, 0x01, 0xAB, 0xCD};
    for (uint8_t b : garbage) parser.FeedByte(b);
    feedAll(parser, buf, kFrameSize);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
}

void test_vn300_reset_clears_state(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    for (int i = 0; i < 60; i++) parser.FeedByte(buf[i]);
    parser.Reset();
    feedAll(parser, buf, kFrameSize);
    TEST_ASSERT_TRUE(parser.TakeFrame().has_value());
}

void test_vn300_take_frame_clears_pending(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto first  = parser.TakeFrame();
    auto second = parser.TakeFrame();
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_FALSE(second.has_value());
}

void test_vn300_take_data_clears_pending(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto first  = parser.TakeVn300Data();
    auto second = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_FALSE(second.has_value());
}

void test_vn300_gps_fix_in_data(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_UINT8(3, data->gpsFix);
}

// Regression for issue #454: YprU is laid out (yaw, pitch, roll) per
// VectorNav UM005 §5.8.8.
void test_vn300_yprU_axis_assignment(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, data->yawSigma);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f, data->pitchSigma);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.4f, data->rollSigma);
}

void test_vn300_est_alt_meters_in_data(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 1378.265, data->estAltMeters);
}

// Per-sample timestamps from Common group (issue #637 wire change).
void test_vn300_time_startup_ns_round_trip(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_UINT64(1'234'567'890ULL, data->timeStartupNs);
}

void test_vn300_time_gps_ns_round_trip(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_UINT64(1'400'123'456'789'000ULL, data->timeGpsNs);
}

void test_vn300_time_status_round_trip(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_UINT8(0x07, data->timeStatus);   // all three valid bits
}

void test_vn300_time_status_date_not_ok(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 0.0f, 0.0f, 0.0f);
    buf[86] = 0x01;   // timeOk only; dateOk cleared
    appendVnCrc(buf, kFrameSize);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(data.has_value());
    TEST_ASSERT_EQUAL_UINT8(0x01, data->timeStatus);
    TEST_ASSERT_FALSE(data->timeStatus & 0x02);   // dateOk clear
}

// EfisFrame::fieldsPresent bit assertions — VN-300 populates only the
// attitude triple on the EfisFrame; the full IMU/GPS dataset and per-sample
// timestamps are on Vn300Data via TakeVn300Data().
// ----------------------------------------------------------------------------
// Two consecutive distinct frames produce two coherent Vn300Data outputs.
// Each frame's lat/lon/pitch/roll/yaw must reflect ITS values, not a mix
// with the prior frame.  Guards against any future regression where the
// parser leaks state between frames (the bench atomic-publish detector
// caught this empirically; this test makes it CI-detectable).
//
// Note: this is a single-threaded parser test, so it doesn't exercise the
// mutex side of the atomic-publish fix.  But it pins down the structural
// invariant on the parser side: when Decode() runs, every field in
// Vn300Data comes from THIS frame, none from the previous frame.  A
// publisher that read partial fields between Decode() calls would still
// see torn structs at the EfisSerialPort level — that's the cross-task
// concurrency bug the mutex in EfisSerialPort guards against.
// ----------------------------------------------------------------------------
void test_vn300_back_to_back_frames_coherent(void)
{
    Vn300Parser parser;

    // Frame 1: distinctive values
    uint8_t buf1[kFrameSize];
    buildVn300Packet(buf1, 3.5f, 10.0f, 270.0f);
    // Override Lat/Lon to verifiable distinct values
    writeDouble(buf1, 38, 40.001000);   // Lat for frame 1
    writeDouble(buf1, 46, -105.001000); // Lon for frame 1
    writeU64(buf1, 10, 2'500'000ULL);   // TimeStartup = frame 1
    // Re-CRC after the field overrides
    {
        uint16_t crc = 0;
        for (size_t i = 1; i < kFrameSize - 2; ++i) {
            crc = static_cast<uint16_t>((crc >> 8) | (crc << 8));
            crc ^= buf1[i];
            crc ^= static_cast<uint16_t>(
                static_cast<uint8_t>(crc & 0xFF) >> 4);
            crc ^= static_cast<uint16_t>((crc << 8) << 4);
            crc ^= static_cast<uint16_t>(((crc & 0xFF) << 4) << 1);
        }
        buf1[kFrameSize - 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        buf1[kFrameSize - 1] = static_cast<uint8_t>(crc & 0xFF);
    }

    feedAll(parser, buf1, kFrameSize);
    auto d1 = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(d1.has_value());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 40.001000, d1->gnssLat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -105.001000, d1->gnssLon);
    TEST_ASSERT_EQUAL_UINT64(2'500'000ULL, d1->timeStartupNs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, d1->pitch);

    // Frame 2: different values entirely
    uint8_t buf2[kFrameSize];
    buildVn300Packet(buf2, -2.0f, -5.5f, 45.0f);
    writeDouble(buf2, 38, 41.222000);
    writeDouble(buf2, 46, -106.333000);
    writeU64(buf2, 10, 5'000'000ULL);
    {
        uint16_t crc = 0;
        for (size_t i = 1; i < kFrameSize - 2; ++i) {
            crc = static_cast<uint16_t>((crc >> 8) | (crc << 8));
            crc ^= buf2[i];
            crc ^= static_cast<uint16_t>(
                static_cast<uint8_t>(crc & 0xFF) >> 4);
            crc ^= static_cast<uint16_t>((crc << 8) << 4);
            crc ^= static_cast<uint16_t>(((crc & 0xFF) << 4) << 1);
        }
        buf2[kFrameSize - 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        buf2[kFrameSize - 1] = static_cast<uint8_t>(crc & 0xFF);
    }

    feedAll(parser, buf2, kFrameSize);
    auto d2 = parser.TakeVn300Data();
    TEST_ASSERT_TRUE(d2.has_value());

    // Every field in d2 must be from frame 2 — none can leak from frame 1.
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 41.222000, d2->gnssLat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -106.333000, d2->gnssLon);
    TEST_ASSERT_EQUAL_UINT64(5'000'000ULL, d2->timeStartupNs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -2.0f, d2->pitch);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.5f, d2->roll);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, d2->yaw);

    // The decoded `data` from frame 2 must NOT carry any frame-1 leftovers.
    TEST_ASSERT_NOT_EQUAL(40.001000,  d2->gnssLat);   // didn't keep frame-1 lat
    TEST_ASSERT_NOT_EQUAL(-105.001000, d2->gnssLon);   // didn't keep frame-1 lon
    TEST_ASSERT_NOT_EQUAL(2'500'000ULL, d2->timeStartupNs);   // didn't keep frame-1 time
}

void test_vn300_presence_bits_attitude_only(void)
{
    uint8_t buf[kFrameSize];
    buildVn300Packet(buf, 3.5f, 10.0f, 270.0f);
    Vn300Parser parser;
    feedAll(parser, buf, kFrameSize);
    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE(frame.has_value());
    const uint32_t fp = frame->fieldsPresent;
    using namespace onspeed::EfisField;
    TEST_ASSERT_TRUE(fp & Pitch);
    TEST_ASSERT_TRUE(fp & Roll);
    TEST_ASSERT_TRUE(fp & Heading);
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
    RUN_TEST(test_vn300_old_format_rejected);
    RUN_TEST(test_vn300_garbage_then_valid_parses);
    RUN_TEST(test_vn300_reset_clears_state);
    RUN_TEST(test_vn300_take_frame_clears_pending);
    RUN_TEST(test_vn300_take_data_clears_pending);
    RUN_TEST(test_vn300_gps_fix_in_data);
    RUN_TEST(test_vn300_yprU_axis_assignment);
    RUN_TEST(test_vn300_est_alt_meters_in_data);
    RUN_TEST(test_vn300_time_startup_ns_round_trip);
    RUN_TEST(test_vn300_time_gps_ns_round_trip);
    RUN_TEST(test_vn300_time_status_round_trip);
    RUN_TEST(test_vn300_time_status_date_not_ok);
    RUN_TEST(test_vn300_back_to_back_frames_coherent);
    RUN_TEST(test_vn300_presence_bits_attitude_only);
    return UNITY_END();
}
