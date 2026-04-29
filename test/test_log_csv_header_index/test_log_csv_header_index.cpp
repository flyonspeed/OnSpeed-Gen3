// test_log_csv_header_index.cpp — unit tests for
// onspeed::proto::log_csv::BuildHeaderIndex
//
// LogReplay needs a name-keyed reader path so old logs (with a smaller,
// larger, or reordered column set) replay correctly under current firmware.
// These tests cover the canonical-minimum-required header — the subset that
// every OnSpeed log carries regardless of optional boom/EFIS groups.

#include <unity.h>
#include <proto/LogCsvHeaderIndex.h>

using onspeed::proto::log_csv::BuildHeaderIndex;
using onspeed::proto::log_csv::HeaderIndex;

void setUp() {}
void tearDown() {}

void test_build_index_canonical_minimum_required(void)
{
    // The minimum always-present column set, in canonical order.
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

    HeaderIndex idx;
    const char* missing = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx,
                               onspeed::proto::log_csv::HeaderStrictness::Strict,
                               &missing);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NULL(missing);
    TEST_ASSERT_EQUAL_INT(0,  idx.idxTimeStampMs);
    TEST_ASSERT_EQUAL_INT(7,  idx.idxIasKt);
    TEST_ASSERT_EQUAL_INT(9,  idx.idxFlapsPos);
    TEST_ASSERT_EQUAL_INT(28, idx.totalColumns);
    TEST_ASSERT_FALSE(idx.boomEnabled);
    TEST_ASSERT_FALSE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_index_canonical_minimum_required);
    return UNITY_END();
}
