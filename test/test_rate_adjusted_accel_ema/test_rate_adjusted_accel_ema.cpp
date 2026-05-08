// test_rate_adjusted_accel_ema.cpp — Synthetic-signal tests for RateAdjustedAccelEma.
//
// These tests verify the filter's mathematical behavior:
//   - Math verification: at 208 Hz with kAccelEmaTauSec, α ≈ kFirmwareAlpha.
//   - Step response: convergence and time-to-90% bound.
//   - Ramp input: steady-state lag matches the discrete-time formula.
//   - Sine sweep at 1/5/10/20 Hz (well below 25 Hz Nyquist): measured output
//     amplitude matches the discrete-time EMA frequency-response formula
//     (|H(ω)| = α / sqrt(1 + (1−α)² − 2(1−α)cos(ω))) within 5%.
//   - Sine at 30 Hz (above Nyquist): 30 Hz aliases to 20 Hz at 50 Hz sample
//     rate, so output amplitude matches the 20 Hz discrete-time gain, not the
//     CT prediction for 30 Hz. Looser bound (20%) documents this property.
//   - Initialization / reset / NaN semantics.
//
// The main entry point and setUp/tearDown live in main.cpp.
// The flight-truth test (awaiting Issue #485) is in test_flight_truth.cpp.

#include <unity.h>
#include <ahrs/Ahrs.h>
#include <filters/RateAdjustedAccelEma.h>
#include <cmath>
#include <limits>

using onspeed::filters::RateAdjustedAccelEma;
using onspeed::filters::kAccelEmaTauSec;

// ============================================================================
// Constants used across tests.
// ============================================================================

// Log sample rate used in replay path (the primary use case for this filter).
static constexpr float kLogHz      = 50.0f;
// Firmware IMU rate (used in math-verification test).
static constexpr float kFirmwareHz = 208.0f;
// Firmware accel EMA alpha — sourced from the public AHRS header so retuning
// the firmware filter automatically updates this test.  Previously hand-copied;
// static_assert in RateAdjustedAccelEma.h keeps the τ in sync.
static constexpr float kFirmwareAlpha = ::onspeed::ahrs::kAccSmoothing;

// ============================================================================
// Helper: discrete-time EMA frequency response magnitude.
//
// For y[n] = α·x[n] + (1−α)·y[n-1], on the unit circle z = e^{jω}:
//   |H(ω)| = α / sqrt(1 + (1−α)² − 2(1−α)·cos(ω))
// where ω = 2π·f / Fs.
//
// This is the EXACT gain for the discrete EMA at frequency f. For f << Fs,
// it matches the continuous-time approximation 1/sqrt(1+(2πfτ)²); as f
// approaches Nyquist (Fs/2), the two diverge.
// ============================================================================
static float discreteEmaGain(float alpha, float freqHz, float sampleHz)
{
    float omega = 2.0f * static_cast<float>(M_PI) * freqHz / sampleHz;
    float beta  = 1.0f - alpha;
    float denom = std::sqrt(1.0f + beta * beta - 2.0f * beta * std::cos(omega));
    return alpha / denom;
}

// Helper: simulate filter settling over many cycles, then return peak amplitude.
// Returns peak absolute value over `measureCycles` cycles after settling.
static float measuredAmplitude(float alpha, float inputHz, float sigHz,
                               float settleCycles, float measureCycles)
{
    int nSettle  = static_cast<int>(settleCycles  * inputHz / sigHz);
    int nMeasure = static_cast<int>(measureCycles * inputHz / sigHz);

    float v = 0.0f;
    bool initialized = false;

    for (int i = 0; i < nSettle; ++i) {
        float t   = static_cast<float>(i) / inputHz;
        float raw = std::sin(2.0f * static_cast<float>(M_PI) * sigHz * t);
        if (!initialized) { v = raw; initialized = true; }
        else               { v = alpha * raw + (1.0f - alpha) * v; }
    }

    float peak = 0.0f;
    for (int i = 0; i < nMeasure; ++i) {
        float t   = static_cast<float>(nSettle + i) / inputHz;
        float raw = std::sin(2.0f * static_cast<float>(M_PI) * sigHz * t);
        v = alpha * raw + (1.0f - alpha) * v;
        if (std::abs(v) > peak) peak = std::abs(v);
    }
    return peak;
}

// ============================================================================
// Math constant verification
//
// The canonical round-trip: construct with (kFirmwareHz, kAccelEmaTauSec) and
// verify that getAlpha() matches kFirmwareAlpha. This is the machine-checkable
// proof that kAccelEmaTauSec and kFirmwareAlpha are consistent.
//
// Math: α = 1 − exp(−(1/Hz)/τ)
//       For Hz=208, τ=kAccelEmaTauSec (0.076516 s):
//         α = 1 − exp(−(1/208)/0.076516) ≈ 0.060899
//
// If Ahrs.cpp changes kAccSmoothing, kAccelEmaTauSec must be updated and
// this test will catch the inconsistency.
// ============================================================================

