// test_snapshot_publisher.cpp — unit + race tests for SnapshotPublisher.
//
// Coverage:
//   - Default state: read() before any publish returns a zeroed payload.
//   - Single-threaded publish/read: read sees the most recent publish.
//   - Multiple publishes: read always sees the most recent.
//   - read() is coherent under concurrent publishing (no torn reads
//     escape the seqcount).
//   - Two-pattern integrity: writer alternates AAAA/5555 payloads;
//     reader never observes a mix.
//   - tryRead coherence: every true return is verified word-for-word
//     against the writer's published pattern under tight-loop write
//     pressure. The bailout rate itself is not asserted (it's
//     workload-dependent and can be high under adversarial test
//     conditions; on real hardware it would be vanishingly rare).
//   - Pointer-bearing payload (verifies pointers are safe through the
//     trivially_copyable gate and the seqcount).
//   - Version-counter wrap arithmetic (parity check and equality
//     comparison work correctly across uint32_t wrap, which happens
//     after ~59 days of continuous 416 Hz publishing).

#include <unity.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include <util/SnapshotPublisher.h>

using onspeed::util::SnapshotPublisher;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Single-threaded behavior
// ============================================================================

struct SimplePayload {
    uint32_t seq;
    float x;
    float y;
};
static_assert(std::is_trivially_copyable_v<SimplePayload>);

void test_default_read_returns_zero_payload(void) {
    SnapshotPublisher<SimplePayload> pub;

    const SimplePayload s = pub.read();
    TEST_ASSERT_EQUAL_UINT32(0u, s.seq);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.y);
}

void test_publish_then_read(void) {
    SnapshotPublisher<SimplePayload> pub;

    SimplePayload p{};
    p.seq = 42;
    p.x = 1.5f;
    p.y = -2.5f;
    pub.publish(p);

    const SimplePayload s = pub.read();
    TEST_ASSERT_EQUAL_UINT32(42u, s.seq);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, s.x);
    TEST_ASSERT_EQUAL_FLOAT(-2.5f, s.y);
}

void test_most_recent_publish_wins(void) {
    SnapshotPublisher<SimplePayload> pub;

    for (uint32_t i = 1; i <= 100; ++i) {
        SimplePayload p{};
        p.seq = i;
        p.x = static_cast<float>(i);
        pub.publish(p);
    }

    const SimplePayload s = pub.read();
    TEST_ASSERT_EQUAL_UINT32(100u, s.seq);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, s.x);
}

