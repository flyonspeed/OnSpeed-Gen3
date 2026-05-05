// test_types_efis_frame.cpp — structural tests for onspeed::EfisFrame

#include <unity.h>
#include <types/EfisFrame.h>

#include <cmath>

using onspeed::EfisFrame;
using onspeed::EfisSource;
using onspeed::kEfisFieldAbsent;

void setUp(void) {}
void tearDown(void) {}

void test_default_initializes_fields_to_absent(void)
{
    // Every numeric field defaults to kEfisFieldAbsent (NaN). Parsers set
    // fields only when they actually decoded a valid value; consumers test
    // std::isfinite() to decide whether to apply or hold the prior value.
    EfisFrame e;
    TEST_ASSERT_FALSE(std::isfinite(e.pitchDeg));
    TEST_ASSERT_FALSE(std::isfinite(e.rollDeg));
    TEST_ASSERT_FALSE(std::isfinite(e.headingDeg));
    TEST_ASSERT_FALSE(std::isfinite(e.iasKt));
    TEST_ASSERT_FALSE(std::isfinite(e.tasKt));
    TEST_ASSERT_FALSE(std::isfinite(e.paltFt));
    TEST_ASSERT_FALSE(std::isfinite(e.vsiFpm));
    TEST_ASSERT_FALSE(std::isfinite(e.oatCelsius));
    TEST_ASSERT_FALSE(std::isfinite(e.aoaPercent));
    TEST_ASSERT_FALSE(std::isfinite(e.lateralG));
    TEST_ASSERT_FALSE(std::isfinite(e.verticalG));
    // Engine fields (Dynon SkyView EMS / Garmin G3X EMS).
    TEST_ASSERT_FALSE(std::isfinite(e.rpm));
    TEST_ASSERT_FALSE(std::isfinite(e.mapInchHg));
    TEST_ASSERT_FALSE(std::isfinite(e.fuelFlowGph));
    TEST_ASSERT_FALSE(std::isfinite(e.fuelRemainingGal));
    TEST_ASSERT_FALSE(std::isfinite(e.percentPower));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::None),
                          static_cast<int>(e.source));
    TEST_ASSERT_EQUAL_UINT32(0u, e.timestampUs);
    // kEfisFieldAbsent is the NaN sentinel itself.
    TEST_ASSERT_FALSE(std::isfinite(kEfisFieldAbsent));
}

void test_fields_are_writable(void)
{
    EfisFrame e;
    e.pitchDeg    =  2.0f;
    e.rollDeg     = -5.0f;
    e.headingDeg  = 270.0f;
    e.iasKt       = 110.0f;
    e.tasKt       = 118.0f;
    e.paltFt      = 5500.0f;
    e.vsiFpm      = 500.0f;
    e.oatCelsius  = 8.0f;
    e.aoaPercent  = 62.0f;
    e.source      = EfisSource::Dynon;
    e.timestampUs = 42000000u;

    TEST_ASSERT_EQUAL_FLOAT(110.0f,   e.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(62.0f,    e.aoaPercent);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EfisSource::Dynon),
                          static_cast<int>(e.source));
    TEST_ASSERT_EQUAL_UINT32(42000000u, e.timestampUs);
}

void test_size_is_reasonable(void)
{
    // 16 floats (64 B) + enum (4 B) + uint32 timestamp (4 B) +
    // char[12] time-of-day (12 B) = 84 B nominal, padded to 96.
    // EfisFrame is a normalized shared type whose size growth has
    // documented cost (copies in pending_ / TakeFrame()), so pin both
    // bounds — accidental field additions that bump padding to the next
    // alignment slot fail closed.
    TEST_ASSERT_GREATER_OR_EQUAL(84u, sizeof(EfisFrame));
    TEST_ASSERT_LESS_OR_EQUAL(96u, sizeof(EfisFrame));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_initializes_fields_to_absent);
    RUN_TEST(test_fields_are_writable);
    RUN_TEST(test_size_is_reasonable);
    return UNITY_END();
}
