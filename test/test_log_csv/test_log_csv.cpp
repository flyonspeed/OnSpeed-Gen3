// test_log_csv.cpp — unit tests for onspeed::proto::log_csv
//
// Tests cover:
//   - Round-trip: LogRow -> FormatRow -> ParseRow -> all fields equal original
//   - Header: every expected column name present
//   - Edge values: extreme integers, zero floats, empty VN-300 UTC string
//   - Fixture rows from a real flight log (ParseRow -> FormatRow -> byte-identical)
//   - Malformed input returns false
//   - Issue #182: PitchRate sign flip is in FormatRow (not in the LogRow)

#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

#include <proto/LogCsv.h>
#include <types/LogRow.h>

using onspeed::LogRow;
using onspeed::kLogRowUtcTimeLen;
namespace csv = onspeed::proto::log_csv;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static char s_hdrBuf[csv::kHeaderMaxBytes];
static char s_rowBuf[csv::kRowMaxBytes];
static char s_rowBuf2[csv::kRowMaxBytes];

// Tolerance for float round-trip via %.2f / %.6f / %.4f
static constexpr float kTolLow  = 0.005f;    // for %.2f columns
static constexpr float kTolHigh = 5e-7f;     // for %.6f columns
static constexpr float kTolAoa  = 5e-5f;     // for %.4f columns

// Fill every numeric field with a distinct, non-default value.
static LogRow MakeTestRow(bool boom = false, bool efis = false, bool vn300 = false,
                          bool flapsRawAdc = false)
{
    LogRow r;
    r.boomEnabled        = boom;
    r.efisEnabled        = efis;
    r.efisIsVn300        = vn300;
    r.flapsRawAdcPresent = flapsRawAdc;
    if (flapsRawAdc) {
        r.flapsRawAdc = 1462;   // representative pot reading at flaps-up detent
    }

    r.timeStampMs       = 123456u;
    r.pfwdCounts        = 42;
    r.pfwdSmoothed      = 42.50f;
    r.p45Counts         = 17;
    r.p45Smoothed       = 17.25f;
    r.pStaticMbar       = 901.23f;
    r.paltFt            = 4500.75f;
    r.iasKt             = 87.34f;
    r.angleOfAttackDeg  = 5.12f;
    r.flapsPos          = 10;
    r.dataMark          = 3;
    r.oatCelsius        = 15.60f;
    r.tasKt             = 92.10f;
    r.imuTempCelsius    = 38.75f;
    r.imuVerticalG      = 1.003456f;
    r.imuLateralG       = 0.012345f;
    r.imuForwardG       = -0.034567f;
    r.imuRollRateDps    = 0.123456f;
    r.imuPitchRateDps   = -0.654321f;   // raw (un-negated) — FormatRow will negate this
    r.imuYawRateDps     = 0.098765f;
    r.pitchDeg          = 2.50f;
    r.rollDeg           = -1.25f;

    if (boom) {
        r.boomStatic  = 901.10f;
        r.boomDynamic = 12.34f;
        r.boomAlpha   = 3.21f;
        r.boomBeta    = -0.50f;
        r.boomIasKt   = 88.00f;
        r.boomAgeMs   = 25;
    }

    if (efis && !vn300) {
        r.efisIasKt         = 88.10f;
        r.efisPitchDeg      = 2.20f;
        r.efisRollDeg       = -0.80f;
        r.efisLateralG      = 0.02f;
        r.efisVerticalG     = 1.01f;
        r.efisPercentLift   = 55;
        r.efisPaltFt        = 4480;
        r.efisVsiFpm        = -120;
        r.efisTasKt         = 93.40f;
        r.efisOatCelsius    = 15.00f;
        r.efisFuelRemaining = 25.50f;
        r.efisFuelFlow      = 8.30f;
        r.efisMap           = 24.50f;
        r.efisRpm           = 2400;
        r.efisPercentPower  = 65;
        r.efisMagHeading    = 270;
        r.efisAgeMs         = 40;
        r.efisTimestampMs   = 123400u;
    }

    if (efis && vn300) {
        r.vnAngularRateRoll  = 0.11f;
        r.vnAngularRatePitch = 0.22f;
        r.vnAngularRateYaw   = 0.33f;
        r.vnVelNedNorth      = 50.10f;
        r.vnVelNedEast       = 10.20f;
        r.vnVelNedDown       = -1.50f;
        r.vnAccelFwd         = 0.05f;
        r.vnAccelLat         = 0.01f;
        r.vnAccelVert        = 9.82f;
        r.vnYawDeg           = 270.00f;
        r.vnPitchDeg         = 2.50f;
        r.vnRollDeg          = -1.25f;
        r.vnLinAccFwd        = 0.04f;
        r.vnLinAccLat        = 0.00f;
        r.vnLinAccVert       = 9.81f;
        r.vnYawSigma         = 0.30f;
        r.vnRollSigma        = 0.10f;
        r.vnPitchSigma       = 0.10f;
        r.vnGnssVelNedNorth  = 49.90f;
        r.vnGnssVelNedEast   = 10.05f;
        r.vnGnssVelNedDown   = -1.40f;
        r.vnGnssLat          = 37.123456;
        r.vnGnssLon          = -122.654321;
        r.vnGpsFix           = 1;
        r.vnDataAgeMs        = 55;
        strncpy(r.vnTimeUtc, "2026-04-11T14:30:00Z", kLogRowUtcTimeLen - 1);
        r.vnTimeUtc[kLogRowUtcTimeLen - 1] = '\0';
    }

    r.earthVerticalG = 0.98f;
    r.flightPathDeg  = -1.50f;
    r.vsiFpm         = -300.00f;
    r.altitudeFt     = 4520.00f;
    r.derivedAoaDeg  = 5.1234f;
    r.coeffP         = 0.3456f;

    return r;
}

