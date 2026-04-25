// test_log_replay_hascolumn.cpp — unit tests for onspeed::proto::HasColumn
//
// LogReplay calls HasColumn against the first line of the CSV log returned
// by fgets to detect optional column groups (boom, EFIS, VN-300) and to
// validate that mandatory columns are present. The boundary handling for
// `\r` and `\n` keeps the trailing column in a well-formed log from being
// rejected once a validator targets it.

#include <unity.h>

#include <proto/CsvHeaderMatch.h>

using onspeed::proto::HasColumn;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Comma-bounded matches
// ---------------------------------------------------------------------------

void test_match_at_start(void)
{
    TEST_ASSERT_TRUE(HasColumn("Pitch,Roll,Yaw", "Pitch"));
}

void test_match_in_middle(void)
{
    TEST_ASSERT_TRUE(HasColumn("a,Pitch,b", "Pitch"));
}

void test_match_at_end_no_terminator(void)
{
    TEST_ASSERT_TRUE(HasColumn("a,Pitch", "Pitch"));
}

void test_match_at_end_with_lf(void)
{
    TEST_ASSERT_TRUE(HasColumn("a,Pitch\n", "Pitch"));
}

void test_match_at_end_with_crlf(void)
{
    TEST_ASSERT_TRUE(HasColumn("a,Pitch\r\n", "Pitch"));
}

void test_match_real_header_middle_column(void)
{
    TEST_ASSERT_TRUE(HasColumn("PfwdSmoothed,P45Smoothed,IAS,DataMark", "IAS"));
}

// ---------------------------------------------------------------------------
// Substring rejections
// ---------------------------------------------------------------------------

void test_reject_substring_at_start(void)
{
    TEST_ASSERT_FALSE(HasColumn("PitchRate,Roll", "Pitch"));
}

void test_reject_substring_mid_line(void)
{
    TEST_ASSERT_FALSE(HasColumn("a,PitchRate,b", "Pitch"));
}

void test_reject_substring_pitchrate_only(void)
{
    TEST_ASSERT_FALSE(HasColumn("PitchRate,b", "Pitch"));
}

void test_reject_ias_against_boomias(void)
{
    TEST_ASSERT_FALSE(HasColumn("boomIAS,b", "IAS"));
}

void test_reject_ias_against_efisias(void)
{
    TEST_ASSERT_FALSE(HasColumn("efisIAS,b", "IAS"));
}

// ---------------------------------------------------------------------------
// Negative cases
// ---------------------------------------------------------------------------

void test_empty_header(void)
{
    TEST_ASSERT_FALSE(HasColumn("", "Pitch"));
}

void test_absent_column(void)
{
    TEST_ASSERT_FALSE(HasColumn("a,b,c", "Pitch"));
}

void test_no_spurious_match_on_empty_field(void)
{
    // An empty field between commas must not match the empty token; the
    // helper rejects empty `name` outright (see test_empty_name_returns_false)
    // and even with a non-empty target name `"Pitch"` no field here matches.
    TEST_ASSERT_FALSE(HasColumn("a,,b", "Pitch"));
}

// HasColumn is never called with an empty `name` in production. The helper
// short-circuits to false in that case so that the strstr loop does not spin
// forever (every position would match the zero-length needle). This test
// pins down that contract.
void test_empty_name_returns_false(void)
{
    TEST_ASSERT_FALSE(HasColumn("a,b,c", ""));
    TEST_ASSERT_FALSE(HasColumn("", ""));
}

// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_match_at_start);
    RUN_TEST(test_match_in_middle);
    RUN_TEST(test_match_at_end_no_terminator);
    RUN_TEST(test_match_at_end_with_lf);
    RUN_TEST(test_match_at_end_with_crlf);
    RUN_TEST(test_match_real_header_middle_column);
    RUN_TEST(test_reject_substring_at_start);
    RUN_TEST(test_reject_substring_mid_line);
    RUN_TEST(test_reject_substring_pitchrate_only);
    RUN_TEST(test_reject_ias_against_boomias);
    RUN_TEST(test_reject_ias_against_efisias);
    RUN_TEST(test_empty_header);
    RUN_TEST(test_absent_column);
    RUN_TEST(test_no_spurious_match_on_empty_field);
    RUN_TEST(test_empty_name_returns_false);
    return UNITY_END();
}
