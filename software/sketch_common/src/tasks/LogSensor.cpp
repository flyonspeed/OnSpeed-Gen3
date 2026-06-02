
#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING
#endif
#include "SdFat.h"

#include <esp_timer.h>          // esp_timer_get_time() — 64-bit µs since boot
#include "src/Globals.h"
#include "src/ahrs/AhrsSnapshot.h"
#include "src/ahrs/SensorSnapshot.h"
#include "src/ahrs/ImuSnapshot.h"
#include <buildinfo.h>
#include <log/ConsumeAlignedWrite.h>
#include <log/LogMetaBuilder.h>
#include <log/LogMetaFile.h>
#include <proto/LogCsv.h>
#include <types/LogRow.h>
#include <util/Perf.h>

#include <type_traits>

// PR #608: LogRow is sent through the logging ring buffer as raw bytes
// (xRingbufferSend(&row, sizeof(row))) and received on the consumer
// task into a stack-local LogRow via memcpy.  Both ends rely on the
// struct being trivially copyable so that byte-for-byte transport is
// safe; if a future change adds a std::string, std::vector, or any
// member that needs constructor/destructor calls, this assert fires
// at compile time before we can ship a wire-format-incompatible row.
static_assert(std::is_trivially_copyable_v<onspeed::LogRow>,
              "LogRow must remain trivially copyable; the logging ring "
              "buffer passes it byte-for-byte between producer and "
              "consumer tasks.");

using onspeed::m2ft;
using onspeed::mps2fpm;
using onspeed::mps2kts;

#define SYNC_INTERVAL_MS            5000                // How often to sync the log file to disk
#define SIDECAR_REFRESH_MS          30000               // How often to refresh the .meta sidecar mid-flight

// Performance variables for debugging
volatile uint64_t   uWriteMax;
volatile uint64_t   uSyncMax;

// Producer-side drop counter from DebugLog (defined in DebugLog.cpp).
// Pulled into the PERF tick alongside our local s_uRingDropCount.
extern volatile uint32_t g_uDebugRingDrops;

// IMU lateness counters from SensorIO.cpp. PERF reports both per-window:
//   - imu_late: count of late-reset events (each = surrendered samples)
//   - imu_lateMaxUs: worst single lateness observed in this window
// On a healthy box these are near zero. Non-zero indicates Core 1
// contention preventing the IMU task from running on schedule.
extern volatile uint32_t g_uImuLateResets;
extern volatile uint32_t g_uImuMaxLateUs;
extern volatile uint32_t g_uImuMaxLateUsAllTime;

// PERF tick thresholds. Emit an event-driven line if any of:
//   - any data ring drops happened this window
//   - any debug ring drops happened this window
//   - any short-writes happened this window
//   - the worst write in this window exceeded this many microseconds
//   - the worst sync in this window exceeded this many microseconds
//   - the ring depth exceeded this fraction of capacity
// ...and always emit a heartbeat once per kPerfHeartbeatMs regardless.
//
// Trip thresholds tuned from bench observation (V4P, Extreme Pro card):
//   - Ring naturally oscillates 50-90% under the 5 sec sync cycle, so a
//     ring% trip alone fires every loop iteration in normal operation.
//     The ring% reading is informative in the emitted line, but isn't
//     itself a trigger — drops and write/sync timing are the real
//     signals that something is going wrong.
//   - Per-write times sit ~1-2 ms in normal operation; bumps over 50 ms
//     indicate SD wear-leveling pauses worth recording.
//   - Sync times are 5 sec apart and typically take 5-20 ms; bumps over
//     100 ms indicate the sync hit a slow path.
static const uint64_t kPerfWriteUsTrip = 50000;     // 50 ms
static const uint64_t kPerfSyncUsTrip  = 100000;    // 100 ms
static const uint32_t kPerfHeartbeatMs = 10000;     // 10 s

// Ring capacity for the percent-full math comes from Globals.h via
// kLoggingRingBufferBytes — same constant used by xRingbufferCreateWithCaps
// in setup(). (vRingbufferGetInfo exposes free bytes but not configured
// capacity, so we need it from somewhere.)

// Log file handle, keep it local in scope
static FsFile       m_hLogFile;

// Count log lines dropped because the logging ring buffer is full. Keep this
// non-blocking so SD-card stalls can't backpressure critical tasks.
static uint32_t      s_uRingDropCount = 0;

// Count short writes from SdFat (write() returned less than requested).
// Reported through the periodic PERF tick so post-flight forensics can
// tell whether the ring drops we see are from sustained SD pressure or
// from the SD layer itself rejecting writes.
static uint32_t      s_uShortWriteCount = 0;

// Count drain-overflow rescues: when a Receive returns a row that
// won't fit alongside what's already in the staging buffer, the row
// is parked in szCarryoverBuf and emitted at the front of the next
// iteration. These counters track how often that path fires and
// how many bytes flowed through it. With NOSPLIT bounding Receive
// to one row, every overflow rescue is a single ≤ kRowMaxBytes copy
// and no data is lost.
static uint32_t      s_uDrainOverflowCount = 0;
static uint32_t      s_uDrainOverflowBytes = 0;

// Count producer-side samples dropped because g_bPause was true.
// HandleDownload and the bulk-delete handlers set g_bPause to keep
// the ring from filling during multi-second SD-mutex holds; every
// IMU sample that fires while paused increments this counter.
// Surfaced in PERF so pilots see "you lost N samples loading /logs"
// instead of finding silent gaps in the CSV post-flight.
static uint32_t      s_uPausedDropCount = 0;

// Staging buffer: accumulate log lines and write in 512-byte-aligned
// chunks so SdFat can pass full sectors straight to multi-block SPI.
// Accessed from LogSensorCommitTask and LogSensor::Close(); both code
// paths must hold xWriteMutex when touching szWriteBuf or uBufUsed.
//
// Sized at 32 KB (64 sectors). At higher data rates a small buffer
// forces the writer through many small iterations per PERF window —
// each iteration takes xWriteMutex, blocking the web server. 32 KB
// lets the writer drain in a small number of large iterations, with
// the mutex held in long-but-rare windows (a better total contention
// profile under web load).
//
// PSRAM-allocated (see EnsureWriteBufferAllocated below). A 32 KB BSS
// (internal SRAM) allocation fragments the internal heap enough that
// WiFi's EAPOL handshake can fail to allocate its control buffers
// (the largest contiguous free block falls below the ~4 KB threshold
// WiFi needs). PSRAM is plentiful (~8 MB free on V4P), DMA-capable
// via cache, and unused by WiFi internals.
//
// Also: SD cards prefer larger contiguous writes (closer to the physical
// erase block, typically 64+ KB). 32 KB is friendlier to the FAT block
// allocator and to wear-leveling than 2 KB.
static const size_t  SECTOR_SIZE    = 512;
static const size_t  WRITE_BUF_SIZE = SECTOR_SIZE * 64;   // 32 KB
static char*         szWriteBuf     = nullptr;            // PSRAM-allocated
static size_t        uBufUsed       = 0;

