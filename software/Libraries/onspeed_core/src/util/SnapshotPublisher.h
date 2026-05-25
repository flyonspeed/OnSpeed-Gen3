// util/SnapshotPublisher.h — Lock-free single-publisher snapshot.
//
// =================================================================
// PURPOSE
// =================================================================
//
// One writer task (e.g. ImuReadTask) updates a payload at high rate.
// Multiple readers on other cores or tasks (web handlers, display task,
// log producer) want a coherent snapshot WITHOUT blocking the writer
// and WITHOUT taking a mutex.
//
// Standard pattern: a version counter ("seqcount") that the writer
// bumps before and after each publish. Readers retry if they observe
// the counter change mid-read. Lock-free for writer, lock-free for
// reader, lock-free across cores.
//
// =================================================================
// USAGE
// =================================================================
//
//   struct MyPayload {
//       float pitchDeg;
//       float rollDeg;
//       // ...
//   };
//   static_assert(std::is_trivially_copyable_v<MyPayload>);
//
//   // Global, owned by some translation unit:
//   onspeed::util::SnapshotPublisher<MyPayload> g_MySnap;
//
//   // Producer side (e.g. ImuReadTask):
//   MyPayload p{};
//   p.pitchDeg = g_AHRS.SmoothedPitch;
//   p.rollDeg  = g_AHRS.SmoothedRoll;
//   g_MySnap.publish(p);
//
//   // Reader side (e.g. web handler):
//   const MyPayload s = g_MySnap.read();    // spins, always coherent
//   // ... use s.pitchDeg, s.rollDeg ...
//
//   // Reader side with explicit bailout handling:
//   MyPayload s;
//   if (g_MySnap.tryRead(s)) {
//       // s is coherent
//   } else {
//       // bounded-retry bailout; skip this frame
//   }
//
// =================================================================
// INVARIANTS AND GUARANTEES
// =================================================================
//
//   1. **Single writer.** Behavior is undefined if more than one task
//      calls publish() on the same instance concurrently. Use a
//      separate SnapshotPublisher per producer if you have multiple
//      writers (recommended) or wrap publish() in a writer-side mutex
//      (not recommended — defeats the lock-free property on the
//      writer side).
//
//   2. **Multiple readers.** Any number of concurrent readers are
//      safe. Readers never block each other and never block the
//      writer.
//
//   3. **read() always returns a coherent payload.** It spins until
//      the seqcount agrees with itself across the data copy. The
//      spin is unbounded but the writer's publish completes in
//      bounded time (one memcpy of a trivially-copyable payload),
//      so the reader can't be starved indefinitely. On the ESP32-S3
//      at our rates (writer publishing at 208–416 Hz, copy time
//      ~100 ns for a 100-byte payload), the expected retry count
//      per call is < 1.
//
//   4. **tryRead() returns false after kMaxRetries spins.** On false,
//      `out` is potentially torn (some fields from one publish, some
//      from another). Callers that pass this check are responsible
//      for not acting on torn data. Use for real-time-deadlined
//      consumers that would rather skip a frame than spin.
//
//   5. **Trivially copyable only.** static_assert enforces. Memcpy
//      must be safe; non-trivial constructors/destructors would
//      execute on torn intermediate state, which is undefined.
//
//   6. **Pointer-bearing payloads are SAFE** because the seqcount
//      catches torn reads before the caller acts. If you pass a
//      pointer through, only act on it inside a successful read()/
//      tryRead()-true path. The pointer itself is a value; the
//      pointee must independently outlive any reader's use.
//
// =================================================================
// MEMORY ORDERING
// =================================================================
//
// Writer side:
//   v = version.load(relaxed);                  // current
//   version.store(v + 1, release);              // odd: "writing"
//   memcpy(&data_, &p, sizeof(p));              // bulk copy
//   version.store(v + 2, release);              // even: "done"
//
// The first release-store synchronizes with any reader's acquire-load
// that sees the odd value (those readers retry). The second
// release-store synchronizes with the reader's second acquire-load
// (the post-copy version check). Together they bracket the memcpy
// with happens-before edges so the reader's data copy sees the
// writer's writes IF the seqcount matched.
//
// Reader side:
//   for (i = 0..kMaxRetries) {
//       v1 = version.load(acquire);             // pre
//       if (v1 & 1) continue;                   // writer in progress
//       memcpy(&out, &data_, sizeof(out));      // bulk copy
//       v2 = version.load(acquire);             // post
//       if (v1 == v2) return out;               // clean
//   }
//   // bailout: out is best-effort
//
// The acquire-loads form happens-before with the writer's
// release-stores so that the reader's memcpy reads consistent bytes
// when v1 == v2. On x86 these compile to plain MOVs (TSO model);
// on ARM/Xtensa they emit the right barriers (dmb / memw).
//
// =================================================================
// COMPARED TO ALTERNATIVES
// =================================================================
//
//   - Mutex: blocks the writer if a reader holds it. Today's pattern;
//     the contention is exactly what this class avoids.
//   - Atomic struct: limited to single-word types on most platforms
//     (no atomic<struct> for our payload sizes).
//   - Double-buffer with index-swap: works but requires the reader to
//     read the index AND the buffer atomically, which is the same
//     problem this class solves with one less indirection.
//   - RCU: kernel-grade, requires grace-period machinery we don't
//     want on a 240 MHz embedded target.
//
// Seqcount is the right pattern for "single writer, many readers,
// readers tolerate occasional retry, payload too big for atomic."

