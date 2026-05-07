// AutoSetpoints — derive the plugin's four AOA setpoint thresholds
// (LDmax / OnSpeedFast / OnSpeedSlow / StallWarn) from X-Plane's
// per-aircraft stall datarefs, with per-flap interpolation.
//
// Three datarefs feed the derivation, all populated for stock X-Plane
// aircraft:
//
//   sim/aircraft/overflow/acf_stall_warn_alpha — wing AOA at which
//     the stall warner fires (single per-aircraft scalar; treated as
//     the clean StallWarn anchor since most stall warners are tuned
//     to clean-config alpha).
//   sim/aircraft/view/acf_Vs  — clean stall speed, KIAS.
//   sim/aircraft/view/acf_Vso — landing-config (full flap) stall
//     speed, KIAS.
//
// Representative values (Laminar stock):
//
//             stall_warn_alpha    Vs    Vso
//   RV-10              10.0°    65    50
//   C172               12.0°    48    40
//   F-14               20.0°   150   110
//
// Per-flap stall AOA is computed from the IAS² ratio of the two stall
// speeds.  At full flaps the wing reaches Cl_max at lower wing AOA
// because the flap shifts the Cl(alpha) curve up-and-left.  The
// approximation here scales alpha_stall by (Vso/Vs)²:
//
//   alpha_stall_clean = stall_warn_alpha / 0.92
//   alpha_stall_full  = alpha_stall_clean × (Vso/Vs)²
//   alpha_stall(ratio) = lerp(clean, full, flap_handle_deploy_ratio)
//
// For the RV-10:
//   alpha_stall_clean = 10.0 / 0.92      = 10.87°
//   alpha_stall_full  = 10.87 × (50/65)² =  6.43°
//
// Setpoints scale by the canonical NAOA fractions per-flap:
//
//   LDmax       = 0.45  × alpha_stall(ratio)
//   OnSpeedFast = 0.549 × alpha_stall(ratio)
//   OnSpeedSlow = 0.640 × alpha_stall(ratio)
//   StallWarn   = 0.92  × alpha_stall(ratio)
//
// For the RV-10 the per-flap setpoints are:
//
//             clean   half flap   full flap
//   LDmax       4.89°    3.89°     2.89°
//   OnSpeedFast 5.97°    4.75°     3.53°
//   OnSpeedSlow 6.96°    5.54°     4.12°
//   StallWarn  10.00°    7.96°     5.92°
//
// Setpoints slide continuously with flap deployment, matching the
// wing's actual aerodynamics in each config.
//
// Approximation note: the (Vso/Vs)² scaling assumes the lift-curve
// slope and the flap's effective alpha-shift produce a quadratic
// relationship between Vs ratio and alpha_stall ratio.  In reality
// the relationship depends on flap effectiveness and chord ratio,
// but for typical GA flap systems (30-40° split or slotted flaps)
// the approximation is within ~1° of the true alpha_stall_full.
// Pilots can fine-tune the NAOA fractions per-aircraft via the
// audio control window.
//
// alpha_0 is implicitly 0.  The plugin's AOA convention is X-Plane's
// `sim/flightmodel/position/alpha`, which is wing AOA, not body
// angle.  alpha_0 = 0 is exact for symmetric airfoils.  Cambered
// airfoils have alpha_0 ≈ -2° to -4°, but X-Plane doesn't expose a
// zero-lift AOA dataref.  The approximation produces setpoints
// ~0.5-1.5° high for cambered GA aircraft — conservative direction
// (cues fire slightly early); pilots can compensate via NAOA tuning.
//
// See issue #392 for the design discussion, and aoa_audio.cpp for
// the production wiring that calls this helper from the flight loop
// and surfaces the radio-button mode toggle in the audio control
// window.

#pragma once

namespace onspeed_xplane::indexer {

// NAOA fractions used to compute setpoints from stall AOA.  These
// are the canonical defaults — pilots may override per-aircraft via
// the audio control window.
struct NaoaFractions
{
    float ldmax;        // typical 0.45  ((Vs/Vbg)² with Vbg ≈ 1.5·Vs)
    float onSpeedFast;  // 1/1.35² ≈ 0.549
    float onSpeedSlow;  // 1/1.25² ≈ 0.640
    float stallWarn;    // 0.92 by wizard convention; setpoint at this
                        // fraction equals acf_stall_warn_alpha.
};

// Canonical defaults matching the calibration wizard's IAS multipliers.
inline constexpr NaoaFractions kCanonicalNaoa{
    /*ldmax=*/        0.45f,
    /*onSpeedFast=*/  0.549f,
    /*onSpeedSlow=*/  0.640f,
    /*stallWarn=*/    0.92f,
};

// Minimum plausible stall-warn AOA for the auto-derive path to fire.
// When `acf_stall_warn_alpha` isn't populated, the read returns 0
// and we leave the live globals at their last-known values rather
// than collapse the entire band to zero.
inline constexpr float kMinPlausibleStallWarnAoaDeg = 0.5f;

// Minimum plausible Vs for the per-flap derivation to fire.  When
// acf_Vs / acf_Vso aren't populated (or one is zero), we fall back
// to a flat alpha_stall across the flap range — same behavior as
// the previous single-anchor implementation.
inline constexpr float kMinPlausibleVsKt = 5.0f;

// Result of the per-frame derivation.  Each field is the AOA
// setpoint in degrees, in the same wing-AOA convention as
// X-Plane's alpha.
struct DerivedSetpoints
{
    float ldmax;
    float onSpeedFast;
    float onSpeedSlow;
    float stallWarn;
    bool  applied;       // false when stallWarnAoa < kMinPlausibleStallWarnAoaDeg
};

// Pure-function setpoint derivation.
//
// stallWarnAoa: acf_stall_warn_alpha (deg).  Treated as the clean
//   StallWarn anchor.
// vsKt, vsoKt: clean and full-flap stall speeds (KIAS).  When both
//   are >= kMinPlausibleVsKt, alpha_stall is interpolated between
//   the clean and full-flap values by flapRatio.  When either is
//   zero or below the floor, the function falls back to a flat
//   alpha_stall (clean value) across all flap positions.
// flapRatio: sim/cockpit2/controls/flap_handle_deploy_ratio, 0..1.
//   Clamped internally; callers don't need to pre-clamp.
// naoa: NAOA fractions to apply.
//
// Returns applied=false (and zeroed setpoints) when stallWarnAoa is
// below kMinPlausibleStallWarnAoaDeg or when naoa.stallWarn is zero
// or negative.  Otherwise returns the four scaled setpoints.
DerivedSetpoints DeriveSetpointsFromDatarefs(float stallWarnAoa,
                                             float vsKt,
                                             float vsoKt,
                                             float flapRatio,
                                             const NaoaFractions& naoa);

}  // namespace onspeed_xplane::indexer
