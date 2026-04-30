// test_config_json.cpp — schema-pin tests for /api/calwiz/state.
//
// Covers the read-only side of the calwiz state endpoint added in PR 2:
//   - Exact key set is emitted.
//   - Aircraft block contains only the documented keys.
//   - Per-flap entries contain only the documented keys.
//   - Non-finite floats sentinel to 0 (uncalibrated).
//   - currentFlapIndex out-of-range clamps to 0.
//   - Empty flaps vector emits "flaps":[].
//
// The test uses string searches rather than a real JSON parser because
// the native test environment doesn't pull in nlohmann or rapidjson.
// Expected key markers are exact substrings of the emitted document
// (e.g. `"alpha0Deg":`); any drift in field naming fails the test.
//
// PLAN_WEB_PREACT_REWRITE §3 PR 2 + §4k schema rules.

#include <unity.h>

#include <api/CalwizStateJson.h>

#include <cmath>
#include <cstring>
#include <string>

using onspeed::api::CalwizStateInputs;
using onspeed::api::SerializeCalwizState;
using onspeed::config::OnSpeedConfig;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

static bool Contains(const std::string& s, const char* sub) {
    return s.find(sub) != std::string::npos;
}

static int CountOccurrences(const std::string& s, const char* sub) {
    int n = 0;
    size_t pos = 0;
    size_t needle = std::strlen(sub);
    if (needle == 0) return 0;
    while ((pos = s.find(sub, pos)) != std::string::npos) {
        ++n;
        pos += needle;
    }
    return n;
}

// Build a representative two-flap input.  Numbers chosen so each field
// is distinguishable in the emitted JSON.
static CalwizStateInputs MakeTwoFlapInputs() {
    CalwizStateInputs in;
    in.acGrossWeightLb = 2700;
    in.acBestGlideKt   = 87.5f;
    in.acVfeKt         = 96.0f;
    in.acGLimit        = 4.4f;

    OnSpeedConfig::SuFlaps clean{};
    clean.iDegrees        = 0;
    clean.fAlpha0         = -2.1f;
    clean.fAlphaStall     = 14.3f;
    clean.fLDMAXAOA       = 6.0f;
    clean.fONSPEEDFASTAOA = 8.0f;
    clean.fONSPEEDSLOWAOA = 9.5f;
    clean.fSTALLWARNAOA   = 11.0f;
    clean.fSTALLAOA       = 12.5f;
    clean.fMANAOA         = 7.0f;

    OnSpeedConfig::SuFlaps full = clean;
    full.iDegrees     = 30;
    full.fAlpha0      = -3.8f;
    full.fAlphaStall  = 15.7f;
    full.fLDMAXAOA    = 5.5f;

    in.flaps.push_back(clean);
    in.flaps.push_back(full);
    in.currentFlapIndex = 1;
    return in;
}

// ----------------------------------------------------------------------------
// Top-level shape
// ----------------------------------------------------------------------------

void test_top_level_keys_present(void) {
    auto in   = MakeTwoFlapInputs();
    auto json = SerializeCalwizState(in);

    TEST_ASSERT_TRUE(Contains(json, "\"aircraft\":"));
    TEST_ASSERT_TRUE(Contains(json, "\"currentFlapIndex\":"));
    TEST_ASSERT_TRUE(Contains(json, "\"flaps\":"));

    // No legacy field names sneak in.  Audit catches drift if anyone
    // copy-pastes a Hungarian-prefixed name into the serializer.
    TEST_ASSERT_FALSE(Contains(json, "iAcGrossWeight"));
    TEST_ASSERT_FALSE(Contains(json, "fAcVfe"));
    TEST_ASSERT_FALSE(Contains(json, "fLDMAXAOA"));
    TEST_ASSERT_FALSE(Contains(json, "fAlpha0"));
}

void test_aircraft_block_keys_exact(void) {
    auto in   = MakeTwoFlapInputs();
    auto json = SerializeCalwizState(in);

    TEST_ASSERT_TRUE(Contains(json, "\"grossWeightLb\":2700"));
    TEST_ASSERT_TRUE(Contains(json, "\"bestGlideKt\":87.5"));
    TEST_ASSERT_TRUE(Contains(json, "\"vfeKt\":96"));
    TEST_ASSERT_TRUE(Contains(json, "\"gLimit\":4.4"));

    // Each aircraft key appears exactly once at the top level.  (The
    // per-flap block uses different names, so collisions are not a
    // concern.)
    TEST_ASSERT_EQUAL_INT(1, CountOccurrences(json, "\"grossWeightLb\":"));
    TEST_ASSERT_EQUAL_INT(1, CountOccurrences(json, "\"bestGlideKt\":"));
    TEST_ASSERT_EQUAL_INT(1, CountOccurrences(json, "\"vfeKt\":"));
    TEST_ASSERT_EQUAL_INT(1, CountOccurrences(json, "\"gLimit\":"));
}

