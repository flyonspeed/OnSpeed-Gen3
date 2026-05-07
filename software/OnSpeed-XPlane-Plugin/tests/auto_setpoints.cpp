// auto_setpoints — pin the pure-function setpoint derivation that the
// X-Plane plugin's auto mode uses each frame to translate
// (acf_max_aoa_no_flap, acf_max_aoa_full_flap, flap_handle_deploy_ratio,
// NAOA fractions) → (LDmax / OnSpeedFast / OnSpeedSlow / StallWarn) AOA.
//
// See issue #392 and AutoSetpoints.h.

#include "../src/m5_indexer/AutoSetpoints.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

bool nearly(float a, float b, float tol = 0.01f)
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
    using onspeed_xplane::indexer::DeriveSetpointsFromStall;
    using onspeed_xplane::indexer::DerivedSetpoints;
    using onspeed_xplane::indexer::kCanonicalNaoa;
    using onspeed_xplane::indexer::LerpStallAoa;
    using onspeed_xplane::indexer::NaoaFractions;

    // ----------------------------------------------------------------
    // 1. Lerp endpoints + midpoint.
    // ----------------------------------------------------------------
    check(nearly(LerpStallAoa(16.0f, 14.0f, 0.0f), 16.0f),
          "lerp at flap=0 returns clean stall AOA");
    check(nearly(LerpStallAoa(16.0f, 14.0f, 1.0f), 14.0f),
          "lerp at flap=1 returns full-flap stall AOA");
    check(nearly(LerpStallAoa(16.0f, 14.0f, 0.5f), 15.0f),
          "lerp at flap=0.5 returns midpoint stall AOA");

    // Out-of-range flap ratios get clamped (defense-in-depth — the
    // production caller pre-clamps too).
    check(nearly(LerpStallAoa(16.0f, 14.0f, -0.5f), 16.0f),
          "lerp clamps negative flap ratio to 0");
    check(nearly(LerpStallAoa(16.0f, 14.0f,  1.5f), 14.0f),
          "lerp clamps flap ratio above 1 to 1");

    // ----------------------------------------------------------------
    // 2. Pure-function derivation: alpha_stall × NAOA fraction for each
    //    setpoint at flap=0 (clean stall AOA = 16°, Cessna-172-ish).
    // ----------------------------------------------------------------
    DerivedSetpoints dClean = DeriveSetpointsFromStall(
        /*alphaStallNoFlap=*/  16.0f,
        /*alphaStallFullFlap=*/14.0f,
        /*flapRatio=*/         0.0f,
        kCanonicalNaoa);

    check(dClean.applied, "auto-derive applies on plausible alpha_stall");
    check(nearly(dClean.ldmax,       0.45f  * 16.0f),  // 7.20
          "LDmax = 0.45 × 16° = 7.20° at flap=0");
    check(nearly(dClean.onSpeedFast, 0.549f * 16.0f),  // 8.784
          "OnSpeedFast = 0.549 × 16° ≈ 8.78° at flap=0");
    check(nearly(dClean.onSpeedSlow, 0.640f * 16.0f),  // 10.24
          "OnSpeedSlow = 0.640 × 16° ≈ 10.24° at flap=0");
    check(nearly(dClean.stallWarn,   0.92f  * 16.0f),  // 14.72
          "StallWarn = 0.92 × 16° = 14.72° at flap=0");

    // Setpoint ordering: must be strictly increasing on canonical
    // NAOA fractions for any positive alpha_stall.
    check(dClean.ldmax       < dClean.onSpeedFast,
          "ordering: LDmax < OnSpeedFast (clean)");
    check(dClean.onSpeedFast < dClean.onSpeedSlow,
          "ordering: OnSpeedFast < OnSpeedSlow (clean)");
    check(dClean.onSpeedSlow < dClean.stallWarn,
          "ordering: OnSpeedSlow < StallWarn (clean)");

    // ----------------------------------------------------------------
    // 3. Setpoints slide with flap deployment.  At full flap the
    //    alpha_stall is lower (14° vs 16°), so every setpoint scales
    //    down proportionally.
    // ----------------------------------------------------------------
    DerivedSetpoints dFull = DeriveSetpointsFromStall(16.0f, 14.0f, 1.0f,
                                                     kCanonicalNaoa);
    check(dFull.applied, "auto-derive applies at full flap");
    check(nearly(dFull.ldmax,       0.45f  * 14.0f),  // 6.30
          "LDmax slides to 0.45 × 14° = 6.30° at full flap");
    check(nearly(dFull.onSpeedFast, 0.549f * 14.0f),  // 7.686
          "OnSpeedFast slides at full flap");
    check(nearly(dFull.stallWarn,   0.92f  * 14.0f),  // 12.88
          "StallWarn slides to 0.92 × 14° = 12.88° at full flap");
    check(dFull.ldmax       < dClean.ldmax,
          "every setpoint is lower at full flap (alpha_stall is lower)");
    check(dFull.stallWarn   < dClean.stallWarn,
          "StallWarn at full flap < StallWarn at clean");

    // Midpoint: alpha_stall = 15°, so setpoints scale by 15/16.
    DerivedSetpoints dHalf = DeriveSetpointsFromStall(16.0f, 14.0f, 0.5f,
                                                     kCanonicalNaoa);
    check(dHalf.applied, "auto-derive applies at flap=0.5");
    check(nearly(dHalf.ldmax,       0.45f  * 15.0f),  // 6.75
          "LDmax at flap=0.5 = 0.45 × 15° = 6.75°");
    check(nearly(dHalf.stallWarn,   0.92f  * 15.0f),  // 13.80
          "StallWarn at flap=0.5 = 0.92 × 15° = 13.80°");

    // ----------------------------------------------------------------
    // 4. Bad datarefs (alpha_stall reads 0).  Auto-derive must NOT
    //    apply — leaving the live globals at their last-known values
    //    rather than collapsing the entire band to zero.
    // ----------------------------------------------------------------
    DerivedSetpoints dBad = DeriveSetpointsFromStall(0.0f, 0.0f, 0.0f,
                                                    kCanonicalNaoa);
    check(!dBad.applied,
          "auto-derive declines to apply when alpha_stall is 0 (bad dataref)");

    DerivedSetpoints dTinyStall = DeriveSetpointsFromStall(0.3f, 0.2f, 0.0f,
                                                           kCanonicalNaoa);
    check(!dTinyStall.applied,
          "auto-derive declines on alpha_stall below kMinPlausibleStallAoaDeg");

    // Just over the plausibility floor: must apply (no false negative
    // on a freaky-low-stall airframe).
    DerivedSetpoints dEdge = DeriveSetpointsFromStall(0.6f, 0.6f, 0.0f,
                                                     kCanonicalNaoa);
    check(dEdge.applied,
          "auto-derive applies at alpha_stall just above plausibility floor");

    // ----------------------------------------------------------------
    // 4b. Partial-dataref fallback.  Aircraft authors commonly leave
    //     one of the two acf_max_aoa_* values at the Plane-Maker
    //     default of 0 while populating the other.  The lerp would
    //     pull the band toward 0 with flap deployment and collapse
    //     approach cues; the helper falls back to the plausible
    //     endpoint as a constant for the entire flap range.
    // ----------------------------------------------------------------

    // no_flap=14, full_flap=0: only clean-stall is plausible.  Use 14
    // for every flap position.
    DerivedSetpoints dNoFlapOnly0 = DeriveSetpointsFromStall(14.0f, 0.0f, 0.0f,
                                                             kCanonicalNaoa);
    check(dNoFlapOnly0.applied,
          "fallback applies when only no_flap dataref is plausible (flap=0)");
    check(nearly(dNoFlapOnly0.stallWarn, 0.92f * 14.0f),
          "no_flap-only fallback uses 14° at flap=0");

    DerivedSetpoints dNoFlapOnly1 = DeriveSetpointsFromStall(14.0f, 0.0f, 1.0f,
                                                             kCanonicalNaoa);
    check(dNoFlapOnly1.applied,
          "fallback applies when only no_flap dataref is plausible (flap=1)");
    check(nearly(dNoFlapOnly1.stallWarn, 0.92f * 14.0f),
          "no_flap-only fallback still uses 14° at flap=1 (no lerp toward 0)");

    // full_flap=14, no_flap=0: symmetric case (rarer, but possible).
    DerivedSetpoints dFullFlapOnly0 = DeriveSetpointsFromStall(0.0f, 14.0f, 0.0f,
                                                               kCanonicalNaoa);
    check(dFullFlapOnly0.applied,
          "fallback applies when only full_flap dataref is plausible (flap=0)");
    check(nearly(dFullFlapOnly0.stallWarn, 0.92f * 14.0f),
          "full_flap-only fallback uses 14° at flap=0 (no lerp toward 0)");

    DerivedSetpoints dFullFlapOnly1 = DeriveSetpointsFromStall(0.0f, 14.0f, 1.0f,
                                                               kCanonicalNaoa);
    check(dFullFlapOnly1.applied,
          "fallback applies when only full_flap dataref is plausible (flap=1)");
    check(nearly(dFullFlapOnly1.stallWarn, 0.92f * 14.0f),
          "full_flap-only fallback uses 14° at flap=1");

    // ----------------------------------------------------------------
    // 5. Custom NAOA fractions: pilot can override the defaults.
    //    Sanity-check the math is strictly multiplicative — no hidden
    //    constants on the path.
    // ----------------------------------------------------------------
    NaoaFractions custom{0.5f, 0.6f, 0.7f, 0.95f};
    DerivedSetpoints dCustom = DeriveSetpointsFromStall(20.0f, 20.0f, 0.0f,
                                                       custom);
    check(dCustom.applied, "auto-derive applies with custom NAOA fractions");
    check(nearly(dCustom.ldmax,       10.0f),  // 0.5 × 20
          "custom NAOA: LDmax = 0.5 × 20° = 10.0°");
    check(nearly(dCustom.onSpeedFast, 12.0f),  // 0.6 × 20
          "custom NAOA: OnSpeedFast = 0.6 × 20° = 12.0°");
    check(nearly(dCustom.onSpeedSlow, 14.0f),  // 0.7 × 20
          "custom NAOA: OnSpeedSlow = 0.7 × 20° = 14.0°");
    check(nearly(dCustom.stallWarn,   19.0f),  // 0.95 × 20
          "custom NAOA: StallWarn = 0.95 × 20° = 19.0°");

    if (failures == 0) {
        std::printf("OK: auto_setpoints (all invariants hold)\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return EXIT_FAILURE;
}
