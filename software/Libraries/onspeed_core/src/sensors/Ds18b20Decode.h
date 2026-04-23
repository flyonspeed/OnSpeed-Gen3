// Ds18b20Decode.h — pure decoding of a DS18B20 scratchpad.
//
// Split out of the hardware-facing Ds18b20 driver so it is natively
// testable without a 1-Wire bus. Given the 9 bytes the sensor puts
// on the wire, this header returns:
//
//   * kDs18b20DisconnectedC (-127.0f) when the scratchpad is all zeros
//     (the sensor isn't there and the bus is floating low), OR the
//     MAXIM CRC-8 on bytes 0..7 does not match byte 8.
//   * Otherwise, the decoded temperature for a DS18B20 (or DS18B20-
//     compatible family) at any of the 9..12-bit resolutions: the
//     low-order bits are zero at lower resolutions and the same
//     int16_t(msb<<8 | lsb) * 0.0625 formula applies.
//
// Intentionally NOT supported here:
//
//   * DS18S20 / DS1820 — a different temperature register layout.
//   * MAX31850 thermocouple-to-digital converter — a different fault
//     encoding. Our hardware is a single plain DS18B20 for OAT.
//
// The disconnect sentinel matches DallasTemperature.h's
// DEVICE_DISCONNECTED_C so the existing FilterOat chain in
// onspeed::sensors (which already rejects -127 and the 85C POR) keeps
// working unchanged.

#pragma once

#include <cstdint>

namespace onspeed::sensors {

constexpr float kDs18b20DisconnectedC = -127.0f;

// Dallas/MAXIM CRC-8, reflected polynomial 0x8C, init 0.
// Bit-identical to OneWire::crc8 — verified against an independent
// implementation in test_ds18b20_decode.cpp.
inline uint8_t Ds18b20Crc8(const uint8_t* data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        uint8_t b = data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t mix = static_cast<uint8_t>(crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }
    return crc;
}

// Decode a 9-byte DS18B20 scratchpad.  Returns Celsius, or
// kDs18b20DisconnectedC on CRC failure or all-zero scratchpad.
inline float DecodeDs18b20Celsius(const uint8_t scratchpad[9])
{
    // Reject all-zero pad.  The MAXIM CRC of eight zero bytes is zero,
    // so a missing sensor pulling the bus low produces a pad that
    // passes CRC — the library explicitly guards this case.
    bool allZero = true;
    for (int i = 0; i < 9; ++i) {
        if (scratchpad[i] != 0) { allZero = false; break; }
    }
    if (allZero) return kDs18b20DisconnectedC;

    if (Ds18b20Crc8(scratchpad, 8) != scratchpad[8]) {
        return kDs18b20DisconnectedC;
    }

    const int16_t raw = static_cast<int16_t>(
        static_cast<uint16_t>(scratchpad[1]) << 8 |
        static_cast<uint16_t>(scratchpad[0]));
    return raw * 0.0625f;
}

} // namespace onspeed::sensors
