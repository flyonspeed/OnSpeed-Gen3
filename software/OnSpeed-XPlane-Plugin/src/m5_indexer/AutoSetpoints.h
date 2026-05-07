// AutoSetpoints — derive the plugin's four AOA setpoint thresholds
// (LDmax / OnSpeedFast / OnSpeedSlow / StallWarn) from X-Plane's
// per-aircraft stall-warning AOA dataref.
//
// X-Plane exposes `sim/aircraft/overflow/acf_stall_warn_alpha` — the
// author-set wing AOA at which the stall warner fires.  Stock aircraft
// populate this from Plane Maker; representative values:
//
//   RV-10 (Laminar):     10.0°
//   C172  (Laminar):     12.0°
//   F-14  (Laminar):     20.0°
//
// The reading is the StallWarn anchor directly.  Implicit alpha_stall
// (used to scale the other three setpoints) = StallWarn / 0.92, since
// the StallWarn NAOA fraction is 0.92 by wizard convention.  The other
// three setpoints come from canonical NAOA fractions:
//
//   LDmax       = 0.45 × alpha_stall    ((Vs/Vbg)² with Vbg ≈ 1.5·Vs)
//   OnSpeedFast = 0.549 × alpha_stall   (1/1.35², from wizard)
//   OnSpeedSlow = 0.640 × alpha_stall   (1/1.25², from wizard)
//   StallWarn   = 0.92 × alpha_stall    (= acf_stall_warn_alpha)
//
// Setpoints are flat across flap positions.  X-Plane doesn't expose
// per-flap stall AOA — only `acf_stall_warn_alpha`, a single per-
// aircraft scalar.  The wing actually stalls at a lower wing AOA with
// flaps deployed, but without a per-flap dataref the plugin assumes a
// constant alpha_stall.  Pilots flying frequent flapped approaches can
// fine-tune NAOA fractions per-aircraft via the audio control window.
//
// alpha_0 is implicitly 0.  The plugin's AOA convention is X-Plane's
// `sim/flightmodel/position/alpha`, which is wing AOA, not body angle.
// alpha_0 = 0 is exact for symmetric airfoils.  Cambered airfoils have
// alpha_0 ≈ -2° to -4° (lift floor is at slightly negative wing AOA),
// but X-Plane doesn't expose a per-aircraft zero-lift AOA dataref.
// The approximation produces setpoints ~0.5-1.5° high for cambered GA
// aircraft — direction of error is conservative (cues fire slightly
// early), and pilots can fine-tune NAOA fractions in the audio control
// window to compensate.
//
// The firmware's per-flap `alpha_0` is the body-angle floor, which
// folds in wing incidence; X-Plane's `alpha` already excludes that.
// So in the plugin: setpoint = naoa * alpha_stall (no alpha_0 term).
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
    float ldmax;        // typical 0.45  ((Vs/Vbg)² with Vbg ≈ 1.5·Vs)
    float onSpeedFast;  // 1/1.35² ≈ 0.549
    float onSpeedSlow;  // 1/1.25² ≈ 0.640
    float stallWarn;    // 0.92 by wizard convention; this is the anchor
                        // we read from acf_stall_warn_alpha (setpoint
                        // value at this fraction equals the dataref).
};

// Canonical defaults matching the calibration wizard's IAS multipliers.
inline constexpr NaoaFractions kCanonicalNaoa{
    /*ldmax=*/        0.45f,
    /*onSpeedFast=*/  0.549f,
    /*onSpeedSlow=*/  0.640f,
    /*stallWarn=*/    0.92f,
};

// Minimum plausible stall-warn AOA for the auto-derive path to fire.
// When `acf_stall_warn_alpha` isn't populated (some freeware airframes,
// vintage aircraft), the read returns 0 and we leave the live globals
// at their last-known values rather than collapse the entire band to
// zero.
inline constexpr float kMinPlausibleStallWarnAoaDeg = 0.5f;

// Result of the per-frame derivation.  Each field is the AOA setpoint
// in degrees, in the same wing-AOA convention as X-Plane's alpha.
struct DerivedSetpoints
{
    float ldmax;
    float onSpeedFast;
    float onSpeedSlow;
    float stallWarn;
    bool  applied;       // false when stall_warn_alpha < kMinPlausibleStallWarnAoaDeg
};

// Pure-function setpoint derivation.  Treats stallWarnAoa as the
// StallWarn anchor and scales the other three setpoints from the
// implicit alpha_stall = stallWarnAoa / naoa.stallWarn.  When
// stallWarnAoa is below kMinPlausibleStallWarnAoaDeg, returns
// applied=false — caller should leave the live globals untouched.
//
// alpha_0 is implicitly 0: X-Plane's alpha is wing AOA (firmware uses
// body angle and folds wing incidence into alpha_0; the plugin doesn't
// need to).  See the file-level note above.
DerivedSetpoints DeriveSetpointsFromStallWarn(float stallWarnAoa,
                                              const NaoaFractions& naoa);

}  // namespace onspeed_xplane::indexer
