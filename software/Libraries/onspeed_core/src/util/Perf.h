// util/Perf.h — Lightweight scoped profiler for hot embedded paths.
//
// =================================================================
// STANDARD PATTERN FOR INSTRUMENTING A FREERTOS TASK
// =================================================================
//
//   void MyTask(void *pvParams) {
//       // 1. one-time init (NOT inside the loop body)
//       ...
//       for (;;) {
//           // 2. SLEEP / WAIT first.
//           //    This is where the task is blocked waiting for its
//           //    next tick. vTaskDelay, vTaskDelayUntil, ulTaskNotifyTake,
//           //    xQueueReceive — all sleep primitives go HERE, OUTSIDE
//           //    the PERF scope.
//           vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
//
//           // 3. PERF SCOPE — wraps ONLY the work that follows.
//           //    Its destructor records (work_end - work_start) into
//           //    the ring. This is what tells you CPU% and theoretical
//           //    headroom; the wake-to-wake period is fixed by the
//           //    schedule above and doesn't need measuring.
//           PerfLoop perfGuard(TaskId::Foo,
//                              uxTaskGetStackHighWaterMark(nullptr));
//
//           // 4. work
//           DoStuff();
//       }
//   }
//
// The invariant: **PerfLoop wraps work, never sleep.** If a task has
// early-continue error paths with their own sleeps, those sleeps also
// go OUTSIDE the perf scope (they're not work either).
//
// Why this matters: a 20 Hz task that sleeps 50 ms then does 200 µs of
// work, if the scope wraps both, reports "max=53ms" — the period, not
// the cost. You can't answer "how much CPU does this consume?" or
// "how fast can I go theoretically?" from period data.
//
// =================================================================
//
// Design (SPSC ring → consumer histogram):
//
//   Producer side (hot path, one per FreeRTOS task):
//     A PerfScope guard at scope entry captures the start timestamp.
//     The destructor pushes ONE {scopeId, durationUs} event into a
//     per-task SPSC ring. No accumulators on the hot path, no atomic
//     fetch_add, no CAS loops — just one relaxed-load + one
//     release-store. ~6–8 cycles on Xtensa LX7.
//
//   Consumer side (1 Hz, on a low-priority task):
//     Drains every producer ring, bins each event into an exponential
//     histogram keyed by ScopeId. Histograms accumulate inside the
//     consumer's snapshot window; at emit time we reconstruct p50,
//     p95, p99, plus count/sum/min/max. Reset histograms after emit.
//
//   Compile-out:
//     The classes are real only when ONSPEED_PERF_ENABLED is defined
//     (the new [env:esp32s3-v4p-perf] PIO env). Production builds get
//     empty inline classes that the compiler eliminates entirely.
//
// Why this shape:
//   - Tracy / Optick / FreeRTOS+Trace all do producer-side event
//     emission with consumer-side aggregation. Atomic counters on the
//     hot path don't scale.
//   - SPSC ring with relaxed load + release store on head is the
//     LMAX Disruptor / boost::lockfree::spsc_queue pattern.
//   - Exponential histograms (HdrHistogram, Prometheus, OpenTelemetry
//     ExponentialBucketHistogram) preserve dynamic range — necessary
//     here because IMU scopes are ~1–10 μs and task loops are 5–60 ms.
//
// Time source:
//   esp_timer_get_time() on ESP-IDF (1 μs monotonic). On native
//   builds, std::chrono::steady_clock. Hidden behind nowUs().
//
// Memory:
//   12 producer rings × 8 KB = 96 KB internal RAM. Histograms in the
//   consumer's static buffer: 32 buckets × scope+task count × 4 B ≈
//   8 KB. All in .bss, no heap.

#ifndef ONSPEED_CORE_UTIL_PERF_H
#define ONSPEED_CORE_UTIL_PERF_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace onspeed::util::perf {

// ===========================================================================
// Scope and task identifiers — add new entries before Count.
// ===========================================================================

enum class ScopeId : uint8_t {
    EkfqPredict = 0,
    EkfqCorrect,
    EkfqAlpha,
    Madgwick,
    Kalman,
    TasCompute,
    ImuRead,
    PressureRead,
    SpiImu,
    SpiAoa,
    SpiPitot,
    SpiStatic,
    SpiSd,
    DisplaySerial,
    WebSocketFrame,
    LogWrite,    ///< SD write — `m_hLogFile.write(buf, aligned)` only.
    LogSync,     ///< SD fsync — `m_hLogFile.sync()` only. Periodic, ~200ms.
    EfisRead,    ///< g_EfisSerial.Read() — UART drain + parser + CRC + apply.
    BoomRead,    ///< g_BoomSerial.Read() — UART drain + ASCII parse.
    Spare0, Spare1, Spare2, Spare3, Spare4,
    Count,
};