// Check that a round-trip (Format -> Parse) reproduces the original row.
// Returns true if all assertions pass (using Unity test macros internally).
static void AssertRoundTrip(const LogRow& original)
{
    // Format
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Parse back into a fresh row with the same feature flags
    LogRow parsed;
    parsed.boomEnabled = original.boomEnabled;
    parsed.efisEnabled        = original.efisEnabled;
    parsed.efisIsVn300        = original.efisIsVn300;
    parsed.flapsRawAdcPresent = original.flapsRawAdcPresent;

    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);

    // Core fields
    TEST_ASSERT_EQUAL_UINT32(original.timeStampMs, parsed.timeStampMs);
    TEST_ASSERT_EQUAL_INT(original.pfwdCounts, parsed.pfwdCounts);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.pfwdSmoothed, parsed.pfwdSmoothed);
    TEST_ASSERT_EQUAL_INT(original.p45Counts, parsed.p45Counts);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.p45Smoothed, parsed.p45Smoothed);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.pStaticMbar, parsed.pStaticMbar);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.paltFt, parsed.paltFt);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.iasKt, parsed.iasKt);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.angleOfAttackDeg, parsed.angleOfAttackDeg);
    TEST_ASSERT_EQUAL_INT(original.flapsPos, parsed.flapsPos);
    TEST_ASSERT_EQUAL_INT(original.dataMark, parsed.dataMark);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.oatCelsius, parsed.oatCelsius);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.tasKt, parsed.tasKt);

    // IMU fields
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.imuTempCelsius, parsed.imuTempCelsius);
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, original.imuVerticalG, parsed.imuVerticalG);
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, original.imuLateralG, parsed.imuLateralG);
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, original.imuForwardG, parsed.imuForwardG);
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, original.imuRollRateDps, parsed.imuRollRateDps);
    // PitchRate: FormatRow emits -imuPitchRateDps; ParseRow restores the sign.
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, original.imuPitchRateDps, parsed.imuPitchRateDps);
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, original.imuYawRateDps, parsed.imuYawRateDps);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.pitchDeg, parsed.pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.rollDeg, parsed.rollDeg);

    // Boom fields
    if (original.boomEnabled) {
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.boomStatic,  parsed.boomStatic);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.boomDynamic, parsed.boomDynamic);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.boomAlpha,   parsed.boomAlpha);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.boomBeta,    parsed.boomBeta);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.boomIasKt,   parsed.boomIasKt);
        TEST_ASSERT_EQUAL_INT(original.boomAgeMs, parsed.boomAgeMs);
    }

    // Tail-optional flapsRawADC: equality when the column is carried.
    if (original.flapsRawAdcPresent) {
        TEST_ASSERT_EQUAL_UINT16(original.flapsRawAdc, parsed.flapsRawAdc);
    }

    // EFIS fields
    if (original.efisEnabled && !original.efisIsVn300) {
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisIasKt, parsed.efisIasKt);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisPitchDeg, parsed.efisPitchDeg);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisRollDeg, parsed.efisRollDeg);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisLateralG, parsed.efisLateralG);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisVerticalG, parsed.efisVerticalG);
        TEST_ASSERT_EQUAL_INT(original.efisPercentLift, parsed.efisPercentLift);
        TEST_ASSERT_EQUAL_INT(original.efisPaltFt, parsed.efisPaltFt);
        TEST_ASSERT_EQUAL_INT(original.efisVsiFpm, parsed.efisVsiFpm);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisTasKt, parsed.efisTasKt);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisOatCelsius, parsed.efisOatCelsius);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisFuelRemaining, parsed.efisFuelRemaining);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisFuelFlow, parsed.efisFuelFlow);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.efisMap, parsed.efisMap);
        TEST_ASSERT_EQUAL_INT(original.efisRpm, parsed.efisRpm);
        TEST_ASSERT_EQUAL_INT(original.efisPercentPower, parsed.efisPercentPower);
        TEST_ASSERT_EQUAL_INT(original.efisMagHeading, parsed.efisMagHeading);
        TEST_ASSERT_EQUAL_INT(original.efisAgeMs, parsed.efisAgeMs);
        TEST_ASSERT_EQUAL_UINT32(original.efisTimestampMs, parsed.efisTimestampMs);
    }

    if (original.efisEnabled && original.efisIsVn300) {
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnAngularRateRoll,  parsed.vnAngularRateRoll);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnAngularRatePitch, parsed.vnAngularRatePitch);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnAngularRateYaw,   parsed.vnAngularRateYaw);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnVelNedNorth,      parsed.vnVelNedNorth);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnVelNedEast,       parsed.vnVelNedEast);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnVelNedDown,       parsed.vnVelNedDown);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnAccelFwd,         parsed.vnAccelFwd);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnAccelLat,         parsed.vnAccelLat);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnAccelVert,        parsed.vnAccelVert);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnYawDeg,           parsed.vnYawDeg);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnPitchDeg,         parsed.vnPitchDeg);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnRollDeg,          parsed.vnRollDeg);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnLinAccFwd,        parsed.vnLinAccFwd);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnLinAccLat,        parsed.vnLinAccLat);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnLinAccVert,       parsed.vnLinAccVert);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnYawSigma,         parsed.vnYawSigma);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnRollSigma,        parsed.vnRollSigma);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnPitchSigma,       parsed.vnPitchSigma);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnGnssVelNedNorth,  parsed.vnGnssVelNedNorth);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnGnssVelNedEast,   parsed.vnGnssVelNedEast);
        TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vnGnssVelNedDown,   parsed.vnGnssVelNedDown);
        // Use float tolerance (cast to float) since Unity double precision
        // is not enabled in the native test config.
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, (float)original.vnGnssLat, (float)parsed.vnGnssLat);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, (float)original.vnGnssLon, (float)parsed.vnGnssLon);
        TEST_ASSERT_EQUAL_INT(original.vnGpsFix,   parsed.vnGpsFix);
        TEST_ASSERT_EQUAL_INT(original.vnDataAgeMs, parsed.vnDataAgeMs);
        TEST_ASSERT_EQUAL_STRING(original.vnTimeUtc, parsed.vnTimeUtc);
    }

    // Derived columns (always present)
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.earthVerticalG, parsed.earthVerticalG);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.flightPathDeg,  parsed.flightPathDeg);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.vsiFpm,         parsed.vsiFpm);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.altitudeFt,     parsed.altitudeFt);
    TEST_ASSERT_FLOAT_WITHIN(kTolAoa, original.derivedAoaDeg,  parsed.derivedAoaDeg);
    TEST_ASSERT_FLOAT_WITHIN(kTolAoa, original.coeffP,         parsed.coeffP);
}

