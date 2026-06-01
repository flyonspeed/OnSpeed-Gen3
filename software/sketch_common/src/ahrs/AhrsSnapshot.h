// ahrs/AhrsSnapshot.h — lock-free snapshot of AHRS output state.
//
// AHRS::Process() runs on ImuReadTask (Core 1) at the IMU rate and writes
// the AHRS output fields (attitude, AOA, altitude/VSI, accel, gyro rates).
// Consumers on other tasks and cores — the WebSocket live-data builder
// (Core 0), the SD log writer, the M5 wire-format builder, the
// Housekeeping G-limit / VNO-chime decisions, the /api/sensorBiases
// endpoint — read those fields to render or log.
//
// This publisher carries a coherent copy of those fields so a consumer
// reads either the whole previous frame or the whole current frame, never
// a mix of the two. The producer is AHRS::PublishSnapshot() (see the
// single-writer-at-a-time contract at the g_AhrsSnapshot declaration
// below); readers call g_AhrsSnapshot.read() (tolerant) or .tryRead(out)
// (deadlined).
//
// The flap vector and per-flap setpoints are NOT in this payload — they
// have a separate ownership and lifetime (HandleConfigSave swaps the flap
// vector) and stay behind xAhrsMutex until their own migration. Consumers
// that need flap state continue to snapshot it under xAhrsMutex.

#pragma once

#include <type_traits>

#include <util/SnapshotPublisher.h>

namespace onspeed::ahrs {

// POD snapshot of AHRS output state. Field units match the legacy g_AHRS
// members each one mirrors (see AHRS.h), so consumers wrap them in the
// same m2ft() / mps2fpm() / mps2kts() conversions at the point of use.
//
// When adding a field here, add the matching assignment in
// AHRS::PublishSnapshot() and audit it against every consumer that reads
// g_AhrsSnapshot.
struct AhrsSnapshotPayload {
    // Attitude estimate (degrees).
    float pitchDeg          = 0.0f;   // SmoothedPitch
    float rollDeg           = 0.0f;   // SmoothedRoll

    // Flight-path angle and body-frame AOA (degrees).
    float flightPathDeg     = 0.0f;   // FlightPath
    float derivedAoaDeg     = 0.0f;   // DerivedAOA

    // Earth-frame vertical G.
    float earthVertG        = 1.0f;   // EarthVertG

    // True airspeed (m/s). Consumers wrap mps2kts at the point of use.
    float tasMps            = 0.0f;   // fTAS

    // Kalman altitude / VSI in legacy metric units (meters, m/s).
    float kalmanAltMeters   = 0.0f;   // KalmanAlt
    float kalmanVsiMps      = 0.0f;   // KalmanVSI

    // Wire-side EMA-smoothed accel components (G). Mirror of the legacy
    // AccelFwdFilter.get() / AccelLatFilter.get() / AccelVertFilter.get().
    float accelFwdFilteredG  = 0.0f;
    float accelLatFilteredG  = 0.0f;
    float accelVertFilteredG = 1.0f;

    // Installation-corrected (unsmoothed) accel components (G). Mirror of
    // AccelFwdCorr / AccelLatCorr / AccelVertCorr. The /api/sensorBiases
    // endpoint derives true pitch/roll from these via accelPitch/accelRoll.
    float accelFwdCorrG      = 0.0f;
    float accelLatCorrG      = 0.0f;
    float accelVertCorrG     = 1.0f;

    // Display-rate gyro running means (deg/s). Mirror of gPitch/gRoll/gYaw.
    float gPitchDps         = 0.0f;
    float gRollDps          = 0.0f;
    float gYawDps           = 0.0f;

    // Low-pass-filtered first derivative of vertical G (G/s). Mirror of
    // gOnsetRate — the M5 wire and the LiveView G-onset tape read this.
    float gOnsetRate        = 0.0f;
};

static_assert(std::is_trivially_copyable_v<AhrsSnapshotPayload>,
              "AhrsSnapshotPayload must be trivially copyable for "
              "SnapshotPublisher's memcpy-based seqcount.");

// Global publisher. Defined in AHRS.cpp. Single writer:
// AHRS::PublishSnapshot(), called at the end of AHRS::Process() (live,
// ImuReadTask) and after LogReplay's per-row write-back (replay,
// LogReplayTask). The two writers never run concurrently — replay mode
// replaces the live IMU loop — so the single-writer invariant holds.
extern onspeed::util::SnapshotPublisher<AhrsSnapshotPayload> g_AhrsSnapshot;

}   // namespace onspeed::ahrs
