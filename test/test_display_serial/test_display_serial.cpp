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
    TEST_ASSERT_FLOAT_WITHIN(DELTA_1,   0.0f, f.vsiFpm);
    TEST_ASSERT_EQUAL(0, f.oatC);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  0.0f, f.flightPathDeg);
    TEST_ASSERT_EQUAL(0, f.flapsDeg);
    TEST_ASSERT_EQUAL(0, f.tonesOnPctLift);
    TEST_ASSERT_EQUAL(0, f.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL(0, f.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL(0, f.stallWarnPctLift);
    TEST_ASSERT_EQUAL(0, f.flapsMinDeg);
    TEST_ASSERT_EQUAL(0, f.flapsMaxDeg);
    TEST_ASSERT_FLOAT_WITHIN(DELTA_100, 0.0f, f.gOnsetRate);
    TEST_ASSERT_EQUAL(0, f.spinRecoveryCue);
    TEST_ASSERT_EQUAL(0, f.dataMark);
    TEST_ASSERT_EQUAL(0, f.pipPctLift);
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
    // verticalGScaled10 is the encoded int (×10).  The encoder side
    // (sketch_common/src/io/DisplaySerial.cpp) uses lroundf(accel * 10);
    // here we just feed the protocol layer a representative integer.
    // 11 corresponds to 1.05–1.149 g rounded to the nearest 0.1.
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

void test_roundtrip_band_edge_percents(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.tonesOnPctLift     = 33;   // L/Dmax body angle through the percent-lift formula
    in.onSpeedFastPctLift = 55;
    in.onSpeedSlowPctLift = 74;
    in.stallWarnPctLift   = 88;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(33, f.tonesOnPctLift);
    TEST_ASSERT_EQUAL(55, f.onSpeedFastPctLift);
    TEST_ASSERT_EQUAL(74, f.onSpeedSlowPctLift);
    TEST_ASSERT_EQUAL(88, f.stallWarnPctLift);
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

void test_roundtrip_pip_pct_lift(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pipPctLift = 53;     // typical mid-deployment value for an RV-10 cal
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(53, f.pipPctLift);
}

void test_pip_pct_lift_at_offset_71(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pipPctLift = 47;
    buildOk(in);
    // Pin the wire-format invariant: pipPctLift occupies bytes 71-72
    // of the frame at v4.23 (just before the 2-byte checksum and CRLF).
    // Shifted from offset 70 at v4.22 because percentLift widened
    // %02u → %03u for tenths-of-a-percent resolution.
    TEST_ASSERT_EQUAL('4', frameBuf[71]);
    TEST_ASSERT_EQUAL('7', frameBuf[72]);
}

void test_pip_pct_lift_clamps_high(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pipPctLift = 250;   // way above the [0,99] saturation range
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(99, f.pipPctLift);
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
    // percentLift on the wire is tenths of a percent (0..999) at v4.23.
    // Clamp saturates at 999 — never emits 1000 (which would render
    // 100.0% on the consumer side).
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 1234;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(999, f.percentLift);
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

void test_percent_lift_sub_percent_roundtrip(void)
{
    // Wire scale at v4.23 is tenths-of-a-percent. 473 = 47.3% lift.
    // The integer-percent consumer reads 47 (`/10`); the tenths
    // consumer reads 47.3 (`/10.0f`).
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 473;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(473, f.percentLift);
}

void test_percent_lift_999_max(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.percentLift = 999;
    buildOk(in);
    DisplayFrame f = parseOk();
    TEST_ASSERT_EQUAL(999, f.percentLift);
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
    // percentLift widened to %03u at v4.23 (tenths of a percent, 0..999).
    char expected_payload[200];
    int n = snprintf(
        expected_payload, sizeof(expected_payload),
        "#1%+04i%+05i%04u%+06i%+05i%+03i%+03i%03u%+04i%+03i%+04i%+03i%02u%02u%02u%02u%+03i%+03i%+04i%+02i%02u%02u",
        0, 0, 0u, 0, 0, 0, 0, 0u, 0, 0, 0, 0,
        0u, 0u, 0u, 0u,                          // tonesOn/Fast/Slow/StallWarn pct
        0, 0,                                    // flapsMin, flapsMax
        0, 0, 0u,
        0u);                                     // pipPctLift
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
// Round-trip: per-flap percent anchors and flap range
// ----------------------------------------------------------------------------

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
// DisplayFrameAccumulator — byte-stream framing
//
// These tests would have caught the class of wire-format-change
// regression where a consumer's hand-rolled state machine hardcodes a
// stale frame size.  Pump a built frame through the accumulator
// byte-by-byte and verify a parsed frame comes out at the last byte.
// ----------------------------------------------------------------------------

void test_accumulator_parses_complete_frame(void)
{
    // Build a frame with non-trivial values so we'd notice if the
    // wrong frame got returned.
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg          = 7.5f;
    in.percentLift       = 42;
    in.tonesOnPctLift    = 33;
    in.onSpeedFastPctLift = 55;
    buildOk(in);

    DisplayFrameAccumulator accum;

    // Inject all bytes except the last; nothing should parse.
    for (size_t i = 0; i + 1 < kDisplayFrameSizeBytes; ++i) {
        auto r = accum.Inject(frameBuf[i]);
        TEST_ASSERT_FALSE(r.has_value());
    }
    // The last byte (LF) completes the frame and should return it.
    auto r = accum.Inject(frameBuf[kDisplayFrameSizeBytes - 1]);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10,  7.5f, r->pitchDeg);
    TEST_ASSERT_EQUAL(42, r->percentLift);
    TEST_ASSERT_EQUAL(33, r->tonesOnPctLift);
    TEST_ASSERT_EQUAL(55, r->onSpeedFastPctLift);
}

void test_accumulator_ignores_bytes_before_magic(void)
{
    DisplayFrameAccumulator accum;
    // Garbage before any '#' is silently dropped.
    static const uint8_t kGarbage[] = {0x00, 0xFF, 'a', 'Z', '?', '!'};
    for (uint8_t b : kGarbage) {
        auto r = accum.Inject(b);
        TEST_ASSERT_FALSE(r.has_value());
    }
    TEST_ASSERT_FALSE(accum.InProgress());
    TEST_ASSERT_EQUAL(0u, accum.Length());
}

void test_accumulator_resets_on_mid_frame_hash(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = 12.3f;
    buildOk(in);

    DisplayFrameAccumulator accum;
    // Start a frame, then partway through, send a stray '#' which
    // should reset to start-of-frame and start over.
    for (size_t i = 0; i < 20; ++i) {
        accum.Inject(frameBuf[i]);
    }
    TEST_ASSERT_EQUAL(20u, accum.Length());

    // Send a stray '#' — should reset.
    auto r = accum.Inject('#');
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(1u, accum.Length());

    // Now send the rest of a real frame starting from byte 1.
    for (size_t i = 1; i < kDisplayFrameSizeBytes - 1; ++i) {
        auto r2 = accum.Inject(frameBuf[i]);
        TEST_ASSERT_FALSE(r2.has_value());
    }
    auto rEnd = accum.Inject(frameBuf[kDisplayFrameSizeBytes - 1]);
    TEST_ASSERT_TRUE(rEnd.has_value());
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 12.3f, rEnd->pitchDeg);
}

void test_accumulator_drops_frame_without_lf_terminator(void)
{
    DisplayFrameAccumulator accum;
    // Send '#' followed by kDisplayFrameSizeBytes-1 garbage bytes.
    // Buffer fills to capacity but the final byte is 'X', not LF, so
    // the validation check at the full-frame point drops the frame
    // and resets the accumulator to idle.
    accum.Inject('#');
    for (size_t i = 1; i < kDisplayFrameSizeBytes; ++i) {
        auto r = accum.Inject('X');
        TEST_ASSERT_FALSE(r.has_value());
    }
    TEST_ASSERT_FALSE(accum.InProgress());
}

void test_accumulator_rejects_bad_crc(void)
{
    DisplayBuildInputs in = zeroInputs();
    in.pitchDeg = 5.0f;
    buildOk(in);

    // Corrupt one byte in the payload to break the CRC.
    frameBuf[10] ^= 0x01;

    DisplayFrameAccumulator accum;
    for (size_t i = 0; i < kDisplayFrameSizeBytes; ++i) {
        auto r = accum.Inject(frameBuf[i]);
        // No frame should ever be emitted (CRC fails on the final byte).
        TEST_ASSERT_FALSE(r.has_value());
    }
    // Accumulator should have reset itself after the failed parse.
    TEST_ASSERT_FALSE(accum.InProgress());
}

void test_accumulator_back_to_back_frames(void)
{
    // After a valid frame parses, a second frame should also parse.
    // This catches a class of bug where the accumulator doesn't fully
    // reset its internal state between frames.
    DisplayBuildInputs in1 = zeroInputs();
    in1.pitchDeg = 1.5f;

    DisplayBuildInputs in2 = zeroInputs();
    in2.pitchDeg = -2.5f;

    DisplayFrameAccumulator accum;

    uint8_t buf1[kDisplayFrameSizeBytes];
    TEST_ASSERT_EQUAL(kDisplayFrameSizeBytes,
                      BuildDisplayFrame(in1, buf1, sizeof(buf1)));
    for (size_t i = 0; i + 1 < kDisplayFrameSizeBytes; ++i) {
        accum.Inject(buf1[i]);
    }
    auto r1 = accum.Inject(buf1[kDisplayFrameSizeBytes - 1]);
    TEST_ASSERT_TRUE(r1.has_value());
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, 1.5f, r1->pitchDeg);

    uint8_t buf2[kDisplayFrameSizeBytes];
    TEST_ASSERT_EQUAL(kDisplayFrameSizeBytes,
                      BuildDisplayFrame(in2, buf2, sizeof(buf2)));
    for (size_t i = 0; i + 1 < kDisplayFrameSizeBytes; ++i) {
        accum.Inject(buf2[i]);
    }
    auto r2 = accum.Inject(buf2[kDisplayFrameSizeBytes - 1]);
    TEST_ASSERT_TRUE(r2.has_value());
    TEST_ASSERT_FLOAT_WITHIN(DELTA_10, -2.5f, r2->pitchDeg);
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
    RUN_TEST(test_roundtrip_vsi);
    RUN_TEST(test_roundtrip_oat);
    RUN_TEST(test_roundtrip_flight_path);
    RUN_TEST(test_roundtrip_flaps);
    RUN_TEST(test_roundtrip_band_edge_percents);
    RUN_TEST(test_roundtrip_data_mark);
    RUN_TEST(test_roundtrip_spin_cue_positive);
    RUN_TEST(test_roundtrip_spin_cue_negative);
    RUN_TEST(test_roundtrip_pip_pct_lift);
    RUN_TEST(test_pip_pct_lift_at_offset_71);
    RUN_TEST(test_pip_pct_lift_clamps_high);

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
    RUN_TEST(test_percent_lift_sub_percent_roundtrip);
    RUN_TEST(test_percent_lift_999_max);

    RUN_TEST(test_parse_null_buffer);
    RUN_TEST(test_parse_too_short);
    RUN_TEST(test_parse_wrong_magic_first_byte);
    RUN_TEST(test_parse_wrong_magic_second_byte);
    RUN_TEST(test_parse_bad_checksum);
    RUN_TEST(test_parse_corrupted_checksum_field);

    RUN_TEST(test_build_null_output);
    RUN_TEST(test_build_small_output);

    RUN_TEST(test_known_frame_content);

    RUN_TEST(test_roundtrip_flap_range);
    RUN_TEST(test_roundtrip_flap_range_negative_min);

    RUN_TEST(test_accumulator_parses_complete_frame);
    RUN_TEST(test_accumulator_ignores_bytes_before_magic);
    RUN_TEST(test_accumulator_resets_on_mid_frame_hash);
    RUN_TEST(test_accumulator_drops_frame_without_lf_terminator);
    RUN_TEST(test_accumulator_rejects_bad_crc);
    RUN_TEST(test_accumulator_back_to_back_frames);

    return UNITY_END();
}