// ---------------------------------------------------------------------------
// setUp / tearDown
// ---------------------------------------------------------------------------

void setUp(void)
{
    memset(s_hdrBuf, 0, sizeof(s_hdrBuf));
    memset(s_rowBuf, 0, sizeof(s_rowBuf));
    memset(s_rowBuf2, 0, sizeof(s_rowBuf2));
}

void tearDown(void) {}

// ============================================================================
// Test: header stability
// ============================================================================

void test_header_core_columns_present(void)
{
    LogRow r;   // no optional columns
    size_t len = csv::WriteHeader(r, s_hdrBuf, sizeof(s_hdrBuf));
    TEST_ASSERT_GREATER_THAN(0u, len);

    // Spot-check a selection of required column names
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "timeStamp"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "PfwdSmoothed"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "P45Smoothed"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "Palt"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "IAS"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "AngleofAttack"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "flapsPos"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "DataMark"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "OAT"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "TAS"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "imuTemp"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "VerticalG"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "LateralG"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "ForwardG"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "RollRate"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "PitchRate"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "YawRate"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "Pitch"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "Roll"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "EarthVerticalG"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "FlightPath"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "VSI"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "Altitude"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "DerivedAOA"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "CoeffP"));
}

void test_header_boom_columns_present_when_enabled(void)
{
    LogRow r;
    r.boomEnabled = true;
    size_t len = csv::WriteHeader(r, s_hdrBuf, sizeof(s_hdrBuf));
    TEST_ASSERT_GREATER_THAN(0u, len);
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "boomStatic"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "boomAlpha"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "boomAge"));
}

void test_header_boom_columns_absent_when_disabled(void)
{
    LogRow r;
    r.boomEnabled = false;
    size_t len = csv::WriteHeader(r, s_hdrBuf, sizeof(s_hdrBuf));
    TEST_ASSERT_GREATER_THAN(0u, len);
    TEST_ASSERT_NULL(strstr(s_hdrBuf, "boomStatic"));
}

void test_header_efis_columns_present_when_enabled(void)
{
    LogRow r;
    r.efisEnabled = true;
    r.efisIsVn300 = false;
    size_t len = csv::WriteHeader(r, s_hdrBuf, sizeof(s_hdrBuf));
    TEST_ASSERT_GREATER_THAN(0u, len);
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "efisIAS"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "efisPercentLift"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "efisTime"));
}

void test_header_vn300_columns_present_when_enabled(void)
{
    LogRow r;
    r.efisEnabled = true;
    r.efisIsVn300 = true;
    size_t len = csv::WriteHeader(r, s_hdrBuf, sizeof(s_hdrBuf));
    TEST_ASSERT_GREATER_THAN(0u, len);
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "vnYaw"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "vnGnssLat"));
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "vnTimeUTC"));
    TEST_ASSERT_NULL(strstr(s_hdrBuf, "efisIAS"));
}

// ============================================================================
// Test: header — flapsRawADC column
// ============================================================================

void test_header_flaps_raw_adc_present_when_enabled(void)
{
    LogRow r;
    r.flapsRawAdcPresent = true;
    size_t len = csv::WriteHeader(r, s_hdrBuf, sizeof(s_hdrBuf));
    TEST_ASSERT_GREATER_THAN(0u, len);
    TEST_ASSERT_NOT_NULL(strstr(s_hdrBuf, "flapsRawADC"));
}

void test_header_flaps_raw_adc_absent_when_disabled(void)
{
    LogRow r;
    r.flapsRawAdcPresent = false;
    size_t len = csv::WriteHeader(r, s_hdrBuf, sizeof(s_hdrBuf));
    TEST_ASSERT_GREATER_THAN(0u, len);
    TEST_ASSERT_NULL(strstr(s_hdrBuf, "flapsRawADC"));
}

// ============================================================================
// Test: round-trip (core only)
// ============================================================================

void test_roundtrip_core_only(void)
{
    LogRow original = MakeTestRow(false, false, false);
    AssertRoundTrip(original);
}

void test_roundtrip_with_flaps_raw_adc(void)
{
    // The whole point of the column: replay tools recover g_Flaps.uValue
    // from the SD log so they can drive the L/Dmax pip interpolation.
    LogRow original = MakeTestRow(false, false, false, /*flapsRawAdc=*/true);
    original.flapsRawAdc = 897;   // representative pot reading at flaps-16 detent
    AssertRoundTrip(original);
}

void test_roundtrip_flaps_raw_adc_extreme_values(void)
{
    // Cover the uint16 boundary so a future producer that emits a wider
    // counts range (12-bit ADC: 0..4095) doesn't silently clamp.
    LogRow original = MakeTestRow(true, true, false, /*flapsRawAdc=*/true);
    original.flapsRawAdc = 0;
    AssertRoundTrip(original);
    original.flapsRawAdc = 4095;
    AssertRoundTrip(original);
    original.flapsRawAdc = 0xFFFFu;
    AssertRoundTrip(original);
}

void test_roundtrip_with_boom(void)
{
    LogRow original = MakeTestRow(true, false, false);
    AssertRoundTrip(original);
}

