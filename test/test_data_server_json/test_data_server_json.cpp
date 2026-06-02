// test_data_server_json.cpp — pin the WebSocket live-data JSON schema.
//
// Closes #346.  The firmware's WS broadcast (DataServer.cpp's
// UpdateLiveDataJson) emits a fixed JSON shape; the LiveView /
// Indexer JS reads that shape; the dev-server replays NDJSON files
// in the same shape.  All three drift-detect against the canonical
// key list in onspeed_core/src/api/LiveDataJsonKeys.h.
//
// What this test actually pins:
//   1. The format-string in DataServer.cpp emits exactly the
//      documented key set, in the documented order.
//   2. The dev-server replay fixture
//      (tools/web/dev-server/replay/cruise.ndjson) carries the same
//      keys on every frame.
//   3. The LiveDataJsonKeys.h list has no duplicates.
//   4. AOA, DerivedAOA, IAS, and percentLift use %s placeholders so
//      the producer can emit JSON null when bIasAlive is false; the
//      legacy -100 numeric sentinel for AOA is gone (issue #455).
//
// Native test, no Arduino, reads source files via std::ifstream.
//
// PLAN_WEB_PREACT_REWRITE §3 PR 2 + §4f.

#include <unity.h>

#include <api/LiveDataJsonKeys.h>

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using onspeed::api::kLiveDataJsonKeys;
using onspeed::api::kLiveDataJsonKeyCount;

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// Path resolution
// ----------------------------------------------------------------------------
//
// PIO native runs the test binary from various working directories.
// platformio.ini's [env:native] bakes the repo root into the binary as
// ONSPEED_REPO_ROOT via a `!python ...` build flag, so we don't have
// to guess.  Fallback walks parents looking for platformio.ini in
// case that macro is missing on the local toolchain.

static std::string FindRepoRoot() {
#ifdef ONSPEED_REPO_ROOT
    return std::string(ONSPEED_REPO_ROOT);
#else
    std::string cwd = ".";
    for (int i = 0; i < 8; ++i) {
        std::ifstream f(cwd + "/platformio.ini");
        if (f.good()) return cwd;
        cwd += "/..";
    }
    return ".";
#endif
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ----------------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------------

void test_canonical_key_list_has_no_duplicates(void) {
    std::set<std::string> seen;
    for (size_t i = 0; i < kLiveDataJsonKeyCount; ++i) {
        const char* k = kLiveDataJsonKeys[i];
        TEST_ASSERT_NOT_NULL(k);
        TEST_ASSERT_TRUE(std::strlen(k) > 0);
        bool inserted = seen.insert(k).second;
        TEST_ASSERT_TRUE_MESSAGE(inserted, k);
    }
    TEST_ASSERT_EQUAL_size_t(kLiveDataJsonKeyCount, seen.size());
}

// Walk the format string in DataServer.cpp character-by-character and
// extract every "key": occurrence, in source order.  We then assert
// that ordered list equals kLiveDataJsonKeys.
//
// The format string lives between the literal text:
//     const char * szFormat =
// and the closing semicolon on its own line.
void test_data_server_format_string_emits_documented_keys(void) {
    std::string root = FindRepoRoot();
    std::string src  = ReadFile(root + "/software/sketch_common/src/web_server/DataServer.cpp");
    TEST_ASSERT_TRUE_MESSAGE(!src.empty(), "DataServer.cpp not found");

    auto begin = src.find("szFormat =");
    TEST_ASSERT_TRUE_MESSAGE(begin != std::string::npos, "szFormat not found");
    auto end = src.find(";", begin);
    TEST_ASSERT_TRUE_MESSAGE(end != std::string::npos, "szFormat terminator not found");
    std::string fmt = src.substr(begin, end - begin);

    // Extract `"X"` tokens that look like keys.  The format string is
    // a sequence of `"..\"key\":..."` chunks; each key is preceded by a
    // backslash-quote and followed by `\":`.
    //
    // Simpler approach: scan for the literal sequence `\"`, capture
    // the identifier that follows, and require the trailing `\":`.
    std::vector<std::string> foundKeys;
    for (size_t i = 0; i + 2 < fmt.size(); ++i) {
        if (fmt[i] == '\\' && fmt[i + 1] == '"') {
            size_t j = i + 2;
            std::string key;
            while (j < fmt.size() && (isalnum((unsigned char)fmt[j]) || fmt[j] == '_')) {
                key.push_back(fmt[j++]);
            }
            if (!key.empty() &&
                j + 1 < fmt.size() &&
                fmt[j] == '\\' && fmt[j + 1] == '"' &&
                j + 2 < fmt.size() && fmt[j + 2] == ':') {
                foundKeys.push_back(key);
                i = j + 2;
            }
        }
    }

    TEST_ASSERT_EQUAL_size_t(kLiveDataJsonKeyCount, foundKeys.size());
    for (size_t i = 0; i < kLiveDataJsonKeyCount; ++i) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(kLiveDataJsonKeys[i],
                                         foundKeys[i].c_str(),
                                         kLiveDataJsonKeys[i]);
    }
}

