#pragma once

#include <cstdint>

#include <onewire_bus.h>
#include <ds18b20.h>

// Minimal sketch-side driver for a single externally-powered DS18B20
// on its own 1-Wire bus, backed by the ESP-IDF RMT onewire_bus +
// ds18b20 managed components. The RMT peripheral times every 1-Wire
// slot in hardware, so the transaction is immune to scheduler
// preemption — unlike the prior bit-bang driver, whose inter-bit
// delays ran at task priority and let the equal-priority IMU task
// stretch slots until clone sensors faulted.
//
// Single-device assumption: Begin() searches the bus and binds to the
// first DS18B20 found. A second device on the bus is ignored.
//
// External-power assumption: the OAT sensor has its own VDD; the
// Convert-T kick does not assert a strong pullup for the conversion
// window. Parasite power would require additional bus-hold handling.
//
// The pure scratchpad-to-Celsius decode and CRC validation still live
// in onspeed_core/src/sensors/Ds18b20Decode.h (natively tested) but
// are no longer on the device read path — the ds18b20 component owns
// the on-wire CRC. Any component error maps to
// onspeed::sensors::kDs18b20DisconnectedC (-127.0f), preserving the
// downstream FilterOat / hold-last-good behavior byte-for-byte.

class Ds18b20
{
public:
    explicit Ds18b20(int gpioPin) : pin_(gpioPin) {}

    // Install the RMT 1-Wire bus, search for the DS18B20, and program
    // the requested bit-resolution (9..12). Returns true when a
    // DS18B20 was found and configured; false on any failure.
    bool Begin(int bits);

    // Start a temperature conversion. Non-blocking — returns as soon
    // as the convert command is issued. Returns true on success.
    bool RequestConversion();

    // Read the most-recent conversion result. Returns Celsius on
    // success, or onspeed::sensors::kDs18b20DisconnectedC (-127.0f)
    // on any component error (timeout, CRC, not initialized).
    float ReadCelsius();

    // One-shot synchronous read: RequestConversion, delay 750 ms for
    // 12-bit, ReadCelsius. Only safe before the FreeRTOS scheduler
    // starts or when blocking 750 ms is acceptable (startup priming).
    float BlockingReadCelsius();

private:
    int                      pin_;
    onewire_bus_handle_t     bus_ = nullptr;
    ds18b20_device_handle_t  dev_ = nullptr;
};
