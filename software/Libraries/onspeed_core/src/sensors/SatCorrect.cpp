// SatCorrect.cpp — ram-rise correction implementation.

#include <sensors/SatCorrect.h>

#include <cmath>

#include <util/OnSpeedTypes.h>

namespace onspeed::sensors {

namespace {

// ISA sea-level speed of sound (m/s).
constexpr float kIsaSpeedOfSoundMps = 340.2941f;

// Celsius/Kelvin offset.
constexpr float kKelvinOffset = 273.15f;

// (γ − 1) / 2 for γ = 1.4 (air).
constexpr float kCompFactor = 0.2f;

}   // namespace

std::optional<float> CorrectSat(float tatCelsius,
                                float iasKt,
                                float recoveryFactorK)
{
    if (!std::isfinite(tatCelsius) || !std::isfinite(iasKt) ||
        !std::isfinite(recoveryFactorK)) {
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

    if (recoveryFactorK == 0.0f) {
        return tatCelsius;
    }

    const float iasMps  = onspeed::kts2mps(iasKt);
    const float mProxy  = iasMps / kIsaSpeedOfSoundMps;
    const float divisor = 1.0f + recoveryFactorK * kCompFactor * mProxy * mProxy;

    const float tatK = tatCelsius + kKelvinOffset;
    const float satK = tatK / divisor;

    if (satK <= 0.0f || !std::isfinite(satK)) {
        return std::nullopt;
    }

    return satK - kKelvinOffset;
}

}   // namespace onspeed::sensors
