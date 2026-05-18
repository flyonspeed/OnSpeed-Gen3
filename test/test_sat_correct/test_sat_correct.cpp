// test_sat_correct.cpp — ram-rise correction (TAT → SAT).
//
// SAT_K = TAT_K / (1 + K · 0.2 · M²), with M = TAS / a(SAT) and the
// helper Newton-iterating to recover the local σ and a(T_static).
// Expected SAT values pre-verified with a Python reference using the
// same ISA constants (kIsaT0K, kIsaLapseKPerFt, kDensityAltExp,
// kInverseSigmaExponent) the C++ uses.

#include <unity.h>
#include <cmath>
#include <sensors/SatCorrect.h>

using onspeed::sensors::CorrectSat;

void setUp(void) {}
void tearDown(void) {}

// ----- Identity (K = 0) -----

void test_k_zero_is_identity_at_cruise_ias(void)
{
    auto sat = CorrectSat(/*tat*/ +5.0f, /*ias*/ 150.0f,
                          /*palt*/ 0.0f, /*k*/ 0.0f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, +5.0f, *sat);
}

void test_k_zero_identity_at_cold_temp_high_altitude(void)
{
    // K=0 identity must hold regardless of altitude.
    auto sat = CorrectSat(-40.0f, 200.0f, 25000.0f, 0.0f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -40.0f, *sat);
}

// ----- Worked examples (true-Mach math, not the IAS proxy) -----

void test_n720ak_8000ft_cruise(void)
{
    // N720AK at 8000ft cruise, 147 KIAS, +5°C TAT, K=0.75.
    // True-Mach calc yields ~+2.25°C SAT (2.75°C cooler than TAT).
    auto sat = CorrectSat(+5.0f, 147.0f, 8000.0f, 0.75f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, +2.25f, *sat);
}

void test_lancair_fl250(void)
{
    // Lancair IV-P at FL250, 200 KIAS, -25°C TAT, K=0.75.
    // True-Mach calc yields ~-33.84°C SAT (8.84°C cooler).
    // (The naive IAS-as-Mach proxy under-corrects to -28.34°C.)
    auto sat = CorrectSat(-25.0f, 200.0f, 25000.0f, 0.75f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.15f, -33.84f, *sat);
}

void test_sea_level_no_altitude_shift(void)
{
    // At sea level σ=1 and a(T)≈a0; the iteration converges to
    // essentially the same answer as the IAS-as-Mach proxy.
    auto sat = CorrectSat(+15.0f, 150.0f, 0.0f, 0.75f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, +12.79f, *sat);
}

// ----- Monotonicity properties -----

void test_higher_ias_gives_lower_sat(void)
{
    auto sat100 = CorrectSat(+10.0f, 100.0f, 0.0f, 0.75f);
    auto sat200 = CorrectSat(+10.0f, 200.0f, 0.0f, 0.75f);
    TEST_ASSERT_TRUE(sat100.has_value());
    TEST_ASSERT_TRUE(sat200.has_value());
    // 200kt SAT colder than 100kt SAT at same TAT and altitude.
    TEST_ASSERT_TRUE(*sat200 < *sat100);
}

void test_higher_k_gives_lower_sat(void)
{
    auto sat_low_k  = CorrectSat(+10.0f, 200.0f, 0.0f, 0.5f);
    auto sat_high_k = CorrectSat(+10.0f, 200.0f, 0.0f, 1.0f);
    TEST_ASSERT_TRUE(sat_low_k.has_value());
    TEST_ASSERT_TRUE(sat_high_k.has_value());
    // K=1 corrects more, so SAT colder.
    TEST_ASSERT_TRUE(*sat_high_k < *sat_low_k);
}

void test_higher_altitude_gives_larger_correction(void)
{
    // Holding TAT/IAS/K fixed, climbing produces a larger
    // correction because σ shrinks (TAS grows for same IAS) and
    // a(T) shrinks (true Mach grows for same TAS).  Both effects
    // amplify the rise.
    auto sat_sl   = CorrectSat(+10.0f, 200.0f,     0.0f, 0.75f);
    auto sat_8k   = CorrectSat(+10.0f, 200.0f,  8000.0f, 0.75f);
    auto sat_fl25 = CorrectSat(+10.0f, 200.0f, 25000.0f, 0.75f);
    TEST_ASSERT_TRUE(sat_sl.has_value());
    TEST_ASSERT_TRUE(sat_8k.has_value());
    TEST_ASSERT_TRUE(sat_fl25.has_value());
    // The correction (TAT - SAT) grows monotonically with altitude.
    const float corr_sl   = 10.0f - *sat_sl;
    const float corr_8k   = 10.0f - *sat_8k;
    const float corr_fl25 = 10.0f - *sat_fl25;
    TEST_ASSERT_TRUE(corr_sl < corr_8k);
    TEST_ASSERT_TRUE(corr_8k < corr_fl25);
}

// ----- Invalid inputs return nullopt -----

void test_nan_tat_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(NAN, 150.0f, 0.0f, 0.75f).has_value());
}

void test_nan_ias_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, NAN, 0.0f, 0.75f).has_value());
}

void test_nan_palt_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 150.0f, NAN, 0.75f).has_value());
}

void test_inf_ias_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, INFINITY, 0.0f, 0.75f).has_value());
}

void test_negative_ias_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, -50.0f, 0.0f, 0.75f).has_value());
}

void test_zero_ias_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 0.0f, 0.0f, 0.75f).has_value());
}

void test_tat_above_range_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+150.0f, 150.0f, 0.0f, 0.75f).has_value());
}

void test_tat_below_range_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(-150.0f, 150.0f, 0.0f, 0.75f).has_value());
}

void test_k_negative_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 150.0f, 0.0f, -0.1f).has_value());
}

void test_k_above_one_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 150.0f, 0.0f, 1.1f).has_value());
}

void test_k_nan_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 150.0f, 0.0f, NAN).has_value());
}

// ----- Pathological inputs -----

void test_extreme_tat_near_lower_bound(void)
{
    // TAT = -99°C (in range) at 200 kt should still produce a finite SAT.
    auto sat = CorrectSat(-99.0f, 200.0f, 0.0f, 1.0f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_TRUE(std::isfinite(*sat));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_k_zero_is_identity_at_cruise_ias);
    RUN_TEST(test_k_zero_identity_at_cold_temp_high_altitude);
    RUN_TEST(test_n720ak_8000ft_cruise);
    RUN_TEST(test_lancair_fl250);
    RUN_TEST(test_sea_level_no_altitude_shift);
    RUN_TEST(test_higher_ias_gives_lower_sat);
    RUN_TEST(test_higher_k_gives_lower_sat);
    RUN_TEST(test_higher_altitude_gives_larger_correction);
    RUN_TEST(test_nan_tat_returns_nullopt);
    RUN_TEST(test_nan_ias_returns_nullopt);
    RUN_TEST(test_nan_palt_returns_nullopt);
    RUN_TEST(test_inf_ias_returns_nullopt);
    RUN_TEST(test_negative_ias_returns_nullopt);
    RUN_TEST(test_zero_ias_returns_nullopt);
    RUN_TEST(test_tat_above_range_returns_nullopt);
    RUN_TEST(test_tat_below_range_returns_nullopt);
    RUN_TEST(test_k_negative_returns_nullopt);
    RUN_TEST(test_k_above_one_returns_nullopt);
    RUN_TEST(test_k_nan_returns_nullopt);
    RUN_TEST(test_extreme_tat_near_lower_bound);
    return UNITY_END();
}
