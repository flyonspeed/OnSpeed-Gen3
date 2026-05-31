// BoomSerial.cpp — UART adapter that drives onspeed::boom::Decode.
//
// Hot-path optimizations relative to the prior strtok/atoi/strtol path:
//   - Single-pass Decode() in onspeed_core (no strtok, no atoi, no
//     strtol — locale-independent and ~2-3× faster on Xtensa LX7).
//   - millis() hoisted out of the per-byte loop (only the active-link
//     timestamp updates per call).
//   - parseHex2() for the CRC instead of strncpy + strtol(_, 16).
//   - Bulk-read via HardwareSerial::readBytes() (one IDF syscall per
//     ASCII line) instead of per-byte Stream::read() (one IDF syscall
//     per character).  Mirrors the VN-300 EfisRead refactor.
//   - Atomic publish of the decoded floats via xBoomDataMutex_ —
//     readers (LogSensorCommitTask) get a coherent struct via
//     Snapshot() instead of risking torn field-by-field reads.

#include "src/Globals.h"
#include <boom/BoomParser.h>
#include <sensors/BoomConvert.h>

#define BOOM_PACKET_SIZE         128

// ----------------------------------------------------------------------------

BoomSerialIO::BoomSerialIO()
{
    BufferIndex     = 0;
    uTimestamp      = millis();

    MaxAvailable = 0;
    // published_ is zero-initialised by the in-class member init.
    // xBoomDataMutex_ stays nullptr — created on first Init() so it
    // works whether BoomSerialIO is constructed as a static (before
    // FreeRTOS is up) or on the heap.
}

// ----------------------------------------------------------------------------

void BoomSerialIO::EnsureMutex()
{
    if (xBoomDataMutex_ == nullptr) {
        xBoomDataMutex_ = xSemaphoreCreateMutex();
    }
}

void BoomSerialIO::Init(Stream * pBoomSerial)
{
    pSerial = pBoomSerial;
    EnsureMutex();
}

void BoomSerialIO::Snapshot(SuBoomData& out) const
{
    if (xBoomDataMutex_ != nullptr &&
        xSemaphoreTake(xBoomDataMutex_, portMAX_DELAY) == pdTRUE) {
        out = published_;
        xSemaphoreGive(xBoomDataMutex_);
    } else {
        // Mutex unavailable (pre-Init, or creation failed) — fall back
        // to an unsynchronized read.  Same shape as the equivalent
        // EfisSerialPort fallback.
        out = published_;
    }
}

// ----------------------------------------------------------------------------

void BoomSerialIO::Read()
{
    if (!g_Config.bReadBoom)
        return;

    // Bulk-drain.  HardwareSerial::readBytes(buf, n, 0) reads up to n
    // bytes non-blocking.  At 50 Hz × ~50 B = ~2.5 KB/sec the boom
    // stream is small, but per-byte Stream::read() still costs an IDF
    // call per character; one bulk call per Read() is 50x cheaper.
    uint8_t scratch[BOOM_PACKET_SIZE];
    int available = pSerial->available();
    if (available <= 0) return;
    if (available > (int)sizeof(scratch)) available = (int)sizeof(scratch);
    const size_t got = pSerial->readBytes(scratch, available);
    if (got == 0) return;

    // Staging buffer for the next publish — we build it from any
    // successfully-decoded frames in this drain and publish at the
    // end under the mutex.  If multiple frames complete in one drain,
    // the most recent one wins (same observable behavior as the prior
    // per-byte-publish code path).
    SuBoomData staging       = published_;   // start from current state
    bool       sawValidFrame = false;

    for (size_t bi = 0; bi < got; ++bi) {
        const char InChar = static_cast<char>(scratch[bi]);
#ifdef BOOMDATADEBUG
        Serial.print(InChar);
#endif

        // Prevent buffer overflow
        if (BufferIndex >= BOOM_BUFFER_SIZE - 1) {
            BufferIndex = 0;
        }

        if (BufferIndex > 0 || InChar == '$') {
            if (InChar == '\n') {
                int decodeLen = BufferIndex;
                if (decodeLen > 0 && Buffer[decodeLen - 1] == '\r')
                    decodeLen--;
                Buffer[decodeLen] = '\0';

                if (decodeLen >= 21 && Buffer[0] == '$') {
                    onspeed::boom::BoomFrame f =
                        onspeed::boom::Decode(Buffer, decodeLen,
                                              g_Config.bBoomChecksum);

                    if (!f.valid && g_Config.bBoomChecksum) {
                        g_Log.printf(MsgLog::EnBoom, MsgLog::EnError,
                                     "Boom decode rejected (bad CRC or malformed)\n");
                    }

                    if (f.valid) {
                        uTimestamp    = millis();
                        sawValidFrame = true;
                        if (g_Config.bBoomConvertData) {
                            staging.Static  = onspeed::BoomStaticConvert (f.staticCounts);
                            staging.Dynamic = onspeed::BoomDynamicConvert(f.dynamicCounts);
                            staging.Alpha   = onspeed::BoomAlphaConvert  (f.alphaCounts);
                            staging.Beta    = onspeed::BoomBetaConvert   (f.betaCounts);
                        } else {
                            staging.Static  = static_cast<float>(f.staticCounts);
                            staging.Dynamic = static_cast<float>(f.dynamicCounts);
                            staging.Alpha   = static_cast<float>(f.alphaCounts);
                            staging.Beta    = static_cast<float>(f.betaCounts);
                        }
                        staging.IAS = 0;

                        g_Log.printf(MsgLog::EnBoom, MsgLog::EnDebug,
                                     "BOOM: Static %.2f, Dynamic %.2f, Alpha %.2f, Beta %.2f, IAS %.2f\n",
                                     (double)staging.Static, (double)staging.Dynamic,
                                     (double)staging.Alpha,  (double)staging.Beta,
                                     (double)staging.IAS);
                    }
                }

                BufferIndex = 0;
            } else {
                Buffer[BufferIndex++] = InChar;
            }
        }
#ifdef BOOMDATADEBUG
        else {
            Serial.print(InChar);
        }
#endif
    }

    // Publish the staging copy atomically.  If no valid frame
    // completed in this drain, staging == published_ already (we
    // started from a copy), so the publish is a no-op overwrite.
    if (sawValidFrame &&
        xBoomDataMutex_ != nullptr &&
        xSemaphoreTake(xBoomDataMutex_, portMAX_DELAY) == pdTRUE) {
        published_ = staging;
        xSemaphoreGive(xBoomDataMutex_);
    }

    // Update the link-alive timestamp once per Read() rather than per
    // byte — saves ~50 millis() syscalls per second at 50 Hz boom rate.
    LastReceivedTime = millis();
}
