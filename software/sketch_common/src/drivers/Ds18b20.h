// Ds18b20.h — minimal sketch-side driver for a single DS18B20 on a
// shared OneWire bus.
//
// Replaces the 240K Arduino-Temperature-Control-Library for the one
// thing OnSpeed actually needs: one OAT sensor, no ROM addressing
// (skip-ROM is fine on a single-device bus), 12-bit resolution,
// non-blocking conversion.
//
// The pure scratchpad-to-Celsius decode lives in
// onspeed_core/src/sensors/Ds18b20Decode.h and is natively tested.
// This class only owns the on-the-wire protocol:
//
//   * Begin(bits)       — program the resolution configuration byte.
//   * RequestConversion — non-blocking kick of a temperature measurement.
//   * ReadCelsius       — read the 9-byte scratchpad and decode.
//
// Single-sensor assumption: every command uses skip-ROM (0xCC). A bus
// with two DS18B20s would garble — add ROM addressing if you ever
// need that (OnSpeed Gen3 has exactly one).
//
// External-power assumption: the OAT sensor is wired with an external
// VDD supply, not parasite power. The Convert-T kick in
// RequestConversion() does NOT assert a strong pullup during the
// 750 ms conversion window, which a parasite-powered sensor would
// require. If a future hardware variant ever switches to parasite
// power, RequestConversion must pass `power=1` on the Convert-T
// write and drive the bus high for the conversion window.

#pragma once

#include <cstdint>

class OneWire;

class Ds18b20
{
public:
    explicit Ds18b20(OneWire& bus) : bus_(bus) {}

    // Program the scratchpad's configuration byte for the requested
    // bit-resolution (9..12). Returns true when the bus reset
    // acknowledged (a device was present); false when the bus is open.
    bool Begin(int bits);

    // Start a temperature conversion. Non-blocking — returns as soon
    // as the convert-T command has been written. The caller waits
    // ~750 ms (12-bit) before calling ReadCelsius().
    // Returns true when the bus reset acknowledged.
    bool RequestConversion();

    // Read the scratchpad and decode. Returns Celsius on success,
    // or onspeed::sensors::kDs18b20DisconnectedC (-127.0f) when the
    // scratchpad is all-zero or fails CRC.
    float ReadCelsius();

    // One-shot synchronous read: RequestConversion, delay 750ms for
    // 12-bit, ReadCelsius. Only safe to call before the FreeRTOS
    // scheduler starts or when blocking the caller for 750ms is
    // acceptable (e.g. startup priming). Async operation uses
    // RequestConversion + timed ReadCelsius instead.
    float BlockingReadCelsius();

private:
    OneWire& bus_;
};