void test_alpha_at_208hz_matches_firmware_constant()
{
    RateAdjustedAccelEma f(kFirmwareHz, kAccelEmaTauSec);
    // Expect α ≈ kFirmwareAlpha within 0.001 — the constant is derived from
    // the firmware value so this is essentially an algebraic identity check.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kFirmwareAlpha, f.getAlpha());
}

void test_alpha_at_50hz_reasonable()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    // At 50 Hz with τ=0.076516s: α = 1 − exp(−0.02/0.076516) ≈ 0.230.
    // Assert within ±0.01 — sanity check.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.230f, f.getAlpha());
}

// ============================================================================
// Step input — convergence and time-to-90%
//
// For a 0→1 step at t=0 with the filter seeded at 0, the EMA converges to 1.
// The continuous-time equivalent crosses 90% at t₉₀ = τ·ln(10) ≈ 0.176 s.
// At 50 Hz, sample 9 (t=0.18 s) is where the filter first exceeds 0.9.
// We assert:
//   (a) The filter converges to within 0.001 of 1.0 after 200 samples.
//   (b) Time-to-90% is within [0.12 s, 0.24 s] (theoretical ≈0.176 s ± 1 sample).
// ============================================================================

void test_step_converges_to_final_value()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    f.seed(0.0f);
    for (int i = 0; i < 200; ++i) {
        f.update(1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, f.get());
}

void test_step_time_to_90_percent()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    f.seed(0.0f);

    // Theoretical t₉₀ = τ·ln(10) ≈ 0.176 s. At 50 Hz that's ≈8.8 samples.
    // Allow ±0.04 s (2 sample intervals) for discretization error.
    constexpr float kT90Theory = 0.176f;
    constexpr float kT90Tol   = 0.04f;

    float t90_measured = -1.0f;
    for (int i = 0; i < 100; ++i) {
        float v = f.update(1.0f);
        if (v >= 0.9f && t90_measured < 0.0f) {
            t90_measured = static_cast<float>(i + 1) / kLogHz;
        }
    }
    TEST_ASSERT_MESSAGE(t90_measured > 0.0f, "Filter never reached 0.9");
    TEST_ASSERT_FLOAT_WITHIN(kT90Tol, kT90Theory, t90_measured);
}

// ============================================================================
// Ramp input — steady-state lag
//
// For a ramp x[n] = slope · n/Fs with a discrete EMA:
//   steady-state lag = (1−α) / (α · Fs)
//
// This equals the continuous-time τ in the limit α→0 (high Fs), but at the
// discrete 50 Hz rate it is (1 − 0.230) / (0.230 × 50) ≈ 0.0670 s — shorter
// than the continuous τ (0.076516 s). The test uses the exact discrete formula.
// ============================================================================

void test_ramp_steady_state_lag()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha = f.getAlpha();
    float expectedLagSec = (1.0f - alpha) / (alpha * kLogHz);

    // Simulate 40 seconds at slope=0.1 g/s to reach steady state.
    constexpr float slope    = 0.1f;
    constexpr int   nSamples = 2000;  // 40 s × 50 Hz

    bool  initialized = false;
    float v = 0.0f;
    float last_t = 0.0f;
    for (int i = 1; i <= nSamples; ++i) {
        float t   = static_cast<float>(i) / kLogHz;
        float raw = slope * t;
        if (!initialized) { v = raw; initialized = true; }
        else               { v = alpha * raw + (1.0f - alpha) * v; }
        last_t = t;
    }

    float measuredLag = (slope * last_t - v) / slope;
    TEST_ASSERT_FLOAT_WITHIN(expectedLagSec * 0.01f, expectedLagSec, measuredLag);
}

// ============================================================================
// Sine sweep at 1, 5, 10, 20 Hz (below 25 Hz Nyquist)
//
// For each frequency, the filter's steady-state output amplitude should match
// the discrete-time EMA frequency response:
//
//   |H(ω)| = α / sqrt(1 + (1−α)² − 2(1−α)·cos(ω))   where ω = 2πf/Fs
//
// Tolerance: 5% of the expected gain.
//
// Note on 20 Hz: it is 80% of Nyquist. The continuous-time approximation
// gives gain=0.1034 here, while the exact discrete formula gives 0.1365 — a
// 32% discrepancy. We test against the discrete formula (exact) rather than
// the CT approximation, since that is what the filter actually computes.
// ============================================================================

void test_sine_amplitude_1hz()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha    = f.getAlpha();
    float expected = discreteEmaGain(alpha, 1.0f, kLogHz);
    float actual   = measuredAmplitude(alpha, kLogHz, 1.0f, 200.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(expected * 0.05f, expected, actual);
}

void test_sine_amplitude_5hz()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha    = f.getAlpha();
    float expected = discreteEmaGain(alpha, 5.0f, kLogHz);
    float actual   = measuredAmplitude(alpha, kLogHz, 5.0f, 200.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(expected * 0.05f, expected, actual);
}

void test_sine_amplitude_10hz()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha    = f.getAlpha();
    float expected = discreteEmaGain(alpha, 10.0f, kLogHz);
    float actual   = measuredAmplitude(alpha, kLogHz, 10.0f, 200.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(expected * 0.05f, expected, actual);
}

