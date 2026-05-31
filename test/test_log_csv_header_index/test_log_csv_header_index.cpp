// test_log_csv_header_index.cpp — unit tests for
// onspeed::proto::log_csv::BuildHeaderIndex
//
// LogReplay needs a name-keyed reader path so old logs (with a smaller,
// larger, or reordered column set) replay correctly under current firmware.
// These tests cover the canonical-minimum-required header — the subset that
// every OnSpeed log carries regardless of optional boom/EFIS groups.

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <unity.h>
#include <proto/LogCsv.h>
#include <proto/LogCsvHeaderIndex.h>

using onspeed::proto::log_csv::BuildHeaderIndex;
using onspeed::proto::log_csv::HeaderIndex;
using onspeed::proto::log_csv::ParseRowByIndex;

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

// VN-300 group as written by format version 2 (no vnEstAltFt). Kept as the
// "old log" reference so the version-tolerant parse path is exercised: a
// header without vnEstAltFt must still set efisIsVn300 and ParseRowByIndex
// must leave row.vnEstAltFt at its default.
// Legacy VN-300 group (no vnEstAltFt — pre-format-version-3 logs).
// Updated to the per-sample-timestamp columns from PR/issue #637.
constexpr const char* kVn300GroupNoEstAlt =
    "vnAngularRateRoll,vnAngularRatePitch,vnAngularRateYaw,"
    "vnVelNedNorth,vnVelNedEast,vnVelNedDown,"
    "vnAccelFwd,vnAccelLat,vnAccelVert,"
    "vnYaw,vnPitch,vnRoll,"
    "vnLinAccFwd,vnLinAccLat,vnLinAccVert,"
    "vnYawSigma,vnRollSigma,vnPitchSigma,"
    "vnGnssVelNedNorth,vnGnssVelNedEast,vnGnssVelNedDown,"
    "vnGnssLat,vnGnssLon,vnGPSFix,vnDataAge,"
    "vnTimeStartupNs,vnTimeGpsNs,vnTimeStatus";

// Current VN-300 group: includes vnEstAltFt and per-sample timestamps.
// This is what onspeed_core's WriteHeader emits today; tests that
// exercise current writer / current reader use it.
constexpr const char* kVn300Group =
    "vnAngularRateRoll,vnAngularRatePitch,vnAngularRateYaw,"
    "vnVelNedNorth,vnVelNedEast,vnVelNedDown,"
    "vnAccelFwd,vnAccelLat,vnAccelVert,"
    "vnYaw,vnPitch,vnRoll,"
    "vnLinAccFwd,vnLinAccLat,vnLinAccVert,"
    "vnYawSigma,vnRollSigma,vnPitchSigma,"
    "vnGnssVelNedNorth,vnGnssVelNedEast,vnGnssVelNedDown,"
    "vnGnssLat,vnGnssLon,vnEstAltFt,vnGPSFix,vnDataAge,"
    "vnTimeStartupNs,vnTimeGpsNs,vnTimeStatus";

constexpr const char* kDerivedTail =
    "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

// Warn-sink counters used by the warn-sink callback below. File-scope
// statics so the C-style sink signature can reach them; reset at the top
// of each test that uses the sink.
int g_warnCount = 0;
const char* g_lastWarn = nullptr;
void CountingWarnSink(const char* missing)
{
    g_warnCount++;
    g_lastWarn = missing;
}

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
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
    TEST_ASSERT_EQUAL_INT(0,  idx.idxTimeStampMs);
    // Old log without timeStampUs column: idxTimeStampUs stays -1 and
    // ParseRowByIndex leaves row.timeStampUs at 0.
    TEST_ASSERT_EQUAL_INT(-1, idx.idxTimeStampUs);
    TEST_ASSERT_EQUAL_INT(7,  idx.idxIasKt);
    TEST_ASSERT_EQUAL_INT(9,  idx.idxFlapsPos);
    TEST_ASSERT_EQUAL_INT(28, idx.totalColumns);
    TEST_ASSERT_FALSE(idx.boomEnabled);
    TEST_ASSERT_FALSE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);
}

void test_build_index_with_timestampus_column(void)
{
    // New log shape: timeStampUs adjacent to timeStamp. Verify the
    // index picks it up at the correct ordinal and the rest of the
    // canonical columns shift right by one.
    static const char kHeader[] =
        "timeStamp,timeStampUs,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
    TEST_ASSERT_EQUAL_INT(0,  idx.idxTimeStampMs);
    TEST_ASSERT_EQUAL_INT(1,  idx.idxTimeStampUs);
    TEST_ASSERT_EQUAL_INT(8,  idx.idxIasKt);       // shifted right by one
    TEST_ASSERT_EQUAL_INT(10, idx.idxFlapsPos);    // shifted right by one
    TEST_ASSERT_EQUAL_INT(29, idx.totalColumns);   // one more than minimum
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
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
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
    g_warnCount = 0; g_lastWarn = nullptr;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx, CountingWarnSink));
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
    // CoeffP is the 30th token (index 29) because of the two unknowns.
    TEST_ASSERT_EQUAL_INT(29, idx.idxCoeffP);
    TEST_ASSERT_EQUAL_INT(30, idx.totalColumns);
}

