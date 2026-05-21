// Regression test pinning the X-Plane plugin's on-ground V²-based
// percent-lift formula.
//
// On-ground rule (Vac, design discussion in PR #434):
//   When the gear is loaded above the user's IAS-mute floor, body
//   angle isn't aerodynamically meaningful — the gear, not the
//   wing, is supporting weight.  Percent-lift becomes "how loaded
//   the wing is at this airspeed if it had to support weight in 1G
//   level."  That's the V² formula:
//
//       pct_v² = (Vs / V)²  ·  stallWarnPct
//
//   pinned at the StallWarn anchor when V == Vs so the V² scale
//   and the alpha scale agree on where "near stall" sits on the
//   indexer band.
//
// IAS-mute floor (matches firmware DisplaySerial.cpp:252):
//   Below the user's iMuteAudioUnderIAS, percent collapses to 0
//   regardless of whether the airplane is on the ground.  Taxi at
//   10-20 kt shows nothing (chevron at the bottom of the band) —
//   the pilot has explicitly configured "below this IAS, don't
//   tell me anything," and the V² path honors that the same way
//   the alpha path does.  V² only runs when iasValid=true AND
//   onGround=true.
//
// The tests below verify:
//   1. Below mute floor (iasValid=false) on the ground: percent
//      collapses to 0 — matches firmware's display gate.
//   2. Above mute floor (iasValid=true) on the ground: V² runs.
//      At low V well below Vs, percent saturates near max.
//   3. As V rises through Vs, the pct passes through StallWarn.
//   4. Past Vs (rolling fast / liftoff regime), percent drops
//      smoothly into the OnSpeed band.
//   5. With iVs1G == 0 (no Vs known), on-ground falls back to the
//      alpha formula — same path the in-flight branch uses.
//   6. In flight (onGround=false), the alpha formula is used
//      regardless of iVs1G, even at V well below Vs.

#include "../src/m5_indexer/DataRefAdapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// Plugin-side globals consumed by FillPercentLift.  Mirror plugin
// defaults so the fixture is self-contained.
float fLDMAXAOA       =  6.0f;
float fONSPEEDFASTAOA =  7.3f;
float fONSPEEDSLOWAOA =  9.6f;
float fSTALLWARNAOA   = 12.5f;
float fALPHASTALL     = 13.6f;

// 60 kt is a representative light-GA 1G clean stall for the
// scenarios below (RV-10 ≈ 62 kt, T-6 ≈ 63, C172 ≈ 40, T-6 high
// gross ≈ 65).  StallWarn pct is around 92–93 in this calibration.
int iVs1G = 60;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool nearly(float a, float b, float tol)
{
    return std::fabs(a - b) <= tol;
}

}  // namespace

