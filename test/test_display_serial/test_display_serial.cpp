// test_display_serial.cpp — unit tests for onspeed::proto DisplaySerial
//
// Covers:
//   - All-zero round-trip
//   - Per-field round-trip (each field non-zero, rest zero)
//   - Clamping: values that exceed field width are clamped, not corrupted
//   - CRC failure → nullopt
//   - Truncated frame → nullopt
//   - Wrong magic → nullopt
//   - Exact byte-level check of a known frame
//   - Extreme values (pitch ±90, IAS 0, IAS 500)
//   - Special percentLift values (0, 50, 99)

#include <unity.h>
#include <proto/DisplaySerial.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace onspeed::proto;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

static uint8_t frameBuf[kDisplayFrameSizeBytes + 4];

static DisplayBuildInputs zeroInputs()
{
    return DisplayBuildInputs{};
}

// Build into frameBuf, assert success.
static void buildOk(const DisplayBuildInputs& in)
{
    memset(frameBuf, 0, sizeof(frameBuf));
    size_t n = BuildDisplayFrame(in, frameBuf, sizeof(frameBuf));
    TEST_ASSERT_EQUAL(kDisplayFrameSizeBytes, n);
}

// Parse from frameBuf, assert success.
static DisplayFrame parseOk()
{
    auto opt = ParseDisplayFrame(frameBuf, kDisplayFrameSizeBytes);
    TEST_ASSERT_TRUE(opt.has_value());
    return *opt;
}

// Float tolerance for round-trip: wire resolution is 0.1 for ×10 fields,
// 0.01 for ×100 fields.
static const float DELTA_10  = 0.11f;
static const float DELTA_100 = 0.011f;
static const float DELTA_1   = 1.01f;

// ----------------------------------------------------------------------------
// Basic frame structure
// ----------------------------------------------------------------------------

void test_all_zero_builds_correct_length(void)
{
    DisplayBuildInputs in = zeroInputs();
    memset(frameBuf, 0, sizeof(frameBuf));
    size_t n = BuildDisplayFrame(in, frameBuf, sizeof(frameBuf));
    TEST_ASSERT_EQUAL((size_t)kDisplayFrameSizeBytes, n);
}

void test_frame_starts_with_magic(void)
{
    buildOk(zeroInputs());
    TEST_ASSERT_EQUAL('#', frameBuf[0]);
    TEST_ASSERT_EQUAL('1', frameBuf[1]);
}

void test_frame_ends_with_crlf(void)
{
    buildOk(zeroInputs());
    TEST_ASSERT_EQUAL(0x0D, frameBuf[kDisplayFrameSizeBytes - 2]);
    TEST_ASSERT_EQUAL(0x0A, frameBuf[kDisplayFrameSizeBytes - 1]);
}

void test_checksum_bytes_are_hex_digits(void)
{
    buildOk(zeroInputs());
    // Two ASCII hex digits (0-9, A-F) immediately before the CRLF.
    auto isHex = [](uint8_t c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    };
    TEST_ASSERT_TRUE(isHex(frameBuf[kDisplayFrameChecksumLen]));
    TEST_ASSERT_TRUE(isHex(frameBuf[kDisplayFrameChecksumLen + 1]));
}

// ----------------------------------------------------------------------------
// Round-trip: all zeros
// ----------------------------------------------------------------------------

void test_roundtrip_all_zeros(void)
{
    buildOk(zeroInputs());
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.iasKt);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_1,   0.0f, f.paltFt);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.turnRateDps);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_100, 0.0f, f.lateralG);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.verticalG);
    TEST_ASSERT_EQUAL(0, f.percentLift);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.aoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_1,   0.0f, f.vsiFpm);
    TEST_ASSERT_EQUAL(0, f.oatC);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.flightPathDeg);
    TEST_ASSERT_EQUAL(0, f.flapsDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.stallWarnAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.onSpeedSlowAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.onSpeedFastAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.tonesOnAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.alpha0Deg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.alphaStallDeg);
    TEST_ASSERT_EQUAL(0, f.flapsMinDeg);
    TEST_ASSERT_EQUAL(0, f.flapsMaxDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_100, 0.0f, f.gOnsetRate);
    TEST_ASSERT_EQUAL(0, f.spinRecoveryCue);
    TEST_ASSERT_EQUAL(0, f.dataMark);
}