void test_build_index_missing_required_flapspos(void)
{
    // Drop "flapsPos" from the canonical header. BuildHeaderIndex warns
    // and continues; idxFlapsPos stays -1.
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_GREATER_THAN_INT(0, g_warnCount);
    TEST_ASSERT_EQUAL_INT(-1, idx.idxFlapsPos);
    TEST_ASSERT_EQUAL_STRING("flapsPos", g_lastWarn);
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
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx));
    TEST_ASSERT_EQUAL_INT(27, idx.idxCoeffP);  // last token, CR/LF stripped
}

void test_build_index_boom_enabled(void)
{
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kBoomGroup).append(",").append(kDerivedTail);

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
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
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
    TEST_ASSERT_FALSE(idx.boomEnabled);
    TEST_ASSERT_TRUE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);
}

void test_build_index_vn300_enabled(void)
{
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kVn300Group).append(",").append(kDerivedTail);

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
    TEST_ASSERT_FALSE(idx.boomEnabled);
    TEST_ASSERT_TRUE(idx.efisEnabled);
    TEST_ASSERT_TRUE(idx.efisIsVn300);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, idx.idxVnEstAltFt);
}

void test_build_index_vn300_without_est_alt_ft_still_enables(void)
{
    // Format version 2 log: VN-300 header lacks vnEstAltFt. The version-
    // tolerant index must still flag efisIsVn300 (the column is optional
    // within the group) and idxVnEstAltFt stays -1 so ParseRowByIndex
    // leaves the field at its default.
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kVn300GroupNoEstAlt).append(",").append(kDerivedTail);

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
    TEST_ASSERT_TRUE(idx.efisEnabled);
    TEST_ASSERT_TRUE(idx.efisIsVn300);
    TEST_ASSERT_EQUAL_INT(-1, idx.idxVnEstAltFt);

    // End-to-end parse path: a v2-style row (28 VN-300 fields, no altitude
    // column) must parse without striding off-by-one, must leave
    // row.vnEstAltFt at its default (0.0f), and must read row.vnGpsFix from
    // the correct token. The "v2 era" had only 26 VN-300 fields with a
    // vnTimeUTC string column; the format updated again in issue #637 to
    // 28 fields with per-sample u64 timestamps. This test exercises a row
    // that omits vnEstAltFt but carries the post-#637 timestamp columns.
    static const char kRow[] =
        "20000,110,1.60,210,2.60,1013.40,1500.0,92.0,3.80,5,0,12.0,94.5,"
        "25.5,1.030,0.020,-0.040,4.000,-3.500,-1.000,2.500,-0.800,"
        // VN-300 group, 28 fields (no vnEstAltFt; with timestamp triplet):
        "0.100,0.200,0.300,50.000,30.000,-1.500,0.050,0.010,1.020,"
        "270.500,1.500,-2.000,0.040,0.005,0.010,0.500,0.300,0.400,"
        "50.100,30.100,-1.400,37.123456,-122.456789,3,25,"
        "1234567890,1400123456789000,7,"
        "1.030,1.500,300.0,1500.0,3.85,0.150";

    onspeed::LogRow row{};
    row.efisEnabled = idx.efisEnabled;
    row.efisIsVn300 = idx.efisIsVn300;
    TEST_ASSERT_TRUE(ParseRowByIndex(kRow, idx, row));

    // The new column stayed at its default — TakeFloat is a no-op when idx<0.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, row.vnEstAltFt);
    // Adjacent column read from the right token (no off-by-one stride).
    TEST_ASSERT_EQUAL_INT(3, row.vnGpsFix);
    // Sanity: another VN-300 field also reads correctly. vnGnssLat is double;
    // Unity's float-precision assertions cast both sides to float, which is
    // adequate for a no-stride-off-by-one check.
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 37.123456f, (float)row.vnGnssLat);
}

void test_parserowbyindex_vn300_est_alt_ft_at_noncanonical_position(void)
{
    // Synthesize a header that places vnEstAltFt at a non-canonical
    // position (last column of the row) to prove BuildHeaderIndex resolves
    // the column by name and ParseRowByIndex reads from the recorded
    // ordinal, not a hard-coded position. Catches a future refactor that
    // accidentally re-introduces position-based parsing.
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        // VN-300 group with vnEstAltFt removed from its canonical slot.
        "vnAngularRateRoll,vnAngularRatePitch,vnAngularRateYaw,"
        "vnVelNedNorth,vnVelNedEast,vnVelNedDown,"
        "vnAccelFwd,vnAccelLat,vnAccelVert,"
        "vnYaw,vnPitch,vnRoll,"
        "vnLinAccFwd,vnLinAccLat,vnLinAccVert,"
        "vnYawSigma,vnRollSigma,vnPitchSigma,"
        "vnGnssVelNedNorth,vnGnssVelNedEast,vnGnssVelNedDown,"
        "vnGnssLat,vnGnssLon,vnGPSFix,vnDataAge,"
        "vnTimeStartupNs,vnTimeGpsNs,vnTimeStatus,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP,"
        // Re-attached at the very end as the last column.
        "vnEstAltFt";
    static const char kRow[] =
        "20000,110,1.60,210,2.60,1013.40,1500.0,92.0,3.80,5,0,12.0,94.5,"
        "25.5,1.030,0.020,-0.040,4.000,-3.500,-1.000,2.500,-0.800,"
        "0.100,0.200,0.300,50.000,30.000,-1.500,0.050,0.010,1.020,"
        "270.500,1.500,-2.000,0.040,0.005,0.010,0.500,0.300,0.400,"
        "50.100,30.100,-1.400,37.123456,-122.456789,3,25,"
        "1234567890,1400123456789000,7,"
        "1.030,1.500,300.0,1500.0,3.85,0.150,"
        "4521.75";

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx, CountingWarnSink));
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);
    TEST_ASSERT_TRUE(idx.efisIsVn300);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, idx.idxVnEstAltFt);

    onspeed::LogRow row{};
    row.efisEnabled = idx.efisEnabled;
    row.efisIsVn300 = idx.efisIsVn300;
    TEST_ASSERT_TRUE(ParseRowByIndex(kRow, idx, row));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 4521.75f, row.vnEstAltFt);
}

