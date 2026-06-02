// ahrs/ImuSnapshot.h — lock-free snapshot of raw IMU state.
//
// ImuReadTask (~208/416 Hz) reads the IMU over SPI (g_pIMU->Read()) and is the
// sole live writer of the raw accel/gyro fields. The AHRS already consumes IMU
// data coherently via g_pIMU->Snapshot() on that same task, but the SD log
// row is built from the raw fields on a DIFFERENT task at low log rates
// (LogSensor::Write runs on SensorReadTask when iLogRate < 208), so it can
// read an accel/gyro triplet that tears across an IMU update.
//
// This publisher carries the coherent IMU frame (the same onspeed::ImuSample
// that g_pIMU->Snapshot() produces) so the cross-task log reader — and the
// future LogProducerTask — read a whole frame from one IMU revision without a
// lock.

#pragma once

#include <util/SnapshotPublisher.h>
#include <types/ImuSample.h>

namespace onspeed::ahrs {

// Global publisher. Defined in SensorIO.cpp (alongside the other snapshot
// globals). Single writer: ImuReadTask (live) publishes g_pIMU->Snapshot()
// after each g_pIMU->Read(); LogReplay's per-row write-back publishes in
// replay mode (mutually exclusive with the live IMU loop). The payload is the
// platform-independent onspeed::ImuSample — no separate struct.
extern onspeed::util::SnapshotPublisher<onspeed::ImuSample> g_ImuSnapshot;

}   // namespace onspeed::ahrs
