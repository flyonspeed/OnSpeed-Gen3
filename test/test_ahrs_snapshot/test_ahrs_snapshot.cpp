// test_ahrs_snapshot — verifies the AHRS output snapshot payload is wired
// correctly onto the (already-tested) SnapshotPublisher primitive.
//
// Scope: this suite proves "AhrsSnapshotPayload round-trips through
// SnapshotPublisher coherently and is the right shape", NOT "the seqcount
// works" — the seqcount's race/coherence guarantees are covered by
// test_snapshot_publisher.  Here we exercise the actual payload type the
// firmware publishes, so a field-shape regression (wrong type, lost
// trivial-copyability, a default that breaks pre-first-publish reads) is
// caught natively.
//
// The sketch-side header `src/ahrs/AhrsSnapshot.h` only pulls in
// <util/SnapshotPublisher.h> (from onspeed_core, on the native include
// path) and <type_traits>, so it compiles natively via a relative include
// without any Arduino/FreeRTOS dependency.

#include <unity.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "../../software/sketch_common/src/ahrs/AhrsSnapshot.h"

using onspeed::ahrs::AhrsSnapshotPayload;
using onspeed::util::SnapshotPublisher;

void setUp(void) {}
void tearDown(void) {}

// --- Shape / trait guarantees -------------------------------------------

void test_payload_is_trivially_copyable(void) {
    // SnapshotPublisher's memcpy seqcount requires this; the header has a
    // static_assert, but assert it here too so the contract is visible in
    // the test record.
    TEST_ASSERT_TRUE(std::is_trivially_copyable_v<AhrsSnapshotPayload>);
}

void test_default_payload_has_sane_rest_state(void) {
    // A reader that races ahead of the first publish() must see a sane
    // zeroed/rest payload (version 0, default data).  The defaults encode
    // "level on the ground": vertical G and the vertical accel filter sit
    // at +1 g, everything else at 0.
    AhrsSnapshotPayload p{};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.pitchDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.rollDeg);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.derivedAoaDeg);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.earthVertG);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.accelVertFilteredG);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.accelVertCorrG);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.gOnsetRate);
}

// --- Publisher round-trip with the real payload type --------------------

void test_default_read_before_publish_is_rest_state(void) {
    SnapshotPublisher<AhrsSnapshotPayload> pub;
    const AhrsSnapshotPayload p = pub.read();   // before any publish
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.pitchDeg);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.earthVertG);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.accelVertFilteredG);
}

void test_publish_then_read_round_trips_all_fields(void) {
    SnapshotPublisher<AhrsSnapshotPayload> pub;

    AhrsSnapshotPayload in{};
    in.pitchDeg           =  3.5f;
    in.rollDeg            = -2.0f;
    in.flightPathDeg      =  1.25f;
    in.derivedAoaDeg      =  4.75f;
    in.earthVertG         =  1.3f;
    in.tasMps             = 51.4f;
    in.kalmanAltMeters    = 305.0f;
    in.kalmanVsiMps       =  2.5f;
    in.accelFwdFilteredG  =  0.10f;
    in.accelLatFilteredG  = -0.20f;
    in.accelVertFilteredG =  1.30f;
    in.accelFwdCorrG      =  0.11f;
    in.accelLatCorrG      = -0.21f;
    in.accelVertCorrG     =  1.31f;
    in.gPitchDps          =  5.0f;
    in.gRollDps           = -6.0f;
    in.gYawDps            =  7.0f;
    in.gOnsetRate         =  0.4f;

    pub.publish(in);

    const AhrsSnapshotPayload out = pub.read();
    TEST_ASSERT_EQUAL_FLOAT(in.pitchDeg,           out.pitchDeg);
    TEST_ASSERT_EQUAL_FLOAT(in.rollDeg,            out.rollDeg);
    TEST_ASSERT_EQUAL_FLOAT(in.flightPathDeg,      out.flightPathDeg);
    TEST_ASSERT_EQUAL_FLOAT(in.derivedAoaDeg,      out.derivedAoaDeg);
    TEST_ASSERT_EQUAL_FLOAT(in.earthVertG,         out.earthVertG);
    TEST_ASSERT_EQUAL_FLOAT(in.tasMps,             out.tasMps);
    TEST_ASSERT_EQUAL_FLOAT(in.kalmanAltMeters,    out.kalmanAltMeters);
    TEST_ASSERT_EQUAL_FLOAT(in.kalmanVsiMps,       out.kalmanVsiMps);
    TEST_ASSERT_EQUAL_FLOAT(in.accelFwdFilteredG,  out.accelFwdFilteredG);
    TEST_ASSERT_EQUAL_FLOAT(in.accelLatFilteredG,  out.accelLatFilteredG);
    TEST_ASSERT_EQUAL_FLOAT(in.accelVertFilteredG, out.accelVertFilteredG);
    TEST_ASSERT_EQUAL_FLOAT(in.accelFwdCorrG,      out.accelFwdCorrG);
    TEST_ASSERT_EQUAL_FLOAT(in.accelLatCorrG,      out.accelLatCorrG);
    TEST_ASSERT_EQUAL_FLOAT(in.accelVertCorrG,     out.accelVertCorrG);
    TEST_ASSERT_EQUAL_FLOAT(in.gPitchDps,          out.gPitchDps);
    TEST_ASSERT_EQUAL_FLOAT(in.gRollDps,           out.gRollDps);
    TEST_ASSERT_EQUAL_FLOAT(in.gYawDps,            out.gYawDps);
    TEST_ASSERT_EQUAL_FLOAT(in.gOnsetRate,         out.gOnsetRate);
}

