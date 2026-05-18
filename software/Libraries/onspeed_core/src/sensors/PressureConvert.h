// PressureConvert.h — raw HSC counts -> PSI -> IAS / Palt math
//
// Pure functions over raw sensor data. All atmospheric formulas are
// International Standard Atmosphere (ISA).
//
// Currently called by:
//   sketch: drivers/HscPressureSensor.cpp  (counts -> PSI)
//   sketch: drivers/SensorIO.cpp           (mbar  -> Palt, PSI -> IAS)

#ifndef ONSPEED_CORE_SENSORS_PRESSURE_CONVERT_H
#define ONSPEED_CORE_SENSORS_PRESSURE_CONVERT_H

#include <cstdint>
#include <optional>

namespace onspeed::sensors {

// Configured pressure range of a Honeywell HSC sensor.
// Passed to CountsToPsi alongside the raw counts.
struct HscRange {
    // Counts at the 10% saturation point (lower output limit).
    // Typical HSC: 10% of 16383 = 1638.
    uint16_t countsMin = 1638u;

    // Counts at the 90% saturation point (upper output limit).
    // Typical HSC: 90% of 16383 = 14745.
    uint16_t countsMax = 14745u;

    // Engineering-unit value at countsMin (PSI).
    float psiMin = 0.0f;

    // Engineering-unit value at countsMax (PSI).
    float psiMax = 0.0f;
};

// ============================================================================
// HSC transfer function
// ============================================================================

// Raw 14-bit HSC counts -> PSI using the linear HSC transfer function
// (linear counts-to-pressure transfer function):
//
//   PSI = (counts - countsMin) * (psiMax - psiMin) / (countsMax - countsMin) + psiMin
//
// Returns std::nullopt if counts are outside the sensor's valid output
// window (below countsMin or above countsMax), which indicates saturation
// or a disconnected sensor. Propagating a saturated value downstream
// would corrupt AOA and IAS — callers should hold the last good value.
std::optional<float> CountsToPsi(uint16_t counts, HscRange range);

// ============================================================================
// Pitot-static conversions
// ============================================================================

// Differential pitot pressure (PSI) -> indicated airspeed (knots).
//
// Uses the ASTM / ICAO compressible-flow definition of calibrated airspeed
// (CAS), which is the inversion of the isentropic subsonic pitot relation
// for a perfect gas with gamma = 1.4:
//
//   qc / P0 = (1 + 0.2 * (V / a0)^2)^(7/2) - 1
//
// Inverted:
//
//   V_mps = a0 * sqrt(5 * ((qc / P0 + 1)^(2/7) - 1))
//
// where:
//   qc  = impact (dynamic) pressure, Pa = dpPsi * 6894.757
//   P0  = 101325 Pa  (ISA sea-level static pressure)
//   a0  = 340.2941 m/s (ISA sea-level speed of sound)
//
// At low Mach this is numerically identical to the incompressible pitot
// equation sqrt(2 * qc / rho0); it diverges above ~M 0.3 (~200 KIAS at
// sea level), where the incompressible form reads roughly 1 kt high and
// grows with airspeed.
//
// The result is technically calibrated airspeed (CAS); the OnSpeed codebase
// conflates IAS and CAS consistently because no installed position-error
// correction is applied. If a position-error table is added later, it
// belongs downstream of this function.
//
// Returns 0.0 for negative or zero dp (sensor at rest or reversed flow).
float PitotPsiToIasKt(float dpPsi);

// Static pressure (millibars) -> pressure altitude (feet) via ISA atmosphere.
//
//   Palt = 145366.45 * (1 - (Pmbar / 1013.25)^0.190284)
//
// This is the standard ICAO ISA troposphere formula. The constant 145366.45
// is the scale height in feet (44330.8 m * 3.28084 ft/m = 145442 ft;
// the value 145366.45 is the numerically tuned constant used throughout
// aviation embedded firmware). Returns 0.0 for non-positive pressure input.
float StaticMbarToPaltFt(float staticMbar);

// Density altitude (feet) from pressure altitude and outside air temperature.
//
//   ISA_T_at_palt = 15.0 - 0.001981 * paltFt          (°C, troposphere lapse)
//   DA = paltFt + 120.0 * (oatCelsius - ISA_T_at_palt)
//
// The constant 120 ft/°C is the standard aviation approximation
// (exact value from ISA: 120.3 ft/°C at sea level, close enough for
// all practical altitudes in the operating envelope of a light aircraft).
//
// If OAT equals the ISA temperature at paltFt, this returns paltFt exactly.
float DensityAltitudeFt(float paltFt, float oatCelsius);

}   // namespace onspeed::sensors

#endif  // ONSPEED_CORE_SENSORS_PRESSURE_CONVERT_H
