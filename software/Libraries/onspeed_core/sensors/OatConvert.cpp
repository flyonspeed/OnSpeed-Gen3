// OatConvert.cpp — DS18B20 1-Wire raw reading -> validated Celsius

#include <sensors/OatConvert.h>

namespace onspeed::sensors {

std::optional<float> FilterOat(float rawCelsius)
{
    // Reject both known sentinel values explicitly before the range check.
    // The disconnect sentinel (-127) would pass the range check lower bound
    // (it equals kOatMinC - 47) and must be caught here.
    if (rawCelsius <= kOatDisconnectSentinel)
        return std::nullopt;

    // Reject the DS18B20 power-on-reset value. 85°C is above kOatMaxC so
    // the range check below would catch it, but calling it out explicitly
    // makes the intent clear in the audit trail.
    if (rawCelsius >= kOatPorSentinel)
        return std::nullopt;

    // Reject implausibly out-of-range readings. This also catches any
    // intermediate corrupted values not matching the known sentinels.
    if (rawCelsius < kOatMinC || rawCelsius > kOatMaxC)
        return std::nullopt;

    return rawCelsius;
}

}   // namespace onspeed::sensors
