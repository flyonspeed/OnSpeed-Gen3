// test_pressure_convert.cpp — characterization tests for PressureConvert.
//
// These tests pin the exact numeric behavior of the ported formulas.
// A "characterization" test means: if the math changes, the test breaks
// on purpose — the developer sees the diff and decides if the change is
// correct before merging.

#include <unity.h>
#include <sensors/PressureConvert.h>

using namespace onspeed::sensors;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// CountsToPsi — HSC linear transfer function + saturation guard
// ============================================================================

// Convenience range matching the real differential sensor (±1 PSI).
static constexpr HscRange kDiffRange{ 1638u, 14745u, -1.0f, 1.0f };

// Convenience range matching the real static sensor (0–23.2 PSI, 1.6 bar).
static constexpr HscRange kAbsRange{ 1638u, 14745u, 0.0f, 23.2f };

void test_counts_to_psi_at_countsMin_returns_psiMin(void)
{
    // At the lower calibration point, output = psiMin exactly.
    auto psi = CountsToPsi(1638u, kDiffRange);
    TEST_ASSERT_TRUE(psi.has_value());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, *psi);
}

void test_counts_to_psi_at_countsMax_returns_psiMax(void)
{
    // At the upper calibration point, output = psiMax exactly.
    auto psi = CountsToPsi(14745u, kDiffRange);
    TEST_ASSERT_TRUE(psi.has_value());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, *psi);
}

void test_counts_to_psi_midrange(void)
{
    // Midpoint counts → midpoint PSI. Uses kDiffRange (±1 PSI).
    // mid = (1638 + 14745) / 2 = 8191 (integer)
    uint16_t mid = static_cast<uint16_t>((1638u + 14745u) / 2u);
    auto psi = CountsToPsi(mid, kDiffRange);
    TEST_ASSERT_TRUE(psi.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, *psi);
}

void test_counts_to_psi_static_sensor_typical(void)
{
    // Sea-level standard: 14.696 PSI → approx. 1013.25 mbar.
    // For the abs range (0–23.2 PSI), find the counts that give 14.696 PSI.
    // counts = countsMin + (psi - psiMin) / (psiMax - psiMin) * (countsMax - countsMin)
    //        = 1638 + (14.696 / 23.2) * 13107 ≈ 1638 + 8297 = 9935
    auto psi = CountsToPsi(9935u, kAbsRange);
    TEST_ASSERT_TRUE(psi.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 14.696f, *psi);
}

void test_counts_below_countsMin_returns_nullopt(void)
{
    // Below the 10% saturation point: sensor disconnected or over-range.
    auto psi = CountsToPsi(1000u, kDiffRange);
    TEST_ASSERT_FALSE(psi.has_value());
}

void test_counts_above_countsMax_returns_nullopt(void)
{
    // Above the 90% saturation point: sensor pegged high or shorted.
    auto psi = CountsToPsi(15000u, kDiffRange);
    TEST_ASSERT_FALSE(psi.has_value());
}

void test_counts_at_exactly_countsMin_minus1_returns_nullopt(void)
{
    // One count below the valid window.
    auto psi = CountsToPsi(1637u, kDiffRange);
    TEST_ASSERT_FALSE(psi.has_value());
}

void test_counts_at_exactly_countsMax_plus1_returns_nullopt(void)
{
    // One count above the valid window.
    auto psi = CountsToPsi(14746u, kDiffRange);
    TEST_ASSERT_FALSE(psi.has_value());
}

// ============================================================================
// PitotPsiToIasKt — incompressible pitot equation
// ============================================================================

void test_pitot_psi_to_ias_zero_dp_returns_zero(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, PitotPsiToIasKt(0.0f));
}

void test_pitot_psi_to_ias_negative_dp_returns_zero(void)
{
    // Reversed flow or sensor below zero: clamp to 0.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, PitotPsiToIasKt(-0.01f));
}

void test_pitot_psi_to_ias_characterize_60kt(void)
{
    // At 60 kt: dp = 0.5 * rho0 * v^2 = 0.5 * 1.225 * (60*0.514444)^2
    //         = 0.5 * 1.225 * 952.5 = 583.6 Pa = 0.08466 PSI
    // Reverse: IAS = sqrt(2 * 0.08466 * 6894.757 / 1.225) * 1.94384 ≈ 60 kt
    const float dp60kt = 0.5f * 1.225f * (60.0f * 0.514444f) * (60.0f * 0.514444f);
    const float dpPsi = dp60kt / 6894.757f;
    float ias = PitotPsiToIasKt(dpPsi);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 60.0f, ias);
}

