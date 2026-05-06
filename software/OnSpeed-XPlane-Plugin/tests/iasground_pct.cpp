// Regression test pinning the X-Plane plugin's on-ground V²-based
// percent-lift formula.
//
// On-ground rule (Vac, design discussion in PR #434):
//   When the gear is loaded, body angle isn't aerodynamically
//   meaningful — the gear, not the wing, is supporting weight.
//   Percent-lift becomes "how loaded the wing is at this airspeed
//   if it had to support weight in 1G level."  That's the V²
//   formula:
//
//       pct_v² = (Vs / V)²  ·  stallWarnPct
//
//   pinned at the StallWarn anchor when V == Vs so the V² scale
//   and the alpha scale agree on where "near stall" sits on the
//   indexer band.
//
// The tests below verify:
//   1. Pre-takeoff at low V, percent saturates near max — the
//      wing is at max effort.
//   2. As V rises through Vs, the pct passes through StallWarn.
//   3. Past Vs (rolling fast / liftoff regime), percent drops
//      smoothly into the OnSpeed band.
//   4. With iVs1G == 0 (no Vs known), on-ground falls back to
//      alpha — old behavior preserved.
//   5. In flight (onGround=false), the alpha formula is used
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

    // --------------------------------------------------------------
    // The StallWarn anchor pct is the V² formula's scaling target.
    // Compute it once via FillPercentLift's own anchor calculation
    // (anchors are pure functions of calibration; alpha or V² mode
    // doesn't change them).
    // --------------------------------------------------------------
    DisplayBuildInputs probe{};
    FillPercentLift(probe, 0.0f, 80.0f, 0.0f, true, /*onGround=*/false);
    const float stallWarnPct = static_cast<float>(probe.stallWarnPctLift);
    check(stallWarnPct > 80.0f && stallWarnPct < 100.0f,
          "fixture: stallWarn anchor in (80, 100)");

    // --------------------------------------------------------------
    // 1. Pre-takeoff (V ≈ 0): percent saturates at stallWarnPct.
    //    Wing is at max effort — pilot's takeoff-roll mental model.
    // --------------------------------------------------------------
    DisplayBuildInputs at0{};
    FillPercentLift(at0,
                    /*liveAoaDeg=*/      4.0f,    // body-angle on the ramp
                    /*liveIasKt=*/       0.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        false,   // below mute threshold
                    /*onGround=*/        true);
    check(nearly(at0.percentLiftPct, stallWarnPct, 0.5f),
          "V=0 saturates at stallWarn pct (V → 0 means max wing effort)");

    DisplayBuildInputs at20{};
    FillPercentLift(at20, 4.0f, 20.0f, 0.0f, false, true);
    // (60/20)^2 * stallWarnPct = 9 * 92 = saturates → clamp at 99.9
    check(at20.percentLiftPct >= 99.0f,
          "V=20 saturates above 99% (well below Vs, max wing effort)");

    DisplayBuildInputs at30{};
    FillPercentLift(at30, 4.0f, 30.0f, 0.0f, false, true);
    // (60/30)^2 * 92 = 4 * 92 = saturates
    check(at30.percentLiftPct >= 99.0f,
          "V=30 saturates above 99%");

    // --------------------------------------------------------------
    // 2. V == Vs: percent is at the StallWarn anchor exactly.
    //    Both percent scales agree at this anchor, by construction.
    // --------------------------------------------------------------
    DisplayBuildInputs atVs{};
    FillPercentLift(atVs, 4.0f, 60.0f, 0.0f, true, true);
    check(nearly(atVs.percentLiftPct, stallWarnPct, 1.0f),
          "V == Vs lands at the StallWarn anchor (alignment property)");

    // --------------------------------------------------------------
    // 3. V > Vs: percent drops smoothly into the OnSpeed band.
    //    Rolling at 65 kt, at 70 kt (rotation), at 80 kt (just
    //    airborne).  Should be monotone-decreasing.
    // --------------------------------------------------------------
    DisplayBuildInputs at65{};
    FillPercentLift(at65, 4.0f, 65.0f, 0.0f, true, true);
    DisplayBuildInputs at70{};
    FillPercentLift(at70, 4.0f, 70.0f, 0.0f, true, true);
    DisplayBuildInputs at80{};
    FillPercentLift(at80, 4.0f, 80.0f, 0.0f, true, true);

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
    FillPercentLift(noVs, 7.0f, 0.0f, 0.0f, false, true);
    // iasValid=false on the alpha path means percent collapses to 0
    // (ComputePercentLift's contract).  This is the OLD behavior —
    // documenting it here proves the V² path is the only escape.
    check(nearly(noVs.percentLiftPct, 0.0f, 0.1f),
          "no Vs configured: on-ground percent falls back to alpha "
          "(returns 0 below mute threshold, the old behavior)");
    iVs1G = 60;   // restore for subsequent assertions

    // --------------------------------------------------------------
    // 5. In flight (onGround=false): always use alpha formula even
    //    if V < Vs (e.g., post-stall recovery, slow flight at idle).
    // --------------------------------------------------------------
    DisplayBuildInputs slowFlight{};
    FillPercentLift(slowFlight,
                    /*liveAoaDeg=*/      11.0f,    // near alpha_stall
                    /*liveIasKt=*/       55.0f,    // below Vs
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        true,
                    /*onGround=*/        false);
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
    // 6. Onground transition: at the moment of liftoff, percent
    //    should be continuous across the regime change at typical
    //    rotation speeds.  Compare V² mode at V=Vr against alpha
    //    mode at the same V.  Should be in the same neighborhood.
    // --------------------------------------------------------------
    const float Vr = 70.0f;
    const float typicalAoaAtRotation = 9.0f;   // ~OnSpeedSlow

    DisplayBuildInputs preLift{};
    FillPercentLift(preLift,
                    typicalAoaAtRotation, Vr, 0.0f,
                    /*iasValid=*/ true,
                    /*onGround=*/ true);

    DisplayBuildInputs postLift{};
    FillPercentLift(postLift,
                    typicalAoaAtRotation, Vr, 0.0f,
                    /*iasValid=*/ true,
                    /*onGround=*/ false);

    // Both should be in the OnSpeed band region (~60-80%).  The
    // exact numbers should differ very little (V² vs alpha agree at
    // the StallWarn anchor by construction; the gap at typical Vr
    // measures ~3 pp empirically with this calibration).  The 5 pp
    // bound is empirically chosen with headroom — not a design
    // constraint — to catch regime-transition regressions while
    // tolerating future small calibration tweaks.
    check(std::fabs(preLift.percentLiftPct - postLift.percentLiftPct) < 5.0f,
          "regime transition at Vr is < 5 percentage points "
          "(V² and alpha agree near the OnSpeed band)");

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
    FillPercentLift(disabled,
                    /*liveAoaDeg=*/      7.0f,
                    /*liveIasKt=*/       40.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        true,
                    /*onGround=*/        true);
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
        // Alternate raw input 30 times.  Each call uses the debounced
        // result (stuck true), so percent should be stable per V²
        // formula at constant (Vs, V).
        for (int i = 0; i < 30; ++i) {
            const bool dbg = DebounceOnGround(i & 1 ? false : true, st);
            DisplayBuildInputs frame{};
            FillPercentLift(frame, 4.0f, 70.0f, 0.0f, true, dbg);
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

    if (failures == 0) {
        std::printf("OK: iasground_pct (all invariants hold)\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return EXIT_FAILURE;
}