enum class TaskId : uint8_t {
    Imu = 0,
    Sensors,
    Audio,
    Display,
    Switch,
    Log,
    LogReplay,
    TestPot,
    RangeSweep,
    Housekeeping,
    WebServer,
    DataServer,
    ArduinoLoop,    ///< Arduino's `loop()` task — drives g_EfisSerial.Read(),
                    ///< g_BoomSerial.Read(), g_ConsoleSerial.Read(). Spins
                    ///< at low priority on Core 1.
    Count,
};

constexpr size_t kScopeCount = static_cast<size_t>(ScopeId::Count);
constexpr size_t kTaskCount  = static_cast<size_t>(TaskId::Count);

// ===========================================================================
// Time source — exposed for tests.
// ===========================================================================
uint64_t nowUs();

#ifdef ONSPEED_PERF_ENABLED

// ===========================================================================
// Event format — 8 bytes packed.
// ===========================================================================
struct PerfEvent {
    uint32_t durationUs;   ///< Scope or loop duration. ≤ ~71 minutes.
    uint8_t  scopeId;      ///< Which ScopeId; or sentinel for loop events.
    uint8_t  flags;        ///< Bit 0: 1 = loop event (use stackHighWaterWords),
                           ///<        0 = scope event.
    uint16_t stackHighWaterWords;  ///< For loop events only.
};
static_assert(sizeof(PerfEvent) == 8, "PerfEvent must stay 8 bytes");

constexpr uint8_t kFlagLoop = 0x01;
constexpr uint8_t kLoopSentinelScopeId = 0xFF;  // unused as scope; marks loop

// ===========================================================================
// Ring — single producer, single consumer. 1024 entries = 8 KB.
//
// Power-of-two capacity so we can use bitmask wrap. head and tail are
// monotonic uint32_t (never wrap in practice within a multi-decade
// uptime); the bitmask handles physical index.
// ===========================================================================
// 1024 entries × 8 B = 8 KB per task ring. 12 task rings = 96 KB,
// fits in internal DRAM. SPI events route through the lock-free
// SpiCounter (NOT the ring), so the ring only carries scope timing
// + loop events — ~4/sec for slow tasks, ~830/sec for the IMU task at
// 208 Hz (PerfLoop + predict + correct + alpha = 4 events/step).
// Comfortable; ~20% headroom against the 1 Hz consumer drain.
//
// If we ever push IMU to 1.66 kHz, this needs to grow to 2048 — and
// then we'll need to either drop some other big DRAM consumer or
// move other instrumentation out of DRAM.
constexpr size_t kRingCapacity = 1024;
constexpr size_t kRingMask     = kRingCapacity - 1;
static_assert((kRingCapacity & kRingMask) == 0, "ring capacity must be 2^n");

struct Ring {
    PerfEvent             events[kRingCapacity];
    std::atomic<uint32_t> head{0};   ///< Producer-only write.
    std::atomic<uint32_t> tail{0};   ///< Consumer-only write.
    std::atomic<uint32_t> drops{0};  ///< Producer increments when full.
};

// Per-task producer rings are file-static in Perf.cpp. Producers find
// "their" ring via the TaskId passed to PerfLoop / a TLS lookup. The
// helpers below abstract that.
Ring* ringForTask(TaskId id);

// ===========================================================================
// Master enable. Default off. Console command toggles. When false,
// pushEvent() bails before touching the ring.
// ===========================================================================
bool perfEnabled();
void setPerfEnabled(bool e);

// ===========================================================================
// Bind the calling task's TaskHandle to the ring for `id`, without
// constructing a PerfLoop. Use this from tasks that have meaningful
// PerfScope events but no natural place for a PerfLoop — Arduino's
// loopTask is the canonical example (no explicit sleep means a
// PerfLoop period measurement is dominated by preemption and not
// meaningful).
//
// Idempotent. Safe to call repeatedly. Cheap on hot-path callers
// that already have the binding (returns after one table walk).
// ===========================================================================
void bindCurrentTaskToRing(TaskId id);