void test_roundtrip_with_efis(void)
{
    LogRow original = MakeTestRow(false, true, false);
    AssertRoundTrip(original);
}

void test_roundtrip_with_vn300(void)
{
    LogRow original = MakeTestRow(false, true, true);
    AssertRoundTrip(original);
}

void test_roundtrip_boom_and_efis(void)
{
    LogRow original = MakeTestRow(true, true, false);
    AssertRoundTrip(original);
}

// ============================================================================
// Test: issue #182 — PitchRate sign flip lives in FormatRow
// ============================================================================

void test_pitch_rate_sign_flip_in_format_row(void)
{
    // Positive raw imuPitchRateDps → CSV PitchRate should be negative.
    LogRow r;
    r.imuPitchRateDps = 0.654321f;
    csv::FormatRow(r, s_rowBuf, sizeof(s_rowBuf));

    // The formatted row has fields separated by commas.  PitchRate is column 19
    // (1-indexed), so count 18 commas from the start and check the value after.
    const char* p = s_rowBuf;
    for (int i = 0; i < 18; ++i) {
        p = strchr(p, ',');
        TEST_ASSERT_NOT_NULL(p);
        ++p;
    }
    // p now points at the PitchRate field.
    float pitchRateCsv = strtof(p, nullptr);
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, -0.654321f, pitchRateCsv);
}

void test_pitch_rate_roundtrip_preserves_raw_value(void)
{
    // LogRow.imuPitchRateDps = -0.654321 (raw).
    // FormatRow emits -(-0.654321) = +0.654321 in the CSV.
    // ParseRow reads +0.654321 from CSV and stores -0.654321 back into LogRow.
    LogRow original;
    original.imuPitchRateDps = -0.654321f;
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow parsed;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(kTolHigh, -0.654321f, parsed.imuPitchRateDps);
}

// ============================================================================
// Test: edge values
// ============================================================================

void test_zero_row_formats_and_parses(void)
{
    LogRow r;   // all fields default to zero
    size_t fmtLen = csv::FormatRow(r, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow parsed;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0u, parsed.timeStampMs);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, 0.0f, parsed.iasKt);
    TEST_ASSERT_FLOAT_WITHIN(kTolAoa, 0.0f, parsed.derivedAoaDeg);
}

void test_large_timestamp(void)
{
    LogRow r;
    r.timeStampMs = 0xFFFFFFFEu;   // near uint32 max
    size_t fmtLen = csv::FormatRow(r, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow parsed;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFEu, parsed.timeStampMs);
}