// ----------------------------------------------------------------------------
// Round-trip: individual fields
// ----------------------------------------------------------------------------

void test_roundtrip_pitch(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = 12.3f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 12.3f, f.pitchDeg);
}

void test_roundtrip_pitch_negative(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = -8.7f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -8.7f, f.pitchDeg);
}

void test_roundtrip_roll(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.rollDeg = -45.2f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -45.2f, f.rollDeg);
}

void test_roundtrip_ias(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.iasKt = 105.5f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 105.5f, f.iasKt);
}

void test_roundtrip_palt(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.paltFt = 3500.0f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_1, 3500.0f, f.paltFt);
}

void test_roundtrip_lateral_g(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.lateralG = 0.05f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_100, 0.05f, f.lateralG);
}

void test_roundtrip_vertical_g(void)
{
    DisplayBuildInputs in = zeroInputs();
    // verticalGScaled10 = ceilf(1.02 * 10) = 11
    in.verticalGScaled10 = 11.0f;
    buildOk(in);
    DisplayFrame f = parseOk();
    // Wire value 11 / 10 = 1.1
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 1.1f, f.verticalG);
}

void test_roundtrip_percent_lift(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 55;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(55, f.percentLift);
}

void test_roundtrip_aoa(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.aoaDeg = 14.7f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 14.7f, f.aoaDeg);
}

void test_roundtrip_vsi(void)
{
    // vsiFpm10 is already vsi/10; ParseFrame gives vsiFpm = wire × 10
    DisplayBuildInputs in = zeroInputs();
    in.vsiFpm10 = -50;   // represents –500 fpm
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_1, -500.0f, f.vsiFpm);
}

void test_roundtrip_oat(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.oatC = 18;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(18, f.oatC);
}

void test_roundtrip_flight_path(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.flightPathDeg = -3.2f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -3.2f, f.flightPathDeg);
}

void test_roundtrip_flaps(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.flapsDeg = 25;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(25, f.flapsDeg);
}

void test_roundtrip_setpoints(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.stallWarnAoaDeg   = 18.0f;
    in.onSpeedSlowAoaDeg = 14.5f;
    in.onSpeedFastAoaDeg = 11.0f;
    in.tonesOnAoaDeg     =  6.5f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 18.0f, f.stallWarnAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 14.5f, f.onSpeedSlowAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 11.0f, f.onSpeedFastAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  6.5f, f.tonesOnAoaDeg);
}

void test_roundtrip_data_mark(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.dataMark = 42;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(42, f.dataMark);
}

void test_roundtrip_spin_cue_positive(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.spinRecoveryCue = 1;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(1, f.spinRecoveryCue);
}

void test_roundtrip_spin_cue_negative(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.spinRecoveryCue = -1;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(-1, f.spinRecoveryCue);
}

// ----------------------------------------------------------------------------
// Clamping tests — out-of-range inputs must not overflow the fixed-width field
// ----------------------------------------------------------------------------

void test_clamp_pitch_high(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = 9999.0f;   // way beyond ±99.9 field maximum
    buildOk(in);   // must not return 0
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 99.9f, f.pitchDeg);
}

void test_clamp_pitch_low(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = -9999.0f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -99.9f, f.pitchDeg);
}

void test_clamp_roll_high(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.rollDeg = 99999.0f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 999.9f, f.rollDeg);
}

void test_clamp_percent_lift_high(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 999;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(99, f.percentLift);
}

// ----------------------------------------------------------------------------
// Special values
// ----------------------------------------------------------------------------

void test_pitch_plus_90(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = 90.0f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 90.0f, f.pitchDeg);
}

void test_pitch_minus_90(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = -90.0f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -90.0f, f.pitchDeg);
}

void test_ias_zero(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.iasKt = 0.0f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 0.0f, f.iasKt);
}

void test_ias_high(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.iasKt = 350.0f;   // fast jet, well within 4-digit ×10 field
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 350.0f, f.iasKt);
}

void test_percent_lift_zero(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 0;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(0, f.percentLift);
}

