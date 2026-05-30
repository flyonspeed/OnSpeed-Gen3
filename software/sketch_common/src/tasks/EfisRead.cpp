// EfisRead.cpp — dedicated EFIS UART task.
//
// Architecture
// ============
// Before PR #609, EFIS bytes were drained from Arduino's loop():
//
//   void loop() {
//       g_ConsoleSerial.Read();
//       g_EfisSerial.Read();        // ← busy-polled Serial2.available()
//       g_BoomSerial.Read();
//       vTaskDelay(10ms);
//   }
//
// Three problems:
//   1. loop() runs at the lowest priority on Core 1 and busy-polls three
//      UARTs per iteration. Most calls find nothing.
//   2. A 127-byte VN-300 frame at 115200 baud takes ~11 ms to arrive. If
//      loop() is preempted mid-frame, bytes accumulate in the 128-byte
//      hardware FIFO; past that, bytes are silently dropped.
//   3. EFIS frames are bursty (127-byte burst then silence). Polling is
//      the wrong shape; the right shape is interrupt-driven.
//
// EfisReadTask replaces the loop()-driven EFIS read with an IDF UART
// event-queue driven task on Core 0:
//
//   void EfisReadTask(void*) {
//       // setup() already called IdfUartStream::Begin which installed
//       // the IDF UART driver and gave us back the event queue.
//       QueueHandle_t q = g_EfisUartStream.GetEventQueue();
//       for (;;) {
//           uart_event_t ev;
//           xQueueReceive(q, &ev, portMAX_DELAY);   // wakes on data
//           // (open PerfLoop AFTER the wake — measures work, not sleep)
//           PerfLoop guard(TaskId::EfisRead, ...);
//           switch (ev.type) {
//               case UART_DATA:        g_EfisSerial.Read(); break;
//               case UART_FIFO_OVF:    // log + flush
//               case UART_BUFFER_FULL: // log + flush
//               default:               // ignore
//           }
//       }
//   }
//
// Wins:
//   - No more silent HW-FIFO drops; the 2 KB software RX buffer absorbs
//     bursts and the task wakes within ~ISR latency of bytes arriving.
//   - PERF telemetry for EFIS becomes a real measurement (task=EfisRead
//     with its own loops/s + work µs/loop, not buried in ArduinoLoop).
//   - loop() shrinks to just ConsoleSerial.Read() — interactive, low rate.
//
// Synth-build behaviour
// =====================
// In perf-synth builds (ONSPEED_SYNTH_SENSORS=1), the EFIS byte source is
// a SyntheticStream, NOT a real UART. setup() does not install the IDF
// UART driver in that case; the SyntheticStream is wired into
// g_EfisSerial.pSerial directly. EfisReadTask then has no event queue to
// block on, so it falls back to a fixed-period vTaskDelay tick at the
// synth's emission cadence (~50 Hz for VN-300). Same task, same Read()
// callsite, different wake primitive.

#include "EfisRead.h"

#include "src/Globals.h"
#include "src/io/IdfUartStream.h"

#include <efis/Vn300.h>
#include <util/Perf.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/uart.h>

#ifndef ONSPEED_SYNTH_SENSORS
// Real-hardware build: the EFIS UART stream is owned here.  setup() in
// the .ino calls EfisReadTaskInit() to install the IDF driver before
// the task is spawned, then xTaskCreatePinnedToCore(EfisReadTask, ...).
//
// We keep the IdfUartStream as a file-static so its lifetime spans the
// whole program (the IDF driver it owns must outlive every task that
// uses it). EfisSerialPort::pSerial points at this stream.
static IdfUartStream s_efisUartStream;

bool EfisReadTaskInit(uint32_t baud) {
    const bool ok = s_efisUartStream.Begin(
        UART_NUM_2, kEfisRx, kEfisTx,
        baud,
        /*rxBufferSize=*/2048,
        /*queueLength=*/16);
    if (!ok) {
        g_Log.println(MsgLog::EnEfis, MsgLog::EnError,
                      "EfisReadTaskInit: uart_driver_install failed");
    }
    return ok;
}

Stream* GetEfisStream() { return &s_efisUartStream; }
#endif