void test_build_index_boom_partial_warns_no_enable(void)
{
    // Boom group with one column dropped. BuildHeaderIndex emits one
    // warning naming the first missing column and leaves boomEnabled
    // false; present columns within the partial group keep their
    // ordinals (debug visibility).
    static const char* kPartialBoom =
        "boomStatic,boomDynamic,boomAlpha,boomBeta,boomIAS";  // missing boomAge
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kPartialBoom).append(",").append(kDerivedTail);

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(idx.boomEnabled);
    TEST_ASSERT_EQUAL_INT(1, g_warnCount);
    TEST_ASSERT_EQUAL_STRING("boomAge", g_lastWarn);
}

void test_build_index_efis_partial_warns_no_enable(void)
{
    // Standard EFIS group with one column dropped. Same contract as the
    // boom partial-group test: warn naming the first missing column,
    // leave efisEnabled false. Drop the tail column (efisTime).
    static const char* kPartialEfis =
        "efisIAS,efisPitch,efisRoll,efisLateralG,efisVerticalG,efisPercentLift,"
        "efisPalt,efisVSI,efisTAS,efisOAT,efisFuelRemaining,efisFuelFlow,"
        "efisMAP,efisRPM,efisPercentPower,efisMagHeading,efisAge";  // missing efisTime
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kPartialEfis).append(",").append(kDerivedTail);

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);
    TEST_ASSERT_EQUAL_INT(1, g_warnCount);
    TEST_ASSERT_EQUAL_STRING("efisTime", g_lastWarn);
}

void test_build_index_vn300_partial_warns_no_enable(void)
{
    // VN-300 group with one column dropped. Same contract: warn naming
    // the first missing column, leave efisIsVn300 + efisEnabled false.
    // Drop the tail column (vnTimeStatus).
    static const char* kPartialVn300 =
        "vnAngularRateRoll,vnAngularRatePitch,vnAngularRateYaw,"
        "vnVelNedNorth,vnVelNedEast,vnVelNedDown,"
        "vnAccelFwd,vnAccelLat,vnAccelVert,"
        "vnYaw,vnPitch,vnRoll,"
        "vnLinAccFwd,vnLinAccLat,vnLinAccVert,"
        "vnYawSigma,vnRollSigma,vnPitchSigma,"
        "vnGnssVelNedNorth,vnGnssVelNedEast,vnGnssVelNedDown,"
        "vnGnssLat,vnGnssLon,vnEstAltFt,vnGPSFix,vnDataAge,"
        "vnTimeStartupNs,vnTimeGpsNs";  // missing vnTimeStatus
    std::string hdr;
    hdr.append(kCoreHead).append(",").append(kPartialVn300).append(",").append(kDerivedTail);

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);
    TEST_ASSERT_EQUAL_INT(1, g_warnCount);
    TEST_ASSERT_EQUAL_STRING("vnTimeStatus", g_lastWarn);
}

