// test_attitude_mode.cpp — PR-0 baseline assertions for `AiGraph` and
// `pitchGraph` (display mode 1, the attitude indicator page).

#include "TestHelpers.h"
#include "goldens.h"

void test_AiGraph_level_flight_draws_cyan_sky_and_ground(void)
{
    resetState();
    g_state.Pitch      = 0.0f;
    g_state.Roll       = 0.0f;
    g_state.FlightPath = 0.0f;

    AiGraph(159, 119, 115, 15, 360, 0, 0, 360, true, 0,
            int16_t(0), int16_t(0), 360, 0.0f);

    // Cyan sky fill must happen.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1,
        drawEvents().colorHistogram().count(0x07FF));    // TFT_CYAN
    // Brown ground color (0x8281) must appear at least 2× (two triangles).
    TEST_ASSERT_EQUAL_UINT32(2, drawEvents().colorHistogram().at(0x8281));
    // Flight-path marker is magenta; multiple lines/circles.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(6,
        drawEvents().colorHistogram().at(0xF81F));       // TFT_MAGENTA

    // Anchor: the AI center marker is a black-outlined yellow circle at
    // (159, 119) with radius 2*HEIGHT/80 = 6.
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("fillCircle", { 159, 119, 6 }),
        "AI center yellow circle at (159,119) r=6");
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("drawCircle", { 159, 119, 6 }),
        "AI center black outline at (159,119) r=6");

    // The pitch-scale labels -90..90 step 10 produce 19 printNum events.
    // The -85..85 step 10 loop is line-only, so printNum count is 19.
    TEST_ASSERT_EQUAL_UINT32(19, drawEvents().drawCallCount("gauges.printNum"));

    assertOrPrintGolden("GOLDEN_attitude_level", GOLDEN_attitude_level);
}

void test_AiGraph_banked_roll_draws_pitch_scale(void)
{
    resetState();
    g_state.Pitch      = 5.0f;
    g_state.Roll       = 20.0f;
    g_state.FlightPath = 3.0f;

    AiGraph(159, 119, 115, 15, 360, 0, 0, 360, true, 0,
            int16_t(5), int16_t(20), 360, 3.0f);

    // Flight-path marker Y is computed from (flightPathAngle - Pitch) = -2
    // → fpY = 120 - (-2)*120/40 = 120 + 6 = 126. Verify 3 concentric rings
    // at that Y.
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("drawCircle", { 159, 126, 12 }),
        "flight-path marker inner ring r=12 at fpY=126");
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("drawCircle", { 159, 126, 14 }),
        "flight-path marker outer ring r=14 at fpY=126");

    assertOrPrintGolden("GOLDEN_attitude_banked", GOLDEN_attitude_banked);
}

void test_pitchGraph_emits_degree_labels(void)
{
    resetState();
    // Baseline: call pitchGraph alone (AiGraph calls it as a sub-step, but
    // isolating it makes the assertion easier to reason about).
    pitchGraph(0, 0, 159, 119, 10);

    // setTextDatum(MC_DATUM) at entry, restored to baseline_left at exit.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, drawEvents().drawCallCount("setTextDatum"));

    // 19 labels from -90..90 step 10.
    TEST_ASSERT_EQUAL_UINT32(19, drawEvents().drawCallCount("gauges.printNum"));
}