void test_latest_publish_wins(void) {
    SnapshotPublisher<AhrsSnapshotPayload> pub;
    AhrsSnapshotPayload a{}; a.pitchDeg = 1.0f;
    AhrsSnapshotPayload b{}; b.pitchDeg = 2.0f;
    pub.publish(a);
    pub.publish(b);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pub.read().pitchDeg);
}

void test_tryread_succeeds_when_idle(void) {
    SnapshotPublisher<AhrsSnapshotPayload> pub;
    AhrsSnapshotPayload in{}; in.derivedAoaDeg = 8.5f;
    pub.publish(in);
    AhrsSnapshotPayload out;
    TEST_ASSERT_TRUE(pub.tryRead(out));   // no concurrent writer
    TEST_ASSERT_EQUAL_FLOAT(8.5f, out.derivedAoaDeg);
}

// --- Concurrent coherence with the real payload -------------------------
//
// Mirrors test_snapshot_publisher's two-pattern integrity test but against
// AhrsSnapshotPayload: the writer alternates two fully-distinct frames; the
// reader must NEVER observe a torn mix (some fields from frame A, some from
// frame B).  A field-by-field consistency check across the whole struct
// catches any tearing the payload's size/layout might introduce.

void test_no_torn_reads_under_concurrent_publish(void) {
    SnapshotPublisher<AhrsSnapshotPayload> pub;

    // Frame A: every field set to one marker value; Frame B: another.
    AhrsSnapshotPayload a{};
    AhrsSnapshotPayload b{};
    float* af = reinterpret_cast<float*>(&a);
    float* bf = reinterpret_cast<float*>(&b);
    const size_t n = sizeof(AhrsSnapshotPayload) / sizeof(float);
    for (size_t i = 0; i < n; ++i) { af[i] = 1.0f; bf[i] = 2.0f; }
    pub.publish(a);

    std::atomic<bool> stop{false};
    std::atomic<long> torn{0};
    std::atomic<long> reads{0};

    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const AhrsSnapshotPayload s = pub.read();
            const float* sf = reinterpret_cast<const float*>(&s);
            const float marker = sf[0];
            // Coherent frames are all-1.0 or all-2.0.  A mix is a tear.
            bool coherent = (marker == 1.0f || marker == 2.0f);
            if (coherent) {
                for (size_t i = 1; i < n; ++i) {
                    if (sf[i] != marker) { coherent = false; break; }
                }
            }
            if (!coherent) torn.fetch_add(1, std::memory_order_relaxed);
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (int i = 0; i < 200000; ++i) {
        pub.publish((i & 1) ? b : a);
    }
    stop.store(true, std::memory_order_relaxed);
    reader.join();

    TEST_ASSERT_TRUE(reads.load() > 0);            // reader actually ran
    TEST_ASSERT_EQUAL_INT(0, (int)torn.load());    // zero torn reads
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_payload_is_trivially_copyable);
    RUN_TEST(test_default_payload_has_sane_rest_state);
    RUN_TEST(test_default_read_before_publish_is_rest_state);
    RUN_TEST(test_publish_then_read_round_trips_all_fields);
    RUN_TEST(test_latest_publish_wins);
    RUN_TEST(test_tryread_succeeds_when_idle);
    RUN_TEST(test_no_torn_reads_under_concurrent_publish);
    return UNITY_END();
}
