// util/Perf.cpp — Producer rings + consumer histogram aggregation.
//
// Layout:
//   rings_[kTaskCount]      — one SPSC ring per producer task. Producer
//                              identifies "its" ring via FreeRTOS thread
//                              storage on ESP-IDF, or thread_local on
//                              native. PerfLoop binds at the top of
//                              every loop iteration; PerfScope looks up.
//   scope_hist_[kScopeCount] — consumer accumulator, scope timings.
//   task_hist_[kTaskCount]   — consumer accumulator, task loop periods.
//   task_stack_min_[kTaskCount] — min stack-words-remaining in interval.
//   spi_counters_[kScopeCount]  — bytes/xfers/max for SPI scopes only.
//
// Producer cost: one relaxed-load + one acquire-load + one store +
// one release-store on the ring. No fetch_add, no CAS loop.
//
// Consumer cost: O(events drained) for histogram binning, runs at 1 Hz
// off the hot path.

#include "Perf.h"

#include <algorithm>
#include <cstring>

#ifdef ARDUINO_ARCH_ESP32
extern "C" {
    int64_t esp_timer_get_time(void);
}
// FreeRTOS thread-local storage. Slot 0 is reserved by ESP-IDF; we use
// slot 1 to stash the per-task PerfRing pointer. PerfLoop binds it at
// the top of every loop iteration so PerfScope can find it without a
// table lookup.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static constexpr BaseType_t kPerfTlsSlot = 1;
#else
#include <chrono>
#include <thread>
#include <unordered_map>
#endif

