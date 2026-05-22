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
#include <cassert>
#include <cstring>

#ifdef ARDUINO_ARCH_ESP32
// esp_attr.h must be included at namespace scope, NOT inside the
// anonymous namespace where it would give every declaration it
// transitively imports internal linkage. Today the header is macros +
// extern "C" typedefs only, so a misplaced include compiles cleanly —
// but the moment someone adds an extern variable, an inside-namespace
// include silently becomes an ODR violation. Define our PSRAM-BSS
// alias here once, before any code that uses it.
#include "esp_attr.h"
#define ONSPEED_PSRAM_BSS_ATTR EXT_RAM_BSS_ATTR

extern "C" {
    int64_t esp_timer_get_time(void);
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Per-task ring lookup. We CAN'T use vTaskSetThreadLocalStoragePointer
// here because the Arduino-ESP32 SDK is built with
//   CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS = 1
//   CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS       = 1
// In TLSP_DELETION_CALLBACKS mode, configNUM_THREAD_LOCAL_STORAGE_POINTERS
// doubles to 2, and the upper half of pvThreadLocalStoragePointers
// stores deletion callback function pointers — not user TLSPs.
// User-callable slots are only [0, N) where N is the raw config value.
// With N=1, slot 0 is the only user slot, and it's taken by ESP-IDF's
// pthread layer (components/pthread/pthread_local_storage.c) and lwIP
// (components/lwip/.../sys_arch.c).
//
// Writing to "slot 1" via vTaskSetThreadLocalStoragePointer succeeds
// (bounds check is xIndex < 2), but it clobbers the deletion-callback
// storage for slot 0. When pthread or lwIP later installs a real del
// callback via vTaskSetThreadLocalStoragePointerAndDelCallback, that
// callback overwrites our ring pointer. The next PerfScope read pulls
// back a function pointer to flash, and the atomic CAS faults on
// LoadStoreError when it tries to S32C1I to a flash address.
//
// Reproduction: this surfaced on V4P bench, master tip + `perf on`,
// at PR #605. Crash signature was LoadStoreError in pushEvent at the
// drops.fetch_add() branch with EXCVADDR in the 0x420xxxxx flash
// range.
//
// Fix: small registry mapping TaskHandle_t → Ring*. Lookup is O(N)
// where N == kTaskCount (12 today). Each PerfScope ctor walks the
// table once. On Xtensa LX7 that's ~30 cycles for a typical hit —
// same order as the atomic store on the hot path it replaces.
#else
#include <chrono>
#include <thread>
#include <unordered_map>
#define ONSPEED_PSRAM_BSS_ATTR
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
    "synth_build",
    "spare0", "spare1", "spare2", "spare3",
};
static_assert(sizeof(kScopeNames) / sizeof(kScopeNames[0]) == kScopeCount,
              "kScopeNames size mismatch with ScopeId::Count");

