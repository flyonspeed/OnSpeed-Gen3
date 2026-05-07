// auto_setpoints — pin the pure-function setpoint derivation that the
// X-Plane plugin's auto mode uses each frame to translate
// (acf_stall_warn_alpha, NAOA fractions) → (LDmax / OnSpeedFast /
// OnSpeedSlow / StallWarn) AOA.
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
    using onspeed_xplane::indexer::DeriveSetpointsFromStallWarn;
    using onspeed_xplane::indexer::DerivedSetpoints;
    using onspeed_xplane::indexer::kCanonicalNaoa;
    using onspeed_xplane::indexer::NaoaFractions;

    // ----------------------------------------------------------------
    // 1. Canonical NAOA fractions on a representative GA stall-warn
    //    AOA.  RV-10 (Laminar): acf_stall_warn_alpha = 10.0°.
    //    Implicit alpha_stall = 10.0 / 0.92 = 10.87°.
    // ----------------------------------------------------------------
    DerivedSetpoints dRv10 = DeriveSetpointsFromStallWarn(
        /*stallWarnAoa=*/ 10.0f, kCanonicalNaoa);
    check(dRv10.applied, "auto-derive applies on plausible stall-warn AOA");
    check(nearly(dRv10.stallWarn, 10.0f),
          "StallWarn equals acf_stall_warn_alpha by construction (RV-10)");
    // alpha_stall_synthetic = 10.87°
    check(nearly(dRv10.ldmax,       0.45f  * (10.0f / 0.92f), 0.05f),
          "LDmax = 0.45 × (StallWarn/0.92) ≈ 4.89° (RV-10)");
    check(nearly(dRv10.onSpeedFast, 0.549f * (10.0f / 0.92f), 0.05f),
          "OnSpeedFast = 0.549 × (StallWarn/0.92) ≈ 5.97° (RV-10)");
    check(nearly(dRv10.onSpeedSlow, 0.640f * (10.0f / 0.92f), 0.05f),
          "OnSpeedSlow = 0.640 × (StallWarn/0.92) ≈ 6.96° (RV-10)");

    // Setpoint ordering: must be strictly increasing on canonical
    // NAOA fractions for any positive stall-warn AOA.
    check(dRv10.ldmax       < dRv10.onSpeedFast,
          "ordering: LDmax < OnSpeedFast");
    check(dRv10.onSpeedFast < dRv10.onSpeedSlow,
          "ordering: OnSpeedFast < OnSpeedSlow");
    check(dRv10.onSpeedSlow < dRv10.stallWarn,
          "ordering: OnSpeedSlow < StallWarn");

    // ----------------------------------------------------------------
    // 2. C172 (Laminar): acf_stall_warn_alpha = 12.0°.  Same NAOA
    //    fractions, different stall-warn anchor → setpoints scale.
    // ----------------------------------------------------------------
    DerivedSetpoints dC172 = DeriveSetpointsFromStallWarn(
        /*stallWarnAoa=*/ 12.0f, kCanonicalNaoa);
    check(dC172.applied, "auto-derive applies (C172)");
    check(nearly(dC172.stallWarn, 12.0f), "StallWarn = 12° (C172)");
    check(nearly(dC172.ldmax, 0.45f * (12.0f / 0.92f), 0.05f),
          "LDmax ≈ 5.87° (C172)");

    // Different aircraft → different setpoints, in proportion.
    check(dC172.ldmax     > dRv10.ldmax,
          "C172 LDmax higher than RV-10 LDmax (higher StallWarn anchor)");
    check(dC172.stallWarn > dRv10.stallWarn,
          "C172 StallWarn higher than RV-10 StallWarn (anchor by construction)");

    // ----------------------------------------------------------------
    // 3. F-14 (Laminar): acf_stall_warn_alpha = 20.0°.  High-AOA
    //    airframe stresses the math at the upper end.
    // ----------------------------------------------------------------
    DerivedSetpoints dF14 = DeriveSetpointsFromStallWarn(
        /*stallWarnAoa=*/ 20.0f, kCanonicalNaoa);
    check(dF14.applied, "auto-derive applies (F-14)");
    check(nearly(dF14.stallWarn, 20.0f), "StallWarn = 20° (F-14)");
    check(nearly(dF14.ldmax, 0.45f * (20.0f / 0.92f), 0.05f),
          "LDmax ≈ 9.78° (F-14)");

    // ----------------------------------------------------------------
    // 4. Bad dataref (acf_stall_warn_alpha reads 0).  Auto-derive must
    //    NOT apply — leaving the live globals at their last-known
    //    values rather than collapsing the entire band to zero.
    // ----------------------------------------------------------------
    DerivedSetpoints dBad = DeriveSetpointsFromStallWarn(0.0f, kCanonicalNaoa);
    check(!dBad.applied,
          "auto-derive declines when stall-warn AOA is 0 (bad dataref)");

    DerivedSetpoints dTinyStall = DeriveSetpointsFromStallWarn(
        0.3f, kCanonicalNaoa);
    check(!dTinyStall.applied,
          "auto-derive declines below kMinPlausibleStallWarnAoaDeg");

    // Just over the plausibility floor: must apply (no false negative
    // on a freaky-low-stall airframe).
    DerivedSetpoints dEdge = DeriveSetpointsFromStallWarn(0.6f, kCanonicalNaoa);
    check(dEdge.applied,
          "auto-derive applies at stall-warn AOA just above plausibility floor");

    // ----------------------------------------------------------------
    // 5. Custom NAOA fractions: pilot can override the defaults.
    //    Sanity-check the math is strictly multiplicative — no hidden
    //    constants on the path.
    // ----------------------------------------------------------------
    NaoaFractions custom{0.5f, 0.6f, 0.7f, 0.95f};
    DerivedSetpoints dCustom = DeriveSetpointsFromStallWarn(19.0f, custom);
    // alpha_stall = 19.0 / 0.95 = 20.0
    check(dCustom.applied, "auto-derive applies with custom NAOA fractions");
    check(nearly(dCustom.ldmax,       10.0f),  // 0.5 × 20
          "custom NAOA: LDmax = 0.5 × 20° = 10.0°");
    check(nearly(dCustom.onSpeedFast, 12.0f),  // 0.6 × 20
          "custom NAOA: OnSpeedFast = 0.6 × 20° = 12.0°");
    check(nearly(dCustom.onSpeedSlow, 14.0f),  // 0.7 × 20
          "custom NAOA: OnSpeedSlow = 0.7 × 20° = 14.0°");
    check(nearly(dCustom.stallWarn,   19.0f),  // 0.95 × 20 (= input)
          "custom NAOA: StallWarn = 0.95 × 20° = 19.0° (= input)");

    // Defensive: zero or negative stallWarn fraction would divide by
    // zero; helper must decline rather than crash.
    NaoaFractions zeroWarn{0.45f, 0.549f, 0.640f, 0.0f};
    DerivedSetpoints dZeroWarn = DeriveSetpointsFromStallWarn(10.0f, zeroWarn);
    check(!dZeroWarn.applied,
          "declines when naoa.stallWarn is 0 (no divide-by-zero)");

    if (failures == 0) {
        std::printf("OK: auto_setpoints (all invariants hold)\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return EXIT_FAILURE;
}