#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>

namespace onspeed::util {

template <typename Payload>
class SnapshotPublisher {
    static_assert(std::is_trivially_copyable_v<Payload>,
                  "SnapshotPublisher<T> requires T to be trivially "
                  "copyable so memcpy can copy bytes safely across "
                  "the seqcount-protected boundary.");

public:
    // Retry budget for the reader spin loop. At publishing rates of
    // 208–416 Hz with payloads of ~100 bytes, a clean read typically
    // succeeds on the first attempt; collisions resolve in 1–2
    // retries. 8 is generous — a caller that bails after 8 either
    // has a runaway writer or a pathologically slow memcpy.
    static constexpr int kMaxRetries = 8;

    // Construct with a zero-initialized payload. Readers that race
    // ahead of the first publish() see a zero/default payload and a
    // non-zero version (0), so the very first read is coherent (all
    // zeros) even before any data has been published.
    constexpr SnapshotPublisher() noexcept = default;

    // Single-writer side. UB if called concurrently from multiple
    // tasks against the same instance.
    void publish(const Payload& p) noexcept {
        const uint32_t v = version_.load(std::memory_order_relaxed);
        // Bump version to odd ("writing"). Release semantics so the
        // pre-publish state (from publish N-1) is visible to any
        // reader that acquires v+1 or later.
        version_.store(v + 1, std::memory_order_release);

        // Release fence: ensures the memcpy stores below cannot be
        // reordered before this fence by the compiler or CPU. Pairs
        // with the reader's acquire fence around its memcpy.
        std::atomic_thread_fence(std::memory_order_release);

        // Bulk copy the payload. memcpy on trivially-copyable bytes
        // is defined behavior; the seqcount around it lets readers
        // detect torn reads.
        std::memcpy(&data_, &p, sizeof(Payload));

        // Bump version to even ("done"). Release semantics so any
        // reader's subsequent acquire-load that sees this value
        // synchronizes-with the memcpy above (the release-store
        // forms happens-before with the reader's acquire-load that
        // observes v+2 or later).
        version_.store(v + 2, std::memory_order_release);
    }

    // Multi-reader side. Spins until a coherent copy is obtained
    // and returns it. NEVER returns a torn copy — the seqcount loop
    // retries unboundedly. For callers who would rather skip a frame
    // than spin, use tryRead() instead.
    //
    // On the ESP32-S3 at our rates (writer publishing at 208–416 Hz,
    // payload memcpy ~100 ns at 256 bytes), the expected number of
    // retries per call is < 1. The unbounded spin is correct because
    // the writer ALWAYS finishes a publish in bounded time (a single
    // memcpy of trivially-copyable bytes); the reader can't get
    // starved indefinitely on a properly-implemented writer.
    Payload read() const noexcept {
        Payload out;
        for (;;) {
            const uint32_t v1 = version_.load(std::memory_order_acquire);
            if ((v1 & 1u) != 0u) {
                // Writer in progress; spin.
                continue;
            }
            // Acquire fence before memcpy: pairs with the writer's
            // release fence + v+2 release-store, so the memcpy below
            // sees writer's prior writes if v1 caught the publish.
            std::atomic_thread_fence(std::memory_order_acquire);
            std::memcpy(&out, &data_, sizeof(Payload));
            // Acquire fence after memcpy: ensures the memcpy bytes
            // are loaded before the v2 check, and prevents the
            // compiler/CPU from speculatively prefetching memcpy
            // bytes from a state past the v2 load.
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint32_t v2 = version_.load(std::memory_order_acquire);
            if (v1 == v2) {
                return out;
            }
            // Writer landed during our copy; retry.
        }
    }

    // Multi-reader side with explicit bailout. Returns true if the
    // copy is coherent; false if the bounded-retry budget was
    // exhausted. On false, `out` may be torn — caller must NOT act
    // on it for correctness-sensitive work. For real-time-deadlined
    // consumers that would rather skip a frame than spin.
    bool tryRead(Payload& out) const noexcept {
        for (int i = 0; i < kMaxRetries; ++i) {
            const uint32_t v1 = version_.load(std::memory_order_acquire);
            if ((v1 & 1u) != 0u) {
                continue;
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            std::memcpy(&out, &data_, sizeof(Payload));
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint32_t v2 = version_.load(std::memory_order_acquire);
            if (v1 == v2) {
                return true;
            }
        }
        return false;
    }

    // Diagnostic: returns the current version counter. Even values
    // mean "no writer in flight"; odd values mean "writer currently
    // publishing." Caller gets the count for telemetry/PERF; do not
    // gate correctness on this value (use read()/tryRead()).
    uint32_t versionForTelemetry() const noexcept {
        return version_.load(std::memory_order_relaxed);
    }

private:
    // Default-initialized to 0. The first publish() makes it 1 (odd,
    // while writing) then 2 (even, done). Any reader that races
    // ahead of the first publish sees version=0 (even, no writer in
    // flight) and data_=zeroed (its in-class default), so the
    // pre-first-publish read is coherent.
    std::atomic<uint32_t> version_{0};

    // The payload itself. Default-initialized to a zeroed POD.
    Payload data_{};
};

}  // namespace onspeed::util
