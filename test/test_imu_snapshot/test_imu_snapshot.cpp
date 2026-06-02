// Scope of this suite: PAYLOAD WIRING for g_ImuSnapshot — round-trip of the
// reused onspeed::ImuSample through publish()/read(), sensible default state,
// trivially-copyable (the memcpy precondition). The seqcount RACE semantics
// (no torn reads under concurrent publish, latest-publish-wins, cross-core
// ordering) are covered exhaustively and payload-agnostically in
// test_snapshot_publisher, so they are NOT re-tested per payload here.
#include <unity.h>
#include <cstring>
#include <type_traits>

// Resolve the sketch-side header by relative path (the native test env does
// not put software/sketch_common/src/ on the include path). Matches
// test_flap_snapshot / test_sensor_snapshot.
#include "../../software/sketch_common/src/ahrs/ImuSnapshot.h"

using onspeed::ImuSample;

void setUp() {}
void tearDown() {}

// A fresh publisher reads back a zeroed payload.
void test_default_is_zero() {
    onspeed::util::SnapshotPublisher<ImuSample> pub;
    const ImuSample p = pub.read();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.accelXG);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.gyroRollDps);
    TEST_ASSERT_EQUAL_UINT32(0u, p.timestampUs);
}

// publish() then read() returns a byte-identical, coherent payload.
void test_publish_read_roundtrip() {
    onspeed::util::SnapshotPublisher<ImuSample> pub;
    ImuSample in{};
    in.accelXG      = 0.12f;
    in.accelYG      = -0.03f;
    in.accelZG      = 1.01f;
    in.gyroRollDps  = 4.5f;
    in.gyroPitchDps = -2.0f;
    in.gyroYawDps   = 0.7f;
    in.tempCelsius  = 26.5f;
    in.timestampUs  = 123456u;
    pub.publish(in);

    const ImuSample out = pub.read();
    TEST_ASSERT_EQUAL_FLOAT(0.12f, out.accelXG);
    TEST_ASSERT_EQUAL_FLOAT(1.01f, out.accelZG);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, out.gyroRollDps);
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, out.gyroPitchDps);
    TEST_ASSERT_EQUAL_FLOAT(26.5f, out.tempCelsius);
    TEST_ASSERT_EQUAL_UINT32(123456u, out.timestampUs);
}

// Payload must be trivially copyable for the memcpy seqcount.
void test_payload_trivially_copyable() {
    TEST_ASSERT_TRUE(std::is_trivially_copyable_v<ImuSample>);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_is_zero);
    RUN_TEST(test_publish_read_roundtrip);
    RUN_TEST(test_payload_trivially_copyable);
    return UNITY_END();
}
