// SatCorrect.h — ram-rise correction from total air temperature (TAT)
// to static air temperature (SAT).
//
//   M       = TAS / sqrt(γRT_static)
//   SAT_K   = TAT_K / (1 + K * 0.2 * M²)
//   SAT_C   = SAT_K - 273.15
//
// K is the probe recovery factor:
//   K = 0.0   disables correction (returns TAT unchanged)
//   K = 0.75  bare/exposed thermistor (typical GA install, default)
//   K = 1.0   ideal TAT probe (Kiel/shielded)
//
// Mach calculation: TAS = IAS / sqrt(σ) where σ = (P/P0)·(T0/T) is
// the density ratio at altitude.  The local speed of sound is
// a(T) = sqrt(γRT_static).  Both depend on SAT (the thing we're
// solving for), so the helper Newton-iterates: seed SAT with TAT, two
// fixed-point iterations bring relative SAT error below 1e-4 across
// the entire subsonic envelope (M < 0.8).

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
// (SAT_K <= 0, σ <= 0, etc.).
//
// paltFt is the pressure altitude in feet (the same value that feeds
// the density-altitude / TAS calculation downstream).  It is required
// to recover the local σ and a(T) needed for accurate Mach above
// ~5000 ft.  Pass paltFt = 0 to get a sea-level approximation
// (equivalent to the IAS-as-Mach proxy at sea level).
std::optional<float> CorrectSat(float tatCelsius,
                                float iasKt,
                                float paltFt,
                                float recoveryFactorK);

}   // namespace onspeed::sensors

#endif  // ONSPEED_CORE_SENSORS_SAT_CORRECT_H