// ===========================================================================
// Producer-side push. Hot path; keep tiny.
// ===========================================================================
inline void pushEvent(Ring* r, const PerfEvent& ev) {
    const uint32_t h = r->head.load(std::memory_order_relaxed);
    const uint32_t t = r->tail.load(std::memory_order_acquire);
    if ((h - t) >= kRingCapacity) {
        r->drops.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    r->events[h & kRingMask] = ev;
    r->head.store(h + 1, std::memory_order_release);
}

// ===========================================================================
// PerfScope — RAII timer for a code region. Emits one scope event.
// ===========================================================================
class PerfScope {
public:
    explicit PerfScope(ScopeId id) noexcept
        : scopeId_(static_cast<uint8_t>(id)),
          ring_(perfEnabled() ? ringForCurrentTask() : nullptr),
          startUs_(ring_ ? nowUs() : 0) {}

    ~PerfScope() noexcept {
        if (ring_) {
            const uint64_t end = nowUs();
            const uint32_t dur = static_cast<uint32_t>(end - startUs_);
            pushEvent(ring_, PerfEvent{dur, scopeId_, /*flags=*/0u, 0u});
        }
    }

    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    static Ring* ringForCurrentTask();
    uint8_t  scopeId_;
    Ring*    ring_;
    uint64_t startUs_;
};

// ===========================================================================
// PerfLoop — RAII timer for one task-loop iteration. Emits one loop
// event with the stack high-water mark embedded.
// ===========================================================================
class PerfLoop {
public:
    PerfLoop(TaskId id, uint32_t stackHighWaterWords) noexcept
        : ring_(perfEnabled() ? ringForTask(id) : nullptr),
          stackHigh_(static_cast<uint16_t>(
              stackHighWaterWords > 0xFFFFu ? 0xFFFFu : stackHighWaterWords)),
          startUs_(ring_ ? nowUs() : 0) {
        if (ring_) bindRingToCurrentTask(ring_);
    }

    ~PerfLoop() noexcept {
        if (ring_) {
            const uint64_t end = nowUs();
            const uint32_t dur = static_cast<uint32_t>(end - startUs_);
            pushEvent(ring_, PerfEvent{
                dur, kLoopSentinelScopeId, kFlagLoop, stackHigh_});
        }
    }

    PerfLoop(const PerfLoop&) = delete;
    PerfLoop& operator=(const PerfLoop&) = delete;

private:
    static void bindRingToCurrentTask(Ring* r);
    Ring*    ring_;
    uint16_t stackHigh_;
    uint64_t startUs_;
};

// ===========================================================================
// SPI transfer recording — explicit (no RAII; called from driver post-xfer).
// scopeId must be one of SpiImu / SpiAoa / SpiPitot / SpiStatic / SpiSd.
// ===========================================================================
void recordSpiTransfer(ScopeId scopeId, uint32_t bytes, uint32_t durationUs);

// ===========================================================================
// Consumer side — histograms.
//
// Log-linear bucketing for honest sub-millisecond resolution:
//   buckets[0..63]   linear, 16 µs wide   →  covers 0..1024 µs
//   buckets[64..95]  exponential √2 steps →  covers 1 ms..64 ms
//
// Tight where it matters (EKFQ scopes are ~30..500 µs), loose where
// it doesn't (task loop work is 1..50 ms; coarse buckets fine).
// 96 buckets × 4 B = 384 B per histogram.
// ===========================================================================
constexpr size_t kLinearBuckets   = 64;   // 16 µs each → 0..1024 µs
constexpr size_t kLinearStepUs    = 16;
constexpr size_t kLinearMaxUs     = kLinearBuckets * kLinearStepUs;  // 1024
constexpr size_t kExpBuckets      = 32;   // √2 steps from 1024 µs
constexpr size_t kHistogramBuckets = kLinearBuckets + kExpBuckets;

struct Histogram {
    uint64_t count;
    uint64_t sumUs;
    uint32_t minUs;
    uint32_t maxUs;
    uint32_t buckets[kHistogramBuckets];

    void reset();
    void add(uint32_t valueUs);
    uint32_t percentile(double p) const;
};

// SPI counters are simpler — bytes/xfers/maxXferUs only, no histogram.
struct SpiCounter {
    uint64_t bytes;
    uint64_t transfers;
    uint32_t maxXferUs;
    void reset();
};

// ===========================================================================
// Consumer aggregator. Drains every producer ring, bins events into the
// internal Histogram array, exposes read-and-reset accessors for the
// dump task.
// ===========================================================================
class Consumer {
public:
    /// Drain all producer rings into the internal histogram array.
    /// Call once per snapshot interval, before reading percentiles.
    void drainAll();

    /// Per-scope timing histogram. Index with ScopeId.
    const Histogram& scopeHistogram(ScopeId id) const;
    /// Per-task loop-period histogram. Index with TaskId.
    const Histogram& taskHistogram(TaskId id) const;
    /// Per-task min stack-words-remaining observed in the interval.
    uint32_t taskStackHighWater(TaskId id) const;
    /// Per-task dropped-event count (producer ring overflowed).
    uint32_t taskDrops(TaskId id) const;
    /// SPI byte / xfer counters by ScopeId.
    const SpiCounter& spiCounter(ScopeId id) const;

    /// Reset all internal histograms / counters. Called after emit.
    void reset();
};

#else  // ONSPEED_PERF_ENABLED not defined — empty stubs.

class PerfScope {
public:
    explicit PerfScope(ScopeId) noexcept {}
};
class PerfLoop {
public:
    PerfLoop(TaskId, uint32_t) noexcept {}
};
inline void recordSpiTransfer(ScopeId, uint32_t, uint32_t) {}
inline bool perfEnabled() { return false; }
inline void setPerfEnabled(bool) {}
inline void bindCurrentTaskToRing(TaskId) {}

#endif  // ONSPEED_PERF_ENABLED

// Always-available name lookups (used by the dump task even in
// perf-disabled builds for the help string).
const char* scopeName(ScopeId id);
const char* taskName(TaskId id);

}  // namespace onspeed::util::perf

#endif  // ONSPEED_CORE_UTIL_PERF_H
