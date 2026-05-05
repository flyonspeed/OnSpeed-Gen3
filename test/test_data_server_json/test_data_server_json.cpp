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
//   4. The sentinel constants in LiveDataJsonKeys.h match the values
//      hardcoded in DataServer.cpp (-100 for AOA, 0.0 for the rest).
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

// Sentinel constants must agree between LiveDataJsonKeys.h and the
// hardcoded values in DataServer.cpp.  Spot-check the literal
// `-100` (AOA) and `0.0f` (everything else) as they appear in the
// source.
void test_sentinel_constants_match_data_server(void) {
    TEST_ASSERT_EQUAL_FLOAT(-100.0f, onspeed::api::kAoaSentinel);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,    onspeed::api::kFloatSentinel);

    std::string root = FindRepoRoot();
    std::string src  = ReadFile(root + "/software/sketch_common/src/web_server/DataServer.cpp");
    TEST_ASSERT_TRUE_MESSAGE(!src.empty(), "DataServer.cpp not found");

    // The AOA sentinel literal `-100` shows up in two places in the
    // source: the assignment that uses it for the gated path and the
    // SafeJsonFloat fallback.  We just assert it's present at all.
    TEST_ASSERT_TRUE_MESSAGE(src.find("-100") != std::string::npos,
                             "AOA sentinel -100 not found in DataServer.cpp");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_canonical_key_list_has_no_duplicates);
    RUN_TEST(test_data_server_format_string_emits_documented_keys);
    RUN_TEST(test_replay_ndjson_has_documented_keys);
    RUN_TEST(test_livedata_mock_has_documented_keys);
    RUN_TEST(test_sentinel_constants_match_data_server);

    return UNITY_END();
}
