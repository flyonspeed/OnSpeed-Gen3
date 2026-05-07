// Regression test pinning the X-Plane plugin's band-edge / pip
// independence from iasValid.
//
// Contract: the four band-edge anchors (tonesOn / fast / slow /
// stallWarn) and the pip lerp endpoints derived from them are pure
// functions of calibration.  Their position on the percent scale
// depends only on the per-flap setpoints, not on whether the air is
// moving.  Only the live `percentLiftPct` field gates on iasValid.
//
// Source of the contract: onspeed_core DisplayPctAnchors.h's
// producer note ("the producer always passes true for the band-edge
// / L/Dmax anchors"), matched by the firmware call sites in
// DisplaySerial.cpp + DataServer.cpp.  This test calls the plugin's
// FillPercentLift directly, exercising the same production code
// path that feeds the wire format.

#include "../src/m5_indexer/DataRefAdapter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// FillPercentLift reads the same plugin-side globals defined in
// aoa_audio.cpp; redefine them here so the test doesn't pull the
// plugin's audio TU.  Values mirror the plugin defaults.
float fLDMAXAOA       =  6.0f;
float fONSPEEDFASTAOA =  7.3f;
float fONSPEEDSLOWAOA =  9.6f;
float fSTALLWARNAOA   = 12.5f;

// 0 disables the V² on-ground formula so this test exercises the
// alpha-only path.  The V² path gets its own test in
// iasground_pct.cpp.
int iVs1G = 0;