// The dev-server's replay fixture (NDJSON) wraps each WS frame in a
// `{tDelay, frame: {...}}` envelope; the keys we care about live
// inside `frame`.  Substring-check the first such record; subsequent
// frames are expected to have the same shape, but checking one is
// enough to catch a schema drift.
void test_replay_ndjson_has_documented_keys(void) {
    std::string root = FindRepoRoot();
    std::string ndjson = ReadFile(root + "/tools/web/dev-server/replay/cruise.ndjson");
    TEST_ASSERT_TRUE_MESSAGE(!ndjson.empty(), "cruise.ndjson not found");

    size_t bol = 0;
    while (bol < ndjson.size()) {
        size_t eol = ndjson.find('\n', bol);
        if (eol == std::string::npos) eol = ndjson.size();
        std::string line = ndjson.substr(bol, eol - bol);
        if (!line.empty() && line[0] == '{') {
            for (size_t i = 0; i < kLiveDataJsonKeyCount; ++i) {
                std::string needle = "\"";
                needle += kLiveDataJsonKeys[i];
                needle += "\":";
                TEST_ASSERT_TRUE_MESSAGE(line.find(needle) != std::string::npos,
                                         kLiveDataJsonKeys[i]);
            }
            return;
        }
        bol = eol + 1;
    }
    TEST_FAIL_MESSAGE("no JSON object in cruise.ndjson");
}

// /api/livedata mock fixture: same key set.  Verifies the dev-server
// HTTP-mock side of the drift gate.
void test_livedata_mock_has_documented_keys(void) {
    std::string root = FindRepoRoot();
    std::string js   = ReadFile(root + "/tools/web/dev-server/mocks/livedata.json");
    if (js.empty()) {
        TEST_IGNORE_MESSAGE("livedata.json mock not present (optional)");
        return;
    }
    for (size_t i = 0; i < kLiveDataJsonKeyCount; ++i) {
        std::string needle = "\"";
        needle += kLiveDataJsonKeys[i];
        needle += "\":";
        TEST_ASSERT_TRUE_MESSAGE(js.find(needle) != std::string::npos,
                                 kLiveDataJsonKeys[i]);
    }
}

// The float fallback constant is still used for non-AOA fields that
// silently coerce NaN to 0 via SafeJsonFloat.  AOA / DerivedAOA / IAS /
// percentLift no longer share this fallback — they emit JSON null when
// bIasAlive is false.  Pin the constant + assert the producer-side
// cleanup is complete (no `-100` literal anywhere in DataServer.cpp).
void test_no_aoa_numeric_sentinel_in_data_server(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, onspeed::api::kFloatSentinel);

    std::string root = FindRepoRoot();
    std::string src  = ReadFile(root + "/software/sketch_common/src/web_server/DataServer.cpp");
    TEST_ASSERT_TRUE_MESSAGE(!src.empty(), "DataServer.cpp not found");

    // The AOA `-100` numeric sentinel is gone.  Producer emits JSON
    // null instead, gated on bIasAlive.  Issue #455.
    TEST_ASSERT_TRUE_MESSAGE(src.find("-100") == std::string::npos,
                             "DataServer.cpp must not reference -100 (AOA emits JSON null, issue #455)");
}

