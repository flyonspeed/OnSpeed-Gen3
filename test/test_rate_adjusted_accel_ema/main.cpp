// main.cpp — Unity entry point for the test_rate_adjusted_accel_ema suite.
//
// Runs all synthetic-signal tests and the flight-truth scaffold.
// Synthetic tests are in test_rate_adjusted_accel_ema.cpp.
// Flight-truth test is in test_flight_truth.cpp (TEST_IGNORE'd per Issue #485).

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---- Forward declarations: synthetic-signal tests --------------------------

void test_alpha_at_208hz_matches_firmware_constant(void);
void test_alpha_at_50hz_reasonable(void);

void test_step_converges_to_final_value(void);
void test_step_time_to_90_percent(void);

void test_ramp_steady_state_lag(void);

void test_sine_amplitude_1hz(void);
void test_sine_amplitude_5hz(void);
void test_sine_amplitude_10hz(void);
void test_sine_amplitude_20hz(void);

void test_sine_aliasing_at_30hz(void);

void test_first_update_seeds_and_returns_input(void);
void test_second_update_blends(void);
void test_seed_marks_initialized(void);
void test_seed_then_update_blends(void);
void test_seed_nan_ignored(void);
void test_reset_returns_to_uninitialized(void);
void test_nan_input_on_initialized_filter_returns_previous(void);
void test_nan_input_on_uninitialized_filter_stays_uninitialized(void);
void test_degenerate_constructor_inputs(void);

// ---- Forward declarations: flight-truth scaffold (Issue #485) --------------

void test_flight_truth_rms_divergence(void);

// ---- Entry point -----------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();

    // Math constant verification
    RUN_TEST(test_alpha_at_208hz_matches_firmware_constant);
    RUN_TEST(test_alpha_at_50hz_reasonable);

    // Step response
    RUN_TEST(test_step_converges_to_final_value);
    RUN_TEST(test_step_time_to_90_percent);

    // Ramp response
    RUN_TEST(test_ramp_steady_state_lag);

    // Sine sweep (below Nyquist)
    RUN_TEST(test_sine_amplitude_1hz);
    RUN_TEST(test_sine_amplitude_5hz);
    RUN_TEST(test_sine_amplitude_10hz);
    RUN_TEST(test_sine_amplitude_20hz);

    // Nyquist aliasing documentation
    RUN_TEST(test_sine_aliasing_at_30hz);

    // Initialization / reset / NaN semantics
    RUN_TEST(test_first_update_seeds_and_returns_input);
    RUN_TEST(test_second_update_blends);
    RUN_TEST(test_seed_marks_initialized);
    RUN_TEST(test_seed_then_update_blends);
    RUN_TEST(test_seed_nan_ignored);
    RUN_TEST(test_reset_returns_to_uninitialized);
    RUN_TEST(test_nan_input_on_initialized_filter_returns_previous);
    RUN_TEST(test_nan_input_on_uninitialized_filter_stays_uninitialized);
    RUN_TEST(test_degenerate_constructor_inputs);

    // Flight-truth (IGNORED until Issue #485 — 208 Hz reference log fixture)
    RUN_TEST(test_flight_truth_rms_divergence);

    return UNITY_END();
}
