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
#include <api/CalwizSaveParse.h>
#include <config/OnSpeedConfig.h>

#include <cstring>
#include <string>

using onspeed::api::ApplyCalwizSave;
using onspeed::api::CalwizSaveInput;
using onspeed::api::CalwizSaveParseResult;
using onspeed::api::ExtractCalwizSave;
using onspeed::api::FindJsonValueStart;
using onspeed::api::ParseJsonInt;
using onspeed::api::ParseJsonNumber;
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

// ============================================================================
// End-to-end JSON-body tests (R1).
//
// PR 3's differential test (above) only exercised ApplyCalwizSave,
// the post-parse mutation helper.  PR 4 adds tests against the
// firmware's actual JSON parser (FindJsonValueStart + ParseJsonNumber
// + ExtractCalwizSave) so the lexer is a CI-pinned contract.  Once
// PR 5 deletes the legacy form path, ParseJsonNumber is the only
// remaining write path into g_Config.aFlaps; getting it wrong here
// silently corrupts calibration.
// ============================================================================

namespace {

// Build a happy-path JSON body programmatically.  Keep it compact —
// the production wizard emits compact JSON.stringify output, so the
// test mirrors that shape.
std::string MakeJsonBody(int flapsPos, const CalwizSaveInput& in) {
    char buf[768];
    std::snprintf(buf, sizeof(buf),
        "{\"flapsPos\":%d,"
        "\"LDmaxSetpoint\":%.6g,"
        "\"OSFastSetpoint\":%.6g,"
        "\"OSSlowSetpoint\":%.6g,"
        "\"StallWarnSetpoint\":%.6g,"
        "\"StallSetpoint\":%.6g,"
        "\"ManeuveringSetpoint\":%.6g,"
        "\"alpha0\":%.6g,"
        "\"alphaStall\":%.6g,"
        "\"K_fit\":%.6g,"
        "\"curve0\":%.6g,"
        "\"curve1\":%.6g,"
        "\"curve2\":%.6g}",
        flapsPos,
        static_cast<double>(in.ldMaxAoaDeg),
        static_cast<double>(in.onSpeedFastAoaDeg),
        static_cast<double>(in.onSpeedSlowAoaDeg),
        static_cast<double>(in.stallWarnAoaDeg),
        static_cast<double>(in.stallAoaDeg),
        static_cast<double>(in.maneuveringAoaDeg),
        static_cast<double>(in.alpha0Deg),
        static_cast<double>(in.alphaStallDeg),
        static_cast<double>(in.kFit),
        static_cast<double>(in.curve0),
        static_cast<double>(in.curve1),
        static_cast<double>(in.curve2));
    return std::string(buf);
}

}  // namespace

// ---- ParseJsonNumber lexer edge cases (R1) -----------------------

void test_parse_json_number_scientific_large(void) {
    std::string body = "{\"x\":1e10}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 0.0f;
    TEST_ASSERT_TRUE(ParseJsonNumber(body, p, &v));
    TEST_ASSERT_EQUAL_FLOAT(1e10f, v);
}

void test_parse_json_number_negative_scientific(void) {
    std::string body = "{\"x\":-0.5e-3}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 0.0f;
    TEST_ASSERT_TRUE(ParseJsonNumber(body, p, &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, -5e-4f, v);
}

void test_parse_json_number_double_dot_rejected(void) {
    std::string body = "{\"x\":1.2.3}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = -999.0f;
    TEST_ASSERT_FALSE(ParseJsonNumber(body, p, &v));
}

void test_parse_json_number_skips_leading_whitespace(void) {
    // The lexer tolerates spaces / tabs adjacent to the colon; the
    // wizard never emits them, but proxies and reformatters can.
    std::string body = "{\"x\":   42.5}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 0.0f;
    TEST_ASSERT_TRUE(ParseJsonNumber(body, p, &v));
    TEST_ASSERT_EQUAL_FLOAT(42.5f, v);
}

void test_parse_json_number_integer_form_for_float_field(void) {
    // The wizard sometimes ships an integer (e.g. 5) into a float
    // field.  ParseJsonNumber accepts the integer form per JSON.
    std::string body = "{\"x\":5}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 0.0f;
    TEST_ASSERT_TRUE(ParseJsonNumber(body, p, &v));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, v);
}

