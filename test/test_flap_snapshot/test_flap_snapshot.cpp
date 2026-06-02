// Scope of this suite: it verifies the PAYLOAD WIRING for g_FlapSnapshot —
// that the FlapSnapshotPayload round-trips through publish()/read() intact,
// that the default (pre-first-publish) state is sensible, and that the
// payload is trivially copyable (the memcpy precondition). The seqcount RACE
// semantics — no torn reads under concurrent publish, latest-publish-wins,
// memory ordering across cores — are tested exhaustively and payload-
// agnostically in test_snapshot_publisher, so they are NOT re-tested per
// payload here. The static_assert in FlapSnapshot.h is the compile-time half
// of the contract.
#include <unity.h>
#include <cstring>
#include <type_traits>

// Resolve the sketch-side header by relative path: the native test env does
// not put software/sketch_common/src/ on the include path (that's a firmware-
// build root). Matches test_ahrs_snapshot's include style.
#include "../../software/sketch_common/src/ahrs/FlapSnapshot.h"

using onspeed::ahrs::FlapSnapshotPayload;

void setUp() {}
void tearDown() {}

// A fresh publisher reads back a zeroed, invalid payload.
void test_default_is_invalid() {
    onspeed::util::SnapshotPublisher<FlapSnapshotPayload> pub;
    const FlapSnapshotPayload p = pub.read();
    TEST_ASSERT_FALSE(p.bValid);
    TEST_ASSERT_EQUAL_UINT8(0, p.nFlaps);
}

// publish() then read() returns a byte-identical, coherent payload.
void test_publish_read_roundtrip() {
    onspeed::util::SnapshotPublisher<FlapSnapshotPayload> pub;
    FlapSnapshotPayload in{};
    in.nFlaps = 2;
    in.aFlaps[0].iDegrees = 0;
    in.aFlaps[1].iDegrees = 30;
    in.iIndex = 1;
    in.iPosition = 30;
    in.uValue = 1234;
    in.bValid = true;
    pub.publish(in);

    const FlapSnapshotPayload out = pub.read();
    TEST_ASSERT_TRUE(out.bValid);
    TEST_ASSERT_EQUAL_UINT8(2, out.nFlaps);
    TEST_ASSERT_EQUAL_INT(1, out.iIndex);
    TEST_ASSERT_EQUAL_INT(30, out.iPosition);
    TEST_ASSERT_EQUAL_UINT16(1234, out.uValue);
    TEST_ASSERT_EQUAL_INT(30, out.aFlaps[1].iDegrees);
}

// Payload must be trivially copyable for the memcpy seqcount.
void test_payload_trivially_copyable() {
    TEST_ASSERT_TRUE(std::is_trivially_copyable_v<FlapSnapshotPayload>);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_is_invalid);
    RUN_TEST(test_publish_read_roundtrip);
    RUN_TEST(test_payload_trivially_copyable);
    return UNITY_END();
}