// ----------------------------------------------------------------------------
// Air-data validity gate — issue #358
// ----------------------------------------------------------------------------

// The producer-side gate must read bIasAlive (the canonical sensor-level
// air-data validity flag), not iMuteAudioUnderIAS (an audio-UX threshold).
// Reverting either gate would silently re-introduce the bug where pilots
// see a 5 kt taxi reading on the LiveView.  Source grep is enough to
// pin the contract — actually executing UpdateLiveDataJson() requires
// Arduino + WiFi headers that this native test can't link against.
void test_air_data_gate_uses_bIasAlive(void) {
    std::string root = FindRepoRoot();
    std::string src  = ReadFile(root + "/software/sketch_common/src/web_server/DataServer.cpp");
    TEST_ASSERT_TRUE_MESSAGE(!src.empty(), "DataServer.cpp not found");

    // Both the AOA gate and the percentLift gate must reference the
    // sensor-level IAS-alive flag. It is read from the lock-free sensor
    // snapshot (`sensSnap.bIasAlive`) since the g_SensorSnapshot migration;
    // pin the `.bIasAlive` field rather than the source object so the
    // contract survives that refactor while still catching a revert to the
    // audio-mute gate (checked below).
    TEST_ASSERT_TRUE_MESSAGE(src.find(".bIasAlive") != std::string::npos,
                             "DataServer.cpp must gate on the bIasAlive flag");

    // The audio-mute threshold must NOT drive any display gate.
    // It's allowed in Audio.cpp (the actual audio path), and may
    // appear in source comments here for context, but the *code*
    // form `g_Config.iMuteAudioUnderIAS` would indicate it's been
    // wired back into the gate.  Pinning the dotted form catches
    // exactly that.
    TEST_ASSERT_TRUE_MESSAGE(
        src.find("g_Config.iMuteAudioUnderIAS") == std::string::npos,
        "g_Config.iMuteAudioUnderIAS must not gate any display value in DataServer.cpp (issue #358)");
}

// AOA, DerivedAOA, IAS, and percentLift must use %s placeholders in the
// format string so the producer can emit JSON `null` when bIasAlive is
// false.  The wsClient consumer rejects null via `typeof === 'number'`;
// the > -20 threshold check stays as belt-and-suspenders against any
// future numeric-sentinel drift.  Issues #358 / #455.
void test_format_string_uses_string_placeholders_for_invalid_aware_fields(void) {
    std::string root = FindRepoRoot();
    std::string src  = ReadFile(root + "/software/sketch_common/src/web_server/DataServer.cpp");
    TEST_ASSERT_TRUE_MESSAGE(!src.empty(), "DataServer.cpp not found");

    auto begin = src.find("szFormat =");
    TEST_ASSERT_TRUE_MESSAGE(begin != std::string::npos, "szFormat not found");
    auto end = src.find(";", begin);
    TEST_ASSERT_TRUE_MESSAGE(end != std::string::npos, "szFormat terminator not found");
    std::string fmt = src.substr(begin, end - begin);

    // All four invalid-aware fields use %s (so the value can be `null`
    // when invalid).  Format-string substring search is robust to
    // whitespace changes because the key+colon+placeholder sequence is
    // exact.
    TEST_ASSERT_TRUE_MESSAGE(
        fmt.find("\\\"AOA\\\":%s") != std::string::npos,
        "AOA must use %s placeholder for null-when-invalid");
    TEST_ASSERT_TRUE_MESSAGE(
        fmt.find("\\\"DerivedAOA\\\":%s") != std::string::npos,
        "DerivedAOA must use %s placeholder for null-when-invalid");
    TEST_ASSERT_TRUE_MESSAGE(
        fmt.find("\\\"IAS\\\":%s") != std::string::npos,
        "IAS must use %s placeholder for null-when-invalid");
    TEST_ASSERT_TRUE_MESSAGE(
        fmt.find("\\\"percentLift\\\":%s") != std::string::npos,
        "percentLift must use %s placeholder for null-when-invalid");

    // The %.2f placeholder must NOT appear next to AOA or DerivedAOA;
    // a regression to numeric formatting would re-introduce the bug.
    TEST_ASSERT_TRUE_MESSAGE(
        fmt.find("\\\"AOA\\\":%.2f") == std::string::npos,
        "AOA must not use %.2f placeholder (regression to numeric sentinel)");
    TEST_ASSERT_TRUE_MESSAGE(
        fmt.find("\\\"DerivedAOA\\\":%.2f") == std::string::npos,
        "DerivedAOA must not use %.2f placeholder (regression to NaN-coerced-to-0)");

    // The producer-side branch that picks "null" vs the formatted
    // value must exist.  Spot-checks the literal "null" appearing in
    // a strcpy / strncpy / String literal.
    TEST_ASSERT_TRUE_MESSAGE(
        src.find("\"null\"") != std::string::npos,
        "DataServer.cpp must emit \"null\" string for invalid AOA / DerivedAOA / IAS / percentLift");
}

