// SatCorrect.h — ram-rise correction from total air temperature (TAT)
// to static air temperature (SAT).
//
//   M_proxy = IAS_mps / a0,        a0 = 340.294 m/s (ISA sea-level)
//   SAT_K   = TAT_K / (1 + K * 0.2 * M_proxy²)
//   SAT_C   = SAT_K - 273.15
//
// K is the probe recovery factor:
//   K = 0.0   disables correction (returns TAT unchanged)
//   K = 0.75  bare/exposed thermistor (typical GA install, default)
//   K = 1.0   ideal TAT probe (Kiel/shielded)
//
// IAS-as-Mach proxy: at M < 0.4 (every aircraft OnSpeed targets),
// M_IAS is within 1% of M_TAS, producing SAT error <0.1°C.

#ifndef ONSPEED_CORE_SENSORS_SAT_CORRECT_H
#define ONSPEED_CORE_SENSORS_SAT_CORRECT_H

#include <optional>

namespace onspeed::sensors {

// Valid input bounds (matched to OatConvert::FilterOat).
inline constexpr float kSatCorrectMinTatC = -100.0f;
inline constexpr float kSatCorrectMaxTatC =  100.0f;
inline constexpr float kSatCorrectMinK    =   0.0f;
inline constexpr float kSatCorrectMaxK    =   1.0f;

// Returns the corrected SAT (°C), or nullopt if any input is non-finite,
// out of range, or the result would be physically nonsensical
// (SAT_K <= 0).
std::optional<float> CorrectSat(float tatCelsius,
                                float iasKt,
                                float recoveryFactorK);

}   // namespace onspeed::sensors

#endif  // ONSPEED_CORE_SENSORS_SAT_CORRECT_H