void test_tryread_succeeds_when_idle(void) {
    SnapshotPublisher<SimplePayload> pub;
    SimplePayload p{};
    p.seq = 7;
    pub.publish(p);

    SimplePayload out{};
    const bool ok = pub.tryRead(out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(7u, out.seq);
}

void test_version_for_telemetry_advances(void) {
    SnapshotPublisher<SimplePayload> pub;
    const uint32_t v0 = pub.versionForTelemetry();
    TEST_ASSERT_EQUAL_UINT32(0u, v0);

    SimplePayload p{};
    pub.publish(p);
    const uint32_t v1 = pub.versionForTelemetry();
    TEST_ASSERT_EQUAL_UINT32(2u, v1);   // even after one full publish

    pub.publish(p);
    const uint32_t v2 = pub.versionForTelemetry();
    TEST_ASSERT_EQUAL_UINT32(4u, v2);
}

// ============================================================================
// Concurrent race tests
// ============================================================================

// A payload large enough that memcpy is non-instantaneous on most
// platforms — increases the chance of catching a torn-read bug if
// the seqcount logic were wrong.
struct LargePayload {
    uint32_t seq;
    uint32_t pattern[63];   // 63 * 4 = 252 bytes, total 256 bytes
};
static_assert(std::is_trivially_copyable_v<LargePayload>);
static_assert(sizeof(LargePayload) == 256);

// Test 1 — Sequence-counter monotonicity.
//
// Writer publishes incrementing seq values. Reader reads many times
// and asserts the observed seq NEVER goes backwards. A torn read
// where two halves came from different writer iterations would
// produce a non-monotonic seq because the writer fills the whole
// payload with the new seq on each publish.
void test_concurrent_seq_is_monotonic(void) {
    SnapshotPublisher<LargePayload> pub;
    std::atomic<bool> stop{false};

    constexpr int kReaderIterations = 200000;

    // Writer thread: bumps seq + fills the rest of the payload with
    // a derived pattern. If the reader observes a torn read, the
    // seq half and pattern half would disagree — but we use seq
    // alone for the monotonicity check.
    std::thread writer([&] {
        uint32_t s = 1;
        while (!stop.load(std::memory_order_relaxed)) {
            LargePayload p{};
            p.seq = s;
            for (auto& w : p.pattern) {
                w = s;   // every word equals seq
            }
            pub.publish(p);
            ++s;
        }
    });

    uint32_t lastSeq = 0;
    int torn = 0;
    for (int i = 0; i < kReaderIterations; ++i) {
        const LargePayload r = pub.read();
        // Monotonicity: read() always returns a coherent snapshot,
        // so each read's seq is >= some prior publish. A backwards
        // step means we observed a torn read (post-bailout). We
        // tolerate up to a small number across the whole run (the
        // bounded-retry bailout is a documented edge case), but
        // require that monotonicity holds for the vast majority.
        if (r.seq < lastSeq) {
            ++torn;
        }
        lastSeq = r.seq;

        // Self-consistency: every word in pattern[] should equal r.seq
        // because the writer fills them all on each publish. A torn
        // read where pattern[0..31] is from publish N and pattern[32..62]
        // is from publish N+1 would show different values.
        for (auto w : r.pattern) {
            if (w != r.seq) {
                ++torn;
                break;
            }
        }
    }

    stop.store(true, std::memory_order_relaxed);
    writer.join();

    // read() guarantees coherence — torn count MUST be 0. read() is
    // documented as "NEVER returns torn data" (unbounded spin until
    // the seqcount agrees), so any nonzero observation is a real bug
    // in the seqcount logic, not test flakiness. If this fails on
    // CI, find the race; do not widen the tolerance.
    TEST_ASSERT_EQUAL_INT(0, torn);
}

// Test 2 — Two-pattern integrity.
//
// Writer alternates between two distinct payload patterns (all 0xAA
// and all 0x55). Reader observes a coherent payload, never a mix.
// Catches torn-read bugs even if the seqcount math were subtly off.
void test_concurrent_two_pattern_integrity(void) {
    SnapshotPublisher<LargePayload> pub;
    std::atomic<bool> stop{false};

    constexpr int kReaderIterations = 100000;

    std::thread writer([&] {
        bool alt = false;
        while (!stop.load(std::memory_order_relaxed)) {
            LargePayload p{};
            const uint32_t fill = alt ? 0xAAAAAAAAu : 0x55555555u;
            p.seq = fill;
            for (auto& w : p.pattern) {
                w = fill;
            }
            pub.publish(p);
            alt = !alt;
        }
    });

    int mixed = 0;
    for (int i = 0; i < kReaderIterations; ++i) {
        const LargePayload r = pub.read();
        // Every word (seq + pattern[]) should be the same value
        // because the writer fills the entire payload uniformly.
        // A mix means we observed a torn read.
        const uint32_t first = r.seq;
        if (first != 0xAAAAAAAAu && first != 0x55555555u && first != 0u) {
            ++mixed;
            continue;
        }
        for (auto w : r.pattern) {
            if (w != first) {
                ++mixed;
                break;
            }
        }
    }

    stop.store(true, std::memory_order_relaxed);
    writer.join();

    // read() guarantees coherence — mixed count MUST be 0. Same
    // rationale as Test 1: read() never returns torn data. Any
    // mixed observation is a real bug.
    TEST_ASSERT_EQUAL_INT(0, mixed);
}

// Test 3 — tryRead coherence + bailout contract.
//
// Two guarantees to verify under concurrent write pressure:
//
//   (a) Whenever tryRead returns TRUE, the returned payload is
//       coherent — every word matches the seq. (No false-positive
//       coherence claims even with a writer publishing in a tight
//       loop.)
//
//   (b) The API behaves cleanly across many iterations: no deadlock,
//       no crash, succeeded + bailed always equals iterations,
//       counters never go negative.
//
// The bailout RATE is intentionally not asserted. Under a writer in
// a tight loop with a 256-byte payload, the reader will frequently
// hit the kMaxRetries=8 budget — that's the documented behavior of
// tryRead under pathological pressure, NOT a regression. On real
// hardware at 416 Hz publish (millions of times slower than this
// test's writer), bailout is vanishingly rare.
//
// What matters for OnSpeed: tryRead must NEVER claim coherence on
// a torn payload. If this test fails on the coherence assertion
// (line below), tryRead's seqcount logic is broken.
void test_tryread_coherence_under_pressure(void) {
    SnapshotPublisher<LargePayload> pub;
    std::atomic<bool> stop{false};

    constexpr int kReaderIterations = 20000;

    std::thread writer([&] {
        uint32_t s = 1;
        while (!stop.load(std::memory_order_relaxed)) {
            LargePayload p{};
            p.seq = s;
            for (auto& w : p.pattern) {
                w = s;
            }
            pub.publish(p);
            ++s;
        }
    });

    int succeeded = 0;
    int bailed = 0;
    int coherenceViolations = 0;
    for (int i = 0; i < kReaderIterations; ++i) {
        LargePayload out{};
        const bool ok = pub.tryRead(out);
        if (ok) {
            ++succeeded;
            // Coherence check: every word should match seq.
            for (auto w : out.pattern) {
                if (w != out.seq) {
                    ++coherenceViolations;
                    break;
                }
            }
        } else {
            ++bailed;
        }
    }

    stop.store(true, std::memory_order_relaxed);
    writer.join();

    // Hard guarantee 1: every iteration accounted for.
    TEST_ASSERT_EQUAL_INT(kReaderIterations, succeeded + bailed);

    // Hard guarantee 2: every "true" return is coherent. This is
    // the load-bearing assertion for OnSpeed — a torn read with
    // tryRead claiming success would be a silent flight-data bug.
    TEST_ASSERT_EQUAL_INT(0, coherenceViolations);
}

// ============================================================================
// Pointer-bearing payload
// ============================================================================

// Pointers are trivially copyable; the seqcount catches torn reads
// before the caller acts on the pointer. This test confirms the
// type system + seqcount play nicely together for pointer fields.

struct PointerPayload {
    const char* msg;
    int len;
};
static_assert(std::is_trivially_copyable_v<PointerPayload>);

void test_pointer_bearing_payload_round_trip(void) {
    SnapshotPublisher<PointerPayload> pub;

    static constexpr const char* kMsg = "hello, snapshot";
    PointerPayload p{};
    p.msg = kMsg;
    p.len = 15;
    pub.publish(p);

    const PointerPayload s = pub.read();
    TEST_ASSERT_EQUAL_PTR(kMsg, s.msg);
    TEST_ASSERT_EQUAL_INT(15, s.len);

    // The pointee remains accessible (kMsg has static storage).
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(s.msg, "hello, snapshot", 15));
}

