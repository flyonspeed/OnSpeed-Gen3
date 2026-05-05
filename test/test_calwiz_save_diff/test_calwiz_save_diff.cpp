// test_calwiz_save_diff.cpp — differential test for /api/calwiz/save.
//
// The new POST /api/calwiz/save endpoint mutates a SuFlaps via the
// pure helper `ApplyCalwizSave` in onspeed_core.  This test asserts
// that, for a representative set of wizard inputs, the post-state
// produced by the new helper is byte-identical to the post-state
// produced by inlining the legacy ConfigWebServer.cpp HandleCalWizard
// step=save mutation sequence (lines 3102-3116) directly here.
//
// What the test pins:
//   - All 9 setpoint floats land in the right SuFlaps fields.
//   - All 4 AoaCurve.afCoeff[] slots are written in the right order:
//       afCoeff[0] hardcoded 0
//       afCoeff[1] = curve0 (quadratic)
//       afCoeff[2] = curve1 (linear)
//       afCoeff[3] = curve2 (constant)
//   - iCurveType is hardcoded 1 (polynomial).
//   - The order-error fixture: SetpointOrderError() emits the same
//     verbatim text from both paths, since both paths feed the same
//     SetpointOrderError() implementation but the test guards against
//     accidentally swallowing a failure in the new path.
//
// Why bytewise: float assignment in the legacy code is a direct
// member-store from `g_Config.ToFloat(arg)` (atof).  The new helper
// reads the same kind of float from a struct member.  Anything other
// than bit-equality means the new path subtly diverged from the
// legacy semantics — typically a missed field, a swapped argument,
// or a `static_cast<float>(double)` somewhere.  TEST_ASSERT_EQUAL_MEMORY
// over the field block catches all three.
//
// CI gate.  Add this test to .github/workflows/ci.yml's pio test
// invocation (the existing one globs all native test dirs).

#include <unity.h>

#include <api/CalwizSave.h>
#include <config/OnSpeedConfig.h>

#include <cstring>
#include <string>

using onspeed::api::ApplyCalwizSave;
using onspeed::api::CalwizSaveInput;
using onspeed::config::OnSpeedConfig;
using SuFlaps = OnSpeedConfig::SuFlaps;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Fixture table — representative wizard inputs.
//
// Each fixture sets every field deliberately.  The byte-equality test
// catches any swap or drop, so the chosen values are made
// distinguishable rather than realistic.  Three "in-order" fixtures
// (clean / half / full flap) exercise the happy path; one "out-of-
// order" fixture tickles SetpointOrderError so we can pin its message
// match.
// ----------------------------------------------------------------------------

struct Fixture {
    const char*     name;
    int             flapDegrees;
    CalwizSaveInput input;
    bool            expectOrderError;
};

static const Fixture kFixtures[] = {
    // ----- clean (0°), realistic well-ordered RV-10-style values -----
    {
        "clean_in_order",
        /* flapDegrees */ 0,
        /* input */ {
            /* LDMAX        */ 4.5f,
            /* OnSpeedFast  */ 7.0f,
            /* OnSpeedSlow  */ 9.5f,
            /* StallWarn    */ 11.5f,
            /* Stall        */ 12.8f,
            /* Maneuvering  */ 6.2f,
            /* alpha0       */ -2.1f,
            /* alphaStall   */ 14.3f,
            /* kFit         */ 25400.0f,
            /* curve0..2    */ 0.0123f, -0.456f, 7.89f,
        },
        /* expectOrderError */ false,
    },

    // ----- half flap (15°), distinct values to catch field-swap bugs -----
    {
        "half_in_order",
        /* flapDegrees */ 15,
        /* input */ {
            5.0f,    // LDMAX
            7.5f,    // OnSpeedFast
            10.0f,   // OnSpeedSlow
            12.0f,   // StallWarn
            13.5f,   // Stall
            6.5f,    // Maneuvering
            -3.2f,   // alpha0
            15.0f,   // alphaStall
            18000.0f,
            0.02f, -0.6f, 8.0f,
        },
        false,
    },

    // ----- full flap (30°), negative-leaning aircraft -----
    {
        "full_in_order",
        /* flapDegrees */ 30,
        /* input */ {
            3.5f, 6.0f, 8.5f, 10.5f, 11.8f, 5.5f,
            -4.0f, 13.6f, 15800.0f,
            0.018f, -0.5f, 7.2f,
        },
        false,
    },

    // ----- order-error fixture: OnSpeedFast LESS than LDMAX -----
    // Triggers the first branch of SetpointOrderError() ("LDMAX must
    // be less than OnSpeedFast").  Both paths must emit identical
    // text — the test confirms the new path doesn't silently swallow
    // the warning.
    {
        "ldmax_above_osfast",
        /* flapDegrees */ 0,
        /* input */ {
            8.0f,    // LDMAX  ← higher than OnSpeedFast
            7.0f,    // OnSpeedFast
            9.5f,    // OnSpeedSlow
            11.5f,   // StallWarn
            12.8f,   // Stall
            6.2f,    // Maneuvering
            -2.1f, 14.3f, 25400.0f,
            0.01f, -0.4f, 7.5f,
        },
        true,
    },
};

