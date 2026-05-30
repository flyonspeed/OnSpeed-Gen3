
#pragma once

#include <HardwareSerial.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "src/Globals.h"

#define BOOM_BUFFER_SIZE    127


// BoomSerialIO — UART adapter for the boom AirDAQ probe.  Pumps bytes
// through onspeed::boom::Decode and publishes the decoded floats
// atomically via Snapshot().
//
// Producer task (BoomRead, Core 0) reads the UART and updates the
// published Snapshot struct under xBoomDataMutex_.  Consumer task
// (LogSensorCommitTask, Core 0) reads via Snapshot(out) to get a
// coherent copy.  Same pattern as EfisSerialPort's atomic-publish
// fix — see EfisSerialPort.h for the rationale.
class BoomSerialIO
{
public:
    // Decoded boom data snapshot.  Single instance lives inside
    // BoomSerialIO as `published_`; readers receive a copy via
    // Snapshot(out).
    struct SuBoomData {
        float Static  = 0.0f;
        float Dynamic = 0.0f;
        float Alpha   = 0.0f;
        float Beta    = 0.0f;
        float IAS     = 0.0f;
    };

    BoomSerialIO();

    // Public data (still directly accessed for parser-internal buffer
    // and per-link timestamps — these are NOT racy in practice because
    // BoomRead is the only writer and the values are single-word).
    Stream            * pSerial;
    char                Buffer[BOOM_BUFFER_SIZE];
    byte                BufferIndex;
    unsigned long       uTimestamp;          // millis() at last successful decode
    unsigned long       LastReceivedTime;
    int                 MaxAvailable;        // debug

    void Init(Stream * pBoomSerial);
    void Read();

    // Atomic snapshot of the published boom data.  Mutex hold time is
    // bounded by sizeof(SuBoomData) ≈ 20 bytes worth of memcpy —
    // sub-microsecond.  Safe to call from any task.
    void Snapshot(SuBoomData& out) const;

private:
    SuBoomData                published_      = {};
    mutable SemaphoreHandle_t xBoomDataMutex_ = nullptr;

    // Lazy mutex creation — same pattern as EfisSerialPort.  Called
    // from Init() so the mutex exists before any BoomRead pump.
    void EnsureMutex();
};