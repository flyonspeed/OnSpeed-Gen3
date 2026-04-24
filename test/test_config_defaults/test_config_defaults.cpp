// test_config_defaults.cpp — invariants on OnSpeedConfig::LoadDefaults().
//
// The compiled-in defaults ship on every fresh install, every factory
// reset, and every brand-new device before the SD card / flash /
// web-UI save path runs. A single RV-4-specific AOA curve and
// setpoints used to be baked in — meaning a fresh OnSpeed in a
// different airplane would produce tones at AOAs that were wrong for
// that aircraft. "Silently wrong" is the worst failure mode for a
// pilot-facing audio cue.
//
// This test pins the policy: defaults leave the per-aircraft knobs at
// a known "uncalibrated" state (all zeros), which the audio-tone
// gate in ToneCalc then interprets as "stay silent until calibrated."
//
// Aircraft-independent knobs (smoothing window, load limits, etc.)
// are allowed to have sensible non-zero defaults and are not pinned
// by this test — the test only cares that the calibration-sensitive
// fields are zeroed.

#include <unity.h>

#include <config/OnSpeedConfig.h>

using onspeed::config::OnSpeedConfig;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// A fresh config always has exactly one flap entry so the sketch-side
// consumers (Audio.cpp, SensorIO.cpp, etc.) can safely dereference
// aFlaps[g_Flaps.iIndex] without an out-of-bounds read.
// ---------------------------------------------------------------------------

void test_loaddefaults_populates_at_least_one_flap_entry()
{
    OnSpeedConfig cfg;   // ctor calls LoadDefaults()
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, static_cast<int>(cfg.aFlaps.size()));
}

// ---------------------------------------------------------------------------
// Calibration-sensitive fields MUST be zero on a fresh config.
// ---------------------------------------------------------------------------

void test_loaddefaults_zeroes_setpoints()
{
    OnSpeedConfig cfg;
    const auto& flap0 = cfg.aFlaps[0];
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fLDMAXAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fONSPEEDFASTAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fONSPEEDSLOWAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fSTALLWARNAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fSTALLAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fMANAOA);
}

void test_loaddefaults_zeroes_aoa_curve_coefficients()
{
    OnSpeedConfig cfg;
    const auto& flap0 = cfg.aFlaps[0];
    for (int i = 0; i < onspeed::MAX_CURVE_COEFF; ++i) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.AoaCurve.afCoeff[i]);
    }
}

void test_loaddefaults_zeroes_alpha_fit_data()
{
    // These four are populated by the calibration wizard from
    // lift-equation fits (K/IAS² + alpha_0). A fresh config has no
    // data to fit, so they must be zero.
    OnSpeedConfig cfg;
    const auto& flap0 = cfg.aFlaps[0];
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fAlpha0);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fAlphaStall);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fKFit);
}

// ---------------------------------------------------------------------------
// Structural fields that are NOT calibration data are allowed to
// have non-zero defaults — pin the ones readers rely on.
// ---------------------------------------------------------------------------

void test_loaddefaults_sets_polynomial_curve_type()
{
    // The curve evaluator branches on iCurveType; leaving the type
    // unset could fall through to a different evaluator path and
    // give semantically different behavior on an uncalibrated
    // config. Keep the polynomial form as the structural default,
    // with zero coefficients (which evaluate to a constant 0 AOA).
    OnSpeedConfig cfg;
    TEST_ASSERT_EQUAL_INT(1, cfg.aFlaps[0].AoaCurve.iCurveType);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_loaddefaults_populates_at_least_one_flap_entry);
    RUN_TEST(test_loaddefaults_zeroes_setpoints);
    RUN_TEST(test_loaddefaults_zeroes_aoa_curve_coefficients);
    RUN_TEST(test_loaddefaults_zeroes_alpha_fit_data);
    RUN_TEST(test_loaddefaults_sets_polynomial_curve_type);
    return UNITY_END();
}
