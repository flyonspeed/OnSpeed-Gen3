// Scope of this suite: PAYLOAD WIRING for g_SensorSnapshot — round-trip
// through publish()/read(), sensible default state, trivially-copyable
// (the memcpy precondition). The seqcount RACE semantics (no torn reads
// under concurrent publish, latest-publish-wins, cross-core ordering) are
// covered exhaustively and payload-agnostically in test_snapshot_publisher,
// so they are NOT re-tested per payload here. The static_assert in
// SensorSnapshot.h is the compile-time half of the contract.
#include <unity.h>
#include <cstring>
#include <type_traits>

// Resolve the sketch-side header by relative path: the native test env does
// not put software/sketch_common/src/ on the include path. Matches
// test_flap_snapshot / test_ahrs_snapshot's include style.
#include "../../software/sketch_common/src/ahrs/SensorSnapshot.h"

using onspeed::ahrs::SensorSnapshotPayload;

void setUp() {}
void tearDown() {}

// A fresh publisher reads back a zeroed, invalid payload.
void test_default_is_invalid() {
    onspeed::util::SnapshotPublisher<SensorSnapshotPayload> pub;
    const SensorSnapshotPayload p = pub.read();
    TEST_ASSERT_FALSE(p.bValid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.iasKt);
}

// publish() then read() returns a byte-identical, coherent payload.
void test_publish_read_roundtrip() {
    onspeed::util::SnapshotPublisher<SensorSnapshotPayload> pub;
    SensorSnapshotPayload in{};
    in.iasKt        = 87.5f;
    in.aoaDeg       = 3.25f;
    in.paltFt       = 5280.0f;
    in.oatC         = 12.0f;
    in.bIasAlive    = true;
    in.pStaticMbar  = 835.2f;
    in.fDecelRate   = -1.5f;
    in.pfwdSmoothed = 42.0f;
    in.p45Smoothed  = -3.0f;
    in.iPfwd        = 1234;
    in.iP45         = -56;
    in.uIasUpdateUs = 9876543u;
    in.bValid       = true;
    pub.publish(in);

    const SensorSnapshotPayload out = pub.read();
    TEST_ASSERT_TRUE(out.bValid);
    TEST_ASSERT_EQUAL_FLOAT(87.5f, out.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(3.25f, out.aoaDeg);
    TEST_ASSERT_EQUAL_FLOAT(5280.0f, out.paltFt);
    TEST_ASSERT_TRUE(out.bIasAlive);
    TEST_ASSERT_EQUAL_FLOAT(-1.5f, out.fDecelRate);
    TEST_ASSERT_EQUAL_INT(1234, out.iPfwd);
    TEST_ASSERT_EQUAL_UINT32(9876543u, out.uIasUpdateUs);
}

// Payload must be trivially copyable for the memcpy seqcount.
void test_payload_trivially_copyable() {
    TEST_ASSERT_TRUE(std::is_trivially_copyable_v<SensorSnapshotPayload>);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_is_invalid);
    RUN_TEST(test_publish_read_roundtrip);
    RUN_TEST(test_payload_trivially_copyable);
    return UNITY_END();
}