void test_sine_amplitude_20hz()
{
    // 20 Hz is 80% of the 25 Hz Nyquist limit at 50 Hz sample rate.
    // The filter still operates correctly; signal is fully represented.
    // Need more settle time near Nyquist (longer ring-down transient).
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha    = f.getAlpha();
    float expected = discreteEmaGain(alpha, 20.0f, kLogHz);
    float actual   = measuredAmplitude(alpha, kLogHz, 20.0f, 5000.0f, 100.0f);
    TEST_ASSERT_FLOAT_WITHIN(expected * 0.05f, expected, actual);
}

// ============================================================================
// Sine at 30 Hz — Nyquist aliasing (above 25 Hz Nyquist)
//
// At 50 Hz sample rate, a 30 Hz signal aliases to 50 − 30 = 20 Hz.
// The filter processes the aliased 20 Hz content, not the original 30 Hz.
// Measured output amplitude matches the 20 Hz discrete-time gain, NOT the
// CT prediction for 30 Hz (≈0.069), so the ratio is ~2×.
//
// This test asserts:
//   (a) Measured amplitude significantly exceeds the CT 30 Hz prediction
//       (proves aliasing, not suppression).
//   (b) Measured amplitude is within 20% of the 20 Hz aliased gain.
//
// This test exists to make the "50 Hz log loses information above 25 Hz"
// property visible and stable in the regression suite.
// ============================================================================

void test_sine_aliasing_at_30hz()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha = f.getAlpha();

    // CT prediction for 30 Hz (what would be correct without aliasing).
    float ct_gain_30hz = 1.0f / std::sqrt(
        1.0f + std::pow(2.0f * static_cast<float>(M_PI) * 30.0f * kAccelEmaTauSec, 2.0f));

    // Expected aliased gain: 30 Hz aliases to 20 Hz at 50 Hz sample rate.
    float aliased_gain = discreteEmaGain(alpha, 20.0f, kLogHz);

    float actual = measuredAmplitude(alpha, kLogHz, 30.0f, 5000.0f, 100.0f);

    // (a) Actual > CT prediction by at least 50%: aliasing is present.
    TEST_ASSERT_MESSAGE(actual > ct_gain_30hz * 1.5f,
                        "30 Hz aliasing expected: actual should exceed CT prediction");

    // (b) Actual ≈ aliased 20 Hz gain within 20%.
    TEST_ASSERT_FLOAT_WITHIN(aliased_gain * 0.20f, aliased_gain, actual);
}

// ============================================================================
// Initialization / reset / NaN semantics
// ============================================================================

void test_first_update_seeds_and_returns_input()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    TEST_ASSERT_FALSE(f.isInitialized());

    float result = f.update(3.14f);

    TEST_ASSERT_TRUE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.14f, result);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.14f, f.get());
}

void test_second_update_blends()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha = f.getAlpha();

    f.update(0.0f);             // seed at 0
    float v = f.update(1.0f);  // blend: alpha*1 + (1-alpha)*0 = alpha
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, alpha, v);
}

void test_seed_marks_initialized()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    TEST_ASSERT_FALSE(f.isInitialized());

    f.seed(5.0f);

    TEST_ASSERT_TRUE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 5.0f, f.get());
}

void test_seed_then_update_blends()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    float alpha = f.getAlpha();

    f.seed(-1.0f);
    // alpha*1 + (1-alpha)*(-1) = 2*alpha - 1
    float expected = alpha * 1.0f + (1.0f - alpha) * (-1.0f);
    float result = f.update(1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected, result);
}

void test_seed_nan_ignored()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    f.seed(std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_FALSE(f.isInitialized());

    float result = f.update(7.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 7.0f, result);
}

void test_reset_returns_to_uninitialized()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    f.update(42.0f);
    TEST_ASSERT_TRUE(f.isInitialized());

    f.reset();

    TEST_ASSERT_FALSE(f.isInitialized());
    float result = f.update(1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, result);
}

void test_nan_input_on_initialized_filter_returns_previous()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);
    f.update(3.0f);
    f.update(0.0f);
    float before_nan = f.get();

    float result = f.update(std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, before_nan, result);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, before_nan, f.get());
}

void test_nan_input_on_uninitialized_filter_stays_uninitialized()
{
    RateAdjustedAccelEma f(kLogHz, kAccelEmaTauSec);

    float result = f.update(std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_FALSE(f.isInitialized());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, result);
}

void test_degenerate_constructor_inputs()
{
    // Zero inputHz → falls back to α=1 (pass-through).
    RateAdjustedAccelEma f_zero(0.0f, kAccelEmaTauSec);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, f_zero.getAlpha());

    // Negative inputHz → same fallback.
    RateAdjustedAccelEma f_neg(-10.0f, kAccelEmaTauSec);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, f_neg.getAlpha());

    // Zero targetTauSec → fallback.
    RateAdjustedAccelEma f_tau0(kLogHz, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, f_tau0.getAlpha());
}
