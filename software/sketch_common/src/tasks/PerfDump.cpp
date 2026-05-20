// PerfDump.cpp — drains the Perf rings, aggregates into histograms,
// emits a per-second summary over USB serial.
//
// The 1 Hz cadence is chosen to match the cost-amortisation budget for
// percentile reconstruction. Histograms accumulate across the interval;
// at emit time we walk each bucket array once to compute p50/p95/p99.
// All emission goes through Serial.print — no malloc, no String objects.

#ifdef ONSPEED_PERF_ENABLED

#include "PerfDump.h"

#include "src/Globals.h"

#include <Arduino.h>
#include <atomic>
#include <cstdio>

#include <util/Perf.h>

namespace onspeed::perf_dump {

using namespace onspeed::util::perf;

namespace {

std::atomic<bool>     g_taskStarted{false};
std::atomic<bool>     g_streaming{false};
std::atomic<bool>     g_oneShotPending{false};
TaskHandle_t          g_taskHandle = nullptr;

// Permanent consumer instance. Its histograms live in Perf.cpp's
// file-static arrays, so we just reuse it across calls.
Consumer g_consumer;

const char* spiScopeNames[] = {
    "spi.imu", "spi.aoa", "spi.pitot", "spi.static", "spi.sd",
};
const ScopeId spiScopeIds[] = {
    ScopeId::SpiImu, ScopeId::SpiAoa, ScopeId::SpiPitot,
    ScopeId::SpiStatic, ScopeId::SpiSd,
};

constexpr ScopeId kTimingScopes[] = {
    ScopeId::EkfqPredict, ScopeId::EkfqCorrect, ScopeId::EkfqAlpha,
    ScopeId::Madgwick,    ScopeId::Kalman,      ScopeId::TasCompute,
    ScopeId::ImuRead,     ScopeId::PressureRead,
    ScopeId::DisplaySerial, ScopeId::WebSocketFrame,
    ScopeId::LogWrite,    ScopeId::LogSync,
    ScopeId::EfisRead,    ScopeId::BoomRead,
};

void emitSnapshot()
{
    char buf[256];

    Serial.println(F("==== PERF ===="));

    // Per-task loop stats.
    for (size_t i = 0; i < kTaskCount; ++i) {
        const auto tid = static_cast<TaskId>(i);
        const Histogram& h = g_consumer.taskHistogram(tid);
        if (h.count == 0) continue;
        const uint32_t avg = static_cast<uint32_t>(h.sumUs / h.count);
        std::snprintf(buf, sizeof(buf),
            "task=%-12s loops=%4llu p50=%6uus p95=%6uus p99=%6uus max=%6uus avg=%6uus stack_free=%5uw drops=%u",
            taskName(tid),
            (unsigned long long)h.count,
            (unsigned)h.percentile(0.50),
            (unsigned)h.percentile(0.95),
            (unsigned)h.percentile(0.99),
            (unsigned)h.maxUs,
            (unsigned)avg,
            (unsigned)g_consumer.taskStackHighWater(tid),
            (unsigned)g_consumer.taskDrops(tid));
        Serial.println(buf);
    }

    // Per-subsystem timing histograms.
    for (auto sid : kTimingScopes) {
        const Histogram& h = g_consumer.scopeHistogram(sid);
        if (h.count == 0) continue;
        const uint64_t totalUs = h.sumUs;
        std::snprintf(buf, sizeof(buf),
            "subsys.%-14s n=%5llu total=%6lluus p50=%4uus p95=%5uus p99=%5uus max=%5uus",
            scopeName(sid),
            (unsigned long long)h.count,
            (unsigned long long)totalUs,
            (unsigned)h.percentile(0.50),
            (unsigned)h.percentile(0.95),
            (unsigned)h.percentile(0.99),
            (unsigned)h.maxUs);
        Serial.println(buf);
    }

    // SPI counters.
    for (size_t i = 0; i < sizeof(spiScopeIds)/sizeof(spiScopeIds[0]); ++i) {
        const SpiCounter& s = g_consumer.spiCounter(spiScopeIds[i]);
        if (s.transfers == 0) continue;
        std::snprintf(buf, sizeof(buf),
            "%-12s bytes=%6llu xfers=%5llu max_xfer=%4uus",
            spiScopeNames[i],
            (unsigned long long)s.bytes,
            (unsigned long long)s.transfers,
            (unsigned)s.maxXferUs);
        Serial.println(buf);
    }

    // System.
    std::snprintf(buf, sizeof(buf),
        "heap free=%lu  min=%lu  largest_block=%lu",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    Serial.println(buf);
}

void DumpTask(void* /*pv*/)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        // Wake every 1000 ms regardless of streaming state. We must
        // always drain the rings so producers don't fill them up while
        // streaming is off.
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));

        // Drain rings → histograms.
        g_consumer.drainAll();

        const bool stream = g_streaming.load(std::memory_order_acquire);
        const bool oneShot = g_oneShotPending.exchange(false, std::memory_order_acq_rel);

        if (stream || oneShot) {
            emitSnapshot();
        }

        // Reset histograms for the next interval.
        g_consumer.reset();
    }
}

}  // namespace

void StartTask()
{
    bool already = g_taskStarted.exchange(true, std::memory_order_acq_rel);
    if (already) return;
    // Pinned to Core 0 (alongside WebServer/DataServer/Log), low priority,
    // 4 KB stack — enough for the printf scratch + Consumer drain.
    xTaskCreatePinnedToCore(
        DumpTask, "PerfDump", 4096, nullptr, /*pri=*/0, &g_taskHandle, /*core=*/0);
}

void SetStreaming(bool on)
{
    g_streaming.store(on, std::memory_order_release);
    // Streaming requires the registry itself to be enabled — flip
    // both together for the obvious caller experience.
    setPerfEnabled(on);
    // Make sure the dump task is running so toggling on actually
    // produces output.
    if (on) StartTask();
}

bool IsStreaming()
{
    return g_streaming.load(std::memory_order_acquire);
}

void EmitOneShot()
{
    // Ensure registry is collecting; one-shot only makes sense if
    // events are being recorded.
    setPerfEnabled(true);
    StartTask();
    g_oneShotPending.store(true, std::memory_order_release);
}

}  // namespace onspeed::perf_dump

#endif  // ONSPEED_PERF_ENABLED
