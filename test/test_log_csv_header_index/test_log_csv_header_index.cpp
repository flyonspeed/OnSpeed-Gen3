// test_log_csv_header_index.cpp — unit tests for
// onspeed::proto::log_csv::BuildHeaderIndex
//
// LogReplay needs a name-keyed reader path so old logs (with a smaller,
// larger, or reordered column set) replay correctly under current firmware.
// These tests cover the canonical-minimum-required header — the subset that
// every OnSpeed log carries regardless of optional boom/EFIS groups.

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

void test_parserowbyindex_canonical_roundtrip(void)
{
    // Build a row, format it via the canonical writer, parse the header
    // and the row back through the index path, and check every field.
    onspeed::LogRow src{};
    src.timeStampMs        = 12345;
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
    TEST_ASSERT_EQUAL_INT(src.flapsPos, dst.flapsPos);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.iasKt, dst.iasKt);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.imuPitchRateDps, dst.imuPitchRateDps);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, src.coeffP, dst.coeffP);
}

void test_parserowbyindex_empty_field_fails(void)
{
    static const char kHeader[] =
        "timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,"
        "AngleofAttack,flapsPos,DataMark,OAT,TAS,"
        "imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,"
        "EarthVerticalG,FlightPath,VSI,Altitude,DerivedAOA,CoeffP";
    HeaderIndex idx;
    TEST_ASSERT_TRUE(BuildHeaderIndex(kHeader, idx));

    // Same shape as the canonical row but with the IAS field empty.
    static const char kRow[] =
        "12345,100,1.5,200,2.5,1013.25,1234.0,,4.2,10,3,15.0,90.1,"
        "25.0,1.02,0.01,-0.03,5.0,-7.5,-2.0,3.5,-1.2,"
        "1.00,2.0,250.0,1300.0,4.3,0.123";

    onspeed::LogRow row{};
    TEST_ASSERT_FALSE(ParseRowByIndex(kRow, idx, row));
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
    src.vnGpsFix           = 3;
    src.vnDataAgeMs        = 25;
    std::strcpy(src.vnTimeUtc, "12:34:56");

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
    TEST_ASSERT_EQUAL_INT(src.vnGpsFix,    dst.vnGpsFix);
    TEST_ASSERT_EQUAL_INT(src.vnDataAgeMs, dst.vnDataAgeMs);
    TEST_ASSERT_EQUAL_STRING(src.vnTimeUtc, dst.vnTimeUtc);
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

void test_parserowbyindex_empty_vntimeutc_preserved(void)
{
    // Round-trip a VN-300 row where vnTimeUtc is the empty string. The
    // canonical writer emits "" for an unset UTC time; the index reader
    // must preserve the row rather than reject it for an empty token.
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
    src.vnTimeUtc[0]       = '\0';   // empty UTC time

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
    TEST_ASSERT_EQUAL_STRING("", dst.vnTimeUtc);
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
    RUN_TEST(test_build_index_reordered_columns);
    RUN_TEST(test_build_index_extra_unknown_columns);
    RUN_TEST(test_build_index_missing_required_flapspos);
    RUN_TEST(test_build_index_tolerates_trailing_crlf);
    RUN_TEST(test_build_index_boom_enabled);
    RUN_TEST(test_build_index_efis_standard_enabled);
    RUN_TEST(test_build_index_vn300_enabled);
    RUN_TEST(test_build_index_boom_partial_warns_no_enable);
    RUN_TEST(test_parserowbyindex_canonical_roundtrip);
    RUN_TEST(test_parserowbyindex_empty_field_fails);
    RUN_TEST(test_parserowbyindex_reordered_roundtrip);
    RUN_TEST(test_parserowbyindex_efis_roundtrip);
    RUN_TEST(test_parserowbyindex_vn300_roundtrip);
    RUN_TEST(test_fixture_canonical_with_boom_and_efis);
    RUN_TEST(test_fixture_canonical_with_vn300);
    RUN_TEST(test_fixture_canonical_with_efis_only);
    RUN_TEST(test_fixture_canonical_core_only);
    RUN_TEST(test_build_index_garbage_header_fails);
    RUN_TEST(test_parserowbyindex_empty_vntimeutc_preserved);
    RUN_TEST(test_parserowbyindex_flaps_raw_adc_present);
    RUN_TEST(test_parserowbyindex_flaps_raw_adc_absent_clears_flag);
    RUN_TEST(test_build_index_over_wide_header_fails);
    return UNITY_END();
}