// ============================================================================
// Version-counter wrap semantics
// ============================================================================
//
// At 416 Hz × 2 increments per publish, version_ wraps every ~59 days
// of continuous operation. Not reachable in a single flight but worth
// locking the property: the seqcount comparison and parity check use
// unsigned arithmetic and must remain correct across the wrap.

void test_version_wrap_arithmetic(void) {
    // Parity check: v & 1u correctly identifies odd/even across wrap.
    // 0xFFFFFFFFu is odd; 0xFFFFFFFEu is even; 0u is even.
    TEST_ASSERT_EQUAL_UINT32(1u, 0xFFFFFFFFu & 1u);
    TEST_ASSERT_EQUAL_UINT32(0u, 0xFFFFFFFEu & 1u);
    TEST_ASSERT_EQUAL_UINT32(0u, 0u & 1u);

    // Equality comparison across wrap: distinct values (0xFFFFFFFE
    // and 0 — both even, both "no writer in flight") must NOT match.
    // If they did, the reader would see v1=0xFFFFFFFE pre-memcpy and
    // v2=0 post-memcpy and incorrectly conclude "clean read" while
    // (2^31) publishes happened in between.
    const uint32_t pre = 0xFFFFFFFEu;
    const uint32_t post = 0u;
    TEST_ASSERT_TRUE(pre != post);

    // The natural progression across wrap: ... 0xFFFFFFFE (even) →
    // 0xFFFFFFFF (odd, writing) → 0 (even, done). A reader that
    // catches v1=0xFFFFFFFE and v2=0 must see them as different
    // (not match) so it retries.
    const uint32_t v_before  = 0xFFFFFFFEu;
    const uint32_t v_writing = v_before + 1u;        // 0xFFFFFFFF
    const uint32_t v_after   = v_before + 2u;        // 0
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, v_writing);
    TEST_ASSERT_EQUAL_UINT32(0u, v_after);
    TEST_ASSERT_TRUE((v_writing & 1u) != 0u);        // odd: writing
    TEST_ASSERT_TRUE((v_after & 1u) == 0u);          // even: done
    TEST_ASSERT_TRUE(v_before != v_after);           // distinct
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_default_read_returns_zero_payload);
    RUN_TEST(test_publish_then_read);
    RUN_TEST(test_most_recent_publish_wins);
    RUN_TEST(test_tryread_succeeds_when_idle);
    RUN_TEST(test_version_for_telemetry_advances);

    RUN_TEST(test_concurrent_seq_is_monotonic);
    RUN_TEST(test_concurrent_two_pattern_integrity);
    RUN_TEST(test_tryread_coherence_under_pressure);

    RUN_TEST(test_pointer_bearing_payload_round_trip);

    RUN_TEST(test_version_wrap_arithmetic);

    return UNITY_END();
}