void test_per_flap_keys_exact(void) {
    auto in   = MakeTwoFlapInputs();
    auto json = SerializeCalwizState(in);

    // Each per-flap key appears once per detent.
    const char* perFlapKeys[] = {
        "\"index\":",
        "\"degrees\":",
        "\"alpha0Deg\":",
        "\"alphaStallDeg\":",
        "\"ldMaxAoaDeg\":",
        "\"onSpeedFastAoaDeg\":",
        "\"onSpeedSlowAoaDeg\":",
        "\"stallWarnAoaDeg\":",
        "\"stallAoaDeg\":",
        "\"maneuveringAoaDeg\":",
    };
    for (const char* k : perFlapKeys) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, CountOccurrences(json, k), k);
    }
}

void test_per_flap_values_present(void) {
    auto in   = MakeTwoFlapInputs();
    auto json = SerializeCalwizState(in);

    // Active flap (full) has its values reflected.  -3.8 alpha0,
    // 15.7 alphaStall.
    TEST_ASSERT_TRUE(Contains(json, "\"alpha0Deg\":-3.8"));
    TEST_ASSERT_TRUE(Contains(json, "\"alphaStallDeg\":15.7"));
    TEST_ASSERT_TRUE(Contains(json, "\"degrees\":30"));

    // Clean flap entries also present.
    TEST_ASSERT_TRUE(Contains(json, "\"alpha0Deg\":-2.1"));
    TEST_ASSERT_TRUE(Contains(json, "\"degrees\":0"));
}

// ----------------------------------------------------------------------------
// Sentinel handling
// ----------------------------------------------------------------------------

void test_nonfinite_floats_sentinel_to_zero(void) {
    CalwizStateInputs in;
    in.acGrossWeightLb = 0;
    in.acBestGlideKt   = std::nanf("");
    in.acVfeKt         = std::numeric_limits<float>::infinity();
    in.acGLimit        = -std::numeric_limits<float>::infinity();

    OnSpeedConfig::SuFlaps f{};
    f.fAlpha0     = std::nanf("");
    f.fAlphaStall = std::numeric_limits<float>::infinity();
    in.flaps.push_back(f);

    auto json = SerializeCalwizState(in);

    // None of "nan" / "inf" / "-inf" appear in the document.
    TEST_ASSERT_FALSE(Contains(json, "nan"));
    TEST_ASSERT_FALSE(Contains(json, "NaN"));
    TEST_ASSERT_FALSE(Contains(json, "inf"));
    TEST_ASSERT_FALSE(Contains(json, "Infinity"));

    // The fields are emitted as 0.
    TEST_ASSERT_TRUE(Contains(json, "\"bestGlideKt\":0"));
    TEST_ASSERT_TRUE(Contains(json, "\"vfeKt\":0"));
    TEST_ASSERT_TRUE(Contains(json, "\"gLimit\":0"));
    TEST_ASSERT_TRUE(Contains(json, "\"alpha0Deg\":0"));
    TEST_ASSERT_TRUE(Contains(json, "\"alphaStallDeg\":0"));
}

void test_currentFlapIndex_out_of_range_clamps_to_zero(void) {
    auto in = MakeTwoFlapInputs();
    in.currentFlapIndex = 99;

    auto json = SerializeCalwizState(in);
    TEST_ASSERT_TRUE(Contains(json, "\"currentFlapIndex\":0"));
}

void test_currentFlapIndex_negative_clamps_to_zero(void) {
    auto in = MakeTwoFlapInputs();
    in.currentFlapIndex = -3;

    auto json = SerializeCalwizState(in);
    TEST_ASSERT_TRUE(Contains(json, "\"currentFlapIndex\":0"));
}

void test_empty_flaps_emits_empty_array(void) {
    CalwizStateInputs in;
    auto json = SerializeCalwizState(in);
    TEST_ASSERT_TRUE(Contains(json, "\"flaps\":[]"));
    TEST_ASSERT_TRUE(Contains(json, "\"currentFlapIndex\":0"));
}

// ----------------------------------------------------------------------------
// Document well-formedness (cheap brace-balance check)
// ----------------------------------------------------------------------------

void test_braces_balance(void) {
    auto in   = MakeTwoFlapInputs();
    auto json = SerializeCalwizState(in);

    int depth = 0;
    int bracketDepth = 0;
    for (char c : json) {
        if      (c == '{') ++depth;
        else if (c == '}') --depth;
        else if (c == '[') ++bracketDepth;
        else if (c == ']') --bracketDepth;
        TEST_ASSERT_TRUE_MESSAGE(depth >= 0,        "negative brace depth");
        TEST_ASSERT_TRUE_MESSAGE(bracketDepth >= 0, "negative bracket depth");
    }
    TEST_ASSERT_EQUAL_INT(0, depth);
    TEST_ASSERT_EQUAL_INT(0, bracketDepth);
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_top_level_keys_present);
    RUN_TEST(test_aircraft_block_keys_exact);
    RUN_TEST(test_per_flap_keys_exact);
    RUN_TEST(test_per_flap_values_present);

    RUN_TEST(test_nonfinite_floats_sentinel_to_zero);
    RUN_TEST(test_currentFlapIndex_out_of_range_clamps_to_zero);
    RUN_TEST(test_currentFlapIndex_negative_clamps_to_zero);
    RUN_TEST(test_empty_flaps_emits_empty_array);

    RUN_TEST(test_braces_balance);

    return UNITY_END();
}
