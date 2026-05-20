// SatCorrect.cpp — ram-rise correction implementation.

#include <sensors/SatCorrect.h>

#include <cmath>

#include <util/OnSpeedTypes.h>

namespace onspeed::sensors {

namespace {

// γ · R for air (γ=1.4, R=287.058 J/(kg·K)).  a(T_K) = sqrt(γ·R·T_K).
constexpr float kGammaR = 1.4f * 287.058f;

// Celsius/Kelvin offset.
constexpr float kKelvinOffset = 273.15f;

// (γ − 1) / 2 for γ = 1.4 (air).
constexpr float kCompFactor = 0.2f;

// ISA sea-level static temperature (Kelvin).
constexpr float kIsaT0K = 288.15f;

// ISA troposphere lapse rate (K/ft).
constexpr float kIsaLapseKPerFt = 0.0019812f;

// Density-altitude formula constants (matched to Ahrs::updateTas_'s
// existing density math; do not retune independently).
constexpr float kDensityAltExp        = 0.2349690f;   // 1 - 1/5.2561
constexpr float kDivisorPerFt         = 6.8755856e-6f;
constexpr float kInverseSigmaExponent = 2.12794f;     // ≈ 5.2561 / 2

// Newton iterations to converge SAT ↔ Mach.  Empirically: one is
// sufficient to bring relative SAT error below 1e-5 across the
// subsonic envelope; we run two for safety against unusual inputs
// (very cold, very high altitude).  Each iteration is ~3 floating
// ops + 2 pow + 1 sqrt; total cost ~10 µs on ESP32-S3.
constexpr int kNewtonIterations = 2;

}   // namespace

std::optional<float> CorrectSat(float tatCelsius,
                                float iasKt,
                                float paltFt,
                                float recoveryFactorK)
{
    if (!std::isfinite(tatCelsius) || !std::isfinite(iasKt) ||
        !std::isfinite(paltFt) || !std::isfinite(recoveryFactorK)) {
        return std::nullopt;
    }
    if (tatCelsius < kSatCorrectMinTatC || tatCelsius > kSatCorrectMaxTatC) {
        return std::nullopt;
    }
    if (iasKt <= 0.0f) {
        return std::nullopt;
    }
    if (recoveryFactorK < kSatCorrectMinK ||
        recoveryFactorK > kSatCorrectMaxK) {
        return std::nullopt;
    }

    // K=0 short-circuit: correction is the TAT identity.  Skip iteration.
    if (recoveryFactorK == 0.0f) {
        return tatCelsius;
    }

    const float tatK    = tatCelsius + kKelvinOffset;
    const float iasMps  = onspeed::kts2mps(iasKt);
    const float isaAtPaltK = kIsaT0K - kIsaLapseKPerFt * paltFt;

    // Solving SAT_K = TAT_K / (1 + K · 0.2 · M²) where M = TAS/a(SAT)
    // and TAS = IAS / sqrt(σ).  σ itself depends on SAT through the
    // density-altitude formula, so we iterate.  Seed with TAT; one
    // iteration is empirically sufficient, but two keeps the inner
    // loop forgiving for extreme inputs.
    float satK = tatK;
    for (int i = 0; i < kNewtonIterations; ++i) {
        // Density altitude from Palt and current SAT estimate.  Mirrors
        // the formula in Ahrs::updateTas_ that consumes our output.
        const float ratio  = isaAtPaltK / satK;
        if (ratio <= 0.0f) {
            return std::nullopt;
        }
        const float densityAltFt = paltFt + (isaAtPaltK / kIsaLapseKPerFt)
                                   * (1.0f - std::pow(ratio, kDensityAltExp));
        const float divisor = 1.0f - kDivisorPerFt * densityAltFt;
        if (divisor <= 0.0f) {
            return std::nullopt;
        }
        const float tasMps = iasMps / std::pow(divisor, kInverseSigmaExponent);

        // Local speed of sound at SAT.  satK was just guarded > 0 via
        // the ratio check above (ratio = isaAtPaltK / satK requires
        // satK > 0 for ratio > 0 given isaAtPaltK > 0 in our envelope).
        const float aMps = std::sqrt(kGammaR * satK);
        const float mach = tasMps / aMps;

        const float divisorRamRise = 1.0f + recoveryFactorK * kCompFactor
                                     * mach * mach;
        const float satKNew = tatK / divisorRamRise;
        if (satKNew <= 0.0f || !std::isfinite(satKNew)) {
            return std::nullopt;
        }
        satK = satKNew;
    }

    return satK - kKelvinOffset;
}

}   // namespace onspeed::sensors
