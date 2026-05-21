// test_synth_frames.cpp — round-trip the synth wire frames through the real
// EFIS / boom parsers. Catches synth drift from the parser spec.
//
// If this test fails, the firmware's perf-synth build is silently feeding
// the parser something it can't decode — `perf dump` would show 0
// frames/sec instead of the protocol's native rate, and the whole
// "all-sensors-present perf capture" exercise turns into a measurement
// of an idle parser.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <test_frames/SynthFrames.h>
#include <boom/BoomParser.h>
#include <efis/Vn300.h>
#include <efis/DynonSkyview.h>
#include <types/EfisFrame.h>

using onspeed::test_frames::Frame;
using onspeed::test_frames::Vn300Frame;
using onspeed::test_frames::SkyviewFrames;
using onspeed::test_frames::BoomFrame;
using onspeed::efis::Vn300Parser;
using onspeed::efis::Vn300Data;
using onspeed::efis::DynonSkyviewParser;
using onspeed::EfisFrame;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// VN-300 — feed bytes through Vn300Parser, assert it produces both an
// EfisFrame and a Vn300Data with the cruise values we hardcoded.
// ---------------------------------------------------------------------------
void test_vn300_synth_decodes(void) {
    const Frame& f = Vn300Frame();
    TEST_ASSERT_EQUAL_size_t(127, f.len);

    Vn300Parser parser;
    for (std::size_t i = 0; i < f.len; i++) parser.FeedByte(f.bytes[i]);

    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE_MESSAGE(frame.has_value(),
                             "Vn300Parser did not produce a frame from synth bytes");

    // Cruise attitude — values match BuildVn300() in SynthFrames.cpp.
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  2.5f, frame->pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  0.5f, frame->rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, frame->headingDeg);

    auto data = parser.TakeVn300Data();
    TEST_ASSERT_TRUE_MESSAGE(data.has_value(),
                             "Vn300Parser did not produce Vn300Data from synth bytes");

    TEST_ASSERT_FLOAT_WITHIN(0.01f,  2.5f, data->pitch);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  0.5f, data->roll);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, data->yaw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, data->velNedNorth);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  5.0f, data->velNedEast);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, data->velNedDown);
    TEST_ASSERT_EQUAL(3, data->gpsFix);
    // Doubles (gnssLat / gnssLon / estAltMeters) are populated and round-trip
    // identical bytes; Unity's TEST_ASSERT_DOUBLE_WITHIN isn't enabled in this
    // build, so cast to float to verify (loses precision; OK for these
    // sentinel values 40.0 / -105.0 / 1500.0 which represent exactly).
    TEST_ASSERT_FLOAT_WITHIN(0.001f,   40.0f, static_cast<float>(data->gnssLat));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -105.0f, static_cast<float>(data->gnssLon));
    TEST_ASSERT_FLOAT_WITHIN(0.5f,   1500.0f, static_cast<float>(data->estAltMeters));
}

// ---------------------------------------------------------------------------
// Skyview — feed both frame types through DynonSkyviewParser, assert each
// produces a valid EfisFrame.
// ---------------------------------------------------------------------------
void test_skyview_adahrs_synth_decodes(void) {
    const Frame* frames = SkyviewFrames();
    const Frame& adahrs = frames[0];
    TEST_ASSERT_EQUAL_size_t(74, adahrs.len);

    DynonSkyviewParser parser;
    for (std::size_t i = 0; i < adahrs.len; i++) parser.FeedByte(adahrs.bytes[i]);

    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE_MESSAGE(frame.has_value(),
                             "DynonSkyviewParser did not decode synth !1 ADAHRS frame");

    // ADAHRS field values — match BuildSkyviewAdahrs() in SynthFrames.cpp.
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  2.5f, frame->pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  0.5f, frame->rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 95.0f, frame->iasKt);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,   5000.0f, frame->paltFt);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,    100.0f, frame->tasKt);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,    1.0f, frame->verticalG);
}

