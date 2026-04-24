// test_log_meta.cpp
//
// Unit tests for onspeed::log — LogMetaFile (Write/Parse) and LogMetaBuilder.

#include <unity.h>

#include <cstring>
#include <string_view>

#include <log/LogMeta.h>
#include <log/LogMetaBuilder.h>
#include <log/LogMetaFile.h>
#include <types/LogRow.h>

using onspeed::log::LogMeta;
using onspeed::log::LogMetaBuilder;
using onspeed::log::EfisType;
namespace lm = onspeed::log;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static LogMeta MakeFullMeta()
{
    LogMeta m;
    m.metaVersion      = 1;
    m.logFormatVersion = 1;
    strncpy(m.firmware,    "4.19.0",  sizeof(m.firmware)    - 1);
    strncpy(m.firmwareSha, "abc1234", sizeof(m.firmwareSha) - 1);
    m.durationMs       = 5432100u;
    m.rowCount         = 271605u;
    m.maxIasKt         = 142.3f;
    m.maxPaltFt        = 8420.0f;
    m.efisType         = EfisType::Vn300;
    m.gpsFixSeen       = true;
    strncpy(m.utcStart,       "2026-04-18T14:32:07Z", sizeof(m.utcStart)       - 1);
    strncpy(m.timeOfDayStart, "14:32:07",             sizeof(m.timeOfDayStart) - 1);
    return m;
}

// ---------------------------------------------------------------------------
// EfisType round-trip
// ---------------------------------------------------------------------------

static void test_efis_type_to_from_string()
{
    TEST_ASSERT_EQUAL_STRING("none",   lm::EfisTypeToString(EfisType::None));
    TEST_ASSERT_EQUAL_STRING("dynon",  lm::EfisTypeToString(EfisType::Dynon));
    TEST_ASSERT_EQUAL_STRING("garmin", lm::EfisTypeToString(EfisType::Garmin));
    TEST_ASSERT_EQUAL_STRING("mgl",    lm::EfisTypeToString(EfisType::Mgl));
    TEST_ASSERT_EQUAL_STRING("vn300",  lm::EfisTypeToString(EfisType::Vn300));

    TEST_ASSERT_EQUAL(EfisType::None,   lm::EfisTypeFromString("none"));
    TEST_ASSERT_EQUAL(EfisType::Dynon,  lm::EfisTypeFromString("dynon"));
    TEST_ASSERT_EQUAL(EfisType::Garmin, lm::EfisTypeFromString("garmin"));
    TEST_ASSERT_EQUAL(EfisType::Mgl,    lm::EfisTypeFromString("mgl"));
    TEST_ASSERT_EQUAL(EfisType::Vn300,  lm::EfisTypeFromString("vn300"));
    TEST_ASSERT_EQUAL(EfisType::None,   lm::EfisTypeFromString("garbage"));
}

// ---------------------------------------------------------------------------
// Write / parse round-trip
// ---------------------------------------------------------------------------

static void test_round_trip_full_meta()
{
    LogMeta in = MakeFullMeta();
    char buf[512];
    size_t n = lm::WriteMetaFile(in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));

    LogMeta out;
    bool ok = lm::ParseMetaFile(std::string_view(buf, n), &out);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_UINT8(in.metaVersion,      out.metaVersion);
    TEST_ASSERT_EQUAL_INT(in.logFormatVersion,   out.logFormatVersion);
    TEST_ASSERT_EQUAL_STRING(in.firmware,        out.firmware);
    TEST_ASSERT_EQUAL_STRING(in.firmwareSha,     out.firmwareSha);
    TEST_ASSERT_EQUAL_UINT32(in.durationMs,      out.durationMs);
    TEST_ASSERT_EQUAL_UINT32(in.rowCount,        out.rowCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, in.maxIasKt,  out.maxIasKt);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  in.maxPaltFt, out.maxPaltFt);
    TEST_ASSERT_EQUAL(in.efisType,               out.efisType);
    TEST_ASSERT_EQUAL(in.gpsFixSeen,             out.gpsFixSeen);
    TEST_ASSERT_EQUAL_STRING(in.utcStart,        out.utcStart);
    TEST_ASSERT_EQUAL_STRING(in.timeOfDayStart,  out.timeOfDayStart);
}

