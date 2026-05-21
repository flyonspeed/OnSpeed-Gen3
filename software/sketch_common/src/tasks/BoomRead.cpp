// BoomRead.cpp — dedicated boom-probe UART task.
//
// Why not the IDF event-queue path that EfisRead uses
// ===================================================
// Serial1 is shared:  RX from the boom probe (Stream consumer:
// g_BoomSerial), TX to the M5/huVVer display (Stream consumer:
// g_DisplaySerial).  Arduino's HardwareSerial layer manages both
// pins in a single Serial1.begin() call.
//
// IDF's uart_driver_install takes EXCLUSIVE ownership of the UART.
// If we install the IDF driver on UART_NUM_1 for boom RX, Arduino's
// HardwareSerial layer breaks for display TX. We'd have to teach
// DisplaySerial to write via uart_write_bytes(), which is a much
// bigger PR.
//
// What we get instead
// ===================
// - Bytes still drain off loop() into a dedicated task; the busy-poll
//   that was a problem on the lowest-priority loop task moves to a
//   priority-3 task on Core 0, away from anything flight-critical.
// - PERF gets clean per-task attribution (task=BoomRead in `perf dump`).
// - The boom protocol is forgiving: ~50 B at 50 Hz, well within the
//   128 B HW FIFO. 1 ms polling never causes data loss; even 10 ms
//   would be fine in practice. We're not chasing latency here, just
//   architectural cleanup.
//
// Synth-build behaviour
// =====================
// In perf-synth builds, g_BoomSerial.pSerial is wired to a SyntheticStream,
// NOT to Serial1. The polling task body is the same either way — both
// SyntheticStream and Serial1 expose Stream::available()/read(). The
// only difference is what's behind the pSerial pointer; the task does
// not care.

#include "BoomRead.h"

#include "src/Globals.h"

#include <util/Perf.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void BoomReadTask(void *pvParams)
{
    (void)pvParams;

    // 5 ms tick. Boom rate is 50 Hz (~20 ms between frames) so this
    // checks the buffer ~4× per inter-frame interval — plenty of
    // headroom before the 128-byte HW FIFO would risk overflow at a
    // 30-byte protocol. 1 ms polling overran the PerfLoop ring buffer
    // (1000 events/sec vs 1024-entry ring); 5 ms keeps us well clear.
    constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(5);
    TickType_t xLastWake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&xLastWake, kPollPeriod);

        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::BoomRead,
            uxTaskGetStackHighWaterMark(nullptr));
        {
            onspeed::util::perf::PerfScope scope(
                onspeed::util::perf::ScopeId::BoomRead);
            g_BoomSerial.Read();
        }
    }
}
