// AutoSetpoints — derive the plugin's four AOA setpoint thresholds
// (LDmax / OnSpeedFast / OnSpeedSlow / StallWarn) from X-Plane's
// per-aircraft stall-AOA datarefs.
//
// X-Plane exposes wing AOA at zero-flap stall and wing AOA at
// full-flap stall as compile-time aircraft constants
// (sim/aircraft/controls/acf_max_aoa_no_flap and
// .../acf_max_aoa_full_flap).  The flight model lerps between them by
// sim/cockpit2/controls/flap_handle_deploy_ratio (continuous 0..1).
// Multiplying the lerped stall AOA by the calibration wizard's NAOA
// fractions produces airframe-correct setpoints for any aircraft the
// flight model supports — no manual tuning per aircraft, no flight
// test.
//
// alpha_0 is implicitly 0.  The plugin's AOA convention is X-Plane's
// `sim/flightmodel/position/alpha`, which is wing AOA, not body angle.
// alpha_0 = 0 is exact for symmetric airfoils.  Cambered airfoils
// have alpha_0 ≈ -2° to -4° (lift floor is at slightly negative wing
// AOA), but X-Plane doesn't expose a per-aircraft zero-lift AOA
// dataref.  The approximation produces setpoints ~0.5-1.5° high for
// cambered GA aircraft — direction of error is conservative (cues
// fire slightly early), and pilots can fine-tune NAOA fractions in
// the audio control window to compensate.
//
// The firmware's per-flap `alpha_0` is the body-angle floor, which
// folds in wing incidence; X-Plane's `alpha` already excludes that.
// So in the plugin: setpoint = naoa * alpha_stall (no alpha_0 term).
//
// The four NAOA fractions come from the calibration wizard's lift-
// equation fit in tools/web/lib/pages/CalWizardPage.js:
//
//   naoa_fast = 1 / 1.35² ≈ 0.549     (OnSpeedFast IAS = 1.35·Vs)
//   naoa_slow = 1 / 1.25² ≈ 0.640     (OnSpeedSlow IAS = 1.25·Vs)
//   naoa_stallwarn ≈ 0.92             ((Vs/(Vs+5))² with Vs ≈ 60 kt)
//   naoa_ldmax     ≈ 0.45             ((Vs/Vbg)² with Vbg ≈ 1.5·Vs)
//
// See issue #392 for the design discussion, and aoa_audio.cpp for the
// production wiring that calls this helper from the flight loop and
// surfaces the radio-button mode toggle in the audio control window.

#pragma once

namespace onspeed_xplane::indexer {

// NAOA fractions used to compute setpoints from stall AOA.  These are
// the canonical defaults — pilots may override per-aircraft via the
// audio control window.
struct NaoaFractions
{
    float ldmax;        // typical 0.45  ((Vs/Vbg)²)
    float onSpeedFast;  // 1/1.35² ≈ 0.549
    float onSpeedSlow;  // 1/1.25² ≈ 0.640
    float stallWarn;    // typical 0.92  ((Vs/(Vs+5))²)
};

// Canonical defaults matching the calibration wizard's IAS multipliers.
inline constexpr NaoaFractions kCanonicalNaoa{
    /*ldmax=*/        0.45f,
    /*onSpeedFast=*/  0.549f,
    /*onSpeedSlow=*/  0.640f,
    /*stallWarn=*/    0.92f,
};

// Minimum plausible stall AOA for the auto-derive path to fire.  When
// X-Plane's acf_max_aoa_* datarefs aren't populated (vintage sim,
// freeware airframes that don't set them, missing aircraft), the
// reads return 0 and we leave the live globals at their last-known
// values rather than collapse the entire band to zero.
inline constexpr float kMinPlausibleStallAoaDeg = 0.5f;

// Result of the per-frame derivation.  Each field is the AOA setpoint
// in degrees, in the same wing-AOA convention as X-Plane's alpha.
struct DerivedSetpoints
{
    float ldmax;
    float onSpeedFast;
    float onSpeedSlow;
    float stallWarn;
    bool  applied;       // false when alpha_stall < kMinPlausibleStallAoaDeg
};

// Lerp between zero-flap and full-flap stall AOA by flap deploy ratio.
// flapRatio is clamped to [0, 1] internally so callers don't need to
// pre-clamp.
float LerpStallAoa(float alphaStallNoFlap,
                   float alphaStallFullFlap,
                   float flapRatio);

// Pure-function setpoint derivation.  Multiplies the (lerped) alpha_stall
// by the four NAOA fractions to produce setpoint AOAs.  When the lerped
// alpha_stall is below kMinPlausibleStallAoaDeg, returns applied=false
// and zeroed setpoints — caller should leave the live globals untouched.
//
// alpha_0 is implicitly 0: X-Plane's alpha is wing AOA (firmware uses
// body angle and folds wing incidence into alpha_0; the plugin doesn't
// need to).  See the file-level note above.
DerivedSetpoints DeriveSetpointsFromStall(float alphaStallNoFlap,
                                          float alphaStallFullFlap,
                                          float flapRatio,
                                          const NaoaFractions& naoa);

}  // namespace onspeed_xplane::indexer