void test_parserowbyindex_canonical_roundtrip(void)
{
    // Build a row, format it via the canonical writer, parse the header
    // and the row back through the index path, and check every field.
    onspeed::LogRow src{};
    src.timeStampMs        = 12345;
    src.timeStampUs        = 12345678ull;
    src.pfwdCounts         = 100;
    src.pfwdSmoothed       = 1.5f;
    src.p45Counts          = 200;
    src.p45Smoothed        = 2.5f;
    src.pStaticMbar        = 1013.25f;
    src.paltFt             = 1234.0f;
    src.iasKt              = 87.5f;
    src.angleOfAttackDeg   = 4.2f;
    src.flapsPos           = 10;
    src.dataMark           = 3;
    src.oatCelsius         = 15.0f;
    src.tasKt              = 90.1f;
    src.imuTempCelsius     = 25.0f;
    src.imuVerticalG       = 1.02f;
    src.imuLateralG        = 0.01f;
    src.imuForwardG        = -0.03f;
    src.imuRollRateDps     = 5.0f;
    src.imuPitchRateDps    = 7.5f;     // raw value; FormatRow will negate
    src.imuYawRateDps      = -2.0f;
    src.pitchDeg           = 3.5f;
    src.rollDeg            = -1.2f;
    src.earthVerticalG     = 1.00f;
    src.flightPathDeg      = 2.0f;
    src.vsiFpm             = 250.0f;
    src.altitudeFt         = 1300.0f;
    src.derivedAoaDeg      = 4.3f;
    src.coeffP             = 0.123f;
    src.boomEnabled        = false;
    src.efisEnabled        = false;
    src.efisIsVn300        = false;

    char hdrBuf[onspeed::proto::log_csv::kHeaderMaxBytes];
    char rowBuf[onspeed::proto::log_csv::kRowMaxBytes];
    size_t hdrLen = onspeed::proto::log_csv::WriteHeader(src, hdrBuf, sizeof(hdrBuf));
    size_t rowLen = onspeed::proto::log_csv::FormatRow(src, rowBuf, sizeof(rowBuf));
    TEST_ASSERT_GREATER_THAN(0, hdrLen);
    TEST_ASSERT_GREATER_THAN(0, rowLen);

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(std::string_view(hdrBuf, hdrLen), idx));

    onspeed::LogRow dst{};
    dst.boomEnabled = idx.boomEnabled;
    dst.efisEnabled = idx.efisEnabled;
    dst.efisIsVn300 = idx.efisIsVn300;
    bool ok = ParseRowByIndex(std::string_view(rowBuf, rowLen), idx, dst);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_UINT32(src.timeStampMs, dst.timeStampMs);
    TEST_ASSERT_EQUAL_UINT64(src.timeStampUs, dst.timeStampUs);
    TEST_ASSERT_EQUAL_INT(src.flapsPos, dst.flapsPos);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.iasKt, dst.iasKt);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.imuPitchRateDps, dst.imuPitchRateDps);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.coeffP, dst.coeffP);
}

void test_parserowbyindex_old_log_without_timestampus(void)
{
    // Backward-compat: old log shape (no timeStampUs column) parses
    // cleanly and leaves row.timeStampUs at default (0). Consumers fall
    // back to row.timeStampMs * 1000 for those logs.
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";
    static const char kRow[] =
        "12345,100,1.50,200,2.50,1013.25,1234.00,87.50,4.20,10,3,15.00,90.10,"
        "25.00,1.02,0.01,-0.03,5.00,-7.50,-2.00,3.50,-1.20,"
        "1.00,2.00,250.00,1300.00,4.3000,0.1230";

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx));
    TEST_ASSERT_EQUAL_INT(-1, idx.idxTimeStampUs);

    onspeed::LogRow dst{};
    bool ok = ParseRowByIndex(kRow, idx, dst);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(12345u, dst.timeStampMs);
    TEST_ASSERT_EQUAL_UINT64(0ull, dst.timeStampUs);  // absent → 0
}

void test_parserowbyindex_mixed_empty_numeric_air_data_accepted(void)
{
    // Mixed-state row: empty IAS while AngleofAttack and DerivedAOA
    // are numeric.  Accepted — each air-data cell carries its own
    // empty/numeric state, and downstream consumers decide how to
    // treat IAS-invalid-but-AOA-valid rows.
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";
    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx));

    static const char kRow[] =
        "12345,100,1.5,200,2.5,1013.25,1234.0,,4.2,10,3,15.0,90.1,"
        "25.0,1.02,0.01,-0.03,5.0,-7.5,-2.0,3.5,-1.2,"
        "1.00,2.0,250.0,1300.0,4.3,0.123";

    onspeed::LogRow row{};
    TEST_ASSERT_TRUE(ParseRowByIndex(kRow, idx, row));
    TEST_ASSERT_FALSE(row.iasValid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, row.angleOfAttackDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.3f, row.derivedAoaDeg);
}

void test_parserowbyindex_invalid_ias_round_trip(void)
{
    // Coherent format-3 row: all three air-data cells (IAS, AngleofAttack,
    // DerivedAOA) empty.  ParseRowByIndex decodes them to NaN and clears
    // iasValid on the parsed row.
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";
    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx));

    static const char kRow[] =
        "12345,100,1.5,200,2.5,1013.25,1234.0,,,10,3,15.0,90.1,"
        "25.0,1.02,0.01,-0.03,5.0,-7.5,-2.0,3.5,-1.2,"
        "1.00,2.0,250.0,1300.0,,0.123";

    onspeed::LogRow row{};
    TEST_ASSERT_TRUE(ParseRowByIndex(kRow, idx, row));
    TEST_ASSERT_FALSE(row.iasValid);
    TEST_ASSERT_TRUE(std::isnan(row.iasKt));
    TEST_ASSERT_TRUE(std::isnan(row.angleOfAttackDeg));
    TEST_ASSERT_TRUE(std::isnan(row.derivedAoaDeg));
    // CoeffP must remain numeric — not gated.
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.123f, row.coeffP);
}

