// ImuSample.h
//
// Snapshot of one IMU read: accelerometer (g), gyroscope (deg/s), temperature,
// and timestamp. Produced by the sketch's IMU330 driver at ~208 Hz; consumed
// by onspeed_core::ahrs::Ahrs and by sensor-log formatters.

#ifndef ONSPEED_CORE_TYPES_IMU_SAMPLE_H
#define ONSPEED_CORE_TYPES_IMU_SAMPLE_H

#include <cstdint>

namespace onspeed {

struct ImuSample {
    // Accelerometer, in aircraft body axes, units of g (9.81 m/s^2).
    // Installation orientation and bias correction applied upstream.
    float accelXG = 0.0f;   // forward  (nose direction)
    float accelYG = 0.0f;   // lateral  (right wing)
    float accelZG = 0.0f;   // vertical (down)

    // Gyroscope, in aircraft body axes, degrees per second.
    float gyroRollDps  = 0.0f;   // roll rate  (about X)
    float gyroPitchDps = 0.0f;   // pitch rate (about Y)
    float gyroYawDps   = 0.0f;   // yaw rate   (about Z)

    // IMU die temperature (Celsius). Useful for bias drift compensation.
    float tempCelsius = 0.0f;

    // Microsecond-resolution timestamp of the read, from the sketch's clock
    // source. Monotonic but wraps ~71 minutes — consumers must diff with
    // `uint32_t` arithmetic (unsigned wrap-around).
    uint32_t timestampUs = 0;
};

}   // namespace onspeed

#endif