// Lazy PSRAM allocation: called once from LogSensorCommitTask on first
// entry, before any access to szWriteBuf. PSRAM-resident so internal
// SRAM stays free for WiFi/lwIP. The 1 MB ring is allocated the same
// way; if PSRAM is somehow unavailable the writer task warns and idles
// (the box still functions for non-logging purposes).
static bool EnsureWriteBufferAllocated()
{
    if (szWriteBuf != nullptr) return true;
    szWriteBuf = static_cast<char*>(
        heap_caps_malloc(WRITE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return szWriteBuf != nullptr;
}
// Flush triggers: whichever fires first wins.
//
//   - SIZE: flush when staging has accumulated ~1/4 of WRITE_BUF_SIZE
//     (8 KB of 32 KB). Keeps each SD write large enough to be efficient
//     (16 sectors) but small enough that the 1 MB ring drains promptly.
//     A larger threshold (e.g. 24 KB) lets the ring fill to 99% because
//     the writer falls behind by ~150 ms between flushes at 416 Hz.
//   - AGE: flush when this many ms have passed since the last write,
//     so low log rates (50 Hz) don't leave rows sitting in SRAM forever.
// Per-rate behavior with size=8KB and age=100ms:
//   50 Hz  (20 KB/s):  age gate dominates -> 2 KB writes, 10/sec
//   208 Hz (83 KB/s):  age gate dominates -> 8 KB writes, 10/sec
//   416 Hz (166 KB/s): size gate dominates -> 8 KB writes, 20/sec
// All three converge to SD-friendly write sizes without overflowing
// the 1 MB ring, and the unsynced tail stays under 100 ms.
static const size_t   WRITE_FLUSH_THRESHOLD_BYTES = 8192;   // 16 sectors
static const uint32_t WRITE_BUF_MAX_AGE_MS        = 100;
static uint32_t       uLastWriteMs                = 0;

// Carryover slot: when xRingbufferReceive returns more bytes than fit
// in the staging buffer right now, we can't "un-receive" them (the ring
// has already consumed them). Park them here and place them at the
// front of the staging buffer on the next iteration after we've
// written the current contents to disk. Sized for one full row.
static char          szCarryoverBuf[onspeed::proto::log_csv::kRowMaxBytes];
static size_t        uCarryoverLen  = 0;

// Rate-limited warning for short writes: emits at most once per 2 s
// so a sustained problem doesn't flood the dbg ring. Returns the
// previous-window count if a warning should fire this call (0 if not).
static uint32_t MaybeReportShortWrite(unsigned long uNowMs)
    {
    static unsigned long uLastWarnMs = 0;
    static uint32_t      uPendingShort = 0;

    uPendingShort++;
    if ((uNowMs - uLastWarnMs) > 2000)
        {
        const uint32_t uReport = uPendingShort;
        uPendingShort = 0;
        uLastWarnMs = uNowMs;
        return uReport;
        }
    return 0;
    }

// Wrap the staging-buffer flush so the four SD-write sites in this file
// share the same short-write handling: capture the count actually
// written, retry the unwritten head next round, bump counters / warn
// rather than silently shifting past lost bytes.
static bool ConsumeWriteAndMaybeWarn(size_t uRequested, size_t uActual)
    {
    bool bOk = onspeed::log::ConsumeAlignedWrite(
        uRequested, uActual, szWriteBuf, WRITE_BUF_SIZE, &uBufUsed);
    if (!bOk)
        {
        __atomic_fetch_add(&s_uShortWriteCount, 1u, __ATOMIC_RELAXED);
        const uint32_t uReport = MaybeReportShortWrite(millis());
        if (uReport > 0)
            g_Log.printf(MsgLog::EnDisk, MsgLog::EnWarning,
                "SD short write x%lu (requested=%u actual=%u)\n",
                (unsigned long)uReport,
                (unsigned)uRequested,
                (unsigned)uActual);
        }
    return bOk;
    }

// Write any remaining bytes in the staging buffer to the log file.
// Caller must hold xWriteMutex and ensure m_hLogFile.isOpen().
//
// On a short write, the unwritten head remains at the front of
// szWriteBuf and uBufUsed reflects the residual. Callers running
// inside the writer loop pick this up next iteration; Close() takes
// the residual as best-effort loss (file is about to close anyway).
static void FlushStagingBufferLocked()
    {
    // szWriteBuf is PSRAM-allocated lazily on first writer-task entry;
    // a Close() before the task ran (very early shutdown) would land
    // here with szWriteBuf=null and uBufUsed=0 — guard both.
    if (szWriteBuf != nullptr && uBufUsed > 0 && m_hLogFile.isOpen())
        {
        const size_t uRequested = uBufUsed;
        const size_t uActual    = m_hLogFile.write(szWriteBuf, uRequested);
        ConsumeWriteAndMaybeWarn(uRequested, uActual);
        }
    }

// ----------------------------------------------------------------------------

// FreeRTOS task to write sensor log data to disk
// Previously formatted strings with log data to be written to disk are sent
// to a ring buffer. This task reads them out of the ring buffer and writes them
// to the open log file. Sometimes the uSD disk will block for a grotesque amount
// of time (300 msec or more) or go into slow down mode for even longer (5 seconds
// or more). The ring buffer needs to be big enough to queue up that data until
// it can finally be written to disk. Doing a flush / sync after every write works
// most of the time... most of the time. But when the disk goes into slow down it
// really messes things up. Flush / sync every 10 seconds or so helps things out
// quite a bit.

void LogSensorCommitTask(void *pvParams)
{
    static size_t         iPrintLen;
    static char         * pchIn;
    static TickType_t     xLastSyncTime = xTaskGetTickCount();
    static TickType_t     xLastSidecarTime = xTaskGetTickCount();
    static uint64_t       uWriteStart, uWriteEnd, uWriteDur;
    static uint64_t       uSyncStart,  uSyncEnd,  uSyncDur;

    // Allocate the 32 KB staging buffer in PSRAM on first entry. Keeping
    // it out of internal SRAM avoids fragmenting the heap region WiFi /
    // lwIP allocate their EAPOL + DHCP buffers from.
    if (!EnsureWriteBufferAllocated())
    {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnError,
                      "LogSensor: PSRAM alloc for staging buffer failed; task idle");
        // Close the log file so producer-side LogSensor::Write() sees
        // m_hLogFile not open and falls into the rate-limited "Log file
        // is not open; discarding queued log data" warning path. Without
        // this, producers keep queuing into the ring buffer forever and
        // the failure is silent until ring drops start.
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000))) {
            if (m_hLogFile.isOpen()) m_hLogFile.close();
            xSemaphoreGive(xWriteMutex);
        }
        while (true) vTaskDelay(pdMS_TO_TICKS(5000));
    }

    while (true)
    {
        // NOTE: this task spends most of its time blocked in
        // xRingbufferReceive() waiting for data. PerfLoop wraps only the
        // post-receive work (SD write + mutex acquisition) so the
        // reported total CPU% reflects actual write cost, not idle wait.

        // Ring-drop reporting moved into the PERF tick at the end of
        // this loop body — same counter, single emit path, with full
        // context (write/sync max, ring depth, short writes, heap).

        if (xLoggingRingBuffer == nullptr)
            {
            static bool bWarned = false;
            if (!bWarned)
                {
                g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Logging ring buffer not allocated; LogSensorCommitTask idle");
                bWarned = true;
                }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
            }

        // When paused (e.g. during web file downloads/listing), stay out
        // of the mutex entirely so web handlers can access the SD card
        // without contention.  Just do the ring buffer receive to keep
        // the watchdog happy, then loop back.
        if (g_bPause)
            {
            pchIn = (char *)xRingbufferReceive(xLoggingRingBuffer, &iPrintLen, pdMS_TO_TICKS(100));
            if (pchIn != NULL)
                vRingbufferReturnItem(xLoggingRingBuffer, pchIn);
            continue;
            }

        // PERF: scope wraps the actual write/mutex/IO work that follows.
        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::Log,
            uxTaskGetStackHighWaterMark(nullptr));

        // Take the write mutex for the full drain+write cycle. Both the
        // staging buffer (szWriteBuf/uBufUsed) and the log file handle
        // are shared with LogSensor::Close() and other SD-consuming paths,
        // so serialize all access through xWriteMutex. The first receive
        // below waits up to 100 ms, so this task will hold the mutex for
        // up to ~100 ms when idle — still well under the 1000+ ms timeouts
        // used by every other taker.
        if (!xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            {
            static unsigned long uLastWarnMs = 0;
            unsigned long uNow = millis();
            if ((uNow - uLastWarnMs) > 2000)
                {
                g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning, "SD busy (xWriteMutex); skipping drain");
                uLastWarnMs = uNow;
                }
            continue;
            }

        // Drain available items from the ring buffer into the staging buffer.
        // First receive blocks up to 100 ms (keeps watchdog happy); subsequent
        // receives are non-blocking to batch everything currently queued.
        bool bGotData = false;
        TickType_t xWait = pdMS_TO_TICKS(100);

        // Place any carryover from the previous iteration first. This is
        // a row that we received but couldn't fit in the staging buffer
        // last time around; the SD write at the end of the loop drained
        // most of the buffer, so it should fit now.
        if (uCarryoverLen > 0 && uBufUsed + uCarryoverLen <= WRITE_BUF_SIZE)
            {
            memcpy(szWriteBuf + uBufUsed, szCarryoverBuf, uCarryoverLen);
            uBufUsed += uCarryoverLen;
            uCarryoverLen = 0;
            bGotData = true;
            }

        // Scratch buffer for formatting one row at a time on this
        // (consumer) task.  PR #608: FormatRow moved here from the
        // producer side to get its ~150 µs/row cost off Core 1.
        // +2 holds the trailing "\n\0".
        static char szLogLine[onspeed::proto::log_csv::kRowMaxBytes + 2];

        while (true)
            {
            // If we already have carryover, don't receive more — first
            // write out what's staged so the carryover has room next round.
            if (uCarryoverLen > 0) break;

            pchIn = (char *)xRingbufferReceive(xLoggingRingBuffer, &iPrintLen, xWait);
            xWait = 0;     // subsequent receives are non-blocking

            if (pchIn == NULL)
                break;

            // Each ring item is a serialized LogRow struct.  Copy it
            // into an aligned local before formatting — pchIn points
            // into the ring buffer's internal storage, which gives no
            // alignment guarantee for the 8-byte members (uint64_t,
            // double) inside LogRow.  Using the bytes directly via
            // reinterpret_cast<LogRow*> would be UB on a misaligned
            // address.  A 700-byte memcpy is ~0.5 µs on the S3 — well
            // inside the noise floor of this task's work budget.
            //
            // If the ring delivers a wrong-sized blob (producer-side
            // regression), skip with a rate-limited warning rather
            // than reinterpreting a surprise byte shape.
            size_t lineLen = 0;
            if (iPrintLen == sizeof(onspeed::LogRow)) {
                onspeed::LogRow row;
                memcpy(&row, pchIn, sizeof(row));
                lineLen = onspeed::proto::log_csv::FormatRow(
                    row, szLogLine, sizeof(szLogLine));
                if (lineLen > 0 && lineLen + 1 < sizeof(szLogLine)) {
                    szLogLine[lineLen]     = '\n';
                    szLogLine[lineLen + 1] = '\0';
                    lineLen += 1;
                } else {
                    // FormatRow either returned 0 (overflow) or filled
                    // the buffer with no room for the newline.  Drop.
                    lineLen = 0;
                    static unsigned long uLastWarnMs = 0;
                    unsigned long uNow = millis();
                    if ((uNow - uLastWarnMs) > 2000) {
                        g_Log.println(MsgLog::EnDisk, MsgLog::EnError,
                                      "FormatRow overflow on writer; dropping row");
                        uLastWarnMs = uNow;
                    }
                }
            } else {
                // Wrong-sized item.  Producer regression or stale data;
                // skip with a rate-limited warning so the cause is
                // visible.
                static unsigned long uLastWarnMs = 0;
                unsigned long uNow = millis();
                if ((uNow - uLastWarnMs) > 2000) {
                    g_Log.printf(MsgLog::EnDisk, MsgLog::EnError,
                                 "Ring item size %u != sizeof(LogRow) %u; skipping\n",
                                 static_cast<unsigned>(iPrintLen),
                                 static_cast<unsigned>(sizeof(onspeed::LogRow)));
                    uLastWarnMs = uNow;
                }
            }

            // If the formatted line won't fit in the staging buffer,
            // park it in the carryover slot. We CAN'T return-without-copy
            // because the ring has already advanced past the source
            // bytes — vRingbufferReturnItem frees them but doesn't put
            // them back into the readable stream. Copying to carryover
            // preserves the row for the next iteration.
            if (lineLen > 0 && uBufUsed + lineLen > WRITE_BUF_SIZE)
                {
                __atomic_fetch_add(&s_uDrainOverflowCount, 1u,                   __ATOMIC_RELAXED);
                __atomic_fetch_add(&s_uDrainOverflowBytes, (uint32_t)lineLen,     __ATOMIC_RELAXED);
                if (lineLen <= sizeof(szCarryoverBuf))
                    {
                    memcpy(szCarryoverBuf, szLogLine, lineLen);
                    uCarryoverLen = lineLen;
                    }
                // If the row is larger than the carryover buffer it's
                // a row-format bug (rows shouldn't exceed kRowMaxBytes);
                // s_uDrainOverflowCount already counted it.  Drop.
                vRingbufferReturnItem(xLoggingRingBuffer, pchIn);
                pchIn = NULL;
                break;
                }

            if (lineLen > 0)
                {
                memcpy(szWriteBuf + uBufUsed, szLogLine, lineLen);
                uBufUsed += lineLen;
                bGotData = true;
                }
            vRingbufferReturnItem(xLoggingRingBuffer, pchIn);
            pchIn = NULL;
            }

        if (g_Log.Test(MsgLog::EnDisk, MsgLog::EnDebug) && bGotData)
            {
            UBaseType_t uxFree;
            UBaseType_t uxRead;
            UBaseType_t uxWrite;
            UBaseType_t uxAcquire;
            UBaseType_t uxItemsWaiting;

            vRingbufferGetInfo(xLoggingRingBuffer, &uxFree, &uxRead, &uxWrite, &uxAcquire, &uxItemsWaiting);
            g_Log.printf(MsgLog::EnDisk, MsgLog::EnDebug, "Write buf %d bytes  Ring waiting %d\n", uBufUsed, uxItemsWaiting);
            }

        // Surface the "file never opened" case directly. Without this,
        // a pilot whose Open() failed at boot (dead SD, full card,
        // mount error) sees one Open-error log on boot and then only
        // generic ring-buffer-full drops once the staging buffer
        // saturates ~1 s later. This warning names the actual cause.
        if (uBufUsed > 0 && !m_hLogFile.isOpen())
            {
            static unsigned long uLastNoFileWarnMs = 0;
            unsigned long uNow = millis();
            if ((uNow - uLastNoFileWarnMs) > 5000)
                {
                g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning, "Log file is not open; discarding queued log data");
                uLastNoFileWarnMs = uNow;
                }
            // Drop staged bytes so the staging buffer doesn't lock at
            // full and the drain loop above can keep recycling ring
            // slots (preventing producer-side ring overflow drops).
            uBufUsed = 0;
            }

        // Write 512-byte-aligned chunks to disk
        bool bDidSync = false;
        if (uBufUsed > 0 && m_hLogFile.isOpen())
        {
            // Gate writes on either "enough bytes accumulated" OR "time
            // elapsed since last write" — whichever fires first. The size
            // gate keeps writes large enough to be SD-efficient; the age
            // gate keeps the 1 MB ring from filling between writes at
            // high log rates and keeps unsynced tail short at low rates.
            const uint32_t uNowMs    = millis();
            const bool     bSizeReady = uBufUsed >= WRITE_FLUSH_THRESHOLD_BYTES;
            const bool     bAgeReady  = (uNowMs - uLastWriteMs) >= WRITE_BUF_MAX_AGE_MS;

            // Write full sectors. Any remainder stays in the buffer for
            // the next batch, keeping writes 512-byte-aligned.
            size_t uAligned   = (uBufUsed / SECTOR_SIZE) * SECTOR_SIZE;

            if (uAligned > 0 && (bSizeReady || bAgeReady))
                {
                uWriteStart = micros();
                size_t uActual;
                {
                    // PERF: scope ONLY the SD write call. This is the
                    // actual disk I/O the firmware's `write_max` already
                    // tracks; PerfLoop on the surrounding task includes
                    // ring receive + mutex hold which is mostly idle.
                    onspeed::util::perf::PerfScope guard(
                        onspeed::util::perf::ScopeId::LogWrite);
                    uActual = m_hLogFile.write(szWriteBuf, uAligned);
                }
                uWriteEnd   = micros();
                uWriteDur   = uWriteEnd - uWriteStart;
                uWriteMax   = uWriteDur > uWriteMax ? uWriteDur : uWriteMax;
                uLastWriteMs = uNowMs;

                // ConsumeAlignedWrite leaves the unwritten head (if any)
                // at the front of szWriteBuf and shifts the post-flush
                // tail behind it, so a short write is retried next round
                // instead of silently disappearing from the CSV stream.
                ConsumeWriteAndMaybeWarn(uAligned, uActual);
                }

            // Sync periodically. This is a blocking call, so we don't want to do it too often.
            // Flush any remaining partial sector before sync so the data is on disk.
            if ((xTaskGetTickCount() - xLastSyncTime) > pdMS_TO_TICKS(SYNC_INTERVAL_MS))
            {
                FlushStagingBufferLocked();
                uSyncStart = micros();
                {
                    // PERF: scope the SD fsync (blocking; runs every 5s).
                    onspeed::util::perf::PerfScope guard(
                        onspeed::util::perf::ScopeId::LogSync);
                    m_hLogFile.sync();
                }
                uSyncEnd   = micros();
                uSyncDur   = uSyncEnd - uSyncStart;
                uSyncMax   = uSyncDur  > uSyncMax  ? uSyncDur  : uSyncMax;
                xLastSyncTime = xTaskGetTickCount();
                bDidSync = true;
            }
        } // end if data to write

        // Refresh the .meta sidecar every SIDECAR_REFRESH_MS regardless
        // of whether we just synced — pilots shut down by killing power
        // at the key, so Close() never runs and the sidecar from Open()
        // would otherwise stay frozen at zero. Done while still holding
        // xWriteMutex so we don't contend with Close()/web-handler SD ops.
        if ((xTaskGetTickCount() - xLastSidecarTime) > pdMS_TO_TICKS(SIDECAR_REFRESH_MS))
        {
            g_LogSensor.WriteSidecarLocked();
            xLastSidecarTime = xTaskGetTickCount();
        }

        // -------------------------------------------------------------
        // PERF tick: event-driven snapshot of the data-path counters.
        //
        // Tracks the worst write/sync duration since the last emit, the
        // since-last-emit ring-drop / short-write / dbg-drop counts,
        // and the current ring depth. Emits a single Warning-level line
        // whenever any threshold is tripped or every kPerfHeartbeatMs
        // as a heartbeat. The same line flows through g_Log to Serial
        // and (via MsgLog dual-sink) to log_NNN.dbg, so post-flight we
        // can correlate gaps in the CSV with the trigger event that
        // produced them.
        // -------------------------------------------------------------
        static uint64_t      uPerfWriteMaxUs   = 0;
        static uint64_t      uPerfSyncMaxUs    = 0;
        static uint32_t      uPerfRingDrops    = 0;
        static uint32_t      uPerfDbgDrops     = 0;
        static uint32_t      uPerfShortWrites  = 0;
        static uint32_t      uPerfImuLate      = 0;
        static uint32_t      uPerfImuMaxLateUs = 0;
        static uint32_t      uPerfOverflowCnt  = 0;
        static uint32_t      uPerfOverflowB    = 0;
        static uint32_t      uPerfPausedDrops  = 0;
        static unsigned long uPerfLastEmitMs   = 0;

        if (uWriteDur > uPerfWriteMaxUs) uPerfWriteMaxUs = uWriteDur;
        if (uSyncDur  > uPerfSyncMaxUs)  uPerfSyncMaxUs  = uSyncDur;
        // Reset per-iteration durations so a quiet iteration after a
        // big one doesn't re-charge the same number into the next
        // PERF window.
        uWriteDur = 0;
        uSyncDur  = 0;
        uPerfRingDrops   += __atomic_exchange_n(&s_uRingDropCount,    0u, __ATOMIC_RELAXED);
        uPerfDbgDrops    += __atomic_exchange_n(&g_uDebugRingDrops,   0u, __ATOMIC_RELAXED);
        uPerfShortWrites += __atomic_exchange_n(&s_uShortWriteCount,  0u, __ATOMIC_RELAXED);
        uPerfImuLate     += __atomic_exchange_n(&g_uImuLateResets,    0u, __ATOMIC_RELAXED);
        {
            const uint32_t uImuMax = __atomic_exchange_n(&g_uImuMaxLateUs, 0u, __ATOMIC_RELAXED);
            if (uImuMax > uPerfImuMaxLateUs) uPerfImuMaxLateUs = uImuMax;
        }
        uPerfOverflowCnt += __atomic_exchange_n(&s_uDrainOverflowCount, 0u, __ATOMIC_RELAXED);
        uPerfOverflowB   += __atomic_exchange_n(&s_uDrainOverflowBytes, 0u, __ATOMIC_RELAXED);
        uPerfPausedDrops += __atomic_exchange_n(&s_uPausedDropCount,    0u, __ATOMIC_RELAXED);

        uint32_t uRingPct = 0;
        if (xLoggingRingBuffer != nullptr)
            {
            UBaseType_t uxFree = 0, uxRead = 0, uxWrite = 0, uxAcquire = 0, uxWaiting = 0;
            vRingbufferGetInfo(xLoggingRingBuffer, &uxFree, &uxRead, &uxWrite, &uxAcquire, &uxWaiting);
            const size_t uUsed = (uxFree < kLoggingRingBufferBytes)
                               ? (kLoggingRingBufferBytes - uxFree)
                               : 0;
            uRingPct = static_cast<uint32_t>((uint64_t(uUsed) * 100) / kLoggingRingBufferBytes);
            }

        const unsigned long uNowPerfMs = millis();
        const bool bHeartbeat = (uPerfLastEmitMs == 0) ||
                                ((uNowPerfMs - uPerfLastEmitMs) >= kPerfHeartbeatMs);
        const bool bTripped   = (uPerfRingDrops   > 0)
                             || (uPerfDbgDrops    > 0)
                             || (uPerfShortWrites > 0)
                             || (uPerfImuLate     > 0)
                             || (uPerfOverflowCnt > 0)
                             || (uPerfPausedDrops > 0)
                             || (uPerfWriteMaxUs  > kPerfWriteUsTrip)
                             || (uPerfSyncMaxUs   > kPerfSyncUsTrip);

        if (bHeartbeat || bTripped)
            {
            const size_t uHeapK  = esp_get_free_heap_size() / 1024;
            const size_t uPsramK = ESP.getFreePsram()        / 1024;
            // Read (no reset) the all-time IMU lateness so bursty
            // spikes that the per-window counter loses are visible.
            // A multi-ms stall that lands between PERF heartbeats was
            // getting wiped by the next emit; this counter preserves
            // it across windows.
            const uint32_t uImuMaxAT = __atomic_load_n(&g_uImuMaxLateUsAllTime, __ATOMIC_RELAXED);
            g_Log.printf(MsgLog::EnDisk, MsgLog::EnWarning,
                "PERF write_max=%lluus sync_max=%lluus ring=%lu%%/%uK "
                "drops=%lu dbg_drops=%lu short=%lu "
                "imu_late=%lu imu_lateMaxUs=%lu imu_lateMaxUsAT=%lu "
                "overflow=%lu overflow_bytes=%lu paused_drops=%lu "
                "heap=%uK psram=%uK%s\n",
                (unsigned long long)uPerfWriteMaxUs,
                (unsigned long long)uPerfSyncMaxUs,
                (unsigned long)uRingPct,
                (unsigned)(kLoggingRingBufferBytes / 1024),
                (unsigned long)uPerfRingDrops,
                (unsigned long)uPerfDbgDrops,
                (unsigned long)uPerfShortWrites,
                (unsigned long)uPerfImuLate,
                (unsigned long)uPerfImuMaxLateUs,
                (unsigned long)uImuMaxAT,
                (unsigned long)uPerfOverflowCnt,
                (unsigned long)uPerfOverflowB,
                (unsigned long)uPerfPausedDrops,
                (unsigned)uHeapK,
                (unsigned)uPsramK,
                bHeartbeat && !bTripped ? " hb" : "");
            uPerfWriteMaxUs   = 0;
            uPerfSyncMaxUs    = 0;
            uPerfRingDrops    = 0;
            uPerfDbgDrops     = 0;
            uPerfShortWrites  = 0;
            uPerfImuLate      = 0;
            uPerfImuMaxLateUs = 0;
            uPerfOverflowCnt  = 0;
            uPerfOverflowB    = 0;
            uPerfPausedDrops  = 0;
            uPerfLastEmitMs   = uNowPerfMs;
            }

        // Drain the dbg ring under our existing xWriteMutex window —
        // SdFat's volume cache is shared, so dbg writes can't run on
        // a separate task without corrupting each other's data.
        g_DebugLog.DrainLocked();

        xSemaphoreGive(xWriteMutex);

        // Yield 1 ms after each mutex release. The writer's loop body
        // is tight enough that re-acquiring the mutex immediately can
        // starve any other waiter (config-save, /api/logs, format).
        // FreeRTOS doesn't queue mutex takers by arrival order; a tight
        // release/re-acquire pattern wins almost every coin flip
        // against a waiter that started later. 1 ms is enough for an
        // equal-priority waiter to be scheduled and claim the mutex,
        // while costing almost nothing at the ring level (1 ms * 80
        // KB/s = 80 bytes of pending data).
        vTaskDelay(pdMS_TO_TICKS(1));

        // Never block SD access while waiting on serial output.
        if (bDidSync)
            g_Log.print(MsgLog::EnDisk, MsgLog::EnDebug, "Sync\n");
    } // end while forever

} // end TaskWriteSensorLog()