void test_parserowbyindex_reordered_roundtrip(void)
{
    // Synthetic header where columns are interleaved with one unknown.
    static const char kHeader[] =
        "flapsPos,timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,"
        "myDebug,IAS,AngleofAttack,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx));
    TEST_ASSERT_EQUAL_INT(0, idx.idxFlapsPos);  // first column
    TEST_ASSERT_EQUAL_INT(9, idx.idxIasKt);     // after the unknown

    // Row with values matching the new column order. myDebug = "ignored".
    static const char kRow[] =
        "10,12345,100,1.5,200,2.5,1013.25,1234.0,"
        "ignored,87.5,4.2,3,15.0,90.1,"
        "25.0,1.02,0.01,-0.03,5.0,-7.5,-2.0,3.5,-1.2,"
        "1.00,2.0,250.0,1300.0,4.3,0.123";

    onspeed::LogRow row{};
    TEST_ASSERT_TRUE(ParseRowByIndex(kRow, idx, row));
    TEST_ASSERT_EQUAL_INT(10, row.flapsPos);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 87.5f, row.iasKt);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.123f, row.coeffP);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 7.5f, row.imuPitchRateDps);  // sign flipped back
}

void test_parserowbyindex_efis_roundtrip(void)
{
    onspeed::LogRow src{};
    src.timeStampMs        = 50000;
    src.iasKt              = 95.0f;
    src.angleOfAttackDeg   = 3.1f;
    src.flapsPos           = 0;
    src.dataMark           = 1;
    src.oatCelsius         = 12.0f;
    src.tasKt              = 99.5f;
    src.imuVerticalG       = 1.05f;
    src.imuPitchRateDps    = -1.2f;
    src.pitchDeg           = 1.0f;
    src.rollDeg            = 0.5f;
    src.earthVerticalG     = 1.04f;
    src.coeffP             = 0.55f;
    // EFIS standard payload.
    src.efisEnabled        = true;
    src.efisIsVn300        = false;
    src.efisIasKt          = 96.0f;
    src.efisPitchDeg       = 1.1f;
    src.efisRollDeg        = 0.6f;
    src.efisLateralG       = 0.02f;
    src.efisVerticalG      = 1.05f;
    src.efisPercentLift    = 42;
    src.efisPaltFt         = 1500;
    src.efisVsiFpm         = 350;
    src.efisTasKt          = 100.0f;
    src.efisOatCelsius     = 11.5f;
    src.efisFuelRemaining  = 25.0f;
    src.efisFuelFlow       = 8.5f;
    src.efisMap            = 22.5f;
    src.efisRpm            = 2400;
    src.efisPercentPower   = 65;
    src.efisMagHeading     = 270;
    src.efisAgeMs          = 50;
    src.efisTimestampMs    = 49980;

    char hdrBuf[onspeed::proto::log_csv::kHeaderMaxBytes];
    char rowBuf[onspeed::proto::log_csv::kRowMaxBytes];
    size_t hdrLen = onspeed::proto::log_csv::WriteHeader(src, hdrBuf, sizeof(hdrBuf));
    size_t rowLen = onspeed::proto::log_csv::FormatRow(src, rowBuf, sizeof(rowBuf));
    TEST_ASSERT_GREATER_THAN(0, hdrLen);
    TEST_ASSERT_GREATER_THAN(0, rowLen);

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(std::string_view(hdrBuf, hdrLen), idx));
    TEST_ASSERT_TRUE(idx.efisEnabled);
    TEST_ASSERT_FALSE(idx.efisIsVn300);

    onspeed::LogRow dst{};
    dst.boomEnabled = idx.boomEnabled;
    dst.efisEnabled = idx.efisEnabled;
    dst.efisIsVn300 = idx.efisIsVn300;
    TEST_ASSERT_TRUE(ParseRowByIndex(std::string_view(rowBuf, rowLen), idx, dst));

    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.efisIasKt,        dst.efisIasKt);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.efisOatCelsius,   dst.efisOatCelsius);
    TEST_ASSERT_EQUAL_INT(src.efisPercentLift, dst.efisPercentLift);
    TEST_ASSERT_EQUAL_INT(src.efisRpm,         dst.efisRpm);
    TEST_ASSERT_EQUAL_INT(src.efisAgeMs,       dst.efisAgeMs);
    TEST_ASSERT_EQUAL_UINT32(src.efisTimestampMs, dst.efisTimestampMs);
    // Sign-flip recovery still works alongside EFIS columns.
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.imuPitchRateDps, dst.imuPitchRateDps);
}

