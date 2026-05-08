// SensorBiasesJson.h
//
// Read-only JSON serializer for GET /api/sensors/biases.  Backs the
// Preact sensor-cal page's "Current sensor calibration" panel and the
// initial pitch / roll / PAlt defaults.  The body keeps the legacy
// HandleSensorConfig() panel shape (Pfwd / P45 / Static / gx-y-z /
// pitch / roll biases plus current IMU + AHRS pitch and roll) and adds
// EFIS-source metadata so the page can decide whether to seed the PAlt
// field from the EFIS or leave it blank.
//
// Pure host-runnable helper; no Arduino, no globals.  The handler in
// ApiHandlers.cpp snapshots inputs under the same mutexes the legacy
// /sensorconfig handler uses, then calls SerializeSensorBiases.

#ifndef ONSPEED_CORE_API_SENSOR_BIASES_JSON_H
#define ONSPEED_CORE_API_SENSOR_BIASES_JSON_H

#include <cstddef>
#include <string>

namespace onspeed::api {

// EFIS source classification mirroring the firmware's
// EfisSerialPort::EnEfisType.  Distinct VN-300 entry: VN-300 carries no
// baro on the wire so the page leaves the PAlt field blank even when
// the EFIS link is fresh.
enum class EfisBaroSource : int {
    None     = 0,  // bReadEfisData false, or stale/invalid frame
    Vn300    = 1,  // fresh frame but no baro field on the wire
    Baro     = 2,  // Dynon SkyView / D10, Garmin G5 / G3X, MGL — supplies PAlt
};

struct SensorBiasesInputs {
    // Bias values from g_Config.  iPFwdBias / iP45Bias are integer ADC
    // counts; the rest are floats.
    int   pFwdBiasCounts    = 0;
    int   p45BiasCounts     = 0;
    float pStaticBiasMb     = 0.0f;
    float gxBias            = 0.0f;
    float gyBias            = 0.0f;
    float gzBias            = 0.0f;
    float pitchBiasDeg      = 0.0f;
    float rollBiasDeg       = 0.0f;

    // Live readings — IMU pitch/roll from raw accel without bias, and
    // the AHRS-corrected (current installed bias applied) values that
    // the legacy panel calls "Calculated True AC Pitch/Roll".
    float imuPitchDeg       = 0.0f;
    float imuRollDeg        = 0.0f;
    float truePitchDeg      = 0.0f;
    float trueRollDeg       = 0.0f;

    // EFIS metadata.  `efisSource` covers the three cases the page
    // distinguishes (no EFIS, VN-300, baro-supplying EFIS).
    EfisBaroSource efisSource = EfisBaroSource::None;

    // EFIS-supplied pitch / roll / PAlt, valid only when
    // efisSource != None and the upstream timestamp is < 2 s old.
    // The handler is responsible for the freshness check; this struct
    // just carries the values through.
    float efisPitchDeg      = 0.0f;
    float efisRollDeg       = 0.0f;
    float efisPaltFt        = 0.0f;
};

// Serialize the snapshot to a compact JSON document.  Never throws;
// non-finite floats are emitted as 0.  Field-name shape:
//
//   {
//     "biases": {
//       "pFwdCounts":   <int>,
//       "p45Counts":    <int>,
//       "pStaticMb":    <float>,
//       "gxDegPerSec":  <float>,
//       "gyDegPerSec":  <float>,
//       "gzDegPerSec":  <float>,
//       "pitchDeg":     <float>,
//       "rollDeg":      <float>
//     },
//     "live": {
//       "imuPitchDeg":  <float>,
//       "imuRollDeg":   <float>,
//       "truePitchDeg": <float>,
//       "trueRollDeg":  <float>
//     },
//     "efis": {
//       "source":   "none" | "vn300" | "baro",
//       "pitchDeg": <float>,
//       "rollDeg":  <float>,
//       "paltFt":   <float>
//     }
//   }
//
// The page consumes `efis.source` to decide PAlt seeding ("baro" → seed
// from `efis.paltFt`, otherwise leave blank with placeholder).
std::string SerializeSensorBiases(const SensorBiasesInputs& in);

}  // namespace onspeed::api

#endif  // ONSPEED_CORE_API_SENSOR_BIASES_JSON_H