// ============================================================================

// Not much to construct
LogSensor::LogSensor()
{

}

// ----------------------------------------------------------------------------

// Open the next sensor log file in sequence
// Be sure to wrap in semaphore

void LogSensor::Open()
{
    char        szSensorLogFilename[32];

    if (g_Config.suDataSrc.enSrc == SuDataSource::EnSensors && g_SdFileSys.bSdAvailable)
    {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnDebug, "Open log file for writing");

        // Find the next file number.
        SdFileSys::SuFileInfoList   suFileList;
        int     iMaxFileNum = 0;

        if (g_SdFileSys.FileList(&suFileList))
            {
            for (int iIdx=0; iIdx<suFileList.size(); iIdx++)
                {
                const char* name = suFileList[iIdx].szFileName;
                // Match both the pre-rename "log_NNN.csv" form and the
                // post-rename "YYYY-MM-DD_NNN.csv" form so that after
                // rename-at-close we still find the highest NNN on
                // subsequent boots.
                int iFileNum = -1;
                if (strncasecmp("log_", name, 4) == 0)
                    {
                    iFileNum = atoi(&name[4]);
                    }
                else if (strlen(name) >= 15 &&
                         name[4] == '-' && name[7] == '-' && name[10] == '_')
                    {
                    // Renamed form: "YYYY-MM-DD_NNN.csv" — NNN starts at 11.
                    iFileNum = atoi(&name[11]);
                    }
                if (iFileNum > iMaxFileNum) iMaxFileNum = iFileNum;
                } // end for all files
            }
        else
            g_Log.print(MsgLog::EnDisk, MsgLog::EnError, "LOGSENSOR FileList() fail");

        snprintf(szSensorLogFilename, sizeof(szSensorLogFilename), "log_%03d.csv", iMaxFileNum + 1);
        snprintf(m_szBaseName, sizeof(m_szBaseName), "log_%03d", iMaxFileNum + 1);

        g_Log.print("Sensor log file:"); g_Log.println(szSensorLogFilename);

        m_hLogFile = g_SdFileSys.open(szSensorLogFilename, O_RDWR | O_CREAT | O_TRUNC);

        if (m_hLogFile.isOpen())
        {
            // Build a feature-flag sentinel row so WriteHeader emits the
            // right optional column groups for this session.
            onspeed::LogRow headerRow;
            headerRow.boomEnabled = g_Config.bReadBoom;
            headerRow.efisEnabled = g_Config.bReadEfisData;
            headerRow.efisIsVn300 = g_Config.bReadEfisData &&
                                    (g_EfisSerial.enType == EfisSerialPort::EnVN300);
            // Always capture the raw flap-pot ADC in new logs; replay tools
            // need it to reproduce the L/Dmax pip slide between detents.
            headerRow.flapsRawAdcPresent = true;

            static char szHeader[onspeed::proto::log_csv::kHeaderMaxBytes];
            size_t hdrLen = onspeed::proto::log_csv::WriteHeader(headerRow, szHeader, sizeof(szHeader));
            if (hdrLen > 0)
                {
                const size_t uActual = m_hLogFile.write(szHeader, hdrLen);
                if (uActual != hdrLen)
                    g_Log.printf(MsgLog::EnDisk, MsgLog::EnError,
                        "SD header short write (requested=%u actual=%u)\n",
                        (unsigned)hdrLen, (unsigned)uActual);
                }
            if (m_hLogFile.write("\n", 1) != 1)
                g_Log.println(MsgLog::EnDisk, MsgLog::EnError,
                    "SD header newline short write");

            m_hLogFile.sync();

            // Initialise sidecar accumulator for this session.
            onspeed::log::EfisType etype = onspeed::log::EfisType::None;
            if (g_Config.bReadEfisData) {
                switch (g_EfisSerial.enType) {
                    case EfisSerialPort::EnVN300:        etype = onspeed::log::EfisType::Vn300;  break;
                    case EfisSerialPort::EnDynonSkyview:
                    case EfisSerialPort::EnDynonD10:     etype = onspeed::log::EfisType::Dynon;  break;
                    case EfisSerialPort::EnGarminG5:
                    case EfisSerialPort::EnGarminG3X:    etype = onspeed::log::EfisType::Garmin; break;
                    case EfisSerialPort::EnMglBinary:    etype = onspeed::log::EfisType::Mgl;    break;
                    case EfisSerialPort::EnNone:
                    default:                             etype = onspeed::log::EfisType::None;   break;
                }
            }
            m_metaBuilder.Begin(BuildInfo::version,
                                BuildInfo::gitShortSha,
                                onspeed::proto::log_csv::kFormatVersion,
                                etype);

            // Paired .dbg file for warnings/errors/PERF lines. We're
            // already inside xWriteMutex (caller's contract), and the
            // SdFat volume requires all file ops on this card to be
            // serialised through it — so just open the .dbg here.
            // If the open fails, Write() no-ops and the rest of the
            // system continues unaffected.
            g_DebugLog.Open(m_szBaseName);

            // No initial sidecar write — the first refresh fires 30 s into
            // the session from LogSensorCommitTask. A power-yank inside
            // that 30 s window leaves no .meta (em-dashes on /logs), which
            // is more honest than a sidecar reporting "0 kt / 0 ft / 0s"
            // for what was actually a partial flight.
        } // end if file open OK

        else
        {
            g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "SensorFile opening error.");
            // Empty base name keeps ActiveBaseName() from advertising
            // a session that never really started; the web UI would
            // otherwise block deletion of a nonexistent "log_NNN.csv".
            //
            // bSdLogging is intentionally not mutated here: the user's
            // saved config is the source of truth for whether logging
            // is wanted. A transient SD failure should leave the user's
            // intent intact and let the next session retry.
            m_szBaseName[0] = '\0';
        }
    } // end if sensor data and SD disk available

    else
        g_Log.println(MsgLog::EnDisk, MsgLog::EnDebug, "Log file not opened for writing");

}