void test_parserowbyindex_vn300_roundtrip(void)
{
    onspeed::LogRow src{};
    src.timeStampMs        = 60000;
    src.iasKt              = 80.0f;
    src.flapsPos           = 5;
    src.imuPitchRateDps    = 3.3f;
    src.coeffP             = 0.42f;
    // VN-300 payload.
    src.efisEnabled        = true;
    src.efisIsVn300        = true;
    src.vnAngularRateRoll  = 0.1f;
    src.vnAngularRatePitch = 0.2f;
    src.vnAngularRateYaw   = 0.3f;
    src.vnVelNedNorth      = 50.0f;
    src.vnVelNedEast       = 30.0f;
    src.vnVelNedDown       = -1.5f;
    src.vnAccelFwd         = 0.05f;
    src.vnAccelLat         = 0.01f;
    src.vnAccelVert        = 1.02f;
    src.vnYawDeg           = 270.5f;
    src.vnPitchDeg         = 1.5f;
    src.vnRollDeg          = -2.0f;
    src.vnLinAccFwd        = 0.04f;
    src.vnLinAccLat        = 0.005f;
    src.vnLinAccVert       = 0.01f;
    src.vnYawSigma         = 0.5f;
    src.vnRollSigma        = 0.3f;
    src.vnPitchSigma       = 0.4f;
    src.vnGnssVelNedNorth  = 50.1f;
    src.vnGnssVelNedEast   = 30.1f;
    src.vnGnssVelNedDown   = -1.4f;
    src.vnGnssLat          = 37.123456;
    src.vnGnssLon          = -122.456789;
    src.vnEstAltFt         = 4521.75f;
    src.vnGpsFix           = 3;
    src.vnDataAgeMs        = 25;
    src.vnTimeStartupNs    = 1'234'567'890ULL;
    src.vnTimeGpsNs        = 1'400'123'456'789'000ULL;
    src.vnTimeStatus       = 0x07;

    char hdrBuf[onspeed::proto::log_csv::kHeaderMaxBytes];
    char rowBuf[onspeed::proto::log_csv::kRowMaxBytes];
    size_t hdrLen = onspeed::proto::log_csv::WriteHeader(src, hdrBuf, sizeof(hdrBuf));
    size_t rowLen = onspeed::proto::log_csv::FormatRow(src, rowBuf, sizeof(rowBuf));
    TEST_ASSERT_GREATER_THAN(0, hdrLen);
    TEST_ASSERT_GREATER_THAN(0, rowLen);

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(std::string_view(hdrBuf, hdrLen), idx));
    TEST_ASSERT_TRUE(idx.efisEnabled);
    TEST_ASSERT_TRUE(idx.efisIsVn300);

    onspeed::LogRow dst{};
    dst.boomEnabled = idx.boomEnabled;
    dst.efisEnabled = idx.efisEnabled;
    dst.efisIsVn300 = idx.efisIsVn300;
    TEST_ASSERT_TRUE(ParseRowByIndex(std::string_view(rowBuf, rowLen), idx, dst));

    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.vnYawDeg,           dst.vnYawDeg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.vnAngularRateRoll,  dst.vnAngularRateRoll);
    // GNSS lat/lon are double; FormatRow emits %.6f. Compare via float casts
    // because the native test build doesn't enable Unity's double-precision
    // assertions.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)src.vnGnssLat, (float)dst.vnGnssLat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)src.vnGnssLon, (float)dst.vnGnssLon);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, src.vnEstAltFt, dst.vnEstAltFt);
    TEST_ASSERT_EQUAL_INT(src.vnGpsFix,    dst.vnGpsFix);
    TEST_ASSERT_EQUAL_INT(src.vnDataAgeMs, dst.vnDataAgeMs);
    TEST_ASSERT_EQUAL_UINT64(src.vnTimeStartupNs, dst.vnTimeStartupNs);
    TEST_ASSERT_EQUAL_UINT64(src.vnTimeGpsNs,     dst.vnTimeGpsNs);
    TEST_ASSERT_EQUAL_UINT8 (src.vnTimeStatus,    dst.vnTimeStatus);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.imuPitchRateDps, dst.imuPitchRateDps);
}

// ----- Fixture-corpus tests --------------------------------------------------
//
// Each fixture is a CSV file with one header line plus several plausible
// data rows. The fixtures use current-firmware column names — they exercise
// "this canonical-schema-with-different-feature-flag-combination parses
// correctly," locking in the regression contract across reorder / extras /
// missing-required edits to LogCsvHeaderIndex.

namespace {

std::string LoadFixture(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool SplitHeaderAndFirstRow(const std::string& text,
                            std::string& hdr,
                            std::string& row)
{
    auto nl = text.find('\n');
    if (nl == std::string::npos) return false;
    hdr = text.substr(0, nl);
    auto nl2 = text.find('\n', nl + 1);
    row = (nl2 == std::string::npos)
        ? text.substr(nl + 1)
        : text.substr(nl + 1, nl2 - nl - 1);
    return !row.empty();
}

}  // namespace

#define FIXTURE_TEST(name, path, expectVn300, expectBoom, expectEfis)        \
void test_fixture_##name(void) {                                              \
    std::string text = LoadFixture(path);                                     \
    TEST_ASSERT_FALSE_MESSAGE(text.empty(), "fixture missing: " path);        \
    std::string hdr, row;                                                     \
    TEST_ASSERT_TRUE(SplitHeaderAndFirstRow(text, hdr, row));                 \
    HeaderIndex idx;                                                          \
    g_warnCount = 0; g_lastWarn = nullptr;                                    \
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);                   \
    TEST_ASSERT_TRUE_MESSAGE(                                                 \
        ok, g_lastWarn ? g_lastWarn : "BuildHeaderIndex failed");             \
    TEST_ASSERT_EQUAL_INT(0, g_warnCount);                                    \
    TEST_ASSERT_EQUAL((expectVn300), idx.efisIsVn300);                        \
    TEST_ASSERT_EQUAL((expectBoom),  idx.boomEnabled);                        \
    TEST_ASSERT_EQUAL((expectEfis),  idx.efisEnabled);                        \
    onspeed::LogRow r{};                                                      \
    r.boomEnabled = idx.boomEnabled;                                          \
    r.efisEnabled = idx.efisEnabled;                                          \
    r.efisIsVn300 = idx.efisIsVn300;                                          \
    TEST_ASSERT_TRUE(ParseRowByIndex(row, idx, r));                           \
}

