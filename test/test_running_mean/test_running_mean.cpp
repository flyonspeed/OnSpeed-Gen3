// test_running_mean.cpp — unit tests for onspeed::RunningMean.
//
// RunningMean replaces the Arduino RunningAverage library for the
// narrow API our firmware uses:
//
//   RunningMean(int capacity);
//   void  addValue(float);
//   float getFastAverage() const;   // sum / count, or 0 if empty
//   void  clear();
//   int   count() const;
//   int   capacity() const;
//
// It does NOT cover Arduino RunningAverage's stats helpers
// (getStandardDeviation, getMin/Max, setPartial, getAverageSubset,
// etc.) because the firmware never calls them.

#include <unity.h>
#include <filters/RunningMean.h>
#include <cmath>

using onspeed::RunningMean;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Capacity and construction
// ---------------------------------------------------------------------------

void test_capacity_floored_to_one()
{
    // 0 and negative capacities clamp to 1 so the buffer is always usable.
    RunningMean a(0);
    TEST_ASSERT_EQUAL_INT(1, a.capacity());

    RunningMean b(-5);
    TEST_ASSERT_EQUAL_INT(1, b.capacity());
}

void test_empty_average_is_zero()
{
    RunningMean m(10);
    TEST_ASSERT_EQUAL_INT(0, m.count());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.getFastAverage());
}

// ---------------------------------------------------------------------------
// Core addValue / getFastAverage semantics
// ---------------------------------------------------------------------------

void test_first_value_is_its_own_average()
{
    RunningMean m(10);
    m.addValue(42.0f);
    TEST_ASSERT_EQUAL_INT(1, m.count());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 42.0f, m.getFastAverage());
}

void test_average_before_full_uses_count_not_capacity()
{
    // Two values into a 10-slot buffer should average the two, not
    // divide by 10.
    RunningMean m(10);
    m.addValue(10.0f);
    m.addValue(20.0f);
    TEST_ASSERT_EQUAL_INT(2, m.count());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 15.0f, m.getFastAverage());
}

void test_count_saturates_at_capacity()
{
    RunningMean m(3);
    for (int i = 0; i < 100; ++i) {
        m.addValue(1.0f);
    }
    TEST_ASSERT_EQUAL_INT(3, m.count());
}

void test_average_after_full_is_window_mean()
{
    // Fill a window-5 with 1..5, average = 3.
    RunningMean m(5);
    for (int i = 1; i <= 5; ++i) m.addValue(static_cast<float>(i));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, m.getFastAverage());
}

void test_addvalue_overwrites_oldest()
{
    // After filling 1..5, pushing 6 evicts the 1: window becomes 2..6,
    // average = 4.
    RunningMean m(5);
    for (int i = 1; i <= 5; ++i) m.addValue(static_cast<float>(i));
    m.addValue(6.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 4.0f, m.getFastAverage());
}

void test_sum_stays_accurate_across_many_wraparounds()
{
    // Wrap the circular buffer thousands of times to catch sum drift
    // from accumulated FP error in the running-sum update. For a
    // stable input the average should stay exactly on the input.
    RunningMean m(16);
    for (int i = 0; i < 16 * 1000; ++i) {
        m.addValue(5.25f);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.25f, m.getFastAverage());
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void test_clear_resets_state()
{
    RunningMean m(4);
    m.addValue(100.0f);
    m.addValue(200.0f);
    m.clear();

    TEST_ASSERT_EQUAL_INT(0, m.count());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.getFastAverage());

    // After clear, the next value becomes the new seed — sum must NOT
    // carry residue from pre-clear values.
    m.addValue(7.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 7.0f, m.getFastAverage());
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_capacity_floored_to_one);
    RUN_TEST(test_empty_average_is_zero);
    RUN_TEST(test_first_value_is_its_own_average);
    RUN_TEST(test_average_before_full_uses_count_not_capacity);
    RUN_TEST(test_count_saturates_at_capacity);
    RUN_TEST(test_average_after_full_is_window_mean);
    RUN_TEST(test_addvalue_overwrites_oldest);
    RUN_TEST(test_sum_stays_accurate_across_many_wraparounds);
    RUN_TEST(test_clear_resets_state);
    return UNITY_END();
}
