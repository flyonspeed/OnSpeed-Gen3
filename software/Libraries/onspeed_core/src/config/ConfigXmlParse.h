// ConfigXmlParse.h — parse an OnSpeed V2 config XML document into OnSpeedConfig.
//
// Extracted from the sketch's Config.cpp as part of PR 3.1 Task 2. The
// sketch-side FOSConfig::LoadConfigFromString delegates the CONFIG2 branch
// here; the legacy V1 CONFIG branch stays behind a separate shim (Task 3
// moves it too).
//
// This module is platform-free: it depends only on <tinyxml2.h>, <string>,
// <string_view>, and the onspeed_core types.  No Arduino String, no mutex,
// no side effects on global state.  All post-parse hardware actions
// (g_AudioPlay.SetVolume, g_EfisSerial.enType, g_AHRS.Init, ...) remain
// sketch-side.
//
// Platform-freeness: enforced by ./scripts/check_core_purity.sh.

#ifndef ONSPEED_CORE_CONFIG_XML_PARSE_H
#define ONSPEED_CORE_CONFIG_XML_PARSE_H

#include <string>
#include <string_view>

#include <config/OnSpeedConfig.h>

namespace onspeed::config {

// Result status of ParseXml.  On any non-Ok status the out config is left
// in a best-effort partial state: fields whose tags were parsed before the
// error are updated, fields whose tags were not reached stay at their
// prior value.  Callers that want defaults on error should call
// LoadDefaults() first, then ParseXml().
enum class XmlParseStatus {
    Ok = 0,
    MissingRoot,       ///< No <CONFIG2> root element (V1 <CONFIG> is rejected here)
    Malformed,         ///< tinyxml2 returned a non-XML_SUCCESS parse error
    TooManyFlaps,      ///< FLAP_POSITION count exceeds onspeed::MAX_AOA_CURVES (audit #013)
};

// Return a short human-readable string for a given status.
const char* XmlParseStatusToString(XmlParseStatus status);

// Parse the given XML document into `outConfig`.  On Ok the returned config
// reflects the document contents; missing optional fields keep whatever
// value `outConfig` had on entry (i.e. call LoadDefaults() first if you
// want defaults-for-missing behaviour).
//
// AUDIT #013: if the document contains more than onspeed::MAX_AOA_CURVES
// `<FLAP_POSITION>` entries the parser truncates to MAX_AOA_CURVES and
// returns TooManyFlaps.  This bounds the downstream flap-emit loop so a
// malformed upload cannot trigger unbounded XML output.
//
// AUDIT #009: the V2 <BIAS>/<PSTATIC> tag is ported verbatim — the value
// in the XML is assigned directly to fPStaticBias.  (The V1 CSV parser
// negates on load because its legacy convention was the opposite sign.)
// test_config_xml pins this V2 convention with a fixture check.
XmlParseStatus ParseXml(std::string_view xml, OnSpeedConfig& outConfig);

}  // namespace onspeed::config

#endif  // ONSPEED_CORE_CONFIG_XML_PARSE_H
