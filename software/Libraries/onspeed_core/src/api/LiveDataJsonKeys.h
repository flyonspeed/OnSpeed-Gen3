// LiveDataJsonKeys.h
//
// Drift-detection contract for the WebSocket live-data JSON broadcast
// emitted by software/sketch_common/src/web_server/DataServer.cpp's
// UpdateLiveDataJson().  The keys here match the format string in that
// function exactly; the schema-pin native test asserts that the
// firmware's emitted payload, the dev-server mock fixture
// (tools/web/dev-server/mocks/livedata.json), and this list all carry
// the same set of names.
//
// Adding or removing a key requires updating all three.  The native
// test in test/test_data_server_json/ catches the mismatch.

#ifndef ONSPEED_CORE_API_LIVE_DATA_JSON_KEYS_H
#define ONSPEED_CORE_API_LIVE_DATA_JSON_KEYS_H

#include <cstddef>

namespace onspeed::api {

inline constexpr const char* kLiveDataJsonKeys[] = {
    "AOA",
    "Pitch",
    "Roll",
    "IAS",
    "PAlt",
    "verticalGLoad",
    "lateralGLoad",
    "flapsPos",
    "flapIndex",
    "flapsMinDeg",
    "flapsMaxDeg",
    "coeffP",
    "dataMark",
    "vsiFpm",
    "flightPath",
    "PitchRate",
    "DecelRate",
    "OAT",
    "DerivedAOA",
    "gOnsetRate",
    "percentLift",
    "tonesOnPctLift",
    "onSpeedFastPctLift",
    "onSpeedSlowPctLift",
    "stallWarnPctLift",
    "pipPctLift",
};

inline constexpr size_t kLiveDataJsonKeyCount =
    sizeof(kLiveDataJsonKeys) / sizeof(kLiveDataJsonKeys[0]);

// Sentinel emitted for the other float fields when the source is
// non-finite.  The audio path treats these as "no data"; the LiveView
// treats them as zero readings.  AOA / DerivedAOA / IAS / percentLift
// instead emit JSON `null` when air data is invalid (bIasAlive=false),
// so they don't share this fallback.
inline constexpr float kFloatSentinel = 0.0f;

}  // namespace onspeed::api

#endif  // ONSPEED_CORE_API_LIVE_DATA_JSON_KEYS_H
