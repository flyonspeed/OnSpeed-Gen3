// auto_setpoints — pin the pure-function setpoint derivation that the
// X-Plane plugin's auto mode uses each frame to translate
// (acf_stall_warn_alpha, acf_Vs, acf_Vso, flap_handle_deploy_ratio,
// NAOA fractions) → (LDmax / OnSpeedFast / OnSpeedSlow / StallWarn).
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
    using onspeed_xplane::indexer::DeriveSetpointsFromDatarefs;
    using onspeed_xplane::indexer::DerivedSetpoints;
    using onspeed_xplane::indexer::kCanonicalNaoa;
    using onspeed_xplane::indexer::NaoaFractions;

    // ----------------------------------------------------------------
    // 1. RV-10 clean (flapRatio=0).  Laminar stock:
    //      acf_stall_warn_alpha = 10.0°
    //      acf_Vs  = 65 KIAS
    //      acf_Vso = 50 KIAS
    //    Implicit alpha_stall_clean = 10.0 / 0.92 = 10.87°.
    // ----------------------------------------------------------------
    DerivedSetpoints dRv10Clean = DeriveSetpointsFromDatarefs(
        /*stallWarnAoa=*/10.0f,
        /*vsKt=*/65.0f,
        /*vsoKt=*/50.0f,
        /*flapRatio=*/0.0f,
        kCanonicalNaoa);
    check(dRv10Clean.applied, "auto-derive applies on plausible inputs");
    check(nearly(dRv10Clean.stallWarn, 10.0f),
          "RV-10 clean: StallWarn equals acf_stall_warn_alpha");
    check(nearly(dRv10Clean.ldmax,       0.45f  * (10.0f / 0.92f), 0.05f),
          "RV-10 clean: LDmax ≈ 4.89°");
    check(nearly(dRv10Clean.onSpeedFast, 0.549f * (10.0f / 0.92f), 0.05f),
          "RV-10 clean: OnSpeedFast ≈ 5.97°");
    check(nearly(dRv10Clean.onSpeedSlow, 0.640f * (10.0f / 0.92f), 0.05f),
          "RV-10 clean: OnSpeedSlow ≈ 6.96°");
    check(dRv10Clean.ldmax       < dRv10Clean.onSpeedFast, "ordering: LDmax < OnSpeedFast");
    check(dRv10Clean.onSpeedFast < dRv10Clean.onSpeedSlow, "ordering: OnSpeedFast < OnSpeedSlow");
    check(dRv10Clean.onSpeedSlow < dRv10Clean.stallWarn,   "ordering: OnSpeedSlow < StallWarn");

    // ----------------------------------------------------------------
    // 2. RV-10 full flaps (flapRatio=1).  alpha_stall_full =
    //    10.87° × (50/65)² = 6.43°.  Setpoints all scale down.
    // ----------------------------------------------------------------
    DerivedSetpoints dRv10Full = DeriveSetpointsFromDatarefs(
        10.0f, 65.0f, 50.0f, 1.0f, kCanonicalNaoa);
    check(dRv10Full.applied, "RV-10 full flap: applied");
    const float alphaStallFull = (10.0f / 0.92f) * (50.0f / 65.0f) * (50.0f / 65.0f);
    check(nearly(dRv10Full.stallWarn, 0.92f * alphaStallFull, 0.05f),
          "RV-10 full flap: StallWarn = 0.92 × alpha_stall_full ≈ 5.92°");
    check(nearly(dRv10Full.ldmax,       0.45f  * alphaStallFull, 0.05f),
          "RV-10 full flap: LDmax ≈ 2.89°");
    check(nearly(dRv10Full.onSpeedFast, 0.549f * alphaStallFull, 0.05f),
          "RV-10 full flap: OnSpeedFast ≈ 3.53°");
    check(nearly(dRv10Full.onSpeedSlow, 0.640f * alphaStallFull, 0.05f),
          "RV-10 full flap: OnSpeedSlow ≈ 4.12°");

    // Setpoint ordering preserved at full flap.
    check(dRv10Full.ldmax       < dRv10Full.onSpeedFast, "full flap ordering: LDmax < OnSpeedFast");
    check(dRv10Full.onSpeedFast < dRv10Full.onSpeedSlow, "full flap ordering: OnSpeedFast < OnSpeedSlow");
    check(dRv10Full.onSpeedSlow < dRv10Full.stallWarn,   "full flap ordering: OnSpeedSlow < StallWarn");

    // Each setpoint shrinks with flap deployment.
    check(dRv10Full.ldmax       < dRv10Clean.ldmax,       "LDmax decreases with flaps");
    check(dRv10Full.onSpeedFast < dRv10Clean.onSpeedFast, "OnSpeedFast decreases with flaps");
    check(dRv10Full.onSpeedSlow < dRv10Clean.onSpeedSlow, "OnSpeedSlow decreases with flaps");
    check(dRv10Full.stallWarn   < dRv10Clean.stallWarn,   "StallWarn decreases with flaps");

    // ----------------------------------------------------------------
    // 3. RV-10 half flaps (flapRatio=0.5).  alpha_stall lerps halfway
    //    between clean and full.
    // ----------------------------------------------------------------
    DerivedSetpoints dRv10Half = DeriveSetpointsFromDatarefs(
        10.0f, 65.0f, 50.0f, 0.5f, kCanonicalNaoa);
    const float alphaStallClean = 10.0f / 0.92f;
    const float alphaStallHalf  = 0.5f * (alphaStallClean + alphaStallFull);
    check(nearly(dRv10Half.stallWarn, 0.92f * alphaStallHalf, 0.05f),
          "RV-10 half flap: StallWarn lerps halfway");
    check(nearly(dRv10Half.ldmax,     0.45f * alphaStallHalf, 0.05f),
          "RV-10 half flap: LDmax lerps halfway");
    // Half flap sits between clean and full for every setpoint.
    check(dRv10Half.ldmax       > dRv10Full.ldmax       && dRv10Half.ldmax       < dRv10Clean.ldmax,       "half flap LDmax between full and clean");
    check(dRv10Half.onSpeedFast > dRv10Full.onSpeedFast && dRv10Half.onSpeedFast < dRv10Clean.onSpeedFast, "half flap OnSpeedFast between");
    check(dRv10Half.onSpeedSlow > dRv10Full.onSpeedSlow && dRv10Half.onSpeedSlow < dRv10Clean.onSpeedSlow, "half flap OnSpeedSlow between");
    check(dRv10Half.stallWarn   > dRv10Full.stallWarn   && dRv10Half.stallWarn   < dRv10Clean.stallWarn,   "half flap StallWarn between");

    // ----------------------------------------------------------------
    // 4. C172 clean (Laminar): acf_stall_warn_alpha=12°, Vs=48, Vso=40.
    // ----------------------------------------------------------------
    DerivedSetpoints dC172 = DeriveSetpointsFromDatarefs(
        12.0f, 48.0f, 40.0f, 0.0f, kCanonicalNaoa);
    check(dC172.applied, "C172: applied");
    check(nearly(dC172.stallWarn, 12.0f, 0.05f), "C172 clean: StallWarn = 12°");
    check(nearly(dC172.ldmax, 0.45f * (12.0f / 0.92f), 0.05f),
          "C172 clean: LDmax ≈ 5.87°");
    check(dC172.ldmax > dRv10Clean.ldmax,
          "C172 LDmax > RV-10 LDmax (higher StallWarn anchor)");

    // C172 full flap: alpha_stall_full = (12/0.92) × (40/48)² = 9.06°.
    DerivedSetpoints dC172Full = DeriveSetpointsFromDatarefs(
        12.0f, 48.0f, 40.0f, 1.0f, kCanonicalNaoa);
    const float c172StallFull = (12.0f / 0.92f) * (40.0f / 48.0f) * (40.0f / 48.0f);
    check(nearly(dC172Full.stallWarn, 0.92f * c172StallFull, 0.05f),
          "C172 full flap: StallWarn ≈ 8.33°");

    // ----------------------------------------------------------------
    // 5. F-14: acf_stall_warn_alpha=20°, Vs=150, Vso=110.  High-AOA
    //    airframe stresses the upper end.
    // ----------------------------------------------------------------
    DerivedSetpoints dF14 = DeriveSetpointsFromDatarefs(
        20.0f, 150.0f, 110.0f, 0.0f, kCanonicalNaoa);
    check(dF14.applied, "F-14: applied");
    check(nearly(dF14.stallWarn, 20.0f, 0.05f), "F-14 clean: StallWarn = 20°");
    check(nearly(dF14.ldmax, 0.45f * (20.0f / 0.92f), 0.05f),
          "F-14 clean: LDmax ≈ 9.78°");

    // ----------------------------------------------------------------
    // 6. Fallback: missing Vs / Vso → flat alpha_stall across flap range.
    // ----------------------------------------------------------------
    DerivedSetpoints dFlatClean = DeriveSetpointsFromDatarefs(
        10.0f, 0.0f, 0.0f, 0.0f, kCanonicalNaoa);
    DerivedSetpoints dFlatFull  = DeriveSetpointsFromDatarefs(
        10.0f, 0.0f, 0.0f, 1.0f, kCanonicalNaoa);
    check(dFlatClean.applied && dFlatFull.applied,
          "Vs missing: still applies (flat across flaps)");
    check(nearly(dFlatClean.stallWarn, dFlatFull.stallWarn),
          "Vs missing: StallWarn flat across flap range");
    check(nearly(dFlatClean.ldmax,     dFlatFull.ldmax),
          "Vs missing: LDmax flat across flap range");
    check(nearly(dFlatClean.stallWarn, 10.0f, 0.05f),
          "Vs missing: StallWarn falls back to clean anchor");

    // Same fallback when only one of Vs/Vso is below the floor.
    DerivedSetpoints dPartialFallback = DeriveSetpointsFromDatarefs(
        10.0f, 65.0f, 0.0f, 1.0f, kCanonicalNaoa);
    check(nearly(dPartialFallback.stallWarn, 10.0f, 0.05f),
          "Vso=0: falls back to flat (won't divide by zero or extrapolate)");

    // ----------------------------------------------------------------
    // 7. flapRatio outside [0,1] is clamped — protects against rare
    //    sim states where the dataref reports a partial extension
    //    beyond the marked detent.
    // ----------------------------------------------------------------
    DerivedSetpoints dOver = DeriveSetpointsFromDatarefs(
        10.0f, 65.0f, 50.0f, 1.5f, kCanonicalNaoa);
    check(nearly(dOver.stallWarn, dRv10Full.stallWarn, 0.05f),
          "flapRatio>1 clamps to full flap");
    DerivedSetpoints dUnder = DeriveSetpointsFromDatarefs(
        10.0f, 65.0f, 50.0f, -0.5f, kCanonicalNaoa);
    check(nearly(dUnder.stallWarn, dRv10Clean.stallWarn, 0.05f),
          "flapRatio<0 clamps to clean");

    // ----------------------------------------------------------------
    // 8. Bad stall-warn dataref (reads 0 or implausibly small).
    // ----------------------------------------------------------------
    DerivedSetpoints dBad = DeriveSetpointsFromDatarefs(
        0.0f, 65.0f, 50.0f, 0.0f, kCanonicalNaoa);
    check(!dBad.applied, "declines when stall-warn AOA is 0");

    DerivedSetpoints dTinyStall = DeriveSetpointsFromDatarefs(
        0.3f, 65.0f, 50.0f, 0.0f, kCanonicalNaoa);
    check(!dTinyStall.applied,
          "declines below kMinPlausibleStallWarnAoaDeg");

    DerivedSetpoints dEdge = DeriveSetpointsFromDatarefs(
        0.6f, 65.0f, 50.0f, 0.0f, kCanonicalNaoa);
    check(dEdge.applied,
          "applies just above plausibility floor");

    // ----------------------------------------------------------------
    // 9. Custom NAOA fractions: math is strictly multiplicative.
    // ----------------------------------------------------------------
    NaoaFractions custom{0.5f, 0.6f, 0.7f, 0.95f};
    DerivedSetpoints dCustom = DeriveSetpointsFromDatarefs(
        19.0f, 65.0f, 65.0f, 0.0f, custom);
    // alpha_stall = 19.0 / 0.95 = 20.0 (Vso==Vs → no flap reduction).
    check(dCustom.applied, "applies with custom NAOA");
    check(nearly(dCustom.ldmax,       10.0f),  "custom NAOA: LDmax = 10°");
    check(nearly(dCustom.onSpeedFast, 12.0f),  "custom NAOA: OnSpeedFast = 12°");
    check(nearly(dCustom.onSpeedSlow, 14.0f),  "custom NAOA: OnSpeedSlow = 14°");
    check(nearly(dCustom.stallWarn,   19.0f),  "custom NAOA: StallWarn = 19°");

    // Defensive: zero stallWarn fraction would divide by zero.
    NaoaFractions zeroWarn{0.45f, 0.549f, 0.640f, 0.0f};
    DerivedSetpoints dZeroWarn = DeriveSetpointsFromDatarefs(
        10.0f, 65.0f, 50.0f, 0.0f, zeroWarn);
    check(!dZeroWarn.applied, "declines when naoa.stallWarn is 0");

    if (failures == 0) {
        std::printf("OK: auto_setpoints (all invariants hold)\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return EXIT_FAILURE;
}
