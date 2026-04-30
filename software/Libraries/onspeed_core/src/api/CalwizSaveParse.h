// CalwizSaveParse.h
//
// Pure JSON-body parsing for POST /api/calwiz/save.  Splits the
// HTTP-bound concerns (CfgServer.arg("plain") body retrieval, error
// response emission) from the parsing itself so the differential test
// can exercise the actual lexer the firmware runs.
//
// `ExtractCalwizSave` reads a compact JSON object body, locates each
// of the 13 required fields (12 floats + 1 int `flapsPos`), parses
// every value, and returns either a populated CalwizSaveInput +
// flapsPos or an error result naming the offending field.
//
// Lexer scope: the body comes from a known-good Preact client emitting
// `JSON.stringify(...)`, so the lexer accepts the JSON-number subset
// {sign, digits, optional fraction, optional exponent} and rejects
// anything else (NaN, Infinity, malformed numbers like 1.2.3).  Floats
// are parsed via std::strtof which surfaces overflow as ±HUGE_VALF —
// the parser rejects non-finite results so a `1e999` body does not
// reach the SuFlaps.

#ifndef ONSPEED_CORE_API_CALWIZ_SAVE_PARSE_H
#define ONSPEED_CORE_API_CALWIZ_SAVE_PARSE_H

#include <string>
#include <string_view>

#include "CalwizSave.h"

namespace onspeed::api {

struct CalwizSaveParseResult {
    bool        ok          = false;
    std::string errorField;     ///< field name on failure ("body" if no JSON)
    std::string errorMessage;   ///< short reason text on failure
    int         flapsPos    = 0;
    CalwizSaveInput input{};
};

/// Parse a JSON body of the shape:
///   {"flapsPos":15,"LDmaxSetpoint":4.5,...,"curve2":7.89}
/// Returns ok=true and populates flapsPos + input on success.
/// On failure, sets errorField to the offending key (or "body" if the
/// body was empty / no key was found at all) and errorMessage to a
/// short human reason.
CalwizSaveParseResult ExtractCalwizSave(std::string_view body);

// ------------------------------------------------------------------
// Low-level lexer helpers exposed for tests.  Production callers
// should use ExtractCalwizSave above — these are how the lexer
// behaves in isolation.
// ------------------------------------------------------------------

/// Find the start of the JSON value after `"<key>":` in `body`.
/// Returns the index of the first non-whitespace value byte, or -1
/// when the key (or the colon following it) is absent.  Tolerates
/// spaces and tabs around the colon.
int FindJsonValueStart(std::string_view body, std::string_view key);

/// Parse a JSON number starting at byteOffset.  On success, writes
/// the value to *out and returns true; on a malformed token (e.g.
/// `1.2.3`, no digits, NaN, Infinity, 1e999 → ±inf) returns false.
/// `out` is untouched on failure.
bool ParseJsonNumber(std::string_view body, int byteOffset, float* out);

/// Parse a JSON integer starting at byteOffset.  Strict integer form
/// only — no fraction, no exponent.  Returns false on out-of-range
/// or malformed input (`out` untouched on failure).
bool ParseJsonInt(std::string_view body, int byteOffset, int* out);

}  // namespace onspeed::api

#endif  // ONSPEED_CORE_API_CALWIZ_SAVE_PARSE_H