int main()
{
    using onspeed::proto::DisplayBuildInputs;
    using onspeed_xplane::indexer::FillPercentLift;
    using onspeed_xplane::indexer::RegimeFadeState;

    // Crossfade state — fresh state per test scope; reset between
    // scenarios that simulate independent flight-loop sessions so a
    // prior test's terminal state doesn't leak into the next.
    RegimeFadeState fade{};

    // --------------------------------------------------------------
    // The StallWarn anchor pct is the V² formula's scaling target.
    // Compute it once via FillPercentLift's own anchor calculation
    // (anchors are pure functions of calibration; alpha or V² mode
    // doesn't change them).
    // --------------------------------------------------------------
    DisplayBuildInputs probe{};
    fade = {};
    FillPercentLift(probe, 0.0f, 80.0f, 0.0f, true, /*onGround=*/false, fade);
    const float stallWarnPct = static_cast<float>(probe.stallWarnPctLift);
    check(stallWarnPct > 80.0f && stallWarnPct < 100.0f,
          "fixture: stallWarn anchor in (80, 100)");

    // --------------------------------------------------------------
    // 1. Below mute floor on the ground (iasValid=false): percent
    //    collapses to 0.  Matches firmware DisplaySerial.cpp:252,
    //    where ComputePercentLift(... iasValid=false) returns 0 and
    //    the M5 indexer chevron sits at the bottom of the band.
    //    Pilot's explicit "mute under X kt" choice is honored —
    //    taxi shows nothing, not a saturated chevron.
    // --------------------------------------------------------------
    DisplayBuildInputs taxi0{};
    fade = {};
    FillPercentLift(taxi0,
                    /*liveAoaDeg=*/      4.0f,    // body-angle on the ramp
                    /*liveIasKt=*/       0.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        false,   // below mute floor
                    /*onGround=*/        true,
                    fade);
    check(nearly(taxi0.percentLiftPct, 0.0f, 0.5f),
          "V=0 with iasValid=false: percent is 0 (firmware-style mute)");

    DisplayBuildInputs taxi10{};
    fade = {};
    FillPercentLift(taxi10, 4.0f, 10.0f, 0.0f, /*iasValid=*/ false, true, fade);
    check(nearly(taxi10.percentLiftPct, 0.0f, 0.5f),
          "V=10 taxi below mute: percent is 0 (no saturated chevron)");

    DisplayBuildInputs taxi20{};
    fade = {};
    FillPercentLift(taxi20, 4.0f, 20.0f, 0.0f, /*iasValid=*/ false, true, fade);
    check(nearly(taxi20.percentLiftPct, 0.0f, 0.5f),
          "V=20 taxi below mute: percent is 0 (matches firmware)");

    // --------------------------------------------------------------
    // 2. Above mute floor on the ground (iasValid=true) at low V:
    //    V² formula runs and saturates near max.  Wing is at max
    //    effort — pilot's takeoff-roll mental model.  The user has
    //    crossed their configured mute floor, so the indicator
    //    starts showing a real reading; well below Vs that reading
    //    is "saturated stall" because the airplane isn't flying.
    // --------------------------------------------------------------
    DisplayBuildInputs roll25{};
    fade = {};
    FillPercentLift(roll25, 4.0f, 25.0f, 0.0f, /*iasValid=*/ true, true, fade);
    // (60/25)^2 * stallWarnPct = 5.76 * 92 = saturates → clamp at 99.9
    check(roll25.percentLiftPct >= 99.0f,
          "V=25 (just past mute floor): saturated near 99 (way below Vs)");

    DisplayBuildInputs roll35{};
    fade = {};
    FillPercentLift(roll35, 4.0f, 35.0f, 0.0f, /*iasValid=*/ true, true, fade);
    // (60/35)^2 * 92 = 2.94 * 92 = saturates → clamp at 99.9
    check(roll35.percentLiftPct >= 99.0f,
          "V=35 takeoff roll: still saturated, well below Vs");

    DisplayBuildInputs roll45{};
    fade = {};
    FillPercentLift(roll45, 4.0f, 45.0f, 0.0f, /*iasValid=*/ true, true, fade);
    // (60/45)^2 * 92 = 1.78 * 92 = saturates → clamp at 99.9
    check(roll45.percentLiftPct >= 99.0f,
          "V=45 mid roll: still saturated, V² > 1.0 below Vs");

    // --------------------------------------------------------------
    // 3. V == Vs (above mute floor): percent is at the StallWarn
    //    anchor exactly.  Both percent scales agree at this anchor,
    //    by construction.
    // --------------------------------------------------------------
    DisplayBuildInputs atVs{};
    fade = {};
    FillPercentLift(atVs, 4.0f, 60.0f, 0.0f, /*iasValid=*/ true, true, fade);
    check(nearly(atVs.percentLiftPct, stallWarnPct, 1.0f),
          "V == Vs lands at the StallWarn anchor (alignment property)");

    // --------------------------------------------------------------
    // 3. V > Vs: percent drops smoothly into the OnSpeed band.
    //    Rolling at 65 kt, at 70 kt (rotation), at 80 kt (just
    //    airborne).  Should be monotone-decreasing.
    // --------------------------------------------------------------
    DisplayBuildInputs at65{};
    fade = {};
    FillPercentLift(at65, 4.0f, 65.0f, 0.0f, true, true, fade);
    DisplayBuildInputs at70{};
    fade = {};
    FillPercentLift(at70, 4.0f, 70.0f, 0.0f, true, true, fade);
    DisplayBuildInputs at80{};
    fade = {};
    FillPercentLift(at80, 4.0f, 80.0f, 0.0f, true, true, fade);

    check(at65.percentLiftPct > at70.percentLiftPct,
          "percent drops as V rises through 65 → 70 (post-Vs roll-out)");
    check(at70.percentLiftPct > at80.percentLiftPct,
          "percent drops as V rises through 70 → 80 (climb-out region)");

    // V=70 (1.17·Vs): pct = (60/70)² · stallWarnPct ≈ 0.735 · 92 ≈ 67%
    // V=80 (1.33·Vs): pct = (60/80)² · stallWarnPct ≈ 0.563 · 92 ≈ 52%
    check(at70.percentLiftPct > 60.0f && at70.percentLiftPct < 75.0f,
          "V=70 (~1.17·Vs) lands in the OnSpeed band (~67%)");
    check(at80.percentLiftPct > 45.0f && at80.percentLiftPct < 60.0f,
          "V=80 (~1.33·Vs) lands below OnSpeedFast (~52%)");

    // --------------------------------------------------------------
    // 4. iVs1G == 0: on-ground falls back to alpha formula.
    //    Same scenario the band_edge_iasvalid test covers; this
    //    just proves the V² path doesn't kick in when Vs is unset.
    // --------------------------------------------------------------
    iVs1G = 0;
    DisplayBuildInputs noVs{};
    fade = {};
    FillPercentLift(noVs, 7.0f, 0.0f, 0.0f, false, true, fade);
    // iasValid=false on the alpha path means percent collapses to 0
    // (ComputePercentLift's contract).  iVs1G == 0 disables the V²
    // path so the alpha path's gate becomes the only behavior.
    check(nearly(noVs.percentLiftPct, 0.0f, 0.1f),
          "no Vs configured: on-ground percent falls back to alpha "
          "(returns 0 below mute threshold)");
    iVs1G = 60;   // restore for subsequent assertions

    // --------------------------------------------------------------
    // 5. In flight (onGround=false): always use alpha formula even
    //    if V < Vs (e.g., post-stall recovery, slow flight at idle).
    // --------------------------------------------------------------
    DisplayBuildInputs slowFlight{};
    fade = {};
    FillPercentLift(slowFlight,
                    /*liveAoaDeg=*/      11.0f,    // near alpha_stall
                    /*liveIasKt=*/       55.0f,    // below Vs
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        true,
                    /*onGround=*/        false,
                    fade);
    // Alpha formula at α=11°: (11 - (-2)) / (12.5*1.075 - (-2))
    //                       = 13 / 15.4375 = 84.2%
    check(slowFlight.percentLiftPct > 75.0f && slowFlight.percentLiftPct < 95.0f,
          "in-flight slow-flight percent comes from alpha (~84%) even at V<Vs");

    // V² formula at V=55 would be (60/55)² · stallWarnPct = 1.19 · 92
    // ≈ 109 → clamped to 99.  Different number; assert we got the
    // alpha number, not the V² number.
    check(slowFlight.percentLiftPct < 99.0f,
          "in-flight at V<Vs uses alpha (would saturate to 99% on V² path)");

    // --------------------------------------------------------------
    // 6. Onground transition: at the moment of liftoff the crossfade
    //    must hold the displayed pct continuous across the regime
    //    change, then ramp linearly to the pure alpha-mode reading
    //    over kRegimeFadeFrames frames.  This is the design fix for
    //    Vac's "weird jump" — without the crossfade the displayed
    //    value would step by ~5 pp at the flip, then the audio
    //    smoother would smear that step into a multi-frame wiggle.
    // --------------------------------------------------------------
    {
        using onspeed_xplane::indexer::kRegimeFadeFrames;

        const float Vr = 70.0f;
        const float typicalAoaAtRotation = 9.0f;   // ~OnSpeedSlow

        RegimeFadeState liftFade{};

        // Last on-ground frame: V² regime.  Establishes prevDisplayedPct.
        DisplayBuildInputs preLift{};
        FillPercentLift(preLift,
                        typicalAoaAtRotation, Vr, 0.0f,
                        /*iasValid=*/ true,
                        /*onGround=*/ true,
                        liftFade);
        const float lastOnGround = preLift.percentLiftPct;

        // First in-flight frame: onGround flips.  Displayed value must
        // equal the previous frame's exactly — the crossfade absorbs
        // the V²-vs-alpha step on this tick.
        DisplayBuildInputs postLift{};
        FillPercentLift(postLift,
                        typicalAoaAtRotation, Vr, 0.0f,
                        /*iasValid=*/ true,
                        /*onGround=*/ false,
                        liftFade);
        check(nearly(postLift.percentLiftPct, lastOnGround, 0.1f),
              "liftoff frame: pct is continuous with last on-ground frame "
              "(crossfade absorbs the V²-vs-alpha step exactly)");

        // Successive frames: alpha-mode reading slowly emerges as the
        // latched delta decays linearly to zero.  After kRegimeFadeFrames
        // frames the fade is complete and the value matches pure alpha.
        DisplayBuildInputs alphaOnly{};
        RegimeFadeState noFade{};      // never-flipped state for a clean
                                       // pure-alpha-mode reading
        FillPercentLift(alphaOnly,
                        typicalAoaAtRotation, Vr, 0.0f,
                        /*iasValid=*/ true,
                        /*onGround=*/ false,
                        noFade);
        const float pureAlpha = alphaOnly.percentLiftPct;

        // Run the fade out and confirm the trajectory:
        //   - monotone toward pureAlpha (no overshoot, no wiggle)
        //   - lands within 0.1 pp of pureAlpha by frame
        //     kRegimeFadeFrames + 1
        float prev = postLift.percentLiftPct;
        for (int i = 1; i < kRegimeFadeFrames + 2; ++i) {
            DisplayBuildInputs f{};
            FillPercentLift(f,
                            typicalAoaAtRotation, Vr, 0.0f,
                            /*iasValid=*/ true,
                            /*onGround=*/ false,
                            liftFade);
            const float curr = f.percentLiftPct;
            // Monotone in the direction of the step (no wiggle).
            const bool stepUp = (pureAlpha > lastOnGround);
            if (stepUp) {
                check(curr >= prev - 0.001f,
                      "fade trajectory is monotone non-decreasing");
            } else {
                check(curr <= prev + 0.001f,
                      "fade trajectory is monotone non-increasing");
            }
            prev = curr;
        }
        check(nearly(prev, pureAlpha, 0.5f),
              "fade settles to pure alpha-mode within ~1 frame of "
              "kRegimeFadeFrames");
    }

    // --------------------------------------------------------------
    // 7. Sentinel iVs1G == -1 ("user explicitly disabled"): on-ground
    //    falls back to alpha just like iVs1G == 0, but the difference
    //    matters at the OnAircraftLoaded layer where 0 triggers an
    //    auto-seed and -1 doesn't.  Since FillPercentLift only sees
    //    the value, the visible behavior here is identical to the
    //    iVs1G==0 case — exercise it explicitly so a future regression
    //    that breaks the `<= 0` check (e.g. only checks `== 0`)
    //    surfaces here.
    // --------------------------------------------------------------
    iVs1G = -1;
    DisplayBuildInputs disabled{};
    fade = {};
    FillPercentLift(disabled,
                    /*liveAoaDeg=*/      7.0f,
                    /*liveIasKt=*/       40.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        true,
                    /*onGround=*/        true,
                    fade);
    // Alpha formula at 7° with iasValid=true returns a real percent
    // around the LDmax anchor.  Specifically NOT the V² number.
    check(disabled.percentLiftPct > 30.0f && disabled.percentLiftPct < 70.0f,
          "iVs1G == -1 (disabled) falls back to alpha (real pct, not V²)");
    // V² at this scenario would saturate to 99.9 (V=40 < Vs=60).
    // We must not see that.
    check(disabled.percentLiftPct < 90.0f,
          "iVs1G == -1 does not take the V² path (would saturate to 99)");
    iVs1G = 60;   // restore

    // --------------------------------------------------------------
    // 8. onground_any debounce: alternating raw inputs must not
    //    propagate to the debounced output.  The debounce threshold
    //    is kOnGroundHoldFrames=5 consecutive ticks; a single-frame
    //    flip should be absorbed.
    // --------------------------------------------------------------
    using onspeed_xplane::indexer::DebounceOnGround;
    using onspeed_xplane::indexer::OnGroundDebounceState;
    using onspeed_xplane::indexer::kOnGroundHoldFrames;

    {
        OnGroundDebounceState st{};       // starts in air (false)
        // Single-frame flicker: one tick of true, then back to false.
        // Output must remain false (we never accumulated 5 ticks).
        check(!DebounceOnGround(true,  st), "1-tick flicker T: still ground=false");
        check(!DebounceOnGround(false, st), "post-flicker F: still ground=false");

        // Sustained true for kOnGroundHoldFrames ticks: output flips.
        bool out = false;
        for (int i = 0; i < kOnGroundHoldFrames; ++i) {
            out = DebounceOnGround(true, st);
        }
        check(out, "sustained true for hold-frames flips debounce true");

        // Now sustained-false flicker once, then back to true.  Must
        // remain true (debounced).
        check(DebounceOnGround(false, st), "1-tick reverse flicker: still ground=true");
        check(DebounceOnGround(true,  st), "post-reverse-flicker: still ground=true");

        // Alternating inputs forever: debounced output never changes
        // because no value accumulates kOnGroundHoldFrames consecutive
        // ticks.  Run 50 alternating cycles and confirm.
        bool stuckOut = true;
        for (int i = 0; i < 50; ++i) {
            stuckOut = DebounceOnGround(i & 1 ? false : true, st);
        }
        check(stuckOut, "alternating raw inputs do not flip debounced output");
    }

    // --------------------------------------------------------------
    // 9. Debounce + percent-lift integration: even if a flicker hits
    //    BuildInputsFromDatarefs, the percent reading must not flip
    //    full-scale per call.  Simulate by running FillPercentLift
    //    with the *debounced* on-ground (what production code uses)
    //    across an alternating raw stream and confirm the percent
    //    stays inside a tight band.
    // --------------------------------------------------------------
    {
        OnGroundDebounceState st{};
        // Settle on-ground = true.
        for (int i = 0; i < kOnGroundHoldFrames; ++i) DebounceOnGround(true, st);

        float minPct =  1e9f;
        float maxPct = -1e9f;
        RegimeFadeState stickyFade{};
        // Alternate raw input 30 times.  Each call uses the debounced
        // result (stuck true), so percent should be stable per V²
        // formula at constant (Vs, V).
        for (int i = 0; i < 30; ++i) {
            const bool dbg = DebounceOnGround(i & 1 ? false : true, st);
            DisplayBuildInputs frame{};
            FillPercentLift(frame, 4.0f, 70.0f, 0.0f, true, dbg, stickyFade);
            if (frame.percentLiftPct < minPct) minPct = frame.percentLiftPct;
            if (frame.percentLiftPct > maxPct) maxPct = frame.percentLiftPct;
        }
        // Tight band: V² formula at constant (Vs=60, V=70) is
        // deterministic — same value every call.  Bound is generous
        // (1 pp) only to absorb floating-point drift if the formula
        // ever picks up a per-call rounding step.
        check((maxPct - minPct) < 1.0f,
              "alternating raw on-ground does not perturb percent (debounce holds it)");
    }

    // --------------------------------------------------------------
    // 10. MaybeSynthesizeAoaFromVSquared — direct test of the audio
    //     path's V² → AOA inversion.  CheckAOAAndPlayTone feeds the
    //     returned AOA to PlayAOATone, which compares against the
    //     fLDMAXAOA / fONSPEEDFAST / fONSPEEDSLOW / fSTALLWARN
    //     thresholds.  Pin the contract so a future change to the
    //     forward formula in MakeFlapCfg doesn't silently desync the
    //     audio path's inversion from the indicator's forward path.
    // --------------------------------------------------------------
    using onspeed_xplane::indexer::MaybeSynthesizeAoaFromVSquared;

    // Helper for the audio-path tests: most of them don't trigger a
    // regime flip, so liveAoaDeg and fadeState are inert.  Use a
    // representative body-angle AoA and a fresh fadeState per call.
    const float kProbeAoa = 4.0f;

    // Off the ground (no prior flip): V² regime not active and no fade
    // tail, returns NaN so the caller falls back to raw alpha.
    {
        RegimeFadeState st{};
        const float r = MaybeSynthesizeAoaFromVSquared(
                            /*liveIasKt=*/70.0f,
                            kProbeAoa,
                            /*iasValid=*/ true,
                            /*onGround=*/ false,
                            st);
        check(std::isnan(r), "off-ground returns NaN (fall back to raw alpha)");
    }

    // On the ground but iasValid=false (below mute floor): NaN.
    {
        RegimeFadeState st{};
        const float r = MaybeSynthesizeAoaFromVSquared(
                            /*liveIasKt=*/10.0f,
                            kProbeAoa,
                            /*iasValid=*/ false,
                            /*onGround=*/ true,
                            st);
        check(std::isnan(r), "iasValid=false on the ground returns NaN");
    }

    // V² active at V == Vs: synthesized AOA must round-trip back
    // through ComputePercentLift to the same StallWarn percent.
    // This is the alignment property that keeps audio cues at the
    // same band boundaries the indicator shows.
    {
        RegimeFadeState st{};
        const float r = MaybeSynthesizeAoaFromVSquared(
                            /*liveIasKt=*/60.0f,   // V == Vs
                            kProbeAoa,
                            /*iasValid=*/ true,
                            /*onGround=*/ true,
                            st);
        check(std::isfinite(r), "V²-active V==Vs returns finite AOA");
        check(r > 0.0f && r < fALPHASTALL,
              "V²-active V==Vs returns AOA in (0, alpha_stall)");
        // Returned AOA at V=Vs should sit at fSTALLWARNAOA exactly
        // (or within float-rounding of it).  This is the alignment
        // anchor: the inversion uses the same MakeFlapCfg the
        // forward path does.
        check(nearly(r, fSTALLWARNAOA, 0.5f),
              "V==Vs synthesized AOA lands near fSTALLWARNAOA");
    }

    // V > Vs (post-rotation regime): synthesized AOA drops below
    // fSTALLWARNAOA, into the OnSpeed band.
    {
        RegimeFadeState st{};
        const float r = MaybeSynthesizeAoaFromVSquared(
                            /*liveIasKt=*/80.0f,   // 1.33·Vs
                            kProbeAoa,
                            /*iasValid=*/ true,
                            /*onGround=*/ true,
                            st);
        check(std::isfinite(r), "V²-active V>Vs returns finite AOA");
        check(r < fSTALLWARNAOA,
              "V>Vs synthesized AOA drops below StallWarn");
        check(r > fLDMAXAOA,
              "V=80 (1.33·Vs) synthesized AOA stays above LDmax");
    }

    // V << Vs (early takeoff roll, well below stall speed): V²
    // saturates near max, synthesized AOA pegs near fALPHASTALL.
    // This is the takeoff-roll case the audio synthesis was added
    // to fix — silent before, audible "near stall" tones now.
    {
        RegimeFadeState st{};
        const float r = MaybeSynthesizeAoaFromVSquared(
                            /*liveIasKt=*/30.0f,   // 0.5·Vs, well below
                            kProbeAoa,
                            /*iasValid=*/ true,
                            /*onGround=*/ true,
                            st);
        check(std::isfinite(r), "V²-active V<<Vs returns finite AOA");
        check(r > fSTALLWARNAOA,
              "V<<Vs synthesized AOA exceeds StallWarn (saturated)");
    }

    // iVs1G == 0 (no Vs known): V² formula declines, returns NaN
    // so the audio path falls back to raw alpha (no fade either).
    {
        const int savedVs = iVs1G;
        iVs1G = 0;
        RegimeFadeState st{};
        const float r = MaybeSynthesizeAoaFromVSquared(
                            /*liveIasKt=*/60.0f,
                            kProbeAoa,
                            /*iasValid=*/ true,
                            /*onGround=*/ true,
                            st);
        check(std::isnan(r),
              "iVs1G==0 on-ground returns NaN (alpha-path fallback)");
        iVs1G = savedVs;
    }

    // -----------------------------------------------------------------
    // 11. Audio-path crossfade: after liftoff (regime flip on→off), the
    //     synthesized return value smoothly tracks the indicator-side
    //     fade.  Without this, audio would snap to raw alpha at the
    //     same moment the indicator is mid-crossfade — tone trigger
    //     would jump regions while the visual is still ramping.
    // -----------------------------------------------------------------
    {
        using onspeed_xplane::indexer::kRegimeFadeFrames;
        RegimeFadeState st{};

        // Last on-ground frame at V=Vr=70, body angle near OnSpeedSlow.
        // V² active, returns a synthesized AoA whose pct sits in the
        // OnSpeed band.
        const float prePct = [&]{
            const float r = MaybeSynthesizeAoaFromVSquared(
                                /*liveIasKt=*/70.0f,
                                /*liveAoaDeg=*/9.0f,
                                /*iasValid=*/  true,
                                /*onGround=*/  true,
                                st);
            check(std::isfinite(r),
                  "pre-liftoff: V² synth returns finite AoA");
            // Convert back to pct for an apples-to-apples compare with
            // the post-flip fade trajectory.
            return (r - 0.0f) / (fALPHASTALL - 0.0f) * 100.0f;
        }();

        // First in-flight frame: onGround flips.  V² goes NaN but the
        // fade kicks in — function returns a blended AoA whose pct
        // equals the previous frame's exactly.
        const float postPct = [&]{
            const float r = MaybeSynthesizeAoaFromVSquared(
                                /*liveIasKt=*/70.0f,
                                /*liveAoaDeg=*/9.0f,
                                /*iasValid=*/  true,
                                /*onGround=*/  false,
                                st);
            check(std::isfinite(r),
                  "first-in-flight: fade keeps AoA finite (not NaN)");
            return (r - 0.0f) / (fALPHASTALL - 0.0f) * 100.0f;
        }();
        check(nearly(postPct, prePct, 0.1f),
              "audio AoA pct is continuous across liftoff (no snap)");

        // Drain the fade and confirm we end up exactly at the raw-alpha
        // value (the inversion of liveAoaDeg=9° through MakeFlapCfg).
        float lastFiniteR = 0.0f;
        bool  sawNaN      = false;
        for (int i = 0; i < kRegimeFadeFrames + 5; ++i) {
            const float r = MaybeSynthesizeAoaFromVSquared(
                                /*liveIasKt=*/70.0f,
                                /*liveAoaDeg=*/9.0f,
                                /*iasValid=*/  true,
                                /*onGround=*/  false,
                                st);
            if (std::isnan(r)) sawNaN = true;
            else               lastFiniteR = r;
        }
        check(sawNaN,
              "post-fade frames return NaN once the fade tail completes "
              "(caller falls back to raw alpha)");
        check(nearly(lastFiniteR, 9.0f, 1.0f),
              "fade settles to raw alpha (~9°) over the fade window");
    }

    if (failures == 0) {
        std::printf("OK: iasground_pct (all invariants hold)\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return EXIT_FAILURE;
}
