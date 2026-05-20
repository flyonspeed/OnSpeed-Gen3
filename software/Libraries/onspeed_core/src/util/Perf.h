// util/Perf.h — Lightweight scoped profiler for hot embedded paths.
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
    LogWrite,
    Spare0, Spare1, Spare2, Spare3,
    Spare4, Spare5, Spare6, Spare7,
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
// Exponential buckets: bucket k covers [2^(k/2), 2^((k+1)/2)) μs.
// 32 buckets → 1 μs .. 65 ms in √2 steps (~6% precision).
// ===========================================================================
constexpr size_t kHistogramBuckets = 32;

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

#endif  // ONSPEED_PERF_ENABLED

// Always-available name lookups (used by the dump task even in
// perf-disabled builds for the help string).
const char* scopeName(ScopeId id);
const char* taskName(TaskId id);

}  // namespace onspeed::util::perf

#endif  // ONSPEED_CORE_UTIL_PERF_H
