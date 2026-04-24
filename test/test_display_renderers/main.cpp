// main.cpp — Unity entry point for the test_display_renderers suite.
//
// These tests run on native x86 only (`pio test -e native`). CI pins
// ubuntu-latest which is x86-64 — the same platform used for baseline
// capture. Cross-compiling this suite for ESP32 is explicitly out of
// scope; see the spec's "Floating-point determinism for render tests"
// section.
//
// Golden-update workflow. When a deliberate layout change lands, run:
//
//    UPDATE_GOLDEN=1 pio test -e native -f test_display_renderers
//
// The test body prints its observed coordHash() instead of asserting
// on it. Paste the printed values into goldens.h, re-run the suite
// without the env var, and commit both the code change and the golden
// update together. Per-call-type counts and anchor assertions stay
// live even in update mode — a golden bump accompanied by a stale
// count assertion is a review signal.

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---- Forward declarations (one per test file) -------------------------------

// test_display_aoa.cpp
void test_displayAOA_draws_expected_shapes(void);
void test_displayAOA_without_numeric_display(void);
void test_displayAOA_no_chevron_when_aoa_below_tones_on(void);

// test_attitude_mode.cpp
void test_AiGraph_level_flight_draws_cyan_sky_and_ground(void);
void test_AiGraph_banked_roll_draws_pitch_scale(void);
void test_pitchGraph_emits_degree_labels(void);

// test_decel_gauge.cpp
void test_displayDecelGauge_zero_decel_centers_indicator(void);
void test_displayDecelGauge_negative_decel_moves_indicator_up(void);

// test_g_history.cpp
void test_displayGloadHistory_flat_1g_trace_all_green(void);
void test_displayGloadHistory_varied_trace_mixes_colors(void);

// test_splash.cpp
void test_displaySplashScreen_draws_title_and_version(void);

// ---- Runner ------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_displayAOA_draws_expected_shapes);
    RUN_TEST(test_displayAOA_without_numeric_display);
    RUN_TEST(test_displayAOA_no_chevron_when_aoa_below_tones_on);

    RUN_TEST(test_AiGraph_level_flight_draws_cyan_sky_and_ground);
    RUN_TEST(test_AiGraph_banked_roll_draws_pitch_scale);
    RUN_TEST(test_pitchGraph_emits_degree_labels);

    RUN_TEST(test_displayDecelGauge_zero_decel_centers_indicator);
    RUN_TEST(test_displayDecelGauge_negative_decel_moves_indicator_up);

    RUN_TEST(test_displayGloadHistory_flat_1g_trace_all_green);
    RUN_TEST(test_displayGloadHistory_varied_trace_mixes_colors);

    RUN_TEST(test_displaySplashScreen_draws_title_and_version);

    return UNITY_END();
}
