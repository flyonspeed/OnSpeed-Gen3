// main.cpp — Unity entry point for the test_gauges suite.
//
// Runs every named test case across the four per-module files. Each test
// case is declared in its sibling .cpp; we forward-declare them here (rather
// than introducing a shared header) so this file remains the single source
// of truth for which tests run.

#include <unity.h>

// Unity's global setUp/tearDown are mandatory; none of these tests need
// per-case setup, so they are empty.
void setUp(void) {}
void tearDown(void) {}

// ---- Forward declarations ---------------------------------------------------

// BarRangeScale
void test_vbar_range_idempotent_when_called_twice(void);
void test_vbar_range_scaled_values_match_reference(void);
void test_vbar_range_clamps_to_maxscaled(void);
void test_vbar_range_clamps_to_minscaled(void);
void test_vbar_range_zero_span_produces_zero_output(void);
void test_vbar_range_valid_false_still_writes_dst(void);
void test_vbar_range_multiple_bands_independent(void);
void test_vbar_normaxis_zero_does_not_crash(void);

// ArcGeometry
void test_cw_quad_a_matches_startangle(void);
void test_cw_quad_b_matches_startangle_plus_step(void);
void test_ccw_quad_a_matches_startangle_minus_j(void);
void test_ccw_quad_b_matches_startangle_minus_j_minus_step(void);
void test_ccw_cw_same_angular_span_cover_same_arc(void);
void test_endcap_cw_lands_at_start_plus_arcangle(void);
void test_endcap_ccw_lands_at_start_minus_abs_arcangle(void);
void test_endcap_zero_arcangle_equals_startangle(void);
void test_endcap_uses_float_trig_not_double(void);
void test_cw_quad_magnitude_is_one(void);

// TickLayout
void test_no_ticks_when_gradmarks_zero(void);
void test_no_ticks_when_gradmarks_one(void);
void test_major_tick_count_matches_gradmarks_plus_one(void);
void test_first_major_tick_is_at_theta(void);
void test_last_major_tick_is_at_theta_plus_arcangle(void);
void test_graduation_marks_align_with_range_bar_start_when_mindisplay_nonzero(void);
void test_minor_tick_halfway_between_major_ticks(void);
void test_negative_gradmarks_produces_same_angle_positions(void);
void test_output_buffer_overflow_guard(void);
void test_uniform_spacing_between_major_ticks(void);

// GaugeState
void test_default_construction_all_range_valid_false(void);
void test_default_construction_range_arrays_zero(void);
void test_default_construction_pointer_arrays_zero(void);
void test_default_construction_clockwise_true(void);
void test_default_construction_style_fields(void);
void test_default_state_is_deterministic(void);
void test_modified_state_does_not_bleed_to_next_instance(void);

// ---- Entry point ------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();

    // BarRangeScale
    RUN_TEST(test_vbar_range_idempotent_when_called_twice);
    RUN_TEST(test_vbar_range_scaled_values_match_reference);
    RUN_TEST(test_vbar_range_clamps_to_maxscaled);
    RUN_TEST(test_vbar_range_clamps_to_minscaled);
    RUN_TEST(test_vbar_range_zero_span_produces_zero_output);
    RUN_TEST(test_vbar_range_valid_false_still_writes_dst);
    RUN_TEST(test_vbar_range_multiple_bands_independent);
    RUN_TEST(test_vbar_normaxis_zero_does_not_crash);

    // ArcGeometry
    RUN_TEST(test_cw_quad_a_matches_startangle);
    RUN_TEST(test_cw_quad_b_matches_startangle_plus_step);
    RUN_TEST(test_ccw_quad_a_matches_startangle_minus_j);
    RUN_TEST(test_ccw_quad_b_matches_startangle_minus_j_minus_step);
    RUN_TEST(test_ccw_cw_same_angular_span_cover_same_arc);
    RUN_TEST(test_endcap_cw_lands_at_start_plus_arcangle);
    RUN_TEST(test_endcap_ccw_lands_at_start_minus_abs_arcangle);
    RUN_TEST(test_endcap_zero_arcangle_equals_startangle);
    RUN_TEST(test_endcap_uses_float_trig_not_double);
    RUN_TEST(test_cw_quad_magnitude_is_one);

    // TickLayout
    RUN_TEST(test_no_ticks_when_gradmarks_zero);
    RUN_TEST(test_no_ticks_when_gradmarks_one);
    RUN_TEST(test_major_tick_count_matches_gradmarks_plus_one);
    RUN_TEST(test_first_major_tick_is_at_theta);
    RUN_TEST(test_last_major_tick_is_at_theta_plus_arcangle);
    RUN_TEST(test_graduation_marks_align_with_range_bar_start_when_mindisplay_nonzero);
    RUN_TEST(test_minor_tick_halfway_between_major_ticks);
    RUN_TEST(test_negative_gradmarks_produces_same_angle_positions);
    RUN_TEST(test_output_buffer_overflow_guard);
    RUN_TEST(test_uniform_spacing_between_major_ticks);

    // GaugeState
    RUN_TEST(test_default_construction_all_range_valid_false);
    RUN_TEST(test_default_construction_range_arrays_zero);
    RUN_TEST(test_default_construction_pointer_arrays_zero);
    RUN_TEST(test_default_construction_clockwise_true);
    RUN_TEST(test_default_construction_style_fields);
    RUN_TEST(test_default_state_is_deterministic);
    RUN_TEST(test_modified_state_does_not_bleed_to_next_instance);

    return UNITY_END();
}
