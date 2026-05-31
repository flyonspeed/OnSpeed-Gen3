// Ds18b20.cpp — RMT-backed DS18B20 driver. See Ds18b20.h for the
// big picture.

#include "src/drivers/Ds18b20.h"

#include <Arduino.h>    // for delay()
#include <esp_err.h>
#include <onewire_bus.h>
#include <ds18b20.h>

#include <sensors/Ds18b20Decode.h>   // kDs18b20DisconnectedC

namespace {

// Map a requested bit count to the component's resolution enum.
ds18b20_resolution_t ResolutionFor(int bits)
{
    switch (bits) {
        case 9:  return DS18B20_RESOLUTION_9B;
        case 10: return DS18B20_RESOLUTION_10B;
        case 11: return DS18B20_RESOLUTION_11B;
        default: return DS18B20_RESOLUTION_12B;   // 12 is the max; clamp.
    }
}

} // namespace

bool Ds18b20::Begin(int bits)
{
    // Install the RMT 1-Wire bus on our pin. The OAT sensor has an
    // external 4.7k pullup, but enabling the internal pullup too is
    // harmless and adds margin.
    onewire_bus_config_t busCfg = {};
    busCfg.bus_gpio_num = pin_;
    busCfg.flags.en_pull_up = true;

    onewire_bus_rmt_config_t rmtCfg = {};
    rmtCfg.max_rx_bytes = 10;   // 1 ROM cmd + 8 ROM bytes + 1 device cmd

    if (onewire_new_bus_rmt(&busCfg, &rmtCfg, &bus_) != ESP_OK) {
        bus_ = nullptr;
        return false;
    }

    // Search the bus and bind to the first DS18B20 we find.
    onewire_device_iter_handle_t iter = nullptr;
    if (onewire_new_device_iter(bus_, &iter) != ESP_OK) {
        onewire_bus_del(bus_);
        bus_ = nullptr;
        return false;
    }

    bool bFound = false;
    onewire_device_t dev;
    while (onewire_device_iter_get_next(iter, &dev) == ESP_OK) {
        ds18b20_config_t dsCfg = {};
        if (ds18b20_new_device(&dev, &dsCfg, &dev_) == ESP_OK) {
            bFound = true;
            break;     // single-device bus: take the first DS18B20.
        }
    }
    onewire_del_device_iter(iter);

    if (!bFound) {
        onewire_bus_del(bus_);
        bus_ = nullptr;
        dev_ = nullptr;
        return false;
    }

    // Leave bus_/dev_ valid only on full success. On a resolution-write
    // failure, tear both down and null them so the dev_ guard in
    // ReadCelsius/RequestConversion correctly reports "not initialized"
    // rather than reading from a half-configured device.
    if (ds18b20_set_resolution(dev_, ResolutionFor(bits)) != ESP_OK) {
        ds18b20_del_device(dev_);
        onewire_bus_del(bus_);
        dev_ = nullptr;
        bus_ = nullptr;
        return false;
    }

    return true;
}

bool Ds18b20::RequestConversion()
{
    if (dev_ == nullptr) return false;
    return ds18b20_trigger_temperature_conversion(dev_) == ESP_OK;
}

float Ds18b20::ReadCelsius()
{
    if (dev_ == nullptr) {
        return onspeed::sensors::kDs18b20DisconnectedC;
    }
    float temperature = 0.0f;
    if (ds18b20_get_temperature(dev_, &temperature) != ESP_OK) {
        // Timeout, CRC failure, or bus fault — same downstream
        // handling as a disconnected sensor: FilterOat rejects -127
        // and the caller holds the last good value.
        return onspeed::sensors::kDs18b20DisconnectedC;
    }
    return temperature;
}

float Ds18b20::BlockingReadCelsius()
{
    if (!RequestConversion()) {
        return onspeed::sensors::kDs18b20DisconnectedC;
    }
    // 12-bit conversion takes up to 750 ms (datasheet max). Wait 800 ms
    // to match the async path's kOatConversionMs margin. Called only at
    // startup before the scheduler runs, so blocking here is acceptable.
    delay(800);
    return ReadCelsius();
}