// ----------------------------------------------------------------------------

// Atomic sidecar refresh. Writes the current LogMetaBuilder snapshot to
// "<basename>.meta.tmp", syncs, then renames over "<basename>.meta". A
// power-yank during the tmp write leaves the previous .meta intact; a
// power-yank during rename leaves either the old or new name pointing
// at intact data — never a half-merged file.
//
// Pilots typically shut down by killing power at the key, so Close()
// never runs and the original "write sidecar at Close() only" design
// produced no metadata for those flights. Calling this once at Open()
// and every 30 s during the flight bounds the loss to at most one
// SIDECAR_REFRESH_MS window.
//
// Caller must hold xWriteMutex (m_szBaseName, m_metaBuilder, and SD
// access are all serialized through it).
void LogSensor::WriteSidecarLocked()
{
    if (m_szBaseName[0] == '\0') return;

    onspeed::log::LogMeta meta = m_metaBuilder.Finalize();

    char buf[512];
    size_t n = onspeed::log::WriteMetaFile(meta, buf, sizeof(buf));
    if (n == 0) {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
                      "Sidecar meta serialize failed");
        return;
    }

    char tmpPath[32];
    char metaPath[32];
    snprintf(tmpPath,  sizeof(tmpPath),  "%s.meta.tmp", m_szBaseName);
    snprintf(metaPath, sizeof(metaPath), "%s.meta",     m_szBaseName);

    FsFile f = g_SdFileSys.open(tmpPath, O_RDWR | O_CREAT | O_TRUNC);
    if (!f.isOpen()) {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
                      "Sidecar meta tmp open failed");
        return;
    }
    const size_t uActual = f.write(buf, n);
    if (uActual != n)
        g_Log.printf(MsgLog::EnDisk, MsgLog::EnWarning,
            "Sidecar short write (requested=%u actual=%u); skipping rename\n",
            (unsigned)n, (unsigned)uActual);
    f.sync();
    f.close();

    // If the .meta.tmp write was short, the atomic-rename promise is
    // gone (a half-meta file would be worse than the previous one).
    // Drop the tmp and leave the prior .meta in place.
    if (uActual != n) {
        g_SdFileSys.remove(tmpPath);
        return;
    }

    // Rename atomically over the prior .meta. Remove any stale .meta
    // first because some FAT implementations refuse rename-onto-existing.
    if (g_SdFileSys.exists(metaPath))
        g_SdFileSys.remove(metaPath);

    if (!g_SdFileSys.rename(tmpPath, metaPath)) {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
                      "Sidecar meta rename failed");
        // Best effort: try to clean up the orphan tmp so it doesn't
        // accumulate across many failed refreshes.
        g_SdFileSys.remove(tmpPath);
    }
}