void test_pitot_psi_to_ias_characterize_100kt(void)
{
    // Same derivation for 100 kt.
    const float dp100kt = 0.5f * 1.225f * (100.0f * 0.514444f) * (100.0f * 0.514444f);
    const float dpPsi = dp100kt / 6894.757f;
    float ias = PitotPsiToIasKt(dpPsi);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, ias);
}

// ============================================================================
// StaticMbarToPaltFt — ISA barometric altitude formula
// ============================================================================

void test_static_mbar_to_palt_sea_level(void)
{
    // ISA sea-level pressure 1013.25 mbar → 0 ft pressure altitude.
    float palt = StaticMbarToPaltFt(1013.25f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, palt);
}

void test_static_mbar_to_palt_5000ft(void)
{
    // ISA standard at 5000 ft: 843.07 mbar.
    float palt = StaticMbarToPaltFt(843.07f);
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 5000.0f, palt);
}

void test_static_mbar_to_palt_10000ft(void)
{
    // ISA standard at 10000 ft: 696.82 mbar.
    float palt = StaticMbarToPaltFt(696.82f);
    TEST_ASSERT_FLOAT_WITHIN(50.0f, 10000.0f, palt);
}

void test_static_mbar_to_palt_zero_input_returns_zero(void)
{
    // Non-positive input: guard against log of negative number.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StaticMbarToPaltFt(0.0f));
}

void test_static_mbar_to_palt_negative_input_returns_zero(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StaticMbarToPaltFt(-10.0f));
}

// ============================================================================
// DensityAltitudeFt — standard aviation DA approximation
// ============================================================================

void test_density_altitude_equals_palt_on_standard_day(void)
{
    // At Palt = 5000 ft with ISA temperature (15 - 0.001981*5000 = 5.095°C)
    // density altitude should equal pressure altitude.
    const float palt     = 5000.0f;
    const float isaOat   = 15.0f - 0.001981f * palt;
    float da = DensityAltitudeFt(palt, isaOat);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, palt, da);
}

void test_density_altitude_higher_than_palt_on_hot_day(void)
{
    // At Palt = 5000 ft, OAT = 30°C (≈25°C above ISA) → DA > Palt.
    float da = DensityAltitudeFt(5000.0f, 30.0f);
    TEST_ASSERT_TRUE(da > 5000.0f);
}

void test_density_altitude_lower_than_palt_on_cold_day(void)
    {
    // At Palt = 5000 ft, OAT = -20°C (well below ISA) → DA < Palt.
    float da = DensityAltitudeFt(5000.0f, -20.0f);
    TEST_ASSERT_TRUE(da < 5000.0f);
}

void test_density_altitude_sea_level_standard_day(void)
{
    // Sea level ISA: Palt=0, OAT=15°C → DA=0.
    float da = DensityAltitudeFt(0.0f, 15.0f);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 0.0f, da);
}

int main(int, char**)
{
    UNITY_BEGIN();

    // CountsToPsi
    RUN_TEST(test_counts_to_psi_at_countsMin_returns_psiMin);
    RUN_TEST(test_counts_to_psi_at_countsMax_returns_psiMax);
    RUN_TEST(test_counts_to_psi_midrange);
    RUN_TEST(test_counts_to_psi_static_sensor_typical);
    RUN_TEST(test_counts_below_countsMin_returns_nullopt);
    RUN_TEST(test_counts_above_countsMax_returns_nullopt);
    RUN_TEST(test_counts_at_exactly_countsMin_minus1_returns_nullopt);
    RUN_TEST(test_counts_at_exactly_countsMax_plus1_returns_nullopt);

    // PitotPsiToIasKt
    RUN_TEST(test_pitot_psi_to_ias_zero_dp_returns_zero);
    RUN_TEST(test_pitot_psi_to_ias_negative_dp_returns_zero);
    RUN_TEST(test_pitot_psi_to_ias_characterize_60kt);
    RUN_TEST(test_pitot_psi_to_ias_characterize_100kt);

    // StaticMbarToPaltFt
    RUN_TEST(test_static_mbar_to_palt_sea_level);
    RUN_TEST(test_static_mbar_to_palt_5000ft);
    RUN_TEST(test_static_mbar_to_palt_10000ft);
    RUN_TEST(test_static_mbar_to_palt_zero_input_returns_zero);
    RUN_TEST(test_static_mbar_to_palt_negative_input_returns_zero);

    // DensityAltitudeFt
    RUN_TEST(test_density_altitude_equals_palt_on_standard_day);
    RUN_TEST(test_density_altitude_higher_than_palt_on_hot_day);
    RUN_TEST(test_density_altitude_lower_than_palt_on_cold_day);
    RUN_TEST(test_density_altitude_sea_level_standard_day);

    return UNITY_END();
}
