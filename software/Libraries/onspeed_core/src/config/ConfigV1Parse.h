// ConfigV1Parse.h — parse a Gen2-era V1 (legacy) OnSpeed config blob.
//
// The V1 format is a flat <TAG>value</TAG> document (no nested elements)
// where multi-value fields like FLAPDEGREES, SETPOINT_LDMAXAOA, AOA_CURVE_FLAPS0
// store comma-separated values as the tag's text content.  Gen2 firmware
// emitted this format and pilots may still have Gen2-era SD cards that they
// want to migrate to Gen3 — keeping the V1 parser preserves that migration
// path and supports a possible future Gen2 firmware build that re-uses
// onspeed_core.
//
// Extracted from the sketch's Config.cpp as part of PR 3.1 Task 3.  The
// sketch-side FOSConfig::LoadConfigFromString delegates the <CONFIG>
// (V1) branch here; the V2 <CONFIG2> branch is handled by ConfigXmlParse.
//
// This module is platform-free: it depends only on the C++ standard library
// and onspeed_core types.  No Arduino String, no mutex, no side effects on
// global state.  Post-parse hardware actions (g_AudioPlay.SetVolume,
// g_EfisSerial.enType, g_AHRS.Init, ...) remain sketch-side.
//
// Platform-freeness is enforced by ./scripts/check_core_purity.sh.

#ifndef ONSPEED_CORE_CONFIG_V1_PARSE_H
#define ONSPEED_CORE_CONFIG_V1_PARSE_H

#include <string>
#include <string_view>

#include <config/OnSpeedConfig.h>

namespace onspeed::config {

// Result status of ParseV1.  On any non-Ok status the out config is left in
// a best-effort partial state; callers that want defaults on error should
// call LoadDefaults() first then ParseV1().
enum class V1ParseStatus {
    Ok = 0,
    Empty,         ///< Input was empty or whitespace-only
    MissingRoot,   ///< No <CONFIG> root tag (V2 <CONFIG2> is rejected here)
};

// Return a short human-readable string for a given status.
const char* V1ParseStatusToString(V1ParseStatus status);

// Detect whether `configText` looks like a V1 (Gen2-era) config blob.
// Returns true iff the text contains both `<CONFIG>` and `</CONFIG>` and
// does NOT contain `<CONFIG2>`.  Useful for the sketch's
// LoadConfigFromString to route to the right parser.
bool IsV1Format(std::string_view configText);

// Parse a V1 config document into `outConfig`.  Missing tags leave the
// corresponding field at whatever value `outConfig` had on entry — call
// LoadDefaults() first if you want defaults-for-missing behaviour.
//
// AUDIT #009: PStaticBias is *negated* on load.  Gen2 V1 configs stored
// the bias for use as `Pstatic + bias` (addition); Gen3 uses
// `Pstatic - bias` (subtraction).  Negating on load normalises the V1
// value to the Gen3/V2 in-memory convention so the same physical bias
// produces the same sensor behaviour regardless of which config format
// the user loaded from.  test_config_v1's
// `test_v1_v2_pstatic_normalize_to_same_memory` pins this — V1 input
// `+X` and V2 input `-X` must yield the same in-memory `fPStaticBias`.
V1ParseStatus ParseV1(std::string_view configText, OnSpeedConfig& outConfig);

}  // namespace onspeed::config

#endif  // ONSPEED_CORE_CONFIG_V1_PARSE_H