void test_parse_json_number_zero_and_negative_zero(void) {
    std::string body0  = "{\"x\":0}";
    std::string bodyN0 = "{\"x\":-0}";
    float v = 999.0f;
    int p = FindJsonValueStart(body0, "x");
    TEST_ASSERT_TRUE(p >= 0);
    TEST_ASSERT_TRUE(ParseJsonNumber(body0, p, &v));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v);

    p = FindJsonValueStart(bodyN0, "x");
    TEST_ASSERT_TRUE(p >= 0);
    TEST_ASSERT_TRUE(ParseJsonNumber(bodyN0, p, &v));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v);  // -0.0f == 0.0f under IEEE 754
}

void test_parse_json_number_overflow_to_infinity_rejected(void) {
    // 1e999 lexes as a valid JSON number, but strtof returns
    // ±HUGE_VALF; the lexer rejects non-finite results so the
    // SuFlaps never sees Inf.
    std::string body = "{\"x\":1e999}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 999.0f;
    TEST_ASSERT_FALSE(ParseJsonNumber(body, p, &v));
}

void test_parse_json_number_rejects_nan_token(void) {
    // `NaN` is not a JSON number — alphabetic tokens fail the lex.
    std::string body = "{\"x\":NaN}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 999.0f;
    TEST_ASSERT_FALSE(ParseJsonNumber(body, p, &v));
}

void test_parse_json_number_rejects_infinity_token(void) {
    std::string body = "{\"x\":Infinity}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 999.0f;
    TEST_ASSERT_FALSE(ParseJsonNumber(body, p, &v));
}

void test_parse_json_number_rejects_e_without_exponent_digits(void) {
    std::string body = "{\"x\":1e}";
    int p = FindJsonValueStart(body, "x");
    TEST_ASSERT_TRUE(p >= 0);
    float v = 999.0f;
    TEST_ASSERT_FALSE(ParseJsonNumber(body, p, &v));
}

// ---- ExtractCalwizSave end-to-end happy path ----------------------

void test_endtoend_json_body_clean_path(void) {
    // Full wizard output flow: build a JSON body, parse it back, and
    // verify the resulting SuFlaps post-state matches what the
    // CalwizSaveInput would produce when applied directly.  Pins the
    // parser/applier pair as a single contract.
    const Fixture& fx = kFixtures[0];  // clean_in_order
    std::string body = MakeJsonBody(fx.flapDegrees, fx.input);
    auto r = ExtractCalwizSave(body);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.errorMessage.c_str());
    TEST_ASSERT_EQUAL_INT(fx.flapDegrees, r.flapsPos);

    SuFlaps direct = MakeFreshFlap(fx.flapDegrees);
    SuFlaps parsed = MakeFreshFlap(fx.flapDegrees);
    ApplyCalwizSave(direct, fx.input);
    ApplyCalwizSave(parsed, r.input);

    AssertFlapBytewiseEqual(direct, parsed, "endtoend_json_body_clean_path");
}

// ---- ExtractCalwizSave error paths (R1 + R2) ---------------------

void test_endtoend_json_body_missing_field_400s(void) {
    // R2: the wizard never strips a field, but a buggy client could.
    // Confirm the parser rejects an absent LDmaxSetpoint with the
    // path-keyed error the HTTP handler turns into 400.  Legacy
    // form path zeros missing fields silently — this divergence is
    // intentional.
    const Fixture& fx = kFixtures[0];
    std::string body = MakeJsonBody(fx.flapDegrees, fx.input);
    // Replace the LDmax key with a different key so it's effectively
    // absent.  Keeps the JSON well-formed.
    auto pos = body.find("\"LDmaxSetpoint\"");
    TEST_ASSERT_TRUE(pos != std::string::npos);
    body.replace(pos, std::string("\"LDmaxSetpoint\"").size(),
                 "\"unrelatedKey\"");
    auto r = ExtractCalwizSave(body);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("LDmaxSetpoint", r.errorField.c_str());
}