void test_percent_lift_50(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 50;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(50, f.percentLift);
}

void test_percent_lift_99(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 99;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(99, f.percentLift);
}

// ----------------------------------------------------------------------------
// Parse failures
// ----------------------------------------------------------------------------

void test_parse_null_buffer(void)
{
    auto opt = ParseDisplayFrame(nullptr, kDisplayFrameSizeBytes);
    TEST_ASSERT_FALSE(opt.has_value());
}

void test_parse_too_short(void)
{
    buildOk(zeroInputs());
    auto opt = ParseDisplayFrame(frameBuf, kDisplayFrameSizeBytes - 1);
    TEST_ASSERT_FALSE(opt.has_value());
}

void test_parse_wrong_magic_first_byte(void)
{
    buildOk(zeroInputs());
    frameBuf[0] = '=';   // corrupt magic
    auto opt = ParseDisplayFrame(frameBuf, kDisplayFrameSizeBytes);
    TEST_ASSERT_FALSE(opt.has_value());
}

void test_parse_wrong_magic_second_byte(void)
{
    buildOk(zeroInputs());
    frameBuf[1] = '2';   // corrupt magic
    auto opt = ParseDisplayFrame(frameBuf, kDisplayFrameSizeBytes);
    TEST_ASSERT_FALSE(opt.has_value());
}

void test_parse_bad_checksum(void)
{
    buildOk(zeroInputs());
    // Flip a bit in the payload to invalidate the checksum.
    frameBuf[10] ^= 0x01;
    auto opt = ParseDisplayFrame(frameBuf, kDisplayFrameSizeBytes);
    TEST_ASSERT_FALSE(opt.has_value());
}

void test_parse_corrupted_checksum_field(void)
{
    buildOk(zeroInputs());
    // Replace the CRC bytes with non-hex characters.
    frameBuf[kDisplayFrameChecksumLen]     = 'G';
    frameBuf[kDisplayFrameChecksumLen + 1] = 'Z';
    auto opt = ParseDisplayFrame(frameBuf, kDisplayFrameSizeBytes);
    TEST_ASSERT_FALSE(opt.has_value());
}

// ----------------------------------------------------------------------------
// BuildFrame: null/small output buffer rejected
// ----------------------------------------------------------------------------

void test_build_null_output(void)
{
    DisplayBuildInputs in = zeroInputs();
    size_t n = BuildDisplayFrame(in, nullptr, kDisplayFrameSizeBytes);
    TEST_ASSERT_EQUAL(0u, n);
}

void test_build_small_output(void)
{
    DisplayBuildInputs in = zeroInputs();
    uint8_t small[10];
    size_t n = BuildDisplayFrame(in, small, sizeof(small));
    TEST_ASSERT_EQUAL(0u, n);
}

// ----------------------------------------------------------------------------
// Byte-level regression: known frame for all-zero inputs
//
// Generate the expected frame by running the same snprintf that the Gen3
// firmware uses and comparing byte-for-byte.
// ----------------------------------------------------------------------------

void test_known_frame_content(void)
{
    // Compute expected payload directly (mirrors Gen3 DisplaySerial::Write).
    char expected_payload[200];
    int n = snprintf(
        expected_payload, sizeof(expected_payload),
        "#1%+04i%+05i%04u%+06i%+05i%+03i%+03i%02u%+04i%+04i%+03i%+04i%+03i%+04i%+04i%+04i%+04i%+04i%+04i%+03i%+03i%+04i%+02i%02u",
        0, 0, 0u, 0, 0, 0, 0, 0u, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0,                                            // alpha0, alphaStall, flapsMin, flapsMax
        0, 0, 0u);
    TEST_ASSERT_EQUAL(static_cast<int>(kDisplayFrameChecksumLen), n);

    buildOk(zeroInputs());

    // Compare the payload bytes.
    TEST_ASSERT_EQUAL_MEMORY(expected_payload, frameBuf, kDisplayFrameChecksumLen);

    // The last two bytes must be CRLF.
    TEST_ASSERT_EQUAL(0x0D, frameBuf[kDisplayFrameSizeBytes - 2]);
    TEST_ASSERT_EQUAL(0x0A, frameBuf[kDisplayFrameSizeBytes - 1]);

    // Verify the frame is parseable.
    auto opt = ParseDisplayFrame(frameBuf, kDisplayFrameSizeBytes);
    TEST_ASSERT_TRUE(opt.has_value());
}