namespace {

// Rate-limited warning logger so a stuck FIFO-overflow condition doesn't
// flood the log.  Same shape as ImuReadTask's late-warning suppression.
void LogRateLimited(MsgLog::EnLevel sev, const char* msg) {
    static unsigned long uLastMs = 0;
    const unsigned long now = millis();
    if (now - uLastMs < 1000) return;
    uLastMs = now;
    g_Log.println(MsgLog::EnEfis, sev, msg);
}

}  // namespace

void EfisReadTask(void *pvParams)
{
    (void)pvParams;

#ifdef ONSPEED_SYNTH_SENSORS
    // Synth path: SyntheticStream's esp_timer calls
    // xTaskNotifyGive(this task) every periodUs.  We block on
    // ulTaskNotifyTake, mirroring the real-hardware IDF event-queue
    // wake-on-data shape below.  Loops/s on the PERF report reads
    // as the actual frame rate.
    //
    // The 1-second timeout is a watchdog against SyntheticStream's
    // esp_timer failing to start (resource exhaustion at boot, etc.).
    // On a healthy box the next emit notify always arrives within
    // periodUs (<= 20 ms at every supported rate) and the wait
    // short-circuits.  Without the timeout, a Start() failure would
    // leave this task blocked forever consuming no CPU but also
    // serving no data.
    constexpr TickType_t kSynthWatchdog = pdMS_TO_TICKS(1000);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, kSynthWatchdog);

        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::EfisRead,
            uxTaskGetStackHighWaterMark(nullptr));
        {
            onspeed::util::perf::PerfScope scope(
                onspeed::util::perf::ScopeId::EfisRead);
            g_EfisSerial.Read();
        }
    }