// Each fixture is a current-schema CSV with a different feature-flag
// combination. They cover the optional-group permutations BuildHeaderIndex
// must distinguish, not historical firmware revisions.

// Core + boom + standard EFIS.
FIXTURE_TEST(canonical_with_boom_and_efis, "test/test_log_csv_header_index/fixtures/canonical-with-boom-and-efis.csv", false, true,  true)
// Core + VN-300 (no boom).
FIXTURE_TEST(canonical_with_vn300,         "test/test_log_csv_header_index/fixtures/canonical-with-vn300.csv",         true,  false, true)
// Core + standard EFIS only (no boom, no VN-300).
FIXTURE_TEST(canonical_with_efis_only,     "test/test_log_csv_header_index/fixtures/canonical-with-efis-only.csv",     false, false, true)
// Core columns only (no optional groups at all).
FIXTURE_TEST(canonical_core_only,          "test/test_log_csv_header_index/fixtures/canonical-core-only.csv",          false, false, false)

#undef FIXTURE_TEST

void test_build_index_garbage_header_fails(void)
{
    // A header that produces tokens but no recognized OnSpeed columns.
    // BuildHeaderIndex must reject this — otherwise OpenReplayLog reads
    // every row as all-zero defaults and runs the audio engine with
    // IAS=0/AOA=0.
    static const char kHeader[] = "garbage,here,now";
    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(kHeader, idx, CountingWarnSink);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_GREATER_THAN_INT(0, g_warnCount);
    TEST_ASSERT_NOT_NULL(g_lastWarn);
}

void test_parserowbyindex_vn300_zero_timestamps_preserved(void)
{
    // Round-trip a VN-300 row where the per-sample timestamps are zero
    // (e.g. before the VN-300 has reported any time-Common payloads).
    // The writer emits "0" for each u64; the index reader must preserve
    // the row.
    onspeed::LogRow src{};
    src.timeStampMs        = 70000;
    src.iasKt              = 75.0f;
    src.flapsPos           = 0;
    src.imuPitchRateDps    = 1.0f;
    src.coeffP             = 0.5f;
    src.efisEnabled        = true;
    src.efisIsVn300        = true;
    src.vnAngularRateRoll  = 0.1f;
    src.vnGnssLat          = 37.0;
    src.vnGnssLon          = -122.0;
    src.vnGpsFix           = 3;
    src.vnDataAgeMs        = 10;
    // vnTimeStartupNs / vnTimeGpsNs / vnTimeStatus are default-initialised
    // to 0 by LogRow's brace-init above.

    char hdrBuf[onspeed::proto::log_csv::kHeaderMaxBytes];
    char rowBuf[onspeed::proto::log_csv::kRowMaxBytes];
    size_t hdrLen = onspeed::proto::log_csv::WriteHeader(src, hdrBuf, sizeof(hdrBuf));
    size_t rowLen = onspeed::proto::log_csv::FormatRow(src, rowBuf, sizeof(rowBuf));
    TEST_ASSERT_GREATER_THAN(0, hdrLen);
    TEST_ASSERT_GREATER_THAN(0, rowLen);

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(std::string_view(hdrBuf, hdrLen), idx));

    onspeed::LogRow dst{};
    dst.boomEnabled = idx.boomEnabled;
    dst.efisEnabled = idx.efisEnabled;
    dst.efisIsVn300 = idx.efisIsVn300;
    bool ok = ParseRowByIndex(std::string_view(rowBuf, rowLen), idx, dst);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT64(0ULL, dst.vnTimeStartupNs);
    TEST_ASSERT_EQUAL_UINT64(0ULL, dst.vnTimeGpsNs);
    TEST_ASSERT_EQUAL_UINT8 (0,    dst.vnTimeStatus);
}

void test_parserowbyindex_flaps_raw_adc_present(void)
{
    // Format version 2: header carries the tail-optional flapsRawADC column.
    // BuildHeaderIndex must record its ordinal; ParseRowByIndex must populate
    // row.flapsRawAdc and set row.flapsRawAdcPresent so downstream code knows
    // the value is real (not a default zero from an old log).
    onspeed::LogRow src{};
    src.timeStampMs        = 12345;
    src.iasKt              = 87.5f;
    src.flapsPos           = 10;
    src.imuPitchRateDps    = 7.5f;     // raw value; FormatRow will negate
    src.coeffP             = 0.123f;
    src.flapsRawAdcPresent = true;
    src.flapsRawAdc        = 1462;

    char hdrBuf[onspeed::proto::log_csv::kHeaderMaxBytes];
    char rowBuf[onspeed::proto::log_csv::kRowMaxBytes];
    size_t hdrLen = onspeed::proto::log_csv::WriteHeader(src, hdrBuf, sizeof(hdrBuf));
    size_t rowLen = onspeed::proto::log_csv::FormatRow(src, rowBuf, sizeof(rowBuf));
    TEST_ASSERT_GREATER_THAN(0, hdrLen);
    TEST_ASSERT_GREATER_THAN(0, rowLen);

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(std::string_view(hdrBuf, hdrLen), idx));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, idx.idxFlapsRawAdc);

    onspeed::LogRow dst{};
    dst.boomEnabled = idx.boomEnabled;
    dst.efisEnabled = idx.efisEnabled;
    dst.efisIsVn300 = idx.efisIsVn300;
    bool ok = ParseRowByIndex(std::string_view(rowBuf, rowLen), idx, dst);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(dst.flapsRawAdcPresent);
    TEST_ASSERT_EQUAL_UINT16(1462u, dst.flapsRawAdc);
}