constexpr size_t kFixtureCount = sizeof(kFixtures) / sizeof(kFixtures[0]);

// ----------------------------------------------------------------------------
// Legacy mutation sequence — copied verbatim from
// ConfigWebServer.cpp HandleCalWizard step=save (lines 3102-3116) but
// rewritten to take values from the fixture instead of CfgServer.arg().
// `g_Config.ToFloat(...)` is `atof(arg.c_str())`; in the test the
// fixture already supplies floats, so we feed them in directly.
//
// Don't refactor or "DRY" this with the new code path — they need to
// stay independent so the test compares two parallel implementations.
// ----------------------------------------------------------------------------

static void ApplyLegacy(SuFlaps& flap, const CalwizSaveInput& in) {
    flap.fLDMAXAOA       = in.ldMaxAoaDeg;
    flap.fONSPEEDFASTAOA = in.onSpeedFastAoaDeg;
    flap.fONSPEEDSLOWAOA = in.onSpeedSlowAoaDeg;
    flap.fSTALLWARNAOA   = in.stallWarnAoaDeg;
    flap.fSTALLAOA       = in.stallAoaDeg;
    flap.fMANAOA         = in.maneuveringAoaDeg;
    flap.fAlpha0         = in.alpha0Deg;
    flap.fAlphaStall     = in.alphaStallDeg;
    flap.fKFit           = in.kFit;

    flap.AoaCurve.afCoeff[0] = 0;
    flap.AoaCurve.afCoeff[1] = in.curve0;
    flap.AoaCurve.afCoeff[2] = in.curve1;
    flap.AoaCurve.afCoeff[3] = in.curve2;
    flap.AoaCurve.iCurveType = 1;  // polynomial
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

// Build a SuFlaps with default-constructed fields.  Setting iDegrees
// here mirrors what HandleCalWizard's caller does — it only enters
// the save branch after matching `g_Config.aFlaps[i].iDegrees` against
// the form's flapsPos field, so the test fixture starts from a flap
// whose iDegrees matches the requested position.
static SuFlaps MakeFreshFlap(int degrees) {
    SuFlaps f;
    f.iDegrees = degrees;
    return f;
}

// Memcmp-style float-block compare.  TEST_ASSERT_EQUAL_FLOAT is too
// loose (Unity's default delta is 5 ULP) — we want exact bit equality
// because both paths assign the same source float to the same target.
// Any difference here is a real divergence, never floating-point noise.
static void AssertFlapBytewiseEqual(const SuFlaps& legacy,
                                    const SuFlaps& neu,
                                    const char* fixtureName) {
    char msg[128];

    // The 9 individual float setpoints — compare as raw bits.
    auto asBits = [](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    };

    auto cmp = [&](const char* field, float lv, float nv) {
        std::snprintf(msg, sizeof(msg),
                      "%s: %s differs (legacy=%g new=%g)",
                      fixtureName, field, static_cast<double>(lv),
                      static_cast<double>(nv));
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(asBits(lv), asBits(nv), msg);
    };

    cmp("fLDMAXAOA",       legacy.fLDMAXAOA,       neu.fLDMAXAOA);
    cmp("fONSPEEDFASTAOA", legacy.fONSPEEDFASTAOA, neu.fONSPEEDFASTAOA);
    cmp("fONSPEEDSLOWAOA", legacy.fONSPEEDSLOWAOA, neu.fONSPEEDSLOWAOA);
    cmp("fSTALLWARNAOA",   legacy.fSTALLWARNAOA,   neu.fSTALLWARNAOA);
    cmp("fSTALLAOA",       legacy.fSTALLAOA,       neu.fSTALLAOA);
    cmp("fMANAOA",         legacy.fMANAOA,         neu.fMANAOA);
    cmp("fAlpha0",         legacy.fAlpha0,         neu.fAlpha0);
    cmp("fAlphaStall",     legacy.fAlphaStall,     neu.fAlphaStall);
    cmp("fKFit",           legacy.fKFit,           neu.fKFit);

    // The 4 curve coefficients + curve type.
    cmp("afCoeff[0]", legacy.AoaCurve.afCoeff[0], neu.AoaCurve.afCoeff[0]);
    cmp("afCoeff[1]", legacy.AoaCurve.afCoeff[1], neu.AoaCurve.afCoeff[1]);
    cmp("afCoeff[2]", legacy.AoaCurve.afCoeff[2], neu.AoaCurve.afCoeff[2]);
    cmp("afCoeff[3]", legacy.AoaCurve.afCoeff[3], neu.AoaCurve.afCoeff[3]);

    std::snprintf(msg, sizeof(msg),
                  "%s: iCurveType differs (legacy=%u new=%u)",
                  fixtureName,
                  static_cast<unsigned>(legacy.AoaCurve.iCurveType),
                  static_cast<unsigned>(neu.AoaCurve.iCurveType));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(legacy.AoaCurve.iCurveType,
                                    neu.AoaCurve.iCurveType, msg);

    // iDegrees + iPotPosition shouldn't be touched by either path; pin
    // that they remain at the values the fixture's MakeFreshFlap set.
    std::snprintf(msg, sizeof(msg), "%s: iDegrees changed", fixtureName);
    TEST_ASSERT_EQUAL_INT_MESSAGE(legacy.iDegrees, neu.iDegrees, msg);
    std::snprintf(msg, sizeof(msg),
                  "%s: iPotPosition changed", fixtureName);
    TEST_ASSERT_EQUAL_INT_MESSAGE(legacy.iPotPosition, neu.iPotPosition, msg);
}

// ----------------------------------------------------------------------------
// Tests — one per fixture for readable failure output, plus a
// SetpointOrderError parity check on the order-error fixture.
// ----------------------------------------------------------------------------

static void RunFixture(const Fixture& fx) {
    SuFlaps legacy = MakeFreshFlap(fx.flapDegrees);
    SuFlaps neu    = MakeFreshFlap(fx.flapDegrees);

    ApplyLegacy(legacy, fx.input);
    ApplyCalwizSave(neu, fx.input);

    AssertFlapBytewiseEqual(legacy, neu, fx.name);

    // Parity check on SetpointOrderError text.  Both flaps see the
    // same setpoint values, so the result must be the same string.
    std::string sLegacy = legacy.SetpointOrderError();
    std::string sNew    = neu.SetpointOrderError();
    char msg[160];
    std::snprintf(msg, sizeof(msg),
                  "%s: SetpointOrderError text differs\n  legacy: \"%s\"\n  new:    \"%s\"",
                  fx.name, sLegacy.c_str(), sNew.c_str());
    TEST_ASSERT_TRUE_MESSAGE(sLegacy == sNew, msg);

    if (fx.expectOrderError) {
        std::snprintf(msg, sizeof(msg),
                      "%s: expected an order-error message, got empty",
                      fx.name);
        TEST_ASSERT_TRUE_MESSAGE(!sLegacy.empty(), msg);
    } else {
        std::snprintf(msg, sizeof(msg),
                      "%s: expected no order error, got \"%s\"",
                      fx.name, sLegacy.c_str());
        TEST_ASSERT_TRUE_MESSAGE(sLegacy.empty(), msg);
    }
}

void test_clean_in_order(void)         { RunFixture(kFixtures[0]); }
void test_half_in_order(void)          { RunFixture(kFixtures[1]); }
void test_full_in_order(void)          { RunFixture(kFixtures[2]); }
void test_ldmax_above_osfast(void)     { RunFixture(kFixtures[3]); }

// Sanity check on the table itself — guards against accidentally
// reordering or dropping a fixture without updating RUN_TEST below.
void test_fixture_count(void) {
    TEST_ASSERT_EQUAL_size_t(4, kFixtureCount);
}

// Pin the curve-coefficient mapping with explicit named asserts.  The
// legacy code does:
//     afCoeff[0] = 0
//     afCoeff[1] = curve0
//     afCoeff[2] = curve1
//     afCoeff[3] = curve2
// so a typo that, say, swaps curve0/curve2 wouldn't drift the
// bytewise-equal test (both paths would be wrong the same way).  Pin
// this directly with hand-written expectations, independent of
// ApplyLegacy.
void test_curve_coefficient_mapping_explicit(void) {
    SuFlaps f;
    CalwizSaveInput in;
    in.curve0 = 0.111f;  // -> afCoeff[1]
    in.curve1 = 0.222f;  // -> afCoeff[2]
    in.curve2 = 0.333f;  // -> afCoeff[3]
    ApplyCalwizSave(f, in);

    TEST_ASSERT_EQUAL_FLOAT(0.0f,   f.AoaCurve.afCoeff[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.111f, f.AoaCurve.afCoeff[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.222f, f.AoaCurve.afCoeff[2]);
    TEST_ASSERT_EQUAL_FLOAT(0.333f, f.AoaCurve.afCoeff[3]);
    TEST_ASSERT_EQUAL_UINT8(1, f.AoaCurve.iCurveType);
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_fixture_count);
    RUN_TEST(test_clean_in_order);
    RUN_TEST(test_half_in_order);
    RUN_TEST(test_full_in_order);
    RUN_TEST(test_ldmax_above_osfast);
    RUN_TEST(test_curve_coefficient_mapping_explicit);

    return UNITY_END();
}