#else
    // Real-hardware path.  Two sub-modes:
    //   1. IDF driver installed (normal): block on its event queue,
    //      wake-on-data.
    //   2. IDF install failed (.ino's setup() fell back to Serial2.begin):
    //      no queue exists, so poll g_EfisSerial.pSerial at 1 ms tick
    //      same shape as BoomRead.  Keeps the box usable on driver
    //      failure rather than going silent.
    QueueHandle_t queue = s_efisUartStream.GetEventQueue();
    if (queue == nullptr) {
        g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning,
                      "EfisReadTask: no IDF queue; falling back to 1 ms polling");
        constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(1);
        TickType_t xLastWake = xTaskGetTickCount();
        for (;;) {
            vTaskDelayUntil(&xLastWake, kPollPeriod);
            onspeed::util::perf::PerfLoop perfGuard(
                onspeed::util::perf::TaskId::EfisRead,
                uxTaskGetStackHighWaterMark(nullptr));
            onspeed::util::perf::PerfScope scope(
                onspeed::util::perf::ScopeId::EfisRead);
            g_EfisSerial.Read();
        }
        return;  // unreachable; explicit so adding a `break` above can't
                 // silently fall through to the queue-based path that
                 // would then dereference a null queue.
    }

    // Bring-up diagnostics: emit a one-line VN-300 parser-state dump every
    // `kDiagPeriodMs` milliseconds whenever the EFIS type is VN-300. Tells us
    // whether bytes are arriving at all, whether sync is being found, and
    // where in the pipeline frames are getting rejected. Cheap (one struct
    // copy + a few atomics + one printf per period); off-path for production
    // because the typical period is 5s.
    constexpr uint32_t kDiagPeriodMs = 5000;
    unsigned long uLastDiagMs = 0;

    uart_event_t event;
    for (;;) {
        // portMAX_DELAY — no timeout; wake on any event.
        if (xQueueReceive(queue, &event, portMAX_DELAY) != pdTRUE) continue;

        // Open the PerfLoop AFTER the wake so the scope measures work,
        // not sleep.  PerfLoop must be inside the loop body for the
        // same reason — putting it outside would record one giant
        // wake-to-wake span as a "loop iteration".
        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::EfisRead,
            uxTaskGetStackHighWaterMark(nullptr));

        switch (event.type) {
            case UART_DATA: {
                // If EFIS read is disabled in config, drain the IDF
                // buffer ourselves so it doesn't fill and start
                // generating UART_BUFFER_FULL events forever.
                // Pre-PR the same path silently dropped bytes at the
                // HW FIFO layer; we keep the same observable behaviour
                // (data discarded) without the spam loop.
                if (!g_Config.bReadEfisData) {
                    uart_flush_input(UART_NUM_2);
                    break;
                }
                onspeed::util::perf::PerfScope scope(
                    onspeed::util::perf::ScopeId::EfisRead);
                g_EfisSerial.Read();
                break;
            }
            case UART_FIFO_OVF:
                // HW FIFO overflowed before the IDF driver drained it.
                // Means we (or a higher-priority task) starved the
                // driver for > 128 bytes / 115200 baud = ~11 ms.
                // Reset the queue so we don't process stale events.
                LogRateLimited(MsgLog::EnError, "EFIS UART FIFO overflow");
                uart_flush_input(UART_NUM_2);
                xQueueReset(queue);
                break;
            case UART_BUFFER_FULL:
                // Software RX buffer (2 KB) filled.  Same shape of
                // problem as FIFO_OVF, just one buffer layer up.
                LogRateLimited(MsgLog::EnError, "EFIS UART RX buffer full");
                uart_flush_input(UART_NUM_2);
                xQueueReset(queue);
                break;
            case UART_BREAK:
            case UART_PARITY_ERR:
            case UART_FRAME_ERR:
                // EnDebug, not EnWarning: a floating RX pin on a no-EFIS
                // production install picks up enough electrical noise to
                // trigger occasional false frames, and even rate-limited
                // warnings every 1 s would be logspam.  Pilots with
                // intermittent EFIS issues can flip the EnEfis module to
                // debug level and see these.
                LogRateLimited(MsgLog::EnDebug, "EFIS UART frame error");
                break;
            case UART_PATTERN_DET:
            case UART_DATA_BREAK:
            case UART_EVENT_MAX:
            default:
                // Other event types not used by our config; ignore.
                break;
        }

        // Periodic diagnostic emit for VN-300 bring-up.
        // Reads the parser's per-stage counters and the live IDF buffer
        // depth. Emitted via the EnEfis module at EnDebug, so flipping
        // `msg debug efis` on the console turns this on and off. The line
        // shape is intentionally one short token-string so it's easy to
        // grep, parse, or pipe through awk.
        if (g_EfisSerial.enType == EfisSerialPort::EnVN300) {
            const unsigned long uNowMs = millis();
            if (uNowMs - uLastDiagMs >= kDiagPeriodMs) {
                uLastDiagMs = uNowMs;
                const auto& d = g_EfisSerial.Vn300Diag();
                // Live IDF software RX buffer depth — tells us if the
                // driver is delivering bytes (or if the wire is silent).
                size_t bufDepth = 0;
                uart_get_buffered_data_len(UART_NUM_2, &bufDepth);
                // Compose the first-byte-by-nibble histogram as a compact
                // 16-hex-counter string so we can see if 0xFA (nibble 0xF)
                // dominates (good — frame starts) or if it's uniform (bad
                // — noise / wrong polarity).
                char nibStr[16 * 9 + 1] = {};
                int pos = 0;
                for (int i = 0; i < 16; ++i) {
                    pos += snprintf(nibStr + pos, sizeof(nibStr) - pos,
                                    "%x=%lu%s", i,
                                    (unsigned long)d.firstByteByNibble[i],
                                    (i == 15 ? "" : ","));
                }
                g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug,
                             "VN300 DIAG bytes=%llu sync1=%lu sync2=%lu "
                             "hdrOk=%lu hdrFail=%lu crcOk=%lu crcFail=%lu "
                             "idfBuf=%lu nib=[%s]",
                             (unsigned long long)d.bytesFed,
                             (unsigned long)d.sync1,
                             (unsigned long)d.sync2,
                             (unsigned long)d.headerOk,
                             (unsigned long)d.headerFail,
                             (unsigned long)d.crcOk,
                             (unsigned long)d.crcFail,
                             (unsigned long)bufDepth, nibStr);
            }
        }
    }
#endif
}
