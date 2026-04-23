// Ds18b20.cpp — minimal DS18B20 driver. See Ds18b20.h for the big
// picture.

#include "src/drivers/Ds18b20.h"

#include <Arduino.h>    // for delay()
#include <OneWire.h>
#include <sensors/Ds18b20Decode.h>

namespace {

// DS18B20 bus commands (datasheet table 2).
constexpr uint8_t kWriteScratchpad = 0x4E;
constexpr uint8_t kReadScratchpad  = 0xBE;
constexpr uint8_t kConvertT        = 0x44;

// Configuration register values for each supported resolution.
// (Datasheet figure 8: bits 7, 5..0 are fixed; bits 6..5 select
// resolution: 00=9-bit .. 11=12-bit.)
constexpr uint8_t kConfig9Bit  = 0x1F;
constexpr uint8_t kConfig10Bit = 0x3F;
constexpr uint8_t kConfig11Bit = 0x5F;
constexpr uint8_t kConfig12Bit = 0x7F;

// Default high/low alarm registers. Arbitrary — we don't use the
// alarm feature, so we just leave the Arduino library's defaults in
// place to stay byte-equivalent with prior behavior.
constexpr uint8_t kDefaultHighAlarm = 0x4B;   // +75 C
constexpr uint8_t kDefaultLowAlarm  = 0x46;   // +70 C

uint8_t ConfigByteFor(int bits)
{
    switch (bits) {
        case 9:  return kConfig9Bit;
        case 10: return kConfig10Bit;
        case 11: return kConfig11Bit;
        default: return kConfig12Bit;  // 12 is the spec maximum; clamp.
    }
}

} // namespace

bool Ds18b20::Begin(int bits)
{
    if (bus_.reset() == 0) return false;   // nobody home on the bus
    bus_.skip();                            // 0xCC = all devices
    bus_.write(kWriteScratchpad);
    bus_.write(kDefaultHighAlarm);
    bus_.write(kDefaultLowAlarm);
    bus_.write(ConfigByteFor(bits));
    // Per the datasheet the scratchpad write does not need a
    // trailing reset — the next bus transaction will issue one.
    return true;
}

bool Ds18b20::RequestConversion()
{
    if (bus_.reset() == 0) return false;
    bus_.skip();
    bus_.write(kConvertT);
    // Non-blocking: return immediately. Caller waits 750 ms for
    // 12-bit before reading.
    return true;
}

float Ds18b20::ReadCelsius()
{
    uint8_t pad[9] = {0};

    if (bus_.reset() == 0) {
        return onspeed::sensors::kDs18b20DisconnectedC;
    }
    bus_.skip();
    bus_.write(kReadScratchpad);
    for (int i = 0; i < 9; ++i) {
        pad[i] = bus_.read();
    }

    // DecodeDs18b20Celsius validates CRC and rejects an all-zero
    // scratchpad; both conditions return kDs18b20DisconnectedC.
    return onspeed::sensors::DecodeDs18b20Celsius(pad);
}

float Ds18b20::BlockingReadCelsius()
{
    if (!RequestConversion()) {
        return onspeed::sensors::kDs18b20DisconnectedC;
    }
    // 12-bit conversion takes up to 750 ms per the datasheet. We
    // wait the worst-case even at lower resolutions — this method
    // is only called at startup before the scheduler runs, so the
    // extra wait is harmless.
    delay(750);
    return ReadCelsius();
}