// ----------------------------------------------------------------------------
// Round-trip: new fields added in PR (alpha_0, alpha_stall, flap range)
// ----------------------------------------------------------------------------

void test_roundtrip_alpha0_negative(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.alpha0Deg = -3.7f;   // typical clean-config zero-lift
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -3.7f, f.alpha0Deg);
}

void test_roundtrip_alpha0_full_flaps(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.alpha0Deg = -9.2f;   // full-flap, more negative
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -9.2f, f.alpha0Deg);
}

void test_roundtrip_alpha_stall(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.alphaStallDeg = 11.6f;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 11.6f, f.alphaStallDeg);
}

void test_roundtrip_flap_range(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.flapsMinDeg = 0;
    in.flapsMaxDeg = 33;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(0,  f.flapsMinDeg);
    TEST_ASSERT_EQUAL(33, f.flapsMaxDeg);
}

void test_roundtrip_flap_range_negative_min(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.flapsMinDeg = -10;   // a glider with reflex flaps
    in.flapsMaxDeg =  40;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(-10, f.flapsMinDeg);
    TEST_ASSERT_EQUAL( 40, f.flapsMaxDeg);
}

// ----------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_all_zero_builds_correct_length);
    RUN_TEST(test_frame_starts_with_magic);
    RUN_TEST(test_frame_ends_with_crlf);
    RUN_TEST(test_checksum_bytes_are_hex_digits);

    RUN_TEST(test_roundtrip_all_zeros);
    RUN_TEST(test_roundtrip_pitch);
    RUN_TEST(test_roundtrip_pitch_negative);
    RUN_TEST(test_roundtrip_roll);
    RUN_TEST(test_roundtrip_ias);
    RUN_TEST(test_roundtrip_palt);
    RUN_TEST(test_roundtrip_lateral_g);
    RUN_TEST(test_roundtrip_vertical_g);
    RUN_TEST(test_roundtrip_percent_lift);
    RUN_TEST(test_roundtrip_aoa);
    RUN_TEST(test_roundtrip_vsi);
    RUN_TEST(test_roundtrip_oat);
    RUN_TEST(test_roundtrip_flight_path);
    RUN_TEST(test_roundtrip_flaps);
    RUN_TEST(test_roundtrip_setpoints);
    RUN_TEST(test_roundtrip_data_mark);
    RUN_TEST(test_roundtrip_spin_cue_positive);
    RUN_TEST(test_roundtrip_spin_cue_negative);

    RUN_TEST(test_clamp_pitch_high);
    RUN_TEST(test_clamp_pitch_low);
    RUN_TEST(test_clamp_roll_high);
    RUN_TEST(test_clamp_percent_lift_high);

    RUN_TEST(test_pitch_plus_90);
    RUN_TEST(test_pitch_minus_90);
    RUN_TEST(test_ias_zero);
    RUN_TEST(test_ias_high);
    RUN_TEST(test_percent_lift_zero);
    RUN_TEST(test_percent_lift_50);
    RUN_TEST(test_percent_lift_99);

    RUN_TEST(test_parse_null_buffer);
    RUN_TEST(test_parse_too_short);
    RUN_TEST(test_parse_wrong_magic_first_byte);
    RUN_TEST(test_parse_wrong_magic_second_byte);
    RUN_TEST(test_parse_bad_checksum);
    RUN_TEST(test_parse_corrupted_checksum_field);

    RUN_TEST(test_build_null_output);
    RUN_TEST(test_build_small_output);

    RUN_TEST(test_known_frame_content);

    RUN_TEST(test_roundtrip_alpha0_negative);
    RUN_TEST(test_roundtrip_alpha0_full_flaps);
    RUN_TEST(test_roundtrip_alpha_stall);
    RUN_TEST(test_roundtrip_flap_range);
    RUN_TEST(test_roundtrip_flap_range_negative_min);

    return UNITY_END();
}
