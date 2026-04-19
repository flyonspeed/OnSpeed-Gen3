// ConfigXmlEmit.h — serialise an OnSpeedConfig to an XML string.
//
// Extracted from FOSConfig::ConfigurationToString() (software/OnSpeed-Gen3-
// ESP32/src/config/Config.cpp) as part of PR 3.1 Task 2.  The sketch-side
// wrapper returns the same bytes; test_config_xml verifies round-trip
// equivalence against Sam's real N720AK config fixture.
//
// Platform-free: no Arduino String, no global state, only tinyxml2 +
// std::string.

#ifndef ONSPEED_CORE_CONFIG_XML_EMIT_H
#define ONSPEED_CORE_CONFIG_XML_EMIT_H

#include <string>

#include <config/OnSpeedConfig.h>

namespace onspeed::config {

// Emit `cfg` as an XML document string.  Always succeeds.  Tag order is
// deterministic so round-trip tests can compare parsed structs.  The
// returned string is suitable for writing to the SD card config file or
// returning from the web UI download endpoint.
//
// AUDIT #013 related: the flap loop iterates at most
// onspeed::MAX_AOA_CURVES entries even if cfg.aFlaps somehow contains
// more (defensive — ParseXml already truncates at load).
std::string EmitXml(const OnSpeedConfig& cfg);

}  // namespace onspeed::config

#endif  // ONSPEED_CORE_CONFIG_XML_EMIT_H
