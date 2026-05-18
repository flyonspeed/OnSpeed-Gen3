// AhrsOutputs.h
//
// Attitude and derived-AOA outputs from the AHRS pipeline. Produced by
// Ahrs::Step in PR 3.2; consumed by ToneCalc, DisplaySerial, LogSensor, and
// the web LiveView. Published via a mutex-protected snapshot holder so
// consumers read a consistent frame.

#ifndef ONSPEED_CORE_TYPES_AHRS_OUTPUTS_H
#define ONSPEED_CORE_TYPES_AHRS_OUTPUTS_H

#include <cstdint>

namespace onspeed {

struct AhrsOutputs {
    // Smoothed attitude from the Madgwick or EKFQ filter (degrees).
    float pitchDeg = 0.0f;
    float rollDeg  = 0.0f;

    // Derived quantities (degrees).
    float flightPathDeg = 0.0f;   // pitch minus inertial AOA rise
    float derivedAoaDeg = 0.0f;   // pitch minus flight-path; primary AOA signal

    // True airspeed and its smoothed first derivative.
    float tasMps     = 0.0f;   // true airspeed (meters/second)
    float tasDotMps2 = 0.0f;   // smoothed TAS derivative (m/s²); used for
                                // forward-acceleration compensation in AHRS.
                                // Computed as d(TAS_mps)/dt in AHRS::Step,
                                // then converted to g via mps2g() for the
                                // accelerometer correction term.

    // Kalman-filtered altitude and vertical speed.
    float kalmanAltFt  = 0.0f;   // altitude (feet)
    float kalmanVsiFpm = 0.0f;   // vertical speed (feet/minute)

    // Earth-frame vertical G (g).
    float earthVertG = 0.0f;

    // Running-averaged gyro rates used for display (degrees/second).
    float gyroRollFiltDps  = 0.0f;
    float gyroPitchFiltDps = 0.0f;
    float gyroYawFiltDps   = 0.0f;

    // Microsecond-resolution timestamp of the AHRS step.
    // Wraps ~71 minutes; consumers must diff with uint32_t arithmetic.
    uint32_t timestampUs = 0;
};

}   // namespace onspeed

#endif
