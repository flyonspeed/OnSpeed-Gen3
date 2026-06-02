// ahrs/SensorSnapshot.h — lock-free snapshot of derived sensor state.
//
// SensorIO::Read() runs on SensorReadTask (~50 Hz) and writes the derived
// air-data fields — IAS, AOA, pressure altitude, OAT, the IAS-alive gate, the
// smoothed pressures, and the deceleration rate. It takes xSensorMutex only
// around the SPI transaction; the field computations happen outside any lock.
//
// Consumers on other tasks and cores — the WebSocket live-data builder
// (Core 0), the M5 wire-format builder, the SD log writer, the /api/sample
// endpoints — read those fields. Before this publisher they read the raw
// g_Sensors members with no lock, so a reader could observe a torn frame (a
// new IAS against a stale AOA). This publisher carries a coherent copy so a
// consumer reads either the whole previous frame or the whole current frame.
//
// xSensorMutex is unchanged: it guards the shared SPI bus (IMU + pressure
// sensors + MCP3202), which is a separate concern from the coherence of the
// derived fields. This publisher is purely additive.

#pragma once

#include <cstdint>
#include <type_traits>

#include <util/SnapshotPublisher.h>

namespace onspeed::ahrs {

// POD snapshot of derived sensor state. Field units match the g_Sensors
// members each one mirrors (see SensorIO.h).
//
// When adding a field here, add the matching assignment in
// SensorIO::PublishSnapshot() and audit every consumer that reads g_Sensors.
struct SensorSnapshotPayload {
    float    iasKt        = 0.0f;   // IAS (knots)
    float    aoaDeg       = 0.0f;   // AOA (body angle, degrees)
    float    paltFt       = 0.0f;   // pressure altitude (feet, bias-corrected)
    float    oatC         = 0.0f;   // outside air temp (deg C)
    float    pStaticMbar  = 0.0f;   // static pressure (millibars)
    float    fDecelRate   = 0.0f;   // IAS deceleration rate (kt/s)
    float    pfwdSmoothed = 0.0f;   // smoothed forward (pitot) pressure
    float    p45Smoothed  = 0.0f;   // smoothed 45-deg (AOA) pressure
    int      iPfwd        = 0;      // raw forward pressure counts
    int      iP45         = 0;      // raw 45-deg pressure counts
    uint32_t uIasUpdateUs = 0;      // micros() of last IAS update
    bool     bIasAlive    = false;  // air-data validity (pitot above noise floor)

    // false until the first publish.
    bool     bValid       = false;
};

static_assert(std::is_trivially_copyable_v<SensorSnapshotPayload>,
              "SensorSnapshotPayload must be trivially copyable for "
              "SnapshotPublisher's memcpy-based seqcount.");

// Global publisher. Defined in SensorIO.cpp. Single writer: SensorIO::Read()
// on SensorReadTask (live), and LogReplay's per-row write-back on LogReplayTask
// (replay, mutually exclusive with the live sensor loop). Readers call
// g_SensorSnapshot.read() (tolerant) or .tryRead(out) (deadlined).
extern onspeed::util::SnapshotPublisher<SensorSnapshotPayload> g_SensorSnapshot;

}   // namespace onspeed::ahrs
