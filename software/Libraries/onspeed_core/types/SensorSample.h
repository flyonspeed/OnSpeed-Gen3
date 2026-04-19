// SensorSample.h
//
// Snapshot of one pressure-sensor read, converted to engineering units.
// Produced by PressureConvert in PR 1.2; consumed by AHRS, display serial,
// log CSV.

#ifndef ONSPEED_CORE_TYPES_SENSOR_SAMPLE_H
#define ONSPEED_CORE_TYPES_SENSOR_SAMPLE_H

#include <cstdint>

namespace onspeed {

// Sentinel for OAT when the sensor is absent or has returned an error.
static constexpr float kOatInvalid = -127.0f;

struct SensorSample {
    // Indicated airspeed (knots), derived from pitot-static differential.
    float iasKt = 0.0f;

    // Pressure altitude (feet), derived from static pressure.
    float paltFt = 0.0f;

    // Outside air temperature (Celsius). Set to kOatInvalid when sensor
    // is absent, not yet read, or reported an error.
    float oatCelsius = kOatInvalid;

    // Raw pressures from individual sensors (PSI).
    float psPsi  = 0.0f;   // static pressure
    float ptPsi  = 0.0f;   // pitot (total) pressure, not IAS-corrected
    float p45Psi = 0.0f;   // differential AOA pressure, signed

    // Density altitude (feet). Set to 0 if OAT is invalid.
    float densityAltitudeFt = 0.0f;

    // Microsecond-resolution timestamp of the read.
    // Wraps ~71 minutes; consumers must diff with uint32_t arithmetic.
    uint32_t timestampUs = 0;
};

}   // namespace onspeed

#endif