void test_endtoend_json_body_nan_string_rejected(void) {
    // R2: a body with the literal token `NaN` (or `Infinity`) is
    // rejected.  Legacy atof would silently store NaN/Inf into the
    // SuFlaps; the new path 400s.
    std::string body =
        "{\"flapsPos\":0,"
        "\"LDmaxSetpoint\":NaN,"
        "\"OSFastSetpoint\":7,\"OSSlowSetpoint\":9,"
        "\"StallWarnSetpoint\":11,\"StallSetpoint\":12,"
        "\"ManeuveringSetpoint\":6,"
        "\"alpha0\":-2,\"alphaStall\":13,"
        "\"K_fit\":25000,"
        "\"curve0\":0.01,\"curve1\":-0.4,\"curve2\":7}";
    auto r = ExtractCalwizSave(body);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("LDmaxSetpoint", r.errorField.c_str());
}

void test_endtoend_json_body_infinity_string_rejected(void) {
    std::string body =
        "{\"flapsPos\":0,"
        "\"LDmaxSetpoint\":4.5,"
        "\"OSFastSetpoint\":Infinity,\"OSSlowSetpoint\":9,"
        "\"StallWarnSetpoint\":11,\"StallSetpoint\":12,"
        "\"ManeuveringSetpoint\":6,"
        "\"alpha0\":-2,\"alphaStall\":13,"
        "\"K_fit\":25000,"
        "\"curve0\":0.01,\"curve1\":-0.4,\"curve2\":7}";
    auto r = ExtractCalwizSave(body);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("OSFastSetpoint", r.errorField.c_str());
}

void test_endtoend_json_body_empty_body_400s(void) {
    auto r = ExtractCalwizSave(std::string_view());
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("body", r.errorField.c_str());
}

// ============================================================================
// Boundary fixtures (R2).
//
// SetpointOrderError() at OnSpeedConfig.cpp:46-47 enforces
// `alpha_0 < LDMAX` strictly; equality is treated as an order error
// (the percent-lift formula divides by alpha_stall - alpha_0 and a
// degenerate alpha_0 == alpha_stall would zero the denominator).
// These fixtures pin the boundary behavior.
// ============================================================================

void test_alpha0_equals_ldmax_is_order_error(void) {
    SuFlaps f = MakeFreshFlap(0);
    CalwizSaveInput in = kFixtures[0].input;
    in.alpha0Deg = in.ldMaxAoaDeg;  // alpha_0 == LDMAX (boundary)
    ApplyCalwizSave(f, in);
    std::string err = f.SetpointOrderError();
    TEST_ASSERT_TRUE_MESSAGE(!err.empty(),
        "alpha_0 == LDMAX should trigger an order error (>= rule)");
    TEST_ASSERT_TRUE_MESSAGE(err.find("Alpha0") != std::string::npos,
        "order error should mention Alpha0");
}

void test_alpha0_above_ldmax_is_order_error(void) {
    SuFlaps f = MakeFreshFlap(0);
    CalwizSaveInput in = kFixtures[0].input;
    in.alpha0Deg = in.ldMaxAoaDeg + 1.0f;  // typo: alpha_0 > LDMAX
    ApplyCalwizSave(f, in);
    std::string err = f.SetpointOrderError();
    TEST_ASSERT_TRUE(!err.empty());
    TEST_ASSERT_TRUE(err.find("Alpha0") != std::string::npos);
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

    // R1 — JSON parser end-to-end.
    RUN_TEST(test_parse_json_number_scientific_large);
    RUN_TEST(test_parse_json_number_negative_scientific);
    RUN_TEST(test_parse_json_number_double_dot_rejected);
    RUN_TEST(test_parse_json_number_skips_leading_whitespace);
    RUN_TEST(test_parse_json_number_integer_form_for_float_field);
    RUN_TEST(test_parse_json_number_zero_and_negative_zero);
    RUN_TEST(test_parse_json_number_overflow_to_infinity_rejected);
    RUN_TEST(test_parse_json_number_rejects_nan_token);
    RUN_TEST(test_parse_json_number_rejects_infinity_token);
    RUN_TEST(test_parse_json_number_rejects_e_without_exponent_digits);
    RUN_TEST(test_endtoend_json_body_clean_path);
    RUN_TEST(test_endtoend_json_body_missing_field_400s);
    RUN_TEST(test_endtoend_json_body_nan_string_rejected);
    RUN_TEST(test_endtoend_json_body_infinity_string_rejected);
    RUN_TEST(test_endtoend_json_body_empty_body_400s);

    // R2 — boundary fixtures for SetpointOrderError.
    RUN_TEST(test_alpha0_equals_ldmax_is_order_error);
    RUN_TEST(test_alpha0_above_ldmax_is_order_error);

    return UNITY_END();
}