void test_empty_vn300_utc_string(void)
{
    LogRow r;
    r.efisEnabled = true;
    r.efisIsVn300 = true;
    // vnTimeUtc is all zeros (empty string) — default-initialised
    size_t fmtLen = csv::FormatRow(r, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow parsed;
    parsed.efisEnabled = true;
    parsed.efisIsVn300 = true;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("", parsed.vnTimeUtc);
}

// Issue #194: vnTimeUtc is the last column of a VN-300 row and is emitted
// as `%s` with no quoting. An embedded comma would split into the next
// column and corrupt every downstream parser. FormatRow refuses to emit
// such a row rather than silently corrupting the log.
void test_vn300_utc_with_comma_refuses_to_format(void)
{
    LogRow r = MakeTestRow(false, true, true);   // EFIS + VN-300
    // Inject a comma into vnTimeUtc. A future format change that produced
    // "2026-04-24 14:30:00,42" would trigger this.
    snprintf(r.vnTimeUtc, sizeof(r.vnTimeUtc), "12:34:56,78");

    size_t fmtLen = csv::FormatRow(r, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_EQUAL_size_t(0u, fmtLen);
}

// ============================================================================
// Test: malformed input
// ============================================================================

void test_empty_line_returns_false(void)
{
    LogRow r;
    TEST_ASSERT_FALSE(csv::ParseRow("", r));
}

void test_too_few_columns_returns_false(void)
{
    // Provide only a timestamp — far fewer than the required 46+ columns.
    LogRow r;
    TEST_ASSERT_FALSE(csv::ParseRow("12345,1,1.00", r));
}

// Issue #195: tests for truncated boom / VN-300 / EFIS sections.
//
// These exercise the optional-group boundaries.  ParseRow's contract:
// when the receiver's `boomEnabled`/`efisEnabled` flags say a section
// must be present, but the row's columns for that section are missing
// or short, the row is rejected (returns false) — never partially
// populated.

// Helper: drop the last `n` characters off a row to simulate a
// section truncated mid-field.
static std::string TruncateBy(const char* row, size_t fmtLen, size_t n)
{
    return std::string(row, fmtLen > n ? fmtLen - n : 0);
}

// Helper: drop the last `n` complete fields off a row by walking back
// from the end and removing characters up to and including `n` commas.
static std::string DropLastFields(const char* row, size_t fmtLen, int nFields)
{
    std::string s(row, fmtLen);
    while (nFields-- > 0) {
        size_t comma = s.rfind(',');
        if (comma == std::string::npos) {
            s.clear();
            return s;
        }
        s.resize(comma);
    }
    return s;
}

void test_truncated_boom_section_returns_false(void)
{
    // Boom section has 6 columns: static, dynamic, alpha, beta, ias, age.
    // Format a complete boom row, drop the last 3 boom fields, and
    // verify ParseRow rejects.
    LogRow original = MakeTestRow(true, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Boom is the LAST optional group when EFIS is disabled, and
    // the post-EFIS derived columns (4) plus DerivedAOA/CoeffP (2)
    // come AFTER it.  So the layout is:
    //   <core (33)> , <boom (6)> , <derived (4)> , <aoa+coeffP (2)>
    // Drop the last 6 fields = derived + aoa/coeffP, then drop 3 more
    // to truncate inside the boom section.
    std::string truncated = DropLastFields(s_rowBuf, fmtLen, 6 + 3);

    LogRow parsed;
    parsed.boomEnabled = true;
    TEST_ASSERT_FALSE(csv::ParseRow(truncated, parsed));
}

void test_truncated_efis_section_returns_false(void)
{
    // EFIS (non-VN300) section has 18 columns. Truncate near the end.
    LogRow original = MakeTestRow(false, true, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Drop derived (4) + aoa/coeffP (2) = 6, then 5 more to truncate
    // mid-EFIS-section.
    std::string truncated = DropLastFields(s_rowBuf, fmtLen, 6 + 5);

    LogRow parsed;
    parsed.efisEnabled = true;
    parsed.efisIsVn300 = false;
    TEST_ASSERT_FALSE(csv::ParseRow(truncated, parsed));
}

void test_truncated_vn300_section_returns_false(void)
{
    // VN-300 section has 26 columns. Truncate inside it.
    LogRow original = MakeTestRow(false, true, true);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Drop derived (4) + aoa/coeffP (2) = 6, then 10 more to truncate
    // deep inside the VN-300 section.
    std::string truncated = DropLastFields(s_rowBuf, fmtLen, 6 + 10);

    LogRow parsed;
    parsed.efisEnabled = true;
    parsed.efisIsVn300 = true;
    TEST_ASSERT_FALSE(csv::ParseRow(truncated, parsed));
}

void test_efis_flag_set_but_no_efis_columns_returns_false(void)
{
    // Producer wrote a no-EFIS row (33 core + 4 derived + 2 aoa = 39
    // columns).  Receiver expects EFIS columns but they're absent.
    LogRow producer;
    size_t fmtLen = csv::FormatRow(producer, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow consumer;
    consumer.efisEnabled = true;
    consumer.efisIsVn300 = false;
    TEST_ASSERT_FALSE(csv::ParseRow(std::string_view(s_rowBuf, fmtLen),
                                    consumer));
}

void test_vn300_flag_set_but_only_efis_columns_returns_false(void)
{
    // Producer wrote a non-VN300 EFIS row.  Receiver expects VN-300
    // columns (26 of them) but only finds 18 EFIS columns.
    LogRow producer = MakeTestRow(false, true, false);
    size_t fmtLen = csv::FormatRow(producer, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow consumer;
    consumer.efisEnabled = true;
    consumer.efisIsVn300 = true;
    TEST_ASSERT_FALSE(csv::ParseRow(std::string_view(s_rowBuf, fmtLen),
                                    consumer));
}

void test_row_truncated_mid_field_returns_false(void)
{
    // Simulate fgets truncating mid-row at the read buffer boundary
    // (LogReplay::OpenReplayLog uses kRowMaxBytes; if a row exceeds
    // that, fgets returns a partial line with no \n).  The resulting
    // partial token must fail to parse.
    LogRow original = MakeTestRow(true, true, true);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Hard-truncate at half the row length — likely lands mid-field.
    std::string truncated = TruncateBy(s_rowBuf, fmtLen, fmtLen / 2);

    LogRow parsed;
    parsed.boomEnabled = true;
    parsed.efisEnabled = true;
    parsed.efisIsVn300 = true;
    TEST_ASSERT_FALSE(csv::ParseRow(truncated, parsed));
}

// Issue #193: a truncated SD write that produces two adjacent commas in
// any numeric column must fail the row, not silently substitute zero.
// Verify each numeric Parse* helper (float, int, uint32) rejects empty.

void test_empty_timestamp_returns_false(void)
{
    // First column (timeStampMs / ParseUint32) blanked: ",pfwdCounts,..."
    // Build a row that's well-formed except for an empty timestamp.
    LogRow original = MakeTestRow(false, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Replace "12345," prefix with "," (drop digits, keep the comma).
    std::string mutated(s_rowBuf, fmtLen);
    size_t firstComma = mutated.find(',');
    TEST_ASSERT_NOT_EQUAL(std::string::npos, firstComma);
    mutated = "," + mutated.substr(firstComma + 1);

    LogRow r;
    TEST_ASSERT_FALSE(csv::ParseRow(mutated, r));
}

void test_empty_int_field_returns_false(void)
{
    // Second column is pfwdCounts (ParseInt). Blank it.
    LogRow original = MakeTestRow(false, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    std::string mutated(s_rowBuf, fmtLen);
    size_t c1 = mutated.find(',');
    size_t c2 = mutated.find(',', c1 + 1);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, c2);
    mutated = mutated.substr(0, c1 + 1) + mutated.substr(c2);

    LogRow r;
    TEST_ASSERT_FALSE(csv::ParseRow(mutated, r));
}

void test_empty_float_field_returns_false(void)
{
    // Third column is pfwdSmoothed (ParseFloat). Blank it.
    LogRow original = MakeTestRow(false, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    std::string mutated(s_rowBuf, fmtLen);
    size_t c1 = mutated.find(',');
    size_t c2 = mutated.find(',', c1 + 1);
    size_t c3 = mutated.find(',', c2 + 1);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, c3);
    mutated = mutated.substr(0, c2 + 1) + mutated.substr(c3);

    LogRow r;
    TEST_ASSERT_FALSE(csv::ParseRow(mutated, r));
}

void test_empty_ias_aoa_only_returns_false(void)
{
    // Format version 3 gates IAS, AngleofAttack, and DerivedAOA together.
    // A row with only IAS+AoA empty (DerivedAOA still numeric) is a
    // corrupt write and must be rejected — empty for one but not the
    // other can't come from a consistent producer.
    LogRow original = MakeTestRow(false, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Blank only IAS (column 8) and AngleofAttack (column 9), leaving
    // DerivedAOA numeric.
    std::string mutated(s_rowBuf, fmtLen);
    size_t pos = 0;
    for (int i = 0; i < 7; ++i) {
        pos = mutated.find(',', pos) + 1;
        TEST_ASSERT_GREATER_THAN(0u, pos);
    }
    size_t aoaEnd = mutated.find(',', mutated.find(',', pos) + 1);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, aoaEnd);
    mutated = mutated.substr(0, pos) + mutated.substr(aoaEnd);

    LogRow r;
    TEST_ASSERT_FALSE(csv::ParseRow(mutated, r));
}

// ============================================================================
// Test: format-version-3 air-data validity gate
//
// FormatRow emits empty cells for IAS, AngleofAttack, DerivedAOA, and
// efisPercentLift when the producer's iasValid (resp. efisPercentLiftValid)
// flag is false.  ParseRow round-trips the empty state to NaN / 0 + flag.
// This is the same convention as the M5 wire / WebSocket JSON in PR #431.
// ============================================================================

// FormatRow with iasValid=false produces empty cells at column 8/9
// (IAS / AngleofAttack) and the DerivedAOA position.
void test_invalid_ias_emits_empty_ias_and_aoa_cells(void)
{
    LogRow r = MakeTestRow(false, false, false);
    r.iasValid = false;

    size_t fmtLen = csv::FormatRow(r, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Walk to the boundary between paltFt (col 7) and flapsPos.  In a
    // valid row the IAS and AngleofAttack values sit there; in an
    // invalid row both cells are empty, leaving four consecutive commas
    // ",," after paltFt and before flapsPos.
    s_rowBuf[fmtLen] = '\0';
    // After paltFt we expect ",,,<flapsPos>" — comma after paltFt, then
    // empty IAS, empty AOA, comma before flapsPos.
    // Walk past 7 commas to land at the start of IAS.
    const char* p = s_rowBuf;
    for (int i = 0; i < 7; ++i) {
        p = strchr(p, ',');
        TEST_ASSERT_NOT_NULL(p);
        ++p;
    }
    // p points right after the 7th comma — start of IAS cell.
    // Empty IAS + empty AOA = ",," (then the comma before flapsPos).
    TEST_ASSERT_EQUAL_CHAR(',', p[0]);   // empty IAS, immediate comma
    TEST_ASSERT_EQUAL_CHAR(',', p[1]);   // empty AOA, immediate comma
    // The third character must NOT be a comma (flapsPos starts here).
    TEST_ASSERT_NOT_EQUAL(',', p[2]);
}

// Round-trip: invalid producer state survives Format -> Parse.
void test_invalid_ias_roundtrip(void)
{
    LogRow original = MakeTestRow(false, true, false);   // EFIS, no boom
    original.iasValid             = false;
    original.efisPercentLiftValid = false;

    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow parsed;
    parsed.efisEnabled = true;
    parsed.efisIsVn300 = false;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_FALSE(parsed.iasValid);
    TEST_ASSERT_FALSE(parsed.efisPercentLiftValid);
    TEST_ASSERT_TRUE(std::isnan(parsed.iasKt));
    TEST_ASSERT_TRUE(std::isnan(parsed.angleOfAttackDeg));
    TEST_ASSERT_TRUE(std::isnan(parsed.derivedAoaDeg));
    TEST_ASSERT_EQUAL_INT(0, parsed.efisPercentLift);
    // CoeffP must remain numeric — it's a raw pressure ratio, not gated.
    TEST_ASSERT_FLOAT_WITHIN(kTolAoa, original.coeffP, parsed.coeffP);
}

// Default LogRow (iasValid=true, efisPercentLiftValid=true) must produce
// the same byte string as before format-version-3 — i.e. round-trips like
// the existing fixture rows.
void test_valid_default_emits_numeric_cells(void)
{
    LogRow r = MakeTestRow(false, true, false);
    // r.iasValid and r.efisPercentLiftValid default to true.
    AssertRoundTrip(r);
}

// efisPercentLift only: float gate stays open (iasValid=true), int gate
// closed.  Catches a bug where a single boolean accidentally dual-gates.
void test_efis_percent_lift_invalid_only(void)
{
    LogRow original = MakeTestRow(false, true, false);
    original.iasValid             = true;
    original.efisPercentLiftValid = false;

    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow parsed;
    parsed.efisEnabled = true;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(parsed.iasValid);
    TEST_ASSERT_FALSE(parsed.efisPercentLiftValid);
    TEST_ASSERT_FLOAT_WITHIN(kTolLow, original.iasKt, parsed.iasKt);
    TEST_ASSERT_EQUAL_INT(0, parsed.efisPercentLift);
}

// VN-300 row carries no efisPercentLift column; iasValid still applies
// to IAS/AngleofAttack/DerivedAOA.
void test_invalid_ias_roundtrip_vn300(void)
{
    LogRow original = MakeTestRow(false, true, true);
    original.iasValid = false;

    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow parsed;
    parsed.efisEnabled = true;
    parsed.efisIsVn300 = true;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(parsed.iasValid);
    TEST_ASSERT_TRUE(std::isnan(parsed.iasKt));
    TEST_ASSERT_TRUE(std::isnan(parsed.angleOfAttackDeg));
    TEST_ASSERT_TRUE(std::isnan(parsed.derivedAoaDeg));
}

// An empty cell in a non-gated column (e.g. paltFt) must still reject —
// the truncation-detection guarantee survives the format-3 exception.
void test_empty_palt_field_still_rejected(void)
{
    LogRow original = MakeTestRow(false, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // paltFt is column 7 (index 6). Blank it.
    std::string mutated(s_rowBuf, fmtLen);
    size_t pos = 0;
    for (int i = 0; i < 6; ++i) {
        pos = mutated.find(',', pos) + 1;
    }
    size_t next = mutated.find(',', pos);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, next);
    mutated = mutated.substr(0, pos) + mutated.substr(next);

    LogRow r;
    TEST_ASSERT_FALSE(csv::ParseRow(mutated, r));
}

void test_trailing_newline_tolerated(void)
{
    // A correctly-formatted core row with a trailing \n should parse OK.
    LogRow original = MakeTestRow(false, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Append a newline to simulate a raw file line.
    std::string withNewline(s_rowBuf, fmtLen);
    withNewline += '\n';

    LogRow parsed;
    bool ok = csv::ParseRow(withNewline, parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(original.timeStampMs, parsed.timeStampMs);
    TEST_ASSERT_FLOAT_WITHIN(kTolAoa, original.derivedAoaDeg, parsed.derivedAoaDeg);
}

void test_trailing_crlf_tolerated(void)
{
    LogRow original = MakeTestRow(false, false, false);
    size_t fmtLen = csv::FormatRow(original, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    std::string withCrLf(s_rowBuf, fmtLen);
    withCrLf += "\r\n";

    LogRow parsed;
    bool ok = csv::ParseRow(withCrLf, parsed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(original.timeStampMs, parsed.timeStampMs);
}

// ============================================================================
// Test: fixture rows from a real flight log
//
// These 5 rows are lifted verbatim from sam_onspeed_aoa_4_11_2026.csv.
// They have EFIS (non-VN300) columns but no boom columns.
// Test: ParseRow -> FormatRow -> byte-identical to the original fixture line.
//
// Note: the original file was produced by the old LogSensor::Write() which
// emitted -Gy directly as PitchRate.  After the refactor, FormatRow also
// emits -imuPitchRateDps.  So the fixture parser must set efisEnabled=true
// and efisIsVn300=false.
// ============================================================================

// Sample rows (5 data lines from the real log, efis non-VN300, no boom).
static const char* kFixtureRows[] = {
    "2094,0,0.00,2,2.00,834.51,5242.12,3.12,-20.00,0,0,0.00,0.00,11.57,1.017090,0.009033,-0.049561,0.054282,-0.307396,-0.183381,1.09,-0.32,0.00,0.00,0.00,0.00,0.00,0,0,0,0.00,0.00,0.00,0.00,0.00,0,0,-1,2092,2,0.02,0.00,0.00,5235.73,1.0939,0.0000",
    "2115,2,0.50,2,2.00,834.51,5242.12,3.12,-20.00,0,0,0.00,3.45,11.57,1.019531,0.009766,-0.048584,0.091666,-0.157860,-0.101136,1.12,-0.33,0.00,0.00,0.00,0.00,0.00,0,0,0,0.00,0.00,0.00,0.00,0.00,0,0,-1,2113,2,0.02,0.00,0.00,5235.54,1.1150,4.0000",
    "2134,2,1.00,2,2.00,834.51,5242.12,4.03,-20.00,0,0,0.00,3.45,11.57,1.019531,0.011475,-0.049561,0.061758,-0.217675,0.063353,1.14,-0.33,0.00,0.00,0.00,0.00,0.00,0,0,0,0.00,0.00,0.00,0.00,0.00,0,0,-1,2132,2,0.02,0.00,0.00,5240.78,1.1434,2.0000",
    "2153,2,1.25,2,2.00,834.51,5242.12,4.03,-20.00,0,0,0.00,4.45,11.57,1.026855,0.009277,-0.052002,0.039328,-0.210198,0.025969,1.12,-0.33,0.00,0.00,0.00,0.00,0.00,0,0,0,0.00,0.00,0.00,0.00,0.00,0,0,-1,2152,2,0.03,0.00,0.00,5242.81,1.1152,1.6000",
    "2174,2,1.40,5,2.00,834.76,5234.33,4.03,-20.00,0,0,0.00,4.45,11.57,1.025635,0.009033,-0.052734,0.001944,-0.180291,0.085784,1.09,-0.33,0.00,0.00,0.00,0.00,0.00,0,0,0,0.00,0.00,0.00,0.00,0.00,0,0,-1,2172,2,0.03,0.00,0.00,5240.60,1.0866,1.4286",
};

static constexpr int kFixtureRowCount = 5;

// Old-log compat: a row written before flapsRawADC existed must parse
// cleanly when the consumer's flag is clear, and report a default-zero
// flapsRawAdc field.
void test_old_log_without_flaps_raw_adc_defaults_to_zero(void)
{
    // Format a row WITHOUT the column, then parse it back without the flag.
    LogRow producer = MakeTestRow(false, true, false, /*flapsRawAdc=*/false);
    size_t fmtLen = csv::FormatRow(producer, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // The emitted row must not contain the column's value as a trailing field.
    // (Equivalently: byte-identical to a row produced before this PR.)
    LogRow consumer;
    consumer.efisEnabled        = true;
    consumer.efisIsVn300        = false;
    consumer.flapsRawAdcPresent = false;
    bool ok = csv::ParseRow(std::string_view(s_rowBuf, fmtLen), consumer);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(0u, consumer.flapsRawAdc);
}

// Consumer flag set but column actually absent: row must be rejected.
// Catches a future bug where the producer drops the column without the
// consumer noticing.
void test_flaps_raw_adc_flag_set_but_column_absent_returns_false(void)
{
    LogRow producer = MakeTestRow(false, true, false, /*flapsRawAdc=*/false);
    size_t fmtLen = csv::FormatRow(producer, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    LogRow consumer;
    consumer.efisEnabled        = true;
    consumer.efisIsVn300        = false;
    consumer.flapsRawAdcPresent = true;
    TEST_ASSERT_FALSE(csv::ParseRow(std::string_view(s_rowBuf, fmtLen), consumer));
}

// Out-of-range token (>0xFFFF) must fail uint16 parse.
void test_flaps_raw_adc_overflow_returns_false(void)
{
    LogRow producer = MakeTestRow(false, false, false, /*flapsRawAdc=*/true);
    size_t fmtLen = csv::FormatRow(producer, s_rowBuf, sizeof(s_rowBuf));
    TEST_ASSERT_GREATER_THAN(0u, fmtLen);

    // Replace the trailing flapsRawADC value with one that overflows uint16.
    std::string mutated(s_rowBuf, fmtLen);
    size_t lastComma = mutated.rfind(',');
    TEST_ASSERT_NOT_EQUAL(std::string::npos, lastComma);
    mutated = mutated.substr(0, lastComma + 1) + "70000";

    LogRow consumer;
    consumer.flapsRawAdcPresent = true;
    TEST_ASSERT_FALSE(csv::ParseRow(mutated, consumer));
}

// For each fixture row: parse it, re-format it, and assert byte-identical output.
void test_fixture_row_parse_format_identical(void)
{
    for (int i = 0; i < kFixtureRowCount; ++i) {
        const char* fixtureRow = kFixtureRows[i];

        // Parse the fixture row.
        LogRow parsed;
        parsed.efisEnabled = true;
        parsed.efisIsVn300 = false;
        bool ok = csv::ParseRow(fixtureRow, parsed);
        TEST_ASSERT_TRUE_MESSAGE(ok, fixtureRow);

        // Re-format and compare byte-for-byte.
        size_t fmtLen = csv::FormatRow(parsed, s_rowBuf, sizeof(s_rowBuf));
        TEST_ASSERT_GREATER_THAN(0u, fmtLen);

        // s_rowBuf is not null-terminated at fmtLen, but strcmp needs it.
        s_rowBuf[fmtLen] = '\0';
        TEST_ASSERT_EQUAL_STRING(fixtureRow, s_rowBuf);
    }
}

// ============================================================================
// Test: null / zero-capacity guards
// ============================================================================

void test_format_row_null_buf_returns_zero(void)
{
    LogRow r;
    size_t len = csv::FormatRow(r, nullptr, 100);
    TEST_ASSERT_EQUAL(0u, len);
}

void test_format_row_zero_cap_returns_zero(void)
{
    LogRow r;
    char tiny[1] = {};
    size_t len = csv::FormatRow(r, tiny, 0);
    TEST_ASSERT_EQUAL(0u, len);
}

void test_write_header_null_buf_returns_zero(void)
{
    LogRow r;
    size_t len = csv::WriteHeader(r, nullptr, 100);
    TEST_ASSERT_EQUAL(0u, len);
}

// ============================================================================
// main
// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();

    // Header stability
    RUN_TEST(test_header_core_columns_present);
    RUN_TEST(test_header_boom_columns_present_when_enabled);
    RUN_TEST(test_header_boom_columns_absent_when_disabled);
    RUN_TEST(test_header_efis_columns_present_when_enabled);
    RUN_TEST(test_header_vn300_columns_present_when_enabled);
    RUN_TEST(test_header_flaps_raw_adc_present_when_enabled);
    RUN_TEST(test_header_flaps_raw_adc_absent_when_disabled);

    // Round-trip
    RUN_TEST(test_roundtrip_core_only);
    RUN_TEST(test_roundtrip_with_boom);
    RUN_TEST(test_roundtrip_with_efis);
    RUN_TEST(test_roundtrip_with_vn300);
    RUN_TEST(test_roundtrip_boom_and_efis);
    RUN_TEST(test_roundtrip_with_flaps_raw_adc);
    RUN_TEST(test_roundtrip_flaps_raw_adc_extreme_values);

    // Old-log compat for tail-optional flapsRawADC
    RUN_TEST(test_old_log_without_flaps_raw_adc_defaults_to_zero);
    RUN_TEST(test_flaps_raw_adc_flag_set_but_column_absent_returns_false);
    RUN_TEST(test_flaps_raw_adc_overflow_returns_false);

    // Issue #182: sign flip
    RUN_TEST(test_pitch_rate_sign_flip_in_format_row);
    RUN_TEST(test_pitch_rate_roundtrip_preserves_raw_value);

    // Edge values
    RUN_TEST(test_zero_row_formats_and_parses);
    RUN_TEST(test_large_timestamp);
    RUN_TEST(test_empty_vn300_utc_string);
    RUN_TEST(test_vn300_utc_with_comma_refuses_to_format);

    // Malformed input
    RUN_TEST(test_empty_line_returns_false);
    RUN_TEST(test_too_few_columns_returns_false);
    RUN_TEST(test_empty_timestamp_returns_false);
    RUN_TEST(test_empty_int_field_returns_false);
    RUN_TEST(test_empty_float_field_returns_false);
    RUN_TEST(test_empty_ias_aoa_only_returns_false);
    RUN_TEST(test_empty_palt_field_still_rejected);

    // Format-version-3 air-data validity gate
    RUN_TEST(test_invalid_ias_emits_empty_ias_and_aoa_cells);
    RUN_TEST(test_invalid_ias_roundtrip);
    RUN_TEST(test_valid_default_emits_numeric_cells);
    RUN_TEST(test_efis_percent_lift_invalid_only);
    RUN_TEST(test_invalid_ias_roundtrip_vn300);
    RUN_TEST(test_truncated_boom_section_returns_false);
    RUN_TEST(test_truncated_efis_section_returns_false);
    RUN_TEST(test_truncated_vn300_section_returns_false);
    RUN_TEST(test_efis_flag_set_but_no_efis_columns_returns_false);
    RUN_TEST(test_vn300_flag_set_but_only_efis_columns_returns_false);
    RUN_TEST(test_row_truncated_mid_field_returns_false);
    RUN_TEST(test_trailing_newline_tolerated);
    RUN_TEST(test_trailing_crlf_tolerated);

    // Fixture rows from real flight log
    RUN_TEST(test_fixture_row_parse_format_identical);

    // Null / zero-capacity guards
    RUN_TEST(test_format_row_null_buf_returns_zero);
    RUN_TEST(test_format_row_zero_cap_returns_zero);
    RUN_TEST(test_write_header_null_buf_returns_zero);

    return UNITY_END();
}