static void test_round_trip_minimal_meta_no_times()
{
    LogMeta in;
    in.logFormatVersion = 1;
    strncpy(in.firmware,    "4.19.0", sizeof(in.firmware)    - 1);
    strncpy(in.firmwareSha, "def5678", sizeof(in.firmwareSha) - 1);
    in.durationMs  = 12000u;
    in.rowCount    = 600u;
    in.maxIasKt    = 0.0f;
    in.maxPaltFt   = 0.0f;
    in.efisType    = EfisType::None;
    in.gpsFixSeen  = false;
    // utcStart and timeOfDayStart intentionally empty.

    char buf[512];
    size_t n = lm::WriteMetaFile(in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    LogMeta out;
    bool ok = lm::ParseMetaFile(std::string_view(buf, n), &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("",           out.utcStart);
    TEST_ASSERT_EQUAL_STRING("",           out.timeOfDayStart);
    TEST_ASSERT_EQUAL(EfisType::None,      out.efisType);
    TEST_ASSERT_EQUAL(false,               out.gpsFixSeen);
    TEST_ASSERT_EQUAL_UINT32(12000u,       out.durationMs);
}

// ---------------------------------------------------------------------------
// Parser robustness
// ---------------------------------------------------------------------------

static void test_parse_ignores_unknown_keys()
{
    const char* text =
        "meta_version=1\n"
        "firmware=4.19.0\n"
        "some_future_key=hello\n"
        "duration_ms=42\n"
        "row_count=3\n";
    LogMeta out;
    bool ok = lm::ParseMetaFile(text, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("4.19.0", out.firmware);
    TEST_ASSERT_EQUAL_UINT32(42u, out.durationMs);
    TEST_ASSERT_EQUAL_UINT32(3u,  out.rowCount);
}

static void test_parse_empty_input_returns_false()
{
    LogMeta out;
    // Non-default so we can verify it wasn't clobbered.
    out.rowCount = 12345;
    bool ok = lm::ParseMetaFile("", &out);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(12345u, out.rowCount);
}

static void test_parse_tolerates_trailing_garbage()
{
    const char* text =
        "meta_version=1\n"
        "firmware=4.19.0\n"
        "\n"                          // blank line
        "=no_key\n"                   // no key before =
        "no_equals_sign\n"            // no =
        "duration_ms=99\n";
    LogMeta out;
    bool ok = lm::ParseMetaFile(text, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(99u, out.durationMs);
}

static void test_write_buffer_too_small_returns_zero()
{
    LogMeta in = MakeFullMeta();
    char tiny[8];
    size_t n = lm::WriteMetaFile(in, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_size_t(0u, n);
}

// ---------------------------------------------------------------------------
// LogMetaBuilder
// ---------------------------------------------------------------------------

static onspeed::LogRow MakeRow(uint32_t t, float ias, float palt)
{
    onspeed::LogRow r;
    r.timeStampMs = t;
    r.iasKt       = ias;
    r.paltFt      = palt;
    return r;
}

static void test_builder_zero_rows()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::None);
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_UINT32(0u, m.durationMs);
    TEST_ASSERT_EQUAL_UINT32(0u, m.rowCount);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.maxIasKt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.maxPaltFt);
    TEST_ASSERT_EQUAL(false, m.gpsFixSeen);
    TEST_ASSERT_EQUAL(EfisType::None, m.efisType);
}

static void test_builder_duration_and_running_max()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Dynon);
    b.OnRow(MakeRow(1000, 30.0f, 500.0f),  nullptr, nullptr);
    b.OnRow(MakeRow(2000, 60.0f, 2000.0f), nullptr, nullptr);
    b.OnRow(MakeRow(3000, 45.0f, 1500.0f), nullptr, nullptr);   // lower, shouldn't drop max
    b.OnRow(MakeRow(4500, 99.5f, 2200.0f), nullptr, nullptr);
    LogMeta m = b.Finalize();

    TEST_ASSERT_EQUAL_UINT32(3500u, m.durationMs);           // 4500 - 1000
    TEST_ASSERT_EQUAL_UINT32(4u,    m.rowCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 99.5f,  m.maxIasKt);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  2200.0f, m.maxPaltFt);
    TEST_ASSERT_EQUAL(EfisType::Dynon, m.efisType);
    TEST_ASSERT_EQUAL(false, m.gpsFixSeen);                  // no time ever seen
}

static void test_builder_captures_first_time_only()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Dynon);
    b.OnRow(MakeRow(0,    0.0f, 0.0f), nullptr,    nullptr);
    b.OnRow(MakeRow(100,  0.0f, 0.0f), "13:01:02", nullptr);
    b.OnRow(MakeRow(200,  0.0f, 0.0f), "13:01:03", nullptr);    // should NOT overwrite
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_STRING("13:01:02", m.timeOfDayStart);
    TEST_ASSERT_EQUAL(true, m.gpsFixSeen);
}

static void test_builder_captures_first_utc_only()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Vn300);
    b.OnRow(MakeRow(0, 0, 0), nullptr, nullptr);
    b.OnRow(MakeRow(100, 0, 0), "14:32:07", "2026-04-18T14:32:07Z");
    b.OnRow(MakeRow(200, 0, 0), "14:32:08", "2026-04-18T14:32:08Z");   // later, ignored
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_STRING("2026-04-18T14:32:07Z", m.utcStart);
    TEST_ASSERT_EQUAL_STRING("14:32:07",              m.timeOfDayStart);
    TEST_ASSERT_EQUAL(true, m.gpsFixSeen);
}

static void test_builder_empty_time_strings_treated_as_null()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Dynon);
    b.OnRow(MakeRow(0, 0, 0), "",         nullptr);    // empty string = absent
    b.OnRow(MakeRow(100, 0, 0), "13:00:00", nullptr);
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_STRING("13:00:00", m.timeOfDayStart);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_efis_type_to_from_string);
    RUN_TEST(test_round_trip_full_meta);
    RUN_TEST(test_round_trip_minimal_meta_no_times);
    RUN_TEST(test_parse_ignores_unknown_keys);
    RUN_TEST(test_parse_empty_input_returns_false);
    RUN_TEST(test_parse_tolerates_trailing_garbage);
    RUN_TEST(test_write_buffer_too_small_returns_zero);
    RUN_TEST(test_builder_zero_rows);
    RUN_TEST(test_builder_duration_and_running_max);
    RUN_TEST(test_builder_captures_first_time_only);
    RUN_TEST(test_builder_captures_first_utc_only);
    RUN_TEST(test_builder_empty_time_strings_treated_as_null);
    return UNITY_END();
}