namespace {

bool nearly(float a, float b, float tol = 0.5f)
{
    return std::fabs(a - b) <= tol;
}

int failures = 0;

void check(bool cond, const char* what)
{
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main()
{
    using onspeed::proto::DisplayBuildInputs;
    using onspeed_xplane::indexer::FillPercentLift;

    // ----------------------------------------------------------------
    // Vac's repro: T-6 on the ground, IAS below the audio mute floor,
    // live alpha at 7° (between LDmax and OnSpeedFast).  Pre-fix, all
    // four anchors collapsed to 0 and the pip's lerp endpoints both
    // collapsed to 0, pinning everything visually to the bottom of
    // the indexer.  Post-fix, anchors come from calibration and the
    // pip slides smoothly with the flap handle.
    // ----------------------------------------------------------------

    // iVs1G=0 above means the on-ground V² path is disabled and
    // FillPercentLift falls back to the alpha-based formula on
    // both regimes — matching the original PR #432 surface.
    DisplayBuildInputs onGround{};
    FillPercentLift(onGround,
                    /*liveAoaDeg=*/      7.0f,
                    /*liveIasKt=*/       40.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        false,
                    /*onGround=*/        true);

    // Live reading: ComputePercentLift's contract returns 0 when
    // iasValid=false.  No airflow, no live percent — by design.
    check(nearly(onGround.percentLiftPct, 0.0f),
          "live percentLiftPct collapses to 0 when iasValid=false");

    // Anchors: pure functions of calibration, do not collapse with iasValid.
    check(onGround.tonesOnPctLift     > 40 && onGround.tonesOnPctLift     < 70,
          "tonesOnPctLift in (40, 70) regardless of iasValid");
    check(onGround.onSpeedFastPctLift > 50 && onGround.onSpeedFastPctLift < 80,
          "onSpeedFastPctLift in (50, 80) regardless of iasValid");
    check(onGround.onSpeedSlowPctLift > 60 && onGround.onSpeedSlowPctLift < 90,
          "onSpeedSlowPctLift in (60, 90) regardless of iasValid");
    check(onGround.stallWarnPctLift   > 90 && onGround.stallWarnPctLift   < 100,
          "stallWarnPctLift in (90, 100) regardless of iasValid");

    // Anchor ordering: must be strictly increasing on a sane
    // calibration.  An aside that catches accidental sign / formula
    // regressions on the anchor calls.
    check(onGround.tonesOnPctLift     < onGround.onSpeedFastPctLift,
          "anchor ordering: LDmax < OnSpeedFast");
    check(onGround.onSpeedFastPctLift < onGround.onSpeedSlowPctLift,
          "anchor ordering: OnSpeedFast < OnSpeedSlow");
    check(onGround.onSpeedSlowPctLift < onGround.stallWarnPctLift,
          "anchor ordering: OnSpeedSlow < StallWarn");

    // Pip: at flapRatio=0 the pip equals the clean LDmax pct because
    // the lerp's clean endpoint is tonesOnPctLift.
    check(onGround.pipPctLift == onGround.tonesOnPctLift,
          "pip == tonesOnPctLift at clean flap setting");
    check(onGround.pipPctLift > 0,
          "pipPctLift is non-zero at clean (anchors stay calibrated)");

    // ----------------------------------------------------------------
    // In-flight (iasValid=true).  Anchors should be identical to the
    // on-ground values — proof that anchors don't depend on iasValid.
    // ----------------------------------------------------------------
    DisplayBuildInputs inFlight{};
    FillPercentLift(inFlight,
                    /*liveAoaDeg=*/      7.0f,
                    /*liveIasKt=*/       80.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/        true,
                    /*onGround=*/        false);

    check(inFlight.tonesOnPctLift     == onGround.tonesOnPctLift,
          "tonesOnPctLift identical across iasValid (anchors are pure)");
    check(inFlight.onSpeedFastPctLift == onGround.onSpeedFastPctLift,
          "onSpeedFastPctLift identical across iasValid");
    check(inFlight.onSpeedSlowPctLift == onGround.onSpeedSlowPctLift,
          "onSpeedSlowPctLift identical across iasValid");
    check(inFlight.stallWarnPctLift   == onGround.stallWarnPctLift,
          "stallWarnPctLift identical across iasValid");

    // Live reading flips on with iasValid=true.
    check(inFlight.percentLiftPct > 0.0f,
          "live percentLiftPct positive when iasValid=true and alpha > alpha_0");

    // Live reading (alpha=7°) sits between LDmax and OnSpeedFast
    // anchors — geometrically consistent with where the chevron
    // should render in the band.
    const float live = inFlight.percentLiftPct;
    check(live >= static_cast<float>(inFlight.tonesOnPctLift) - 1.0f,
          "live percent >= LDmax anchor at alpha=7° > LDmax(6°)");
    check(live <= static_cast<float>(inFlight.onSpeedFastPctLift) + 1.0f,
          "live percent <= OnSpeedFast anchor at alpha=7° < OnSpeedFast(7.3°)");

    // ----------------------------------------------------------------
    // Pip slides as flaps deploy.  Pre-fix this also collapsed to 0
    // when iasValid=false; post-fix it slides smoothly.
    // ----------------------------------------------------------------
    DisplayBuildInputs flapHalf{};
    FillPercentLift(flapHalf, 7.0f, 40.0f, 0.5f, false, true);

    DisplayBuildInputs flapFull{};
    FillPercentLift(flapFull, 7.0f, 40.0f, 1.0f, false, true);

    check(flapHalf.pipPctLift > onGround.pipPctLift,
          "pipPctLift rises as flaps deploy (clean -> half)");
    check(flapFull.pipPctLift > flapHalf.pipPctLift,
          "pipPctLift rises as flaps deploy (half -> full)");
    check(flapFull.pipPctLift > onGround.pipPctLift + 5,
          "pipPctLift slides a meaningful amount across the flap range");

    // Anchors don't move with flap handle (they're tied to active
    // detent's calibration, and the plugin only has one detent).
    check(flapFull.tonesOnPctLift == onGround.tonesOnPctLift,
          "tonesOnPctLift independent of flap handle (single-detent plugin)");
    check(flapFull.stallWarnPctLift == onGround.stallWarnPctLift,
          "stallWarnPctLift independent of flap handle");

    if (failures == 0) {
        std::printf("OK: band_edge_iasvalid (all invariants hold)\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return EXIT_FAILURE;
}
