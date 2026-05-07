// Test pinning the legacy json-config import parser.
//
// The pre-subtree X-Plane plugin (flyonspeed/OnSpeed-XPlane on the
// `json-config` branch) wrote settings as a flat JSON object like:
//
//     {"Below LDMax":6.0,"Below OnSpeed":7.3,"OnSpeed Max":9.6,
//      "Above OnSpeed":12.5,"IAS Tone Enable":1.0}
//
// The new plugin's TryImportLegacyJson uses a hand-rolled parser
// (extractJsonFloat in aoa_audio.cpp) to pull those five keys.  This
// test pins the parser's behavior on the legacy format and a
// representative spread of malformed/edge-case inputs.
//
// The function under test is static inside aoa_audio.cpp, so this
// file copies it.  If the production function changes, the test's
// copy must be updated to match — keeping them in sync is the
// review burden, but the alternative (exposing extractJsonFloat in
// a header solely for testing) leaks an implementation detail of a
// one-shot import path.

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Mirrored from aoa_audio.cpp::extractJsonFloat.  Keep in sync.
static bool extractJsonFloat(const std::string& body,
                             const char* key,
                             float& out)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return false;
    k += needle.size();
    while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
    if (k >= body.size() || body[k] != ':') return false;
    ++k;
    while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
    size_t end = k;
    while (end < body.size()) {
        char c = body[end];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+'
            || c == 'e' || c == 'E') {
            ++end;
        } else {
            break;
        }
    }
    if (end == k) return false;
    const std::string numStr = body.substr(k, end - k);
    char* parseEnd = nullptr;
    const float v = std::strtof(numStr.c_str(), &parseEnd);
    if (parseEnd != numStr.c_str() + numStr.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool nearly(float a, float b, float tol = 0.001f)
{
    return std::fabs(a - b) <= tol;
}

}  // namespace

int main()
{
    // The canonical legacy format: a flat object, no whitespace.
    const std::string compact =
        "{\"Below LDMax\":6.0,\"Below OnSpeed\":7.3,"
        "\"OnSpeed Max\":9.6,\"Above OnSpeed\":12.5,"
        "\"IAS Tone Enable\":1.0}";

    float v = 0.0f;
    check(extractJsonFloat(compact, "Below LDMax",     v) && nearly(v, 6.0f),
          "compact: Below LDMax = 6.0");
    check(extractJsonFloat(compact, "Below OnSpeed",   v) && nearly(v, 7.3f),
          "compact: Below OnSpeed = 7.3");
    check(extractJsonFloat(compact, "OnSpeed Max",     v) && nearly(v, 9.6f),
          "compact: OnSpeed Max = 9.6");
    check(extractJsonFloat(compact, "Above OnSpeed",   v) && nearly(v, 12.5f),
          "compact: Above OnSpeed = 12.5");
    check(extractJsonFloat(compact, "IAS Tone Enable", v) && nearly(v, 1.0f),
          "compact: IAS Tone Enable = 1.0");

    // Whitespace-tolerant: pretty-printed form with newlines.
    const std::string pretty =
        "{\n"
        "  \"Below LDMax\"      :   6.5  ,\n"
        "  \"Below OnSpeed\"    :   8.0  ,\n"
        "  \"OnSpeed Max\"      :   10.2 ,\n"
        "  \"Above OnSpeed\"    :   13.0 ,\n"
        "  \"IAS Tone Enable\"  :   0.0\n"
        "}\n";
    check(extractJsonFloat(pretty, "Below LDMax",     v) && nearly(v, 6.5f),
          "pretty: Below LDMax with whitespace");
    check(extractJsonFloat(pretty, "OnSpeed Max",     v) && nearly(v, 10.2f),
          "pretty: OnSpeed Max with whitespace");
    check(extractJsonFloat(pretty, "IAS Tone Enable", v) && nearly(v, 0.0f),
          "pretty: IAS Tone Enable = 0 (false)");

    // Negative + scientific notation — unlikely in legacy but the
    // parser supports it.
    const std::string sci = "{\"k\":-1.5e2,\"m\":+3.14}";
    check(extractJsonFloat(sci, "k", v) && nearly(v, -150.0f),
          "scientific: -1.5e2");
    check(extractJsonFloat(sci, "m", v) && nearly(v, 3.14f),
          "scientific: +3.14");

    // Missing key returns false; out parameter unchanged.
    v = 42.0f;
    check(!extractJsonFloat(compact, "Nonexistent", v),
          "missing key returns false");
    check(nearly(v, 42.0f),
          "missing key leaves out parameter unchanged");

    // Empty string → false, no crash.
    check(!extractJsonFloat("", "anything", v),
          "empty input returns false");

    // Truncated value (key found, no value follows).
    check(!extractJsonFloat("{\"key\":", "key", v),
          "truncated after colon returns false");
    check(!extractJsonFloat("{\"key\"", "key", v),
          "truncated before colon returns false");

    // Garbage value where a number is expected.
    check(!extractJsonFloat("{\"key\":\"hello\"}", "key", v),
          "string value where number expected returns false");

    // Key as a substring of another key — must not match the wrong one.
    // ("LDMax" is a prefix of "LDMax2" in this fixture; the literal-
    //  match form requires the closing quote.)
    const std::string substring =
        "{\"LDMax2\":99.0,\"LDMax\":6.0}";
    check(extractJsonFloat(substring, "LDMax", v) && nearly(v, 6.0f),
          "literal match doesn't pick up substring-prefixed key");
    check(extractJsonFloat(substring, "LDMax2", v) && nearly(v, 99.0f),
          "longer key matches its own value");

    // The actual legacy plugin output, captured from its writeFile
    // implementation in flyonspeed/OnSpeed-XPlane@json-config:
    //   nlohmann::json::dump() with no indent → no spaces
    const std::string realLegacy =
        "{\"Below LDMax\":6.0,\"Below OnSpeed\":7.300000190734863,"
        "\"OnSpeed Max\":9.600000381469727,"
        "\"Above OnSpeed\":12.5,"
        "\"IAS Tone Enable\":1.0}";
    check(extractJsonFloat(realLegacy, "Below LDMax",     v) && nearly(v, 6.0f),
          "real-legacy: LDmax with float-precision artifact");
    check(extractJsonFloat(realLegacy, "Below OnSpeed",   v) && nearly(v, 7.3f, 0.0001f),
          "real-legacy: OnSpeedFast with float-precision artifact");
    check(extractJsonFloat(realLegacy, "OnSpeed Max",     v) && nearly(v, 9.6f, 0.0001f),
          "real-legacy: OnSpeedSlow with float-precision artifact");
    check(extractJsonFloat(realLegacy, "Above OnSpeed",   v) && nearly(v, 12.5f),
          "real-legacy: StallWarn");
    check(extractJsonFloat(realLegacy, "IAS Tone Enable", v) && nearly(v, 1.0f),
          "real-legacy: IAS Tone Enable");

    if (failures == 0) {
        std::printf("OK: legacy_json_parse (all checks pass)\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return EXIT_FAILURE;
}
