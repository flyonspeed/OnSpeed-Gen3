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
    "spare0", "spare1", "spare2", "spare3",
    "spare4", "spare5", "spare6", "spare7",
};
static_assert(sizeof(kScopeNames) / sizeof(kScopeNames[0]) == kScopeCount,
              "kScopeNames size mismatch with ScopeId::Count");

constexpr const char* kTaskNames[] = {
    "Imu", "Sensors", "Audio", "Display", "Switch",
    "Log", "LogReplay", "TestPot", "RangeSweep",
    "Housekeeping", "WebServer", "DataServer",
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
    Ring* r = getTlsRing();
    if (!r) return;  // SPI called from a non-task context — drop silently.
    // Encode bytes in the upper 16 bits of stackHighWaterWords field.
    // Cap at 65535 — SPI single xfers are nowhere near that.
    const uint16_t bytesU16 = (bytes > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(bytes);
    pushEvent(r, PerfEvent{
        durationUs, static_cast<uint8_t>(scopeId), /*flags=*/0u, bytesU16});
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
inline size_t bucketIndex(uint32_t v) {
    // Bucket k covers [2^(k/2), 2^((k+1)/2)). Compute floor(2·log2(v)).
    if (v == 0) return 0;
    // __builtin_clz returns leading zeros of a 32-bit. log2 floor = 31 - clz.
    const int log2v = 31 - __builtin_clz(v);
    // Within each integer log2 bin, the √2-midpoint splits "low" from
    // "high". v has top bit at position log2v; the bit one below tells
    // us if we're past 1.5×.
    const uint32_t mid = (1u << log2v) + (1u << (log2v > 0 ? log2v - 1 : 0));
    const size_t k = static_cast<size_t>(log2v) * 2u + (v >= mid ? 1u : 0u);
    return (k < kHistogramBuckets) ? k : kHistogramBuckets - 1;
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
            // Bucket k upper bound = 2^((k+1)/2). For odd k we'd want
            // 2^((k+1)/2) which is the same; for even k same. Use the
            // upper bound of the bin.
            const int upperLog = static_cast<int>((k + 1) / 2);
            uint32_t upper = 1u << upperLog;
            // Half-bins get √2 multiplier — approximate as upper*1.5 / 1
            // For an unbiased estimate, the bucket midpoint is closer
            // to the truth, but the upper bound is conservative.
            return std::min<uint32_t>(upper, maxUs);
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

inline bool isSpiScope(ScopeId id) {
    return id == ScopeId::SpiImu  || id == ScopeId::SpiAoa ||
           id == ScopeId::SpiPitot || id == ScopeId::SpiStatic ||
           id == ScopeId::SpiSd;
}

}  // namespace

void Consumer::reset() {
    for (auto& h : g_scope_hist) h.reset();
    for (auto& h : g_task_hist)  h.reset();
    for (auto& s : g_task_stack_min) s = 0xFFFFFFFFu;
    for (auto& d : g_task_drops)     d = 0;
    for (auto& c : g_spi_counters)   c.reset();
}

void Consumer::drainAll() {
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
                const auto sid = static_cast<ScopeId>(ev.scopeId);
                const size_t si = ev.scopeId;
                if (si < kScopeCount) {
                    if (isSpiScope(sid)) {
                        // stackHighWaterWords field carries byte count for SPI.
                        g_spi_counters[si].bytes += ev.stackHighWaterWords;
                        g_spi_counters[si].transfers++;
                        if (ev.durationUs > g_spi_counters[si].maxXferUs) {
                            g_spi_counters[si].maxXferUs = ev.durationUs;
                        }
                    } else {
                        g_scope_hist[si].add(ev.durationUs);
                    }
                }
            }
            tail++;
        }
        r.tail.store(tail, std::memory_order_release);
        g_task_drops[ti] += r.drops.exchange(0, std::memory_order_acq_rel);
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