namespace onspeed::util::perf {

// ===========================================================================
// Name tables — always available.
// ===========================================================================

namespace {

constexpr const char* kScopeNames[] = {
    "ekfq.predict",
    "ekfq.correct",
    "ekfq.alpha",
    "madgwick",
    "kalman",
    "tas",
    "imu_read",
    "press_read",
    "spi.imu",
    "spi.aoa",
    "spi.pitot",
    "spi.static",
    "spi.sd",
    "display_ser",
    "ws_frame",
    "log_write",
    "log_sync",
    "efis_read",
    "boom_read",
    "spare0", "spare1", "spare2", "spare3", "spare4",
};
static_assert(sizeof(kScopeNames) / sizeof(kScopeNames[0]) == kScopeCount,
              "kScopeNames size mismatch with ScopeId::Count");

constexpr const char* kTaskNames[] = {
    "Imu", "Sensors", "Audio", "Display", "Switch",
    "Log", "LogReplay", "TestPot", "RangeSweep",
    "Housekeeping", "WebServer", "DataServer",
    "ArduinoLoop",
};
static_assert(sizeof(kTaskNames) / sizeof(kTaskNames[0]) == kTaskCount,
              "kTaskNames size mismatch with TaskId::Count");

}  // namespace

const char* scopeName(ScopeId id) {
    const auto i = static_cast<size_t>(id);
    return (i < kScopeCount) ? kScopeNames[i] : "?";
}

const char* taskName(TaskId id) {
    const auto i = static_cast<size_t>(id);
    return (i < kTaskCount) ? kTaskNames[i] : "?";
}

uint64_t nowUs() {
#ifdef ARDUINO_ARCH_ESP32
    return static_cast<uint64_t>(esp_timer_get_time());
#else
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

#ifdef ONSPEED_PERF_ENABLED

// ===========================================================================
// Producer rings.
// ===========================================================================

namespace {

Ring g_rings[kTaskCount];

// SPI counters: direct atomic accumulators, not events in a ring.
// Sized per-scope (one slot per ScopeId, but only SPI scopes used).
// Drained read-and-reset by the consumer 1× per snapshot.
std::atomic<uint64_t> g_spi_bytes_[kScopeCount];
std::atomic<uint64_t> g_spi_xfers_[kScopeCount];
std::atomic<uint32_t> g_spi_max_xfer_us_[kScopeCount];

std::atomic<bool> g_perfEnabled{false};

#ifdef ARDUINO_ARCH_ESP32
inline Ring* getTlsRing() {
    return static_cast<Ring*>(pvTaskGetThreadLocalStoragePointer(nullptr, kPerfTlsSlot));
}
inline void setTlsRing(Ring* r) {
    vTaskSetThreadLocalStoragePointer(nullptr, kPerfTlsSlot, r);
}
#else
// Native side uses thread_local. Map from thread id is overkill; the
// thread-local pointer is plenty for tests.
thread_local Ring* tl_ring_ = nullptr;
inline Ring* getTlsRing() { return tl_ring_; }
inline void setTlsRing(Ring* r) { tl_ring_ = r; }
#endif

}  // namespace

Ring* ringForTask(TaskId id) {
    const auto i = static_cast<size_t>(id);
    return (i < kTaskCount) ? &g_rings[i] : nullptr;
}

Ring* PerfScope::ringForCurrentTask() {
    return getTlsRing();
}

void PerfLoop::bindRingToCurrentTask(Ring* r) {
    setTlsRing(r);
}

bool perfEnabled() {
    return g_perfEnabled.load(std::memory_order_acquire);
}

void setPerfEnabled(bool e) {
    // On enable transition, zero all rings so the first drain reflects
    // a clean interval. Producers are still active during this — they
    // either see "not enabled" and bail, or see "enabled" and write
    // into a freshly-reset ring. Either is fine.
    if (e && !g_perfEnabled.load(std::memory_order_relaxed)) {
        for (auto& ring : g_rings) {
            ring.head.store(0, std::memory_order_relaxed);
            ring.tail.store(0, std::memory_order_relaxed);
            ring.drops.store(0, std::memory_order_relaxed);
        }
    }
    g_perfEnabled.store(e, std::memory_order_release);
}

void recordSpiTransfer(ScopeId scopeId, uint32_t bytes, uint32_t durationUs) {
    if (!perfEnabled()) return;
    const auto i = static_cast<size_t>(scopeId);
    if (i >= kScopeCount) return;
    // Direct atomic counters — SPI traffic is too high-rate (304 xfers/sec
    // just for IMU at 208 Hz) to route through the per-task ring without
    // overflowing it. Three relaxed atomic ops; no contention since each
    // ScopeId has its own counter slot.
    g_spi_bytes_[i].fetch_add(bytes, std::memory_order_relaxed);
    g_spi_xfers_[i].fetch_add(1, std::memory_order_relaxed);
    // Atomic max via CAS loop. Cheap when value isn't a new max.
    uint32_t prev = g_spi_max_xfer_us_[i].load(std::memory_order_relaxed);
    while (durationUs > prev &&
           !g_spi_max_xfer_us_[i].compare_exchange_weak(
               prev, durationUs,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

// ===========================================================================
// Histogram.
// ===========================================================================

void Histogram::reset() {
    count = 0;
    sumUs = 0;
    minUs = 0xFFFFFFFFu;
    maxUs = 0;
    std::memset(buckets, 0, sizeof(buckets));
}

namespace {

// Map a duration in µs to its bucket index.
//   - 0..1023 µs → linear 16-µs bins (buckets 0..63)
//   - 1024..65535 µs → exponential √2-step bins (buckets 64..95)
inline size_t bucketIndex(uint32_t v) {
    if (v < kLinearMaxUs) {
        return v / kLinearStepUs;        // 0..63
    }
    // Exponential region. We want bucket k=64 to cover [1024, 1448),
    // k=65 to cover [1448, 2048), and so on (√2 steps). Compute the
    // index relative to kLinearMaxUs.
    const int log2v = 31 - __builtin_clz(v);   // floor(log2(v))
    // For v >= 1024 (= 2^10), log2v >= 10. Each integer log2 bin contributes
    // 2 buckets (low / high half). Offset so log2v=10 corresponds to bucket 64.
    const uint32_t mid = (1u << log2v) + (1u << (log2v - 1));
    size_t k = kLinearBuckets
        + static_cast<size_t>(log2v - 10) * 2u
        + (v >= mid ? 1u : 0u);
    return (k < kHistogramBuckets) ? k : kHistogramBuckets - 1;
}

// Upper bound (exclusive) of the value range covered by bucket k.
// Used by Histogram::percentile() to report bucket boundaries.
inline uint32_t bucketUpperBound(size_t k) {
    if (k < kLinearBuckets) {
        return (k + 1) * kLinearStepUs;  // 16, 32, ..., 1024
    }
    const size_t expIdx = k - kLinearBuckets;          // 0..31
    const int log2v = 10 + static_cast<int>(expIdx / 2);
    // Even expIdx → upper at the √2 midpoint of that log2 bin.
    // Odd  expIdx → upper at the next integer power of two.
    if ((expIdx & 1u) == 0) {
        return (1u << log2v) + (1u << (log2v - 1));    // 1.5 × 2^log2v
    } else {
        return 1u << (log2v + 1);                      // next power of two
    }
}

}  // namespace

void Histogram::add(uint32_t v) {
    count++;
    sumUs += v;
    if (v < minUs) minUs = v;
    if (v > maxUs) maxUs = v;
    buckets[bucketIndex(v)]++;
}

uint32_t Histogram::percentile(double p) const {
    if (count == 0) return 0;
    if (p <= 0.0) return minUs;
    if (p >= 1.0) return maxUs;
    const uint64_t target = static_cast<uint64_t>(static_cast<double>(count) * p);
    uint64_t cum = 0;
    for (size_t k = 0; k < kHistogramBuckets; ++k) {
        cum += buckets[k];
        if (cum >= target) {
            return std::min<uint32_t>(bucketUpperBound(k), maxUs);
        }
    }
    return maxUs;
}

void SpiCounter::reset() {
    bytes = 0;
    transfers = 0;
    maxXferUs = 0;
}

// ===========================================================================
// Consumer.
// ===========================================================================

namespace {

Histogram   g_scope_hist[kScopeCount];
Histogram   g_task_hist[kTaskCount];
uint32_t    g_task_stack_min[kTaskCount];
uint32_t    g_task_drops[kTaskCount];
SpiCounter  g_spi_counters[kScopeCount];

}  // namespace

void Consumer::reset() {
    for (auto& h : g_scope_hist) h.reset();
    for (auto& h : g_task_hist)  h.reset();
    for (auto& s : g_task_stack_min) s = 0xFFFFFFFFu;
    for (auto& d : g_task_drops)     d = 0;
    for (auto& c : g_spi_counters)   c.reset();
}

void Consumer::drainAll() {
    // Drain per-task rings → task/scope histograms.
    for (size_t ti = 0; ti < kTaskCount; ++ti) {
        Ring& r = g_rings[ti];
        const uint32_t head = r.head.load(std::memory_order_acquire);
        uint32_t tail = r.tail.load(std::memory_order_relaxed);
        while (tail != head) {
            const PerfEvent& ev = r.events[tail & kRingMask];
            if (ev.flags & kFlagLoop) {
                g_task_hist[ti].add(ev.durationUs);
                if (ev.stackHighWaterWords < g_task_stack_min[ti]) {
                    g_task_stack_min[ti] = ev.stackHighWaterWords;
                }
            } else {
                const size_t si = ev.scopeId;
                if (si < kScopeCount) {
                    g_scope_hist[si].add(ev.durationUs);
                }
            }
            tail++;
        }
        r.tail.store(tail, std::memory_order_release);
        g_task_drops[ti] += r.drops.exchange(0, std::memory_order_acq_rel);
    }

    // Drain SPI direct-counter accumulators (separate from rings).
    for (size_t si = 0; si < kScopeCount; ++si) {
        const uint64_t b = g_spi_bytes_[si].exchange(0, std::memory_order_acq_rel);
        const uint64_t x = g_spi_xfers_[si].exchange(0, std::memory_order_acq_rel);
        const uint32_t m = g_spi_max_xfer_us_[si].exchange(0, std::memory_order_acq_rel);
        if (x > 0) {
            g_spi_counters[si].bytes     += b;
            g_spi_counters[si].transfers += x;
            if (m > g_spi_counters[si].maxXferUs) {
                g_spi_counters[si].maxXferUs = m;
            }
        }
    }
}

const Histogram& Consumer::scopeHistogram(ScopeId id) const {
    return g_scope_hist[static_cast<size_t>(id)];
}

const Histogram& Consumer::taskHistogram(TaskId id) const {
    return g_task_hist[static_cast<size_t>(id)];
}

uint32_t Consumer::taskStackHighWater(TaskId id) const {
    return g_task_stack_min[static_cast<size_t>(id)];
}

uint32_t Consumer::taskDrops(TaskId id) const {
    return g_task_drops[static_cast<size_t>(id)];
}

const SpiCounter& Consumer::spiCounter(ScopeId id) const {
    return g_spi_counters[static_cast<size_t>(id)];
}

#endif  // ONSPEED_PERF_ENABLED

}  // namespace onspeed::util::perf