// ----------------------------------------------------------------------------

// Close the log file. Caller must hold xWriteMutex.
// Flushes any remaining bytes from the staging buffer before closing, so
// the last ~1-2 log lines are not lost on graceful shutdown (LOG DISABLE,
// FORMAT, soft restart). The consumer task holds xWriteMutex while touching
// szWriteBuf, so this is safe to call from any mutex-holding context.

void LogSensor::Close()
{
    // Drain any row stranded in the carryover slot from the last drain
    // iteration. Without this, a card stall that fills the staging
    // buffer just before Close() fires (web LOG DISABLE, FORMAT, etc.)
    // loses the parked row silently — the drain loop wouldn't run again
    // to place it at the front of szWriteBuf.
    //
    // Two flushes: the first drains any sub-sector residual left by the
    // last writer iteration (so szWriteBuf has room for a full row), the
    // second writes the carryover. Without the leading flush, a 2 KB
    // carryover row + 1-511 bytes of residual would overflow WRITE_BUF_SIZE
    // and the bounds check would silently drop the carryover.
    FlushStagingBufferLocked();
    // szWriteBuf is null until the writer task ran EnsureWriteBufferAllocated();
    // uCarryoverLen is also 0 in that case (only set by the drain loop), so
    // this branch is unreachable from a fresh-Open()+Close() with no rows.
    // The explicit null guard is defense-in-depth against a future producer
    // path that could populate carryover without going through the drain.
    if (szWriteBuf != nullptr && uCarryoverLen > 0 && uBufUsed + uCarryoverLen <= WRITE_BUF_SIZE)
        {
        memcpy(szWriteBuf + uBufUsed, szCarryoverBuf, uCarryoverLen);
        uBufUsed += uCarryoverLen;
        uCarryoverLen = 0;
        }
    FlushStagingBufferLocked();
    m_hLogFile.close();

    // Close the paired .dbg file. Caller already holds xWriteMutex,
    // which is what serialises all SD ops on the shared volume.
    g_DebugLog.Close();

    // Nothing to serialise if we never opened a file in this session.
    if (m_szBaseName[0] == '\0') return;

    WriteSidecarLocked();

    // Re-snapshot for the rename decision; WriteSidecarLocked() doesn't
    // expose its snapshot, but Finalize() is non-destructive so calling
    // it again is cheap and yields the same data.
    onspeed::log::LogMeta meta = m_metaBuilder.Finalize();

    // Optional rename: only when we captured an actual ISO-8601 UTC date
    // (format "YYYY-MM-DDTHH:MM:SSZ", at least 11 chars — we only use the
    // first 10 for the filename prefix). The VN-300 serial parser currently
    // emits only "H:M:S" without date, so this condition is false in that
    // case and we skip the rename — leaving behind a well-named
    // log_NNN.{csv,meta} pair. If the parser is extended later to include
    // date (or some other source produces full UTC), this path activates
    // automatically. We explicitly reject ':'-containing prefixes here
    // because they would produce filenames illegal on FAT / Windows hosts.
    auto isIsoDatePrefix = [](const char* s) -> bool {
        if (strlen(s) < 11) return false;
        if (s[4] != '-' || s[7] != '-' || s[10] != 'T') return false;
        for (int i = 0; i < 10; i++) {
            if (i == 4 || i == 7) continue;
            if (s[i] < '0' || s[i] > '9') return false;
        }
        return true;
    };

    if (isIsoDatePrefix(meta.utcStart)) {
        char datePrefix[11];
        memcpy(datePrefix, meta.utcStart, 10);
        datePrefix[10] = '\0';

        // Extract "NNN" from m_szBaseName which is "log_NNN".
        const char* nnn = m_szBaseName + 4;   // skip "log_"

        char newCsvName[32];
        char newMetaName[32];
        snprintf(newCsvName,  sizeof(newCsvName),  "%s_%s.csv",  datePrefix, nnn);
        snprintf(newMetaName, sizeof(newMetaName), "%s_%s.meta", datePrefix, nnn);

        char oldCsvName[32];
        char oldMetaName[32];
        snprintf(oldCsvName,  sizeof(oldCsvName),  "%s.csv",  m_szBaseName);
        snprintf(oldMetaName, sizeof(oldMetaName), "%s.meta", m_szBaseName);

        // Rename the .meta first — it's smaller and fails earlier on
        // disk-full or name collisions, so the .csv stays at its old name
        // if the meta rename can't succeed (rather than leaving an
        // orphaned pair). Collision check still covers both up front.
        bool bRenamedTrio = false;
        if (!g_SdFileSys.exists(newCsvName) && !g_SdFileSys.exists(newMetaName)) {
            bool okMeta = g_SdFileSys.rename(oldMetaName, newMetaName);
            if (okMeta) {
                bool okCsv = g_SdFileSys.rename(oldCsvName, newCsvName);
                if (!okCsv) {
                    // Csv rename failed; try to roll back the meta rename
                    // so we don't leave an orphaned pair.
                    g_SdFileSys.rename(newMetaName, oldMetaName);
                    g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
                                  "Log csv rename failed; meta rolled back");
                } else {
                    bRenamedTrio = true;
                }
            } else {
                g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
                              "Log meta rename failed; skipping csv rename");
            }
        }

        // Rename the .dbg file to match — only if both CSV and meta
        // succeeded. Otherwise we'd leave a dated .dbg next to an
        // undated .csv/.meta pair, which is worse than no rename.
        // Caller already holds xWriteMutex for SD serialisation.
        if (bRenamedTrio)
            g_DebugLog.RenameWithPrefix(datePrefix);
    }

    m_szBaseName[0] = '\0';
}

