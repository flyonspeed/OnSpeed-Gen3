// test_log_csv_header_index.cpp — unit tests for
// onspeed::proto::log_csv::BuildHeaderIndex
//
// LogReplay needs a name-keyed reader path so old logs (with a smaller,
// larger, or reordered column set) replay correctly under current firmware.
// These tests cover the canonical-minimum-required header — the subset that
// every OnSpeed log carries regardless of optional boom/EFIS groups.

#include <string>

#include <unity.h>
#include <proto/LogCsvHeaderIndex.h>

using onspeed::proto::log_csv::BuildHeaderIndex;
using onspeed::proto::log_csv::HeaderIndex;
using onspeed::proto::log_csv::HeaderStrictness;

namespace {

// Helpers for assembling synthetic headers. Concatenation only — no
// allocation overhead in the tests, just compile-time literal pieces.

constexpr const char* kCoreHead =
    "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
    "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
    "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll";

constexpr const char* kBoomGroup =
    "boomStatic,boomDynamic,boomAlpha,boomBeta,boomIAS,boomAge";

constexpr const char* kEfisStandardGroup =
    "efisIAS,efisPitch,efisRoll,efisLateralG,efisVerticalG,efisPercentLift,"
    "efisPalt,efisVSI,efisTAS,efisOAT,efisFuelRemaining,efisFuelFlow,"
    "efisMAP,efisRPM,efisPercentPower,efisMagHeading,efisAge,efisTime";

constexpr const char* kVn300Group =
    "vnAngularRateRoll,vnAngularRatePitch,vnAngularRateYaw,"
    "vnVelNedNorth,vnVelNedEast,vnVelNedDown,"
    "vnAccelFwd,vnAccelLat,vnAccelVert,"
    "vnYaw,vnPitch,vnRoll,"
    "vnLinAccFwd,vnLinAccLat,vnLinAccVert,"
    "vnYawSigma,vnRollSigma,vnPitchSigma,"
    "vnGnssVelNedNorth,vnGnssVelNedEast,vnGnssVelNedDown,"
    "vnGnssLat,vnGnssLon,vnGPSFix,vnDataAge,vnTimeUTC";

constexpr const char* kDerivedTail =
    "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

}  // namespace

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

void test_build_index_reordered_columns(void)
{
    // Same column set as canonical, but with the IMU block moved to the
    // front and the pressure block after it. Canonical ordinals are
    // shuffled but every required column is still present.
    static const char kHeader[] =
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

    HeaderIndex idx;
    const char* missing = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx,
                               onspeed::proto::log_csv::HeaderStrictness::Strict,
                               &missing);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NULL(missing);
    TEST_ASSERT_EQUAL_INT(0,  idx.idxImuTemp);   // first column now
    TEST_ASSERT_EQUAL_INT(9,  idx.idxTimeStampMs);
    TEST_ASSERT_EQUAL_INT(16, idx.idxIasKt);
}

void test_build_index_extra_unknown_columns(void)
{
    // Two unknown columns interleaved between known ones.
    static const char kHeader[] =
        "timeStamp,myCustomDebug1,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,myCustomDebug2,CoeffP";

    HeaderIndex idx;
    const char* missing = nullptr;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx,
                                      onspeed::proto::log_csv::HeaderStrictness::Strict,
                                      &missing));
    TEST_ASSERT_NULL(missing);
    // CoeffP is the 30th token (index 29) because of the two unknowns.
    TEST_ASSERT_EQUAL_INT(29, idx.idxCoeffP);
    TEST_ASSERT_EQUAL_INT(30, idx.totalColumns);
}

void test_build_index_missing_required_flapspos(void)
{
    // Drop "flapsPos" from the canonical header.
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

    HeaderIndex idx;
    const char* missing = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx,
                               onspeed::proto::log_csv::HeaderStrictness::Strict,
                               &missing);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(missing);
    TEST_ASSERT_EQUAL_STRING("flapsPos", missing);
}

void test_build_index_tolerates_trailing_crlf(void)
{
    // Header line as fgets would deliver it (with trailing \r\n).
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP\r\n";

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx,
                                      onspeed::proto::log_csv::HeaderStrictness::Strict,
                                      nullptr));
    TEST_ASSERT_EQUAL_INT(27, idx.idxCoeffP);  // last token, CR/LF stripped
}

void test_build_index_boom_enabled(void)
{
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kBoomGroup).append(",").append(kDerivedTail);

    HeaderIndex idx;
    const char* missing = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, HeaderStrictness::Strict, &missing);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NULL(missing);
    TEST_ASSERT_TRUE(idx.boomEnabled);
    TEST_ASSERT_FALSE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);
    TEST_ASSERT_EQUAL_INT(22, idx.idxBoomStatic);
    TEST_ASSERT_EQUAL_INT(27, idx.idxBoomAge);
}

void test_build_index_efis_standard_enabled(void)
{
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kEfisStandardGroup).append(",").append(kDerivedTail);

    HeaderIndex idx;
    const char* missing = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, HeaderStrictness::Strict, &missing);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NULL(missing);
    TEST_ASSERT_FALSE(idx.boomEnabled);
    TEST_ASSERT_TRUE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);
}

void test_build_index_vn300_enabled(void)
{
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kVn300Group).append(",").append(kDerivedTail);

    HeaderIndex idx;
    const char* missing = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, HeaderStrictness::Strict, &missing);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NULL(missing);
    TEST_ASSERT_FALSE(idx.boomEnabled);
    TEST_ASSERT_TRUE(idx.efisEnabled);
    TEST_ASSERT_TRUE(idx.efisIsVn300);
}

void test_build_index_boom_partial_fails(void)
{
    // Boom group with one column dropped — partial presence is a hard
    // fail in Strict mode, naming the first missing column.
    static const char* kPartialBoom =
        "boomStatic,boomDynamic,boomAlpha,boomBeta,boomIAS";  // missing boomAge
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kPartialBoom).append(",").append(kDerivedTail);

    HeaderIndex idx;
    const char* missing = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, HeaderStrictness::Strict, &missing);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(missing);
    TEST_ASSERT_EQUAL_STRING("boomAge", missing);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_index_canonical_minimum_required);
    RUN_TEST(test_build_index_reordered_columns);
    RUN_TEST(test_build_index_extra_unknown_columns);
    RUN_TEST(test_build_index_missing_required_flapspos);
    RUN_TEST(test_build_index_tolerates_trailing_crlf);
    RUN_TEST(test_build_index_boom_enabled);
    RUN_TEST(test_build_index_efis_standard_enabled);
    RUN_TEST(test_build_index_vn300_enabled);
    RUN_TEST(test_build_index_boom_partial_fails);
    return UNITY_END();
}