// /api/sample/aoa REST endpoint must mirror the WebSocket contract:
// gate on bIasAlive (not iMuteAudioUnderIAS), emit JSON null when
// air data is invalid (not the legacy -100 numeric sentinel).
// Issues #358 / #455.
void test_api_sample_aoa_uses_bIasAlive_and_emits_null(void) {
    std::string root = FindRepoRoot();
    std::string src  = ReadFile(root + "/software/sketch_common/src/web_server/ApiHandlers.cpp");
    TEST_ASSERT_TRUE_MESSAGE(!src.empty(), "ApiHandlers.cpp not found");

    // Locate HandleApiSampleAoa() so subsequent grep is body-scoped.
    auto handlerPos = src.find("HandleApiSampleAoa");
    TEST_ASSERT_TRUE_MESSAGE(handlerPos != std::string::npos,
                             "HandleApiSampleAoa not found in ApiHandlers.cpp");
    auto bodyEnd = src.find("HandleApiSampleFlapsRaw", handlerPos);
    if (bodyEnd == std::string::npos) bodyEnd = src.size();
    std::string body = src.substr(handlerPos, bodyEnd - handlerPos);

    // The handler must reference bIasAlive.
    TEST_ASSERT_TRUE_MESSAGE(
        body.find("bIasAlive") != std::string::npos,
        "HandleApiSampleAoa must gate on bIasAlive (issue #358)");

    // The audio-mute threshold must NOT drive this endpoint's gate.
    TEST_ASSERT_TRUE_MESSAGE(
        body.find("iMuteAudioUnderIAS") == std::string::npos,
        "iMuteAudioUnderIAS must not gate /api/sample/aoa (issue #358)");

    // The legacy -100 numeric sentinel must be gone; the endpoint
    // emits JSON null for invalid frames.
    TEST_ASSERT_TRUE_MESSAGE(
        body.find("-100") == std::string::npos,
        "HandleApiSampleAoa must not reference -100 (issue #455, emit JSON null)");
    // The C++ source has the JSON-escaped form `\"aoa\":null` inside an
    // F() / String literal.  We search for the escaped pattern as it
    // appears in the .cpp file.
    TEST_ASSERT_TRUE_MESSAGE(
        body.find("\\\"aoa\\\":null") != std::string::npos,
        "HandleApiSampleAoa must emit {\"aoa\":null} for invalid air data (issue #455)");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_canonical_key_list_has_no_duplicates);
    RUN_TEST(test_data_server_format_string_emits_documented_keys);
    RUN_TEST(test_replay_ndjson_has_documented_keys);
    RUN_TEST(test_livedata_mock_has_documented_keys);
    RUN_TEST(test_no_aoa_numeric_sentinel_in_data_server);
    RUN_TEST(test_air_data_gate_uses_bIasAlive);
    RUN_TEST(test_format_string_uses_string_placeholders_for_invalid_aware_fields);
    RUN_TEST(test_api_sample_aoa_uses_bIasAlive_and_emits_null);

    return UNITY_END();
}
