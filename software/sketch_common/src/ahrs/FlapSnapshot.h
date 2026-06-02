// ahrs/FlapSnapshot.h — lock-free snapshot of flap state.
//
// Flap state — the configured flap vector, the active detent index/degrees,
// and the raw lever ADC — is published from these writer paths:
//   1. Flaps::Update()      auto-detect, SensorReadTask (~1 Hz)
//   2. Flaps::Update(int)   manual/test-pot override, SensorReadTask/TestPotTask
//   3. HandleConfigSave     flap-vector swap, WebServer task
//   4. HandleApiCalwizSave  per-flap edit, WebServer task
//   + LogReplay::PublishReplayResult in replay mode (mutually exclusive with
//     the live IMU loop, so it's the sole writer when it runs).
// EVERY publish is inside an xAhrsMutex hold, which is what serializes them
// into a single in-flight publish() and satisfies SnapshotPublisher's
// single-writer invariant. Adding a new writer WITHOUT taking xAhrsMutex
// would break that invariant (concurrent publish() is undefined behavior).
//
// Consumers — the audio tone path, the SensorIO AOA calc, the M5 wire-format
// builder, the WebSocket live-data builder, the calwiz API — read the active
// flap's calibration and (for the display/web widget) the whole vector's
// degree range. This publisher carries all of it as one coherent POD frame so
// a reader sees either the whole previous flap state or the whole current one,
// never a mix — without taking xAhrsMutex.

#pragma once

#include <cstdint>
#include <type_traits>

#include <util/SnapshotPublisher.h>
#include <util/OnSpeedTypes.h>        // MAX_AOA_CURVES
#include <config/OnSpeedConfig.h>     // FOSConfig::SuFlaps

namespace onspeed::ahrs {

// The flap-calibration record. Use the platform-independent core type
// (onspeed::config::OnSpeedConfig::SuFlaps) so this header stays native-
// testable; the sketch-side FOSConfig::SuFlaps is the same type via
// inheritance, so firmware callsites assign between them freely.
using FlapEntry = onspeed::config::OnSpeedConfig::SuFlaps;

// POD snapshot of the full flap state. Field units/semantics match the live
// globals each mirrors: aFlaps[] = g_Config.aFlaps, iIndex/iPosition =
// g_Flaps.iIndex/iPosition, uValue = g_Flaps.uValue.
//
// When adding a field, add the matching assignment in the publish helper
// (Flaps.cpp PublishFlapSnapshot_) and audit every consumer that reads it.
struct FlapSnapshotPayload {
    // The configured flap vector, sorted ascending by degrees (same order as
    // g_Config.aFlaps). Only [0, nFlaps) are valid.
    FlapEntry          aFlaps[onspeed::MAX_AOA_CURVES]{};
    uint8_t            nFlaps    = 0;

    // Active detent, as last written by Flaps::Update.
    int                iIndex    = 0;
    int                iPosition = -1;   // degrees; -1 = undetected

    // Raw flap-lever ADC reading.
    uint16_t           uValue    = 0;

    // false until the first publish, or when iIndex is out of [0, nFlaps).
    bool               bValid    = false;
};

static_assert(std::is_trivially_copyable_v<FlapSnapshotPayload>,
              "FlapSnapshotPayload must be trivially copyable for "
              "SnapshotPublisher's memcpy-based seqcount.");

// Global publisher. Defined in Flaps.cpp. Writers (all serialized by
// xAhrsMutex): Flaps::Update() and Flaps::Update(int) on SensorReadTask /
// override paths, and HandleConfigSave's flap-vector swap on the WebServer
// task. Readers call g_FlapSnapshot.read().
extern onspeed::util::SnapshotPublisher<FlapSnapshotPayload> g_FlapSnapshot;

}   // namespace onspeed::ahrs
