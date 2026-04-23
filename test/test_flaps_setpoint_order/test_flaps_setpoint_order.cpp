// test_flaps_setpoint_order.cpp — Unit tests for OnSpeedConfig::SuFlaps::SetpointOrderError.
//
// SetpointOrderError() validates that flap-position AOA setpoints are
// monotonically ordered: LDMAX < OnSpeedFast < OnSpeedSlow < StallWarn < Stall.
// If a user misconfigures setpoints (e.g. LDMAX above OnSpeedFast), this is
// what flags it, protecting against dangerously wrong audio-tone behavior at
// the edge of the flight envelope.
//
// Skips the StallWarn < Stall check when fSTALLAOA == 0.0f (uncalibrated
// default).

#include <unity.h>

#include <string>

#include <config/OnSpeedConfig.h>

using onspeed::config::OnSpeedConfig;

using SuFlaps = OnSpeedConfig::SuFlaps;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helper: build a SuFlaps with a valid monotonic chain, mutate per-test.
// ---------------------------------------------------------------------------

static SuFlaps MakeValidFlap()
{
    SuFlaps f;
    f.fLDMAXAOA       =  4.0f;
    f.fONSPEEDFASTAOA =  6.0f;
    f.fONSPEEDSLOWAOA =  8.0f;
    f.fSTALLWARNAOA   = 10.0f;
    f.fSTALLAOA       = 12.0f;
    return f;
}

// ---------------------------------------------------------------------------
// Valid orderings → empty string.
// ---------------------------------------------------------------------------

void test_valid_ordering_returns_empty()
{
    SuFlaps f = MakeValidFlap();
    TEST_ASSERT_TRUE(f.SetpointOrderError().empty());
}

void test_stall_zero_skips_stall_check()
{
    // fSTALLAOA == 0 means uncalibrated; StallWarn >= 0 should not error.
    SuFlaps f = MakeValidFlap();
    f.fSTALLAOA = 0.0f;
    TEST_ASSERT_TRUE(f.SetpointOrderError().empty());
}

// ---------------------------------------------------------------------------
// Each individual misordering → exactly one error message naming the pair.
// ---------------------------------------------------------------------------

void test_ldmax_ge_onspeedfast_errors()
{
    SuFlaps f = MakeValidFlap();
    f.fLDMAXAOA = 7.0f;   // now > OnSpeedFast (6.0)
    const std::string err = f.SetpointOrderError();
    TEST_ASSERT_FALSE(err.empty());
    TEST_ASSERT_TRUE(err.find("LDMAX")         != std::string::npos);
    TEST_ASSERT_TRUE(err.find("OnSpeedFast")   != std::string::npos);
    TEST_ASSERT_TRUE(err.find("7.0")           != std::string::npos);
    TEST_ASSERT_TRUE(err.find("6.0")           != std::string::npos);
}

void test_onspeedfast_ge_onspeedslow_errors()
{
    SuFlaps f = MakeValidFlap();
    f.fONSPEEDFASTAOA = 9.0f;   // now > OnSpeedSlow (8.0)
    const std::string err = f.SetpointOrderError();
    TEST_ASSERT_FALSE(err.empty());
    TEST_ASSERT_TRUE(err.find("OnSpeedFast") != std::string::npos);
    TEST_ASSERT_TRUE(err.find("OnSpeedSlow") != std::string::npos);
}

void test_onspeedslow_ge_stallwarn_errors()
{
    SuFlaps f = MakeValidFlap();
    f.fONSPEEDSLOWAOA = 11.0f;   // now > StallWarn (10.0)
    const std::string err = f.SetpointOrderError();
    TEST_ASSERT_FALSE(err.empty());
    TEST_ASSERT_TRUE(err.find("OnSpeedSlow") != std::string::npos);
    TEST_ASSERT_TRUE(err.find("StallWarn")   != std::string::npos);
}

void test_stallwarn_ge_stall_errors()
{
    SuFlaps f = MakeValidFlap();
    f.fSTALLWARNAOA = 13.0f;   // now > Stall (12.0)
    const std::string err = f.SetpointOrderError();
    TEST_ASSERT_FALSE(err.empty());
    TEST_ASSERT_TRUE(err.find("StallWarn") != std::string::npos);
    TEST_ASSERT_TRUE(err.find("Stall (")   != std::string::npos);
}

// ---------------------------------------------------------------------------
// Boundary: strict `>=` comparison — equal setpoints error.
// ---------------------------------------------------------------------------

void test_equality_is_an_error()
{
    SuFlaps f = MakeValidFlap();
    f.fLDMAXAOA = f.fONSPEEDFASTAOA;   // equal, must fail the strict >= test
    TEST_ASSERT_FALSE(f.SetpointOrderError().empty());
}

// ---------------------------------------------------------------------------
// Multiple simultaneous violations → all reported, no trailing "; ".
// ---------------------------------------------------------------------------

void test_multiple_violations_all_reported()
{
    SuFlaps f;
    // All setpoints identical — every adjacent pair violates.
    f.fLDMAXAOA = f.fONSPEEDFASTAOA = f.fONSPEEDSLOWAOA = f.fSTALLWARNAOA = f.fSTALLAOA = 5.0f;
    const std::string err = f.SetpointOrderError();
    TEST_ASSERT_FALSE(err.empty());
    // Every pair's first name should appear:
    TEST_ASSERT_TRUE(err.find("LDMAX")       != std::string::npos);
    TEST_ASSERT_TRUE(err.find("OnSpeedFast") != std::string::npos);
    TEST_ASSERT_TRUE(err.find("OnSpeedSlow") != std::string::npos);
    TEST_ASSERT_TRUE(err.find("StallWarn")   != std::string::npos);
}

void test_no_trailing_semicolon_space()
{
    SuFlaps f = MakeValidFlap();
    f.fLDMAXAOA = 7.0f;   // one violation
    const std::string err = f.SetpointOrderError();
    TEST_ASSERT_FALSE(err.empty());
    // The source uses "; " as a separator; trim logic must strip trailing.
    TEST_ASSERT_TRUE(err.size() >= 2);
    const std::string tail = err.substr(err.size() - 2);
    TEST_ASSERT_TRUE(tail != "; ");
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_ordering_returns_empty);
    RUN_TEST(test_stall_zero_skips_stall_check);
    RUN_TEST(test_ldmax_ge_onspeedfast_errors);
    RUN_TEST(test_onspeedfast_ge_onspeedslow_errors);
    RUN_TEST(test_onspeedslow_ge_stallwarn_errors);
    RUN_TEST(test_stallwarn_ge_stall_errors);
    RUN_TEST(test_equality_is_an_error);
    RUN_TEST(test_multiple_violations_all_reported);
    RUN_TEST(test_no_trailing_semicolon_space);
    return UNITY_END();
}
