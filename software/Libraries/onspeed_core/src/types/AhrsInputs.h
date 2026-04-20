// AhrsInputs.h
//
// Per-frame input bundle for onspeed::ahrs::Ahrs::Step. Produced by the
// sketch's IMU/sensor task each cycle (snapshot under sensor mutex) and
// consumed by the AHRS pipeline. All fields here are values that can change
// per-frame; rarely-changing config (pitch/roll bias, algorithm choice,
// sample rate) lives on Ahrs as constructor-time state.
//
// Notes
// -----
// * `imu` carries accelerations in g (NOT m/s²) and gyro rates in deg/s.
//   The AHRS pipeline does the unit conversions internally.
// * `sensors.iasKt` is in knots; `sensors.paltFt` is feet.
// * `iasUpdateTimestampUs` is the microsecond-resolution timestamp of the
//   last IAS update from the pressure sensor task. The AHRS uses the diff
//   between this and the previous frame's value to drive a variable-rate
//   EMA on TAS — the IAS only updates at ~50 Hz while AHRS runs at 208 Hz,
//   so this lets the TAS derivative track real measurement cadence.
// * `efisOatCelsius` and `efisOatValid` are populated by the sketch from
//   the EFIS port if EFIS is the calibration source and data is fresh.
//   When invalid, the AHRS falls back to `sensors.oatCelsius` (DS18B20).
// * `useEfisOat` tells the AHRS *whether* to look at the EFIS OAT at all.
//   Together with `useInternalOat` this captures the
//   `bCalSourceEfis && bReadEfisData && IsDataFresh` and `bOatSensor`
//   gates the legacy sketch code applied inline.

#ifndef ONSPEED_CORE_TYPES_AHRS_INPUTS_H
#define ONSPEED_CORE_TYPES_AHRS_INPUTS_H

#include <cstdint>

#include <types/ImuSample.h>
#include <types/SensorSample.h>

namespace onspeed {

struct AhrsInputs {
    // Per-frame raw IMU sample (accel in g, gyro in deg/s).
    ImuSample imu;

    // Per-frame sensor snapshot (IAS, Palt, OAT in engineering units).
    SensorSample sensors;

    // Microsecond-resolution timestamp of the last IAS pressure update.
    // The AHRS detects "new IAS" by comparing against its stored previous
    // value; equal => no IAS-rate work this frame.
    uint32_t iasUpdateTimestampUs = 0;

    // Whether to consider the EFIS-supplied OAT as a candidate. The
    // sketch sets this to (bCalSourceEfis && bReadEfisData &&
    // EfisSerial::IsDataFresh(2000)).
    bool useEfisOat = false;

    // EFIS-supplied OAT (Celsius). Only consulted when useEfisOat is true.
    // Considered valid when within (-100, 100) Celsius.
    float efisOatCelsius = 0.0f;

    // Whether to fall back to the internal DS18B20 sensor OAT
    // (`sensors.oatCelsius`) when the EFIS path is not used or invalid.
    // Sketch sets this to g_Config.bOatSensor.
    bool useInternalOat = false;
};

}   // namespace onspeed

#endif   // ONSPEED_CORE_TYPES_AHRS_INPUTS_H