// ----------------------------------------------------------------------------

// Generate a formatted line of sensor data and send it to the ring queue

void LogSensor::Write()
{
    // g_bPause is held by HandleDownload and the bulk-delete handlers,
    // which sit on xWriteMutex for many seconds; pausing the producer
    // there prevents the ring from overflowing during that window.
    // Each dropped sample bumps s_uPausedDropCount so the loss is
    // visible in PERF telemetry instead of silently disappearing.
    if (g_bPause)
        {
        __atomic_fetch_add(&s_uPausedDropCount, 1u, __ATOMIC_RELAXED);
        return;
        }

    if (!g_Config.bSdLogging)
        return;

    // --- Snapshot sensor state into a LogRow ---
    // Snapshot both timestamps back-to-back so they refer to the same
    // sample instant (offset is a few ns). esp_timer_get_time() returns
    // 64-bit µs since boot — no rollover at flight timescales (vs
    // micros() which wraps every ~71 min).
    unsigned long uTimeStamp   = millis();
    // esp_timer_get_time() returns int64_t but is monotonic-from-boot
    // (always non-negative); cast to uint64_t is safe and matches the
    // CSV column type.
    uint64_t      uTimeStampUs = static_cast<uint64_t>(esp_timer_get_time());

    // Read the AHRS output fields as one coherent frame from the
    // lock-free snapshot (published once per AHRS::Process() iteration).
    // Reading them all from a single snapshot keeps the row's AHRS fields
    // from splitting across two AHRS iterations — the coherence that
    // closed issue #520, now without taking xAhrsMutex on the writer's
    // critical path. read() is wait-free (~150 ns) and never blocks
    // AHRS::Process().
    const onspeed::ahrs::AhrsSnapshotPayload ahrsSnap =
        onspeed::ahrs::g_AhrsSnapshot.read();
    // Coherent derived-sensor frame. LogSensor::Write runs on both
    // SensorReadTask (same task as the writer) and ImuReadTask (cross-task,
    // 208 Hz); reading the snapshot gives the ImuReadTask path a coherent
    // sensor row instead of unguarded g_Sensors fields from mid-update.
    const onspeed::ahrs::SensorSnapshotPayload sensSnap =
        onspeed::ahrs::g_SensorSnapshot.read();
    // Coherent raw IMU frame. On the 50 Hz log path (SensorReadTask) this is
    // a cross-task read of IMU data written by ImuReadTask, so the snapshot
    // keeps an accel/gyro triplet from tearing across an IMU update; on the
    // 208 Hz path (ImuReadTask) it returns the frame this task just published.
    const onspeed::ImuSample imuSnap =
        onspeed::ahrs::g_ImuSnapshot.read();
    const float ahrsTasMps          = ahrsSnap.tasMps;
    const float ahrsPitchDeg        = ahrsSnap.pitchDeg;
    const float ahrsRollDeg         = ahrsSnap.rollDeg;
    const float ahrsEarthVertG      = ahrsSnap.earthVertG;
    const float ahrsFlightPathDeg   = ahrsSnap.flightPathDeg;
    const float ahrsKalmanVsiMps    = ahrsSnap.kalmanVsiMps;
    const float ahrsKalmanAltMeters = ahrsSnap.kalmanAltMeters;
    const float ahrsDerivedAoaDeg   = ahrsSnap.derivedAoaDeg;

    onspeed::LogRow row;
    row.boomEnabled = g_Config.bReadBoom;
    row.efisEnabled = g_Config.bReadEfisData;
    row.efisIsVn300 = g_Config.bReadEfisData &&
                      (g_EfisSerial.enType == EfisSerialPort::EnVN300);
    row.flapsRawAdcPresent = true;

    row.timeStampMs      = (uint32_t)uTimeStamp;
    row.timeStampUs      = uTimeStampUs;
    row.pfwdCounts       = sensSnap.iPfwd;
    row.pfwdSmoothed     = sensSnap.pfwdSmoothed;
    row.p45Counts        = sensSnap.iP45;
    row.p45Smoothed      = sensSnap.p45Smoothed;
    row.pStaticMbar      = sensSnap.pStaticMbar;
    row.paltFt           = sensSnap.paltFt;
    row.iasKt            = sensSnap.iasKt;
    row.angleOfAttackDeg = sensSnap.aoaDeg;
    // CSV cells for IAS / AngleofAttack / DerivedAOA / efisPercentLift go
    // empty when the sensor-level air-data gate is closed (matches the
    // M5 wire / WebSocket JSON convention from PR #431).
    row.iasValid              = sensSnap.bIasAlive;
    row.efisPercentLiftValid  = sensSnap.bIasAlive;
    row.flapsPos         = g_Flaps.iPosition;
    row.flapsRawAdc      = g_Flaps.uValue;
    row.dataMark         = g_iDataMark;
    row.oatCelsius       = sensSnap.oatC;
    row.tasKt            = mps2kts(ahrsTasMps);

    // IMU — imuPitchRateDps holds the raw (un-negated) gyro value.
    // FormatRow applies the sign flip when writing the PitchRate column (issue #182).
    row.imuTempCelsius   = imuSnap.tempCelsius;
    row.imuVerticalG     = imuSnap.accelZG;
    row.imuLateralG      = imuSnap.accelYG;
    row.imuForwardG      = imuSnap.accelXG;
    row.imuRollRateDps   = imuSnap.gyroRollDps;
    row.imuPitchRateDps  = imuSnap.gyroPitchDps;   // un-negated; FormatRow emits -imuPitchRateDps
    row.imuYawRateDps    = imuSnap.gyroYawDps;
    row.pitchDeg         = ahrsPitchDeg;
    row.rollDeg          = ahrsRollDeg;

    if (g_Config.bReadBoom)
        {
        // Atomic snapshot — Static / Dynamic / Alpha / Beta / IAS all
        // come from the SAME decoded frame.  Prevents the prior torn-
        // read pattern where field-by-field assignment could mix
        // values from two adjacent frames if BoomRead preempted us
        // mid-block.  uTimestamp is a single aligned 32-bit word so
        // its read is atomic without the mutex.
        BoomSerialIO::SuBoomData boom;
        g_BoomSerial.Snapshot(boom);
        const int boomAge  = (int)((unsigned long)millis() - g_BoomSerial.uTimestamp);
        row.boomStatic     = boom.Static;
        row.boomDynamic    = boom.Dynamic;
        row.boomAlpha      = boom.Alpha;
        row.boomBeta       = boom.Beta;
        row.boomIasKt      = boom.IAS;
        row.boomAgeMs      = boomAge;
        }

    if (g_Config.bReadEfisData)
        {
        int efisAge = (int)((unsigned long)millis() - g_EfisSerial.uTimestamp);
        if (g_EfisSerial.enType == EfisSerialPort::EnVN300)
            {
            // Snapshot the published VN-300 data atomically.  All field
            // reads below are from the local copy — no torn-struct risk
            // even if EfisReadTask preempts us mid-block.
            EfisSerialPort::SuVN300Data vn;
            g_EfisSerial.SnapshotVn300(vn);
            row.vnAngularRateRoll  = vn.AngularRateRoll;
            row.vnAngularRatePitch = vn.AngularRatePitch;
            row.vnAngularRateYaw   = vn.AngularRateYaw;
            row.vnVelNedNorth      = vn.VelNedNorth;
            row.vnVelNedEast       = vn.VelNedEast;
            row.vnVelNedDown       = vn.VelNedDown;
            row.vnAccelFwd         = vn.AccelFwd;
            row.vnAccelLat         = vn.AccelLat;
            row.vnAccelVert        = vn.AccelVert;
            row.vnYawDeg           = vn.Yaw;
            row.vnPitchDeg         = vn.Pitch;
            row.vnRollDeg          = vn.Roll;
            row.vnLinAccFwd        = vn.LinAccFwd;
            row.vnLinAccLat        = vn.LinAccLat;
            row.vnLinAccVert       = vn.LinAccVert;
            row.vnYawSigma         = vn.YawSigma;
            row.vnRollSigma        = vn.RollSigma;
            row.vnPitchSigma       = vn.PitchSigma;
            row.vnGnssVelNedNorth  = vn.GnssVelNedNorth;
            row.vnGnssVelNedEast   = vn.GnssVelNedEast;
            row.vnGnssVelNedDown   = vn.GnssVelNedDown;
            row.vnWindSpd          = vn.WindSpd;
            row.vnWindDir          = vn.WindDir;
            row.vnWindVertical     = vn.WindVertical;
            row.vnGnssLat          = vn.GnssLat;
            row.vnGnssLon          = vn.GnssLon;
            row.vnEstAltFt         = m2ft(static_cast<float>(vn.EstAltMeters));
            row.vnGpsFix           = vn.GPSFix;
            row.vnDataAgeMs        = efisAge;
            row.vnTimeStartupNs    = vn.TimeStartupNs;
            row.vnTimeGpsNs        = vn.TimeGpsNs;
            row.vnTimeStatus       = vn.TimeStatus;
            }
        else
            {
            // Snapshot the published normalised EFIS data atomically.
            EfisSerialPort::SuEfisData ef;
            g_EfisSerial.SnapshotEfis(ef);
            row.efisIasKt         = ef.IAS;
            row.efisPitchDeg      = ef.Pitch;
            row.efisRollDeg       = ef.Roll;
            row.efisLateralG      = ef.LateralG;
            row.efisVerticalG     = ef.VerticalG;
            row.efisPercentLift   = ef.PercentLift;
            row.efisPaltFt        = ef.Palt;
            row.efisVsiFpm        = ef.VSI;
            row.efisTasKt         = ef.TAS;
            row.efisOatCelsius    = ef.OAT;
            row.efisFuelRemaining = ef.FuelRemaining;
            row.efisFuelFlow      = ef.FuelFlow;
            row.efisMap           = ef.MAP;
            row.efisRpm           = ef.RPM;
            row.efisPercentPower  = ef.PercentPower;
            row.efisMagHeading    = ef.Heading;
            row.efisAgeMs         = efisAge;
            row.efisTimestampMs   = (uint32_t)g_EfisSerial.uTimestamp;
            }
        }

    row.earthVerticalG = ahrsEarthVertG;
    row.flightPathDeg  = ahrsFlightPathDeg;
    row.vsiFpm         = mps2fpm(ahrsKalmanVsiMps);
    row.altitudeFt     = m2ft(ahrsKalmanAltMeters);
    row.derivedAoaDeg  = ahrsDerivedAoaDeg;
    row.coeffP         = g_fCoeffP;

    // Sidecar accumulator: feed time-of-day if available. The VN-300
    // wire-format change (issue #637) dropped the GNSS1.UTC string in
    // favor of per-sample ns timestamps (vnTimeGpsNs); the sidecar's
    // ISO-8601 first-time field can be reconstructed offline from
    // vnTimeGpsNs when dateOk is set.
    {
        const char* hmsOrNull = nullptr;
        // Snapshot under mutex into a local buffer that outlives the
        // m_metaBuilder.OnRow call below.  szTime is a fixed-size char
        // array embedded in SuEfisData; copying the local out keeps the
        // pointer valid for the OnRow call.
        EfisSerialPort::SuEfisData efSnap;
        if (g_Config.bReadEfisData) {
            g_EfisSerial.SnapshotEfis(efSnap);
            if (efSnap.szTime[0] != '\0')
                hmsOrNull = efSnap.szTime;
        }
        m_metaBuilder.OnRow(row, hmsOrNull, nullptr);
    }

    // Send the LogRow struct (not the formatted CSV) to the ring.
    // The consumer task (LogSensorCommitTask on Core 0) runs FormatRow
    // and writes to SD.  Issue #608: this moves ~150 µs of CSV
    // formatting per row off the IMU/Sensors tasks on Core 1.
    //
    // Defense-in-depth: Write() is only called from SensorReadTask and
    // ImuReadTask, both pinned to Core 1 and created after setup()
    // allocates the ring buffer, so a null handle here means a
    // regression. Warn (rate-limited) and drop the row; never mutate
    // g_Config.bSdLogging — see the matching note in Open() above.
    if (xLoggingRingBuffer == nullptr)
        {
        static unsigned long uLastWarnMs = 0;
        unsigned long uNow = millis();
        if ((uNow - uLastWarnMs) > 2000)
            {
            g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Logging ring buffer not allocated; dropping log row");
            uLastWarnMs = uNow;
            }
        return;
        }

    // sizeof(LogRow) is on the order of 700 B (vs ~250 B for a typical
    // formatted CSV line).  At 208 Hz this is ~145 KB/s of producer
    // traffic, well under the 256 KB ring's capacity even allowing for
    // SD write pauses.
    bool bSendOK = xRingbufferSend(xLoggingRingBuffer, &row, sizeof(row), 0);
    if (!bSendOK)
        __atomic_fetch_add(&s_uRingDropCount, 1u, __ATOMIC_RELAXED);

} // end LogSensor::Write()
