// Regression test pinning the X-Plane plugin's band-edge / pip
// independence from iasValid.
//
// Bug: DataRefAdapter::BuildInputsFromDatarefs forwarded `iasValid`
// to the four band-edge anchor calls (tonesOn / fast / slow /
// stallWarn) and to the pip lerp endpoints derived from them.  On
// the ground at IAS below the audio mute threshold, every anchor
// collapsed to 0 and the indexer's pip pinned to the bottom of the
// band regardless of live alpha — wrong: the pip is an aerodynamic
// visual reference at calibration anchors, and those anchors don't
// depend on whether the air is moving.
//
// Contract pinned in onspeed_core's DisplayPctAnchors.h producer
// note ("the producer always passes true for the band-edge / L/Dmax
// anchors") and matched by the firmware call sites in
// DisplaySerial.cpp + DataServer.cpp.  This test calls the plugin's
// FillPercentLift directly, exercising the production code path
// post-fix.

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

    DisplayBuildInputs onGround{};
    FillPercentLift(onGround,
                    /*liveAoaDeg=*/   7.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/    false);

    // Live reading: ComputePercentLift's contract returns 0 when
    // iasValid=false.  No airflow, no live percent — by design.
    check(nearly(onGround.percentLiftPct, 0.0f),
          "live percentLiftPct collapses to 0 when iasValid=false");

    // Anchors: must NOT collapse — the bug.
    check(onGround.tonesOnPctLift     > 40 && onGround.tonesOnPctLift     < 70,
          "tonesOnPctLift sits in (40, 70) on the ground (was 0, the bug)");
    check(onGround.onSpeedFastPctLift > 50 && onGround.onSpeedFastPctLift < 80,
          "onSpeedFastPctLift sits in (50, 80) on the ground (was 0, the bug)");
    check(onGround.onSpeedSlowPctLift > 60 && onGround.onSpeedSlowPctLift < 90,
          "onSpeedSlowPctLift sits in (60, 90) on the ground (was 0, the bug)");
    check(onGround.stallWarnPctLift   > 90 && onGround.stallWarnPctLift   < 100,
          "stallWarnPctLift sits in (90, 100) on the ground (was 0, the bug)");

    // Anchor ordering: must be strictly increasing on a sane
    // calibration.  An aside that catches accidental sign / formula
    // regressions on the anchor calls.
    check(onGround.tonesOnPctLift     < onGround.onSpeedFastPctLift,
          "anchor ordering: LDmax < OnSpeedFast");
    check(onGround.onSpeedFastPctLift < onGround.onSpeedSlowPctLift,
          "anchor ordering: OnSpeedFast < OnSpeedSlow");
    check(onGround.onSpeedSlowPctLift < onGround.stallWarnPctLift,
          "anchor ordering: OnSpeedSlow < StallWarn");

    // Pip: at flapRatio=0 the pip equals the clean LDmax pct (was 0
    // pre-fix because `cleanPip = tonesOnPctLift = 0`).
    check(onGround.pipPctLift == onGround.tonesOnPctLift,
          "pip == tonesOnPctLift at clean (was 0 == 0 pre-fix; now real == real)");
    check(onGround.pipPctLift > 0,
          "pipPctLift is non-zero on the ground at clean (was 0, the bug)");

    // ----------------------------------------------------------------
    // In-flight (iasValid=true).  Anchors should be identical to the
    // on-ground values — proof that anchors don't depend on iasValid.
    // ----------------------------------------------------------------
    DisplayBuildInputs inFlight{};
    FillPercentLift(inFlight,
                    /*liveAoaDeg=*/   7.0f,
                    /*flapHandleRatio=*/ 0.0f,
                    /*iasValid=*/    true);

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
    FillPercentLift(flapHalf, 7.0f, 0.5f, false);

    DisplayBuildInputs flapFull{};
    FillPercentLift(flapFull, 7.0f, 1.0f, false);

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
