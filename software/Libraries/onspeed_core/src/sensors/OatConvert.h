// OatConvert.h — DS18B20 1-Wire raw reading -> validated Celsius
//
// The DS18B20 returns -127.0 when it fails to respond (disconnect, CRC
// error, or bus fault). This sentinel MUST NOT propagate downstream:
// AHRS uses OAT for TAS compensation, and -127°C would cause a
// catastrophic over-read of TAS that corrupts DerivedAOA (audit #006).
//
// The DS18B20 also returns 85.0°C on power-on reset before the first
// real conversion completes. That value is implausible in flight and
// is also rejected here.
//
// Valid range -80..+80°C covers general aviation operations from
// arctic surface ops (-50°C) to desert high-DA ops (+50°C) with margin.
// FL450 ISA temperature is -56.5°C, which falls within the lower guard.

#ifndef ONSPEED_CORE_SENSORS_OAT_CONVERT_H
#define ONSPEED_CORE_SENSORS_OAT_CONVERT_H

#include <optional>

namespace onspeed::sensors {

// DS18B20 disconnect sentinel. The sensor drives this value when the
// 1-Wire bus transaction fails to complete (open circuit, short, or
// firmware timeout). Value is -127.0°C.
inline constexpr float kOatDisconnectSentinel = -127.0f;

// DS18B20 power-on-reset value. The sensor returns this before the
// first user-initiated conversion completes after a hard power cycle.
inline constexpr float kOatPorSentinel = 85.0f;

// Valid operating range (°C). Values outside this window are rejected.
inline constexpr float kOatMinC = -80.0f;
inline constexpr float kOatMaxC =  80.0f;

// Validate a raw Celsius reading from the DS18B20.
//
// Returns std::nullopt if the reading is:
//   - The disconnect sentinel (-127.0°C), or
//   - The power-on-reset value (85.0°C), or
//   - Outside the plausible range [kOatMinC, kOatMaxC].
//
// Returns the reading unchanged if it is valid.
//
// Callers should hold the last-good value on nullopt and log the event
// for diagnostics. Core does not log — logging is a sketch concern.
std::optional<float> FilterOat(float rawCelsius);

}   // namespace onspeed::sensors

#endif  // ONSPEED_CORE_SENSORS_OAT_CONVERT_H