constexpr const char* kTaskNames[] = {
    "Imu", "Sensors", "Audio", "Display", "Switch",
    "Log", "LogReplay", "TestPot", "RangeSweep",
    "Housekeeping", "WebServer", "DataServer",
    "ArduinoLoop", "EfisRead", "BoomRead",
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

// Per-task event buffers. IMU gets the big one; everyone else uses the
// smaller default size.
//
// Memory placement: tagged with ONSPEED_PSRAM_BSS_ATTR (= EXT_RAM_BSS_ATTR
// on ESP32, empty on native). EXT_RAM_BSS_ATTR places the symbol in
// the .ext_ram.bss section, which is PSRAM-backed BSS *if* the SDK is
// built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y.
//
// Today, the Arduino-ESP32 SDK we link against has that config OFF
// (verified by `grep CONFIG_SPIRAM_ALLOW_BSS_SEG ...sdkconfig`), so
// the attribute is a no-op and these buffers live in regular DRAM.
// The build still shrinks DIRAM use (~16 KB) because the per-task
// buffers are smaller in aggregate than the prior 12 × 1024-entry rings.
//
// Keeping the attribute means the buffers migrate to PSRAM automatically
// the day the SDK enables ALLOW_BSS_SEG_EXTERNAL_MEMORY (or we swap to
// vanilla ESP-IDF) — no code change needed. If you're chasing DRAM, that
// SDK config flip is the lever.
// Tasks with per-task overrides get their own buffers; everyone else
// shares the small-buffer pool. Adding a new override means: declaring
// the buffer, special-casing it in PerfRingsInit, AND subtracting one
// more from g_other_events's leading dimension. The runtime assert at
// the end of PerfRingsInit catches drift.
ONSPEED_PSRAM_BSS_ATTR PerfEvent g_imu_events  [kImuRingCapacity];
ONSPEED_PSRAM_BSS_ATTR PerfEvent g_efis_events [kEfisRingCapacity];
ONSPEED_PSRAM_BSS_ATTR PerfEvent g_boom_events [kBoomRingCapacity];
// Three task slots (Imu, EfisRead, BoomRead) are sized separately;
// kTaskCount - 3 rows of the default-capacity pool cover the rest.
ONSPEED_PSRAM_BSS_ATTR PerfEvent g_other_events[kTaskCount - 3][kDefaultRingCapacity];

// Static initializer: wires each Ring's events pointer + mask + capacity
// at module construction time. C++ initialises static-storage objects in
// declaration order within a TU, so g_rings[] (zero-init for its trivial
// members) is set up before this ctor runs. After this ctor, each Ring
// has its full per-task config.
//
// Defense in depth: if anything calls pushEvent BEFORE this ctor fires
// (e.g. a hypothetical extern-linkage call from another TU's static init
// — not currently the case, but cheap to guard), pushEvent sees
// capacity == 0 and drops the event instead of dereferencing a null
// events pointer. Zero-init of POD static storage gives us this for
// free.
struct PerfRingsInit {
    PerfRingsInit() {
        // Tasks with dedicated buffers.
        const size_t imuIdx  = static_cast<size_t>(TaskId::Imu);
        const size_t efisIdx = static_cast<size_t>(TaskId::EfisRead);
        const size_t boomIdx = static_cast<size_t>(TaskId::BoomRead);

        g_rings[imuIdx].events    = g_imu_events;
        g_rings[imuIdx].capacity  = kImuRingCapacity;
        g_rings[imuIdx].mask      = kImuRingCapacity - 1;

        g_rings[efisIdx].events   = g_efis_events;
        g_rings[efisIdx].capacity = kEfisRingCapacity;
        g_rings[efisIdx].mask     = kEfisRingCapacity - 1;

        g_rings[boomIdx].events   = g_boom_events;
        g_rings[boomIdx].capacity = kBoomRingCapacity;
        g_rings[boomIdx].mask     = kBoomRingCapacity - 1;

        // All other tasks share the small-buffer pool. We walk task
        // ordinals 0..kTaskCount-1, skip the three special-cased slots,
        // and assign each remaining task a row of g_other_events. Ends
        // with otherIdx == kTaskCount - 3, which we assert below.
        size_t otherIdx = 0;
        for (size_t t = 0; t < kTaskCount; ++t) {
            if (t == imuIdx || t == efisIdx || t == boomIdx) continue;
            g_rings[t].events   = g_other_events[otherIdx];
            g_rings[t].capacity = kDefaultRingCapacity;
            g_rings[t].mask     = kDefaultRingCapacity - 1;
            ++otherIdx;
        }
        // g_other_events is sized for kTaskCount-3 rows. If a new TaskId
        // is added and this count drifts, the assignments above would
        // index past the buffer. Catch it here instead of as a silent
        // OOB at first PERF emit.
        assert(otherIdx == kTaskCount - 3);
    }
};
static PerfRingsInit s_perfRingsInit;

// SPI counters: direct atomic accumulators, not events in a ring.
// Sized per-scope (one slot per ScopeId, but only SPI scopes used).
// Drained read-and-reset by the consumer 1× per snapshot.
std::atomic<uint64_t> g_spi_bytes_[kScopeCount];
std::atomic<uint64_t> g_spi_xfers_[kScopeCount];
std::atomic<uint32_t> g_spi_max_xfer_us_[kScopeCount];

std::atomic<bool> g_perfEnabled{false};

#ifdef ARDUINO_ARCH_ESP32
// Per-task ring registry. See the header comment about why this is NOT
// a FreeRTOS TLS slot lookup.
//
// 16-entry table: { TaskHandle_t, Ring* }. Linear scan on lookup;
// insertion via lock-free CAS into the first nullptr-handle slot.
// Sized to kTaskCount (12) + 4 headroom for any non-instrumented task
// that might happen to call PerfScope (which falls through to nullptr
// and bails harmlessly).
//
// Writes are infrequent: one per task per `setPerfEnabled(true)`
// transition (or first PerfLoop iteration after enable). Reads happen
// on every PerfScope construction — that's the hot path.
struct RingRegistryEntry {
    std::atomic<TaskHandle_t> handle{nullptr};
    Ring*                     ring{nullptr};
};
constexpr size_t kRingRegistryCap = 16;
RingRegistryEntry g_ringRegistry[kRingRegistryCap];

inline Ring* getTlsRing() {
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (self == nullptr) return nullptr;
    for (auto& entry : g_ringRegistry) {
        if (entry.handle.load(std::memory_order_acquire) == self) {
            return entry.ring;
        }
    }
    return nullptr;
}

inline void setTlsRing(Ring* r) {
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (self == nullptr) return;
    // First: if we already have an entry, update its ring pointer.
    for (auto& entry : g_ringRegistry) {
        if (entry.handle.load(std::memory_order_acquire) == self) {
            entry.ring = r;
            return;
        }
    }
    // Otherwise: claim the first free slot via CAS.
    for (auto& entry : g_ringRegistry) {
        TaskHandle_t expected = nullptr;
        if (entry.handle.compare_exchange_strong(expected, self,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_relaxed)) {
            entry.ring = r;
            return;
        }
    }
    // Registry full. Silently drop — this task's PerfScope events will
    // route to nullptr and be no-ops. kRingRegistryCap = 16 > kTaskCount
    // = 12, so we should never hit this in practice.
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

void bindCurrentTaskToRing(TaskId id) {
    Ring* r = ringForTask(id);
    if (r != nullptr) setTlsRing(r);
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
            const PerfEvent& ev = r.events[tail & r.mask];
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
