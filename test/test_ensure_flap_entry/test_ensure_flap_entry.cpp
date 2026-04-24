// test_ensure_flap_entry.cpp — EnsureAtLeastOneFlap invariant.
//
// After the web-UI save handler clears and rebuilds aFlaps based on
// which rows the user marked for deletion, there's no built-in
// guarantee that the vector has at least one entry. If the user
// deletes every row, aFlaps becomes empty and every downstream reader
// (Audio.cpp, SensorIO.cpp, DisplaySerial.cpp, DataServer.cpp,
// LogReplay.cpp) hits an out-of-bounds dereference on
// aFlaps[g_Flaps.iIndex] — undefined behavior on ESP32.
//
// EnsureAtLeastOneFlap is the safety net: it pushes a fresh zeroed
// entry when the vector is empty, so the invariant "aFlaps.size() >= 1
// always" holds everywhere except inside the save loop itself.

#include <unity.h>

#include <config/OnSpeedConfig.h>

using onspeed::config::OnSpeedConfig;
using onspeed::config::EnsureAtLeastOneFlap;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Empty vector → one entry pushed, returns true.
// ---------------------------------------------------------------------------

void test_empty_vector_gets_one_default_entry()
{
    std::vector<OnSpeedConfig::SuFlaps> aFlaps;
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(aFlaps.size()));

    bool pushed = EnsureAtLeastOneFlap(aFlaps);

    TEST_ASSERT_TRUE(pushed);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(aFlaps.size()));
}

void test_empty_vector_pushes_zeroed_setpoints()
{
    // The helper matches LoadDefaults' shape: all setpoints zero,
    // so the audio gate keeps tones silent until calibration.
    std::vector<OnSpeedConfig::SuFlaps> aFlaps;
    EnsureAtLeastOneFlap(aFlaps);

    const auto& flap0 = aFlaps[0];
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fLDMAXAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fONSPEEDFASTAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fONSPEEDSLOWAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.fSTALLWARNAOA);
    for (int i = 0; i < onspeed::MAX_CURVE_COEFF; ++i) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flap0.AoaCurve.afCoeff[i]);
    }
}

void test_empty_vector_pushes_polynomial_curve_type()
{
    // Match the structural default used by LoadDefaults so the curve
    // evaluator branches the same way it does for a fresh config.
    std::vector<OnSpeedConfig::SuFlaps> aFlaps;
    EnsureAtLeastOneFlap(aFlaps);

    TEST_ASSERT_EQUAL_INT(1, aFlaps[0].AoaCurve.iCurveType);
}

// ---------------------------------------------------------------------------
// Non-empty vector is left untouched, returns false.
// ---------------------------------------------------------------------------

void test_nonempty_vector_unchanged()
{
    std::vector<OnSpeedConfig::SuFlaps> aFlaps;
    OnSpeedConfig::SuFlaps user;
    user.iDegrees       = 20;
    user.fLDMAXAOA      = 4.5f;
    user.fSTALLWARNAOA  = 14.5f;
    aFlaps.push_back(user);

    bool pushed = EnsureAtLeastOneFlap(aFlaps);

    TEST_ASSERT_FALSE(pushed);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(aFlaps.size()));
    TEST_ASSERT_EQUAL_INT(20, aFlaps[0].iDegrees);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4.5f, aFlaps[0].fLDMAXAOA);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 14.5f, aFlaps[0].fSTALLWARNAOA);
}

void test_multiple_entries_left_alone()
{
    std::vector<OnSpeedConfig::SuFlaps> aFlaps;
    for (int i = 0; i < 3; ++i) {
        OnSpeedConfig::SuFlaps f;
        f.iDegrees = i * 10;
        aFlaps.push_back(f);
    }

    bool pushed = EnsureAtLeastOneFlap(aFlaps);

    TEST_ASSERT_FALSE(pushed);
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(aFlaps.size()));
    TEST_ASSERT_EQUAL_INT(0,  aFlaps[0].iDegrees);
    TEST_ASSERT_EQUAL_INT(10, aFlaps[1].iDegrees);
    TEST_ASSERT_EQUAL_INT(20, aFlaps[2].iDegrees);
}

// ---------------------------------------------------------------------------
// Idempotence: calling the helper twice on the same empty vector
// leaves exactly one entry (not two).
// ---------------------------------------------------------------------------

void test_idempotent_on_empty()
{
    std::vector<OnSpeedConfig::SuFlaps> aFlaps;

    bool first  = EnsureAtLeastOneFlap(aFlaps);
    bool second = EnsureAtLeastOneFlap(aFlaps);

    TEST_ASSERT_TRUE(first);
    TEST_ASSERT_FALSE(second);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(aFlaps.size()));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_vector_gets_one_default_entry);
    RUN_TEST(test_empty_vector_pushes_zeroed_setpoints);
    RUN_TEST(test_empty_vector_pushes_polynomial_curve_type);
    RUN_TEST(test_nonempty_vector_unchanged);
    RUN_TEST(test_multiple_entries_left_alone);
    RUN_TEST(test_idempotent_on_empty);
    return UNITY_END();
}