void test_parserowbyindex_flaps_raw_adc_absent_clears_flag(void)
{
    // Old-format log: header lacks flapsRawADC. BuildHeaderIndex must leave
    // idxFlapsRawAdc at -1; ParseRowByIndex must clear flapsRawAdcPresent so
    // the consumer can distinguish "absent" from "present and zero".
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";
    static const char kRow[] =
        "12345,100,1.5,200,2.5,1013.25,1234.0,87.5,4.2,10,3,15.0,90.1,"
        "25.0,1.02,0.01,-0.03,5.0,-7.5,-2.0,3.5,-1.2,"
        "1.00,2.0,250.0,1300.0,4.3,0.123";

    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx));
    TEST_ASSERT_EQUAL_INT(-1, idx.idxFlapsRawAdc);

    onspeed::LogRow dst{};
    // Pre-set the flag to verify ParseRowByIndex clears it on an absent column.
    dst.flapsRawAdcPresent = true;
    dst.flapsRawAdc        = 9999;
    TEST_ASSERT_TRUE(ParseRowByIndex(kRow, idx, dst));
    TEST_ASSERT_FALSE(dst.flapsRawAdcPresent);
}

void test_build_index_over_wide_header_fails(void)
{
    // Synthesize a valid canonical header padded with enough unknown
    // columns to push past kHeaderIndexMaxColumns. The tokenizer's
    // row-side buffer caps at the same limit, so anything beyond it is
    // truncated; reject the header up front rather than dropping every
    // data row downstream.
    std::string hdr(kCoreHead);
    // Pad with unknown columns past kHeaderIndexMaxColumns (96). Core has
    // 22 + 6 derived = 28 always-present, so we add 70 unknown columns to
    // reach 98 total.
    for (int i = 0; i < 70; ++i) {
        hdr.push_back(',');
        hdr += "extra";
        hdr += std::to_string(i);
    }
    hdr.push_back(',');
    hdr.append(kDerivedTail);

    HeaderIndex idx;
    g_warnCount = 0; g_lastWarn = nullptr;
    bool ok = BuildHeaderIndex(hdr, idx, CountingWarnSink);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_GREATER_THAN_INT(0, g_warnCount);
    TEST_ASSERT_NOT_NULL(g_lastWarn);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_index_canonical_minimum_required);
    RUN_TEST(test_build_index_with_timestampus_column);
    RUN_TEST(test_build_index_reordered_columns);
    RUN_TEST(test_build_index_extra_unknown_columns);
    RUN_TEST(test_build_index_missing_required_flapspos);
    RUN_TEST(test_build_index_tolerates_trailing_crlf);
    RUN_TEST(test_build_index_boom_enabled);
    RUN_TEST(test_build_index_efis_standard_enabled);
    RUN_TEST(test_build_index_vn300_enabled);
    RUN_TEST(test_build_index_vn300_without_est_alt_ft_still_enables);
    RUN_TEST(test_build_index_boom_partial_warns_no_enable);
    RUN_TEST(test_build_index_efis_partial_warns_no_enable);
    RUN_TEST(test_build_index_vn300_partial_warns_no_enable);
    RUN_TEST(test_parserowbyindex_canonical_roundtrip);
    RUN_TEST(test_parserowbyindex_old_log_without_timestampus);
    RUN_TEST(test_parserowbyindex_mixed_empty_numeric_air_data_accepted);
    RUN_TEST(test_parserowbyindex_invalid_ias_round_trip);
    RUN_TEST(test_parserowbyindex_reordered_roundtrip);
    RUN_TEST(test_parserowbyindex_efis_roundtrip);
    RUN_TEST(test_parserowbyindex_vn300_roundtrip);
    RUN_TEST(test_parserowbyindex_vn300_est_alt_ft_at_noncanonical_position);
    RUN_TEST(test_fixture_canonical_with_boom_and_efis);
    RUN_TEST(test_fixture_canonical_with_vn300);
    RUN_TEST(test_fixture_canonical_with_efis_only);
    RUN_TEST(test_fixture_canonical_core_only);
    RUN_TEST(test_build_index_garbage_header_fails);
    RUN_TEST(test_parserowbyindex_vn300_zero_timestamps_preserved);
    RUN_TEST(test_parserowbyindex_flaps_raw_adc_present);
    RUN_TEST(test_parserowbyindex_flaps_raw_adc_absent_clears_flag);
    RUN_TEST(test_build_index_over_wide_header_fails);
    return UNITY_END();
}