void test_skyview_ems_synth_decodes(void) {
    const Frame* frames = SkyviewFrames();
    const Frame& ems = frames[1];
    TEST_ASSERT_EQUAL_size_t(225, ems.len);

    DynonSkyviewParser parser;
    for (std::size_t i = 0; i < ems.len; i++) parser.FeedByte(ems.bytes[i]);

    auto frame = parser.TakeFrame();
    TEST_ASSERT_TRUE_MESSAGE(frame.has_value(),
                             "DynonSkyviewParser did not decode synth !3 EMS frame");

    // EMS field values — match BuildSkyviewEms() in SynthFrames.cpp.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 2500.0f, frame->rpm);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.0f, frame->mapInchHg);
    TEST_ASSERT_FLOAT_WITHIN(0.05f,  8.0f, frame->fuelFlowGph);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 30.0f, frame->fuelRemainingGal);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  70.0f, frame->percentPower);
}

// ---------------------------------------------------------------------------
// Boom — drive the synth bytes through onspeed::boom::Decode and assert
// it decodes to the values BuildBoom() seeded. Catches drift in either
// the synth wire format or the parser.
// ---------------------------------------------------------------------------
void test_boom_synth_well_formed(void) {
    const Frame& f = BoomFrame();

    TEST_ASSERT_GREATER_OR_EQUAL_size_t(25, f.len);
    TEST_ASSERT_EQUAL_CHAR('$', static_cast<char>(f.bytes[0]));
    TEST_ASSERT_EQUAL_CHAR('\r', static_cast<char>(f.bytes[f.len - 2]));
    TEST_ASSERT_EQUAL_CHAR('\n', static_cast<char>(f.bytes[f.len - 1]));

    // Real $AIRDAQ uses ',' as the separator before the CRC. The synth
    // matches the real wire format (see SynthFrames.cpp::BuildBoom).
    TEST_ASSERT_EQUAL_CHAR(',', static_cast<char>(f.bytes[f.len - 5]));

    // First int must start at offset 21 — that's BoomSerial.cpp / boom::Decode's
    // fixed anchor for the comma-separated integer fields.
    TEST_ASSERT_EQUAL_CHAR(',', static_cast<char>(f.bytes[20]));

    // Round-trip through the pure parser. Strip CR/LF before decode, as
    // BoomSerial.cpp does at the framing layer.
    const int decodeLen = static_cast<int>(f.len) - 2;
    onspeed::boom::BoomFrame parsed =
        onspeed::boom::Decode(reinterpret_cast<const char*>(f.bytes),
                              decodeLen, /*checkCrc=*/true);
    TEST_ASSERT_TRUE_MESSAGE(parsed.valid,
                             "Boom synth frame failed to round-trip through Decode");
    // BuildBoom seeds 9842, 8152, 3942, 4006.
    TEST_ASSERT_EQUAL_INT(9842, parsed.staticCounts);
    TEST_ASSERT_EQUAL_INT(8152, parsed.dynamicCounts);
    TEST_ASSERT_EQUAL_INT(3942, parsed.alphaCounts);
    TEST_ASSERT_EQUAL_INT(4006, parsed.betaCounts);
}

// ---------------------------------------------------------------------------
// Multi-frame cycle: pipe several VN-300 frames back-to-back through the
// parser, assert each one decodes. This is what the firmware actually does
// — fixed frame, repeated. We're confirming the parser is happy with
// identical-byte streams (which it absolutely should be, but a regression
// here would silently zero the perf numbers).
// ---------------------------------------------------------------------------
void test_vn300_cycle_decodes_repeatedly(void) {
    const Frame& f = Vn300Frame();
    Vn300Parser parser;

    for (int cycle = 0; cycle < 5; cycle++) {
        for (std::size_t i = 0; i < f.len; i++) parser.FeedByte(f.bytes[i]);
        auto frame = parser.TakeFrame();
        TEST_ASSERT_TRUE_MESSAGE(frame.has_value(),
                                 "VN-300 parser failed on a repeated frame");
        // Drain the Vn300Data too so it doesn't accumulate.
        (void)parser.TakeVn300Data();
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_vn300_synth_decodes);
    RUN_TEST(test_skyview_adahrs_synth_decodes);
    RUN_TEST(test_skyview_ems_synth_decodes);
    RUN_TEST(test_boom_synth_well_formed);
    RUN_TEST(test_vn300_cycle_decodes_repeatedly);
    return UNITY_END();
}
