
#include "src/Globals.h"

// --------------------------------------------------

void _softRestart()
{
    g_Log.println(MsgLog::EnMain, MsgLog::EnDebug, "System restarting...");

    // Attempt to gracefully close the log file before restarting.
    // Wait up to 1 second to acquire the lock. If the disk is busy, we might not get it, but we'll reboot anyway.
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
    {
        g_LogSensor.Close();
        xSemaphoreGive(xWriteMutex);
    }

    // NOTE: Serial.end() was historically called here ("clears the
    // serial monitor if used"), which made sense when `Serial` was a
    // UART. On the V4P/V4B (ARDUINO_USB_MODE=1, ARDUINO_USB_CDC_ON_BOOT=0)
    // `Serial` is HWCDC over the native USB-OTG peripheral.
    //
    // HWCDC::end() (Arduino-ESP32 cores/esp32/HWCDC.cpp) calls
    // `vSemaphoreDelete(tx_lock)` synchronously without waiting for any
    // task currently inside HWCDC::write() to release that mutex. Any
    // task mid-write reaches its trailing `xSemaphoreGive(tx_lock)` on
    // a deleted handle and wedges in the FreeRTOS queue internals — no
    // panic, no reset, no further serial output. Only a power cycle
    // recovers the chip. The probability of catching a writer
    // mid-HWCDC::write scales with serial throughput, so this freeze
    // is most reproducible at 416 Hz where the writer task emits its
    // PERF heartbeat ~2x more often than at 208 Hz.
    //
    // esp_restart() resets the USB peripheral as part of the full-chip
    // reset, so the explicit teardown is not needed; the host monitor
    // reconnects after re-enumeration.
    delay(100);
    esp_restart();
}

// --------------------------------------------------

uint32_t freeMemory()
{
    return esp_get_minimum_free_heap_size();
}


// --------------------------------------------------
