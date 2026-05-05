// CalwizSaveParse.cpp — pure-JSON-body parser for POST /api/calwiz/save.
//
// Lexer rules:
//   - A JSON number is `[-+]? digits ('.' digits)? ([eE][-+]? digits)?`.
//     `digits` is one or more 0-9.  No leading-zero check; the wizard
//     never emits leading zeros, so the looser grammar is fine here.
//   - Reject `NaN`, `Infinity`, `-Infinity`: surface as parse failures.
//   - Reject `1.2.3` and other multi-decimal-point tokens.
//   - Reject `1e` with no exponent digits, `+e3`, etc.
//   - std::strtof returns ±HUGE_VALF on overflow; non-finite results
//     are rejected so the SuFlaps never sees Inf or NaN.
//   - See test_parse_json_number_lexer_known_limitations in
//     test_calwiz_save_diff.cpp for behaviors that diverge from strict
//     RFC-8259 JSON (trailing garbage, leading `+`, fractional-only
//     mantissa).  JSON.stringify never emits those forms.
//
// Whitespace tolerance: only spaces and tabs adjacent to the colon are
// skipped before parsing the value.  The wizard emits compact
// JSON.stringify output; supporting newlines/CR adds complexity for
// no production benefit.

#include "CalwizSaveParse.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace onspeed::api {

int FindJsonValueStart(std::string_view body, std::string_view key) {
    // Build the literal token `"<key>"` and search for it.
    std::string needle;
    needle.reserve(key.size() + 2);
    needle += '"';
    needle.append(key.data(), key.size());
    needle += '"';
    auto pos = body.find(needle);
    if (pos == std::string_view::npos) return -1;
    auto colon = body.find(':', pos + needle.size());
    if (colon == std::string_view::npos) return -1;
    int p = static_cast<int>(colon) + 1;
    while (p < static_cast<int>(body.size()) &&
           (body[p] == ' ' || body[p] == '\t'))
        ++p;
    return p;
}

bool ParseJsonNumber(std::string_view body, int byteOffset, float* out) {
    const int N = static_cast<int>(body.size());
    if (byteOffset < 0 || byteOffset >= N) return false;

    // Walk the candidate token character-by-character, enforcing the
    // JSON-number grammar: optional sign, mantissa digits with at most
    // one '.', optional exponent (single 'e'/'E' with optional sign and
    // one or more digits).
    int p = byteOffset;
    int i = p;

    // Optional mantissa sign.
    if (i < N && (body[i] == '+' || body[i] == '-')) ++i;

    // Mantissa: digits, optional '.', digits.  Reject zero-digit
    // mantissas and double-dot tokens.
    bool sawIntDigit  = false;
    bool sawFracDigit = false;
    while (i < N && body[i] >= '0' && body[i] <= '9') {
        sawIntDigit = true;
        ++i;
    }
    if (i < N && body[i] == '.') {
        ++i;
        while (i < N && body[i] >= '0' && body[i] <= '9') {
            sawFracDigit = true;
            ++i;
        }
        // A second '.' immediately follows? -> 1.2.3 case.
        if (i < N && body[i] == '.') return false;
    }
    if (!sawIntDigit && !sawFracDigit) return false;

    // Optional exponent: e/E, optional sign, one or more digits.
    if (i < N && (body[i] == 'e' || body[i] == 'E')) {
        ++i;
        if (i < N && (body[i] == '+' || body[i] == '-')) ++i;
        bool sawExpDigit = false;
        while (i < N && body[i] >= '0' && body[i] <= '9') {
            sawExpDigit = true;
            ++i;
        }
        if (!sawExpDigit) return false;
    }

    // After the number, the next byte must NOT be a continuation of
    // the lexical form (a stray '.', extra exponent, etc.) — otherwise
    // tokens like `1.2.3` would slip through.  Anything else (',',
    // '}', ']', whitespace, end-of-buffer) is fine.
    if (i < N) {
        char c = body[i];
        if (c == '.' || c == 'e' || c == 'E' ||
            (c >= '0' && c <= '9'))
            return false;
    }

    // Hand the captured slice to strtof for the actual conversion.
    // strtof respects the JSON-number subset above; we already
    // rejected NaN/Infinity by lex (they'd be alphabetic tokens, not
    // numeric).
    std::string token(body.substr(p, i - p));
    char* parseEnd = nullptr;
    errno = 0;
    float v = std::strtof(token.c_str(), &parseEnd);
    if (parseEnd != token.c_str() + token.size()) return false;
    if (!std::isfinite(v)) return false;  // 1e999 → ±HUGE_VALF rejected.
    *out = v;
    return true;
}

bool ParseJsonInt(std::string_view body, int byteOffset, int* out) {
    const int N = static_cast<int>(body.size());
    if (byteOffset < 0 || byteOffset >= N) return false;
    int i = byteOffset;
    if (i < N && (body[i] == '-' || body[i] == '+')) ++i;
    bool sawDigit = false;
    while (i < N && body[i] >= '0' && body[i] <= '9') {
        sawDigit = true;
        ++i;
    }
    if (!sawDigit) return false;
    // Reject fractional / scientific tokens — caller wants a strict int.
    if (i < N) {
        char c = body[i];
        if (c == '.' || c == 'e' || c == 'E') return false;
    }
    std::string token(body.substr(byteOffset, i - byteOffset));
    char* parseEnd = nullptr;
    errno = 0;
    long v = std::strtol(token.c_str(), &parseEnd, 10);
    if (parseEnd != token.c_str() + token.size()) return false;
    if (errno == ERANGE) return false;
    if (v < INT32_MIN || v > INT32_MAX) return false;
    *out = static_cast<int>(v);
    return true;
}

CalwizSaveParseResult ExtractCalwizSave(std::string_view body) {
    CalwizSaveParseResult out;
    if (body.empty()) {
        out.errorField   = "body";
        out.errorMessage = "JSON body required";
        return out;
    }

    {
        int p = FindJsonValueStart(body, "flapsPos");
        if (p < 0 || !ParseJsonInt(body, p, &out.flapsPos)) {
            out.errorField   = "flapsPos";
            out.errorMessage = "missing or non-numeric";
            return out;
        }
    }

    struct Field { const char* key; float* dest; };
    const Field fields[] = {
        { "LDmaxSetpoint",       &out.input.ldMaxAoaDeg       },
        { "OSFastSetpoint",      &out.input.onSpeedFastAoaDeg },
        { "OSSlowSetpoint",      &out.input.onSpeedSlowAoaDeg },
        { "StallWarnSetpoint",   &out.input.stallWarnAoaDeg   },
        { "StallSetpoint",       &out.input.stallAoaDeg       },
        { "ManeuveringSetpoint", &out.input.maneuveringAoaDeg },
        { "alpha0",              &out.input.alpha0Deg         },
        { "alphaStall",          &out.input.alphaStallDeg     },
        { "K_fit",               &out.input.kFit              },
        { "curve0",              &out.input.curve0            },
        { "curve1",              &out.input.curve1            },
        { "curve2",              &out.input.curve2            },
    };
    for (const Field& f : fields) {
        int p = FindJsonValueStart(body, f.key);
        if (p < 0 || !ParseJsonNumber(body, p, f.dest)) {
            out.errorField   = f.key;
            out.errorMessage = "missing or non-numeric";
            return out;
        }
    }
    out.ok = true;
    return out;
}

}  // namespace onspeed::api
