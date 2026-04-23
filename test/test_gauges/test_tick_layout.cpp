// test_tick_layout.cpp — unit tests for onspeed::gauges::ComputeTickAngles.
//
// Locks down the fix for finding 041: all four graduation-mark loops in
// arcGraph used `_startAngle` as the rotation origin, but range bars and
// pointer use `_theta = _startAngle - _normAxis * minDisplay`. Whenever
// minDisplay != 0 the ticks drift away from the ranges they should align
// with. The extracted function takes `theta` explicitly, so the tests
// below verify that ticks anchor at theta, not at some hidden startAngle.

#include <unity.h>

#include <gauges/TickLayout.h>

#include <cmath>

using namespace onspeed::gauges;

static constexpr float kFloatTol = 1e-5f;
static constexpr float kPi       = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Guard cases: gradMarks that produce no output
// ---------------------------------------------------------------------------

void test_no_ticks_when_gradmarks_zero(void)
{
    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(/*theta=*/0.0f, /*arcAngle=*/kPi,
                              /*gradMarks=*/0,
                              buf, kMaxTicks);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_no_ticks_when_gradmarks_one(void)
{
    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(0.0f, kPi, /*gradMarks=*/1, buf, kMaxTicks);
    TEST_ASSERT_EQUAL_INT(0, n);
}

// ---------------------------------------------------------------------------
// Tick count
// ---------------------------------------------------------------------------

void test_major_tick_count_matches_gradmarks_plus_one(void)
{
    // gradMarks=4: 5 major (i=0..4) + 4 minor = 9.
    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(0.0f, kPi, /*gradMarks=*/4, buf, kMaxTicks);
    TEST_ASSERT_EQUAL_INT(9, n);

    int majorCount = 0;
    int minorCount = 0;
    for (int i = 0; i < n; ++i)
    {
        if (buf[i].isMajor) ++majorCount;
        else                ++minorCount;
    }
    TEST_ASSERT_EQUAL_INT(5, majorCount);
    TEST_ASSERT_EQUAL_INT(4, minorCount);
}

// ---------------------------------------------------------------------------
// theta anchoring — finding 041
// ---------------------------------------------------------------------------

void test_first_major_tick_is_at_theta(void)
{
    // theta=0.5: first major tick (i=0) must be at 0.5 (theta + 0*delta).
    // If the function used startAngle instead of theta, this would fail
    // whenever startAngle != theta.
    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(/*theta=*/0.5f, kPi, /*gradMarks=*/4,
                              buf, kMaxTicks);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(buf[0].isMajor);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 0.5f, buf[0].angle);
}

void test_last_major_tick_is_at_theta_plus_arcangle(void)
{
    // gradMarks=4, arcAngle=PI: last major tick at i=4 => angle =
    // theta + 4*(PI/4) = theta + PI.
    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(/*theta=*/0.5f, kPi, /*gradMarks=*/4,
                              buf, kMaxTicks);
    TEST_ASSERT_TRUE(n >= 5);

    // The 5th major tick (index 4, since majors are emitted first) is the
    // last one.
    TEST_ASSERT_TRUE(buf[4].isMajor);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 0.5f + kPi, buf[4].angle);
}

void test_graduation_marks_align_with_range_bar_start_when_mindisplay_nonzero(void)
{
    // The canonical finding 041 test. A range bar starting at minDisplay
    // would be drawn starting at theta (after scaling). The first major
    // tick must be at the same angle.
    //
    // Concrete numbers from a typical arcGraph: startAngle=0 rad,
    // normAxis=0.01 rad/unit, minDisplay=50 units.
    //   theta = startAngle - normAxis * minDisplay = 0 - 0.01 * 50 = -0.5.
    const float startAngle = 0.0f;
    const float normAxis   = 0.01f;
    const float minDisplay = 50.0f;
    const float theta      = startAngle - normAxis * minDisplay;

    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(theta, kPi, /*gradMarks=*/4, buf, kMaxTicks);
    TEST_ASSERT_TRUE(n >= 1);

    // First major tick must be at theta (= -0.5), not at startAngle (= 0).
    TEST_ASSERT_TRUE(buf[0].isMajor);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, theta, buf[0].angle);
    // And explicitly NOT at startAngle.
    TEST_ASSERT_TRUE(std::fabs(buf[0].angle - startAngle) > 0.1f);
}

// ---------------------------------------------------------------------------
// Minor tick positions
// ---------------------------------------------------------------------------

void test_minor_tick_halfway_between_major_ticks(void)
{
    // gradMarks=4, arcAngle=PI: delta=PI/4. First minor at delta/2=PI/8.
    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(0.0f, kPi, /*gradMarks=*/4, buf, kMaxTicks);
    TEST_ASSERT_EQUAL_INT(9, n);

    // Find the first minor tick.
    float firstMinorAngle = 0.0f;
    bool found = false;
    for (int i = 0; i < n; ++i)
    {
        if (!buf[i].isMajor)
        {
            firstMinorAngle = buf[i].angle;
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, kPi / 8.0f, firstMinorAngle);
}

void test_negative_gradmarks_produces_same_angle_positions(void)
{
    // gradMarks=-4 must produce the same angle positions as gradMarks=+4.
    // The sign only changes which style parameters the caller uses; the
    // geometry is identical.
    TickAngle pos[kMaxTicks];
    TickAngle neg[kMaxTicks];

    int nPos = ComputeTickAngles(0.25f, kPi, /*gradMarks=*/ 4, pos, kMaxTicks);
    int nNeg = ComputeTickAngles(0.25f, kPi, /*gradMarks=*/-4, neg, kMaxTicks);

    TEST_ASSERT_EQUAL_INT(nPos, nNeg);
    for (int i = 0; i < nPos; ++i)
    {
        TEST_ASSERT_FLOAT_WITHIN(kFloatTol, pos[i].angle, neg[i].angle);
        TEST_ASSERT_EQUAL_INT(pos[i].isMajor ? 1 : 0,
                              neg[i].isMajor ? 1 : 0);
    }
}

// ---------------------------------------------------------------------------
// Buffer safety
// ---------------------------------------------------------------------------

void test_output_buffer_overflow_guard(void)
{
    // gradMarks=20 would naturally produce 21 major + 20 minor = 41 ticks.
    // With outCapacity=2 the function must write at most 2 and return 2.
    TickAngle buf[2];
    int n = ComputeTickAngles(0.0f, kPi, /*gradMarks=*/20, buf, 2);
    TEST_ASSERT_TRUE(n <= 2);
    TEST_ASSERT_EQUAL_INT(2, n);
}

// ---------------------------------------------------------------------------
// Uniform spacing
// ---------------------------------------------------------------------------

void test_uniform_spacing_between_major_ticks(void)
{
    // gradMarks=5, arcAngle=PI: expect delta=PI/5 between adjacent majors.
    TickAngle buf[kMaxTicks];
    int n = ComputeTickAngles(0.0f, kPi, /*gradMarks=*/5, buf, kMaxTicks);

    // Walk only the major ticks in order. They are emitted first in
    // ascending order by construction.
    const float expectedDelta = kPi / 5.0f;
    int lastMajorIdx = -1;
    for (int i = 0; i < n; ++i)
    {
        if (!buf[i].isMajor) break;   // majors are all at the front
        if (lastMajorIdx >= 0)
        {
            const float actualDelta = buf[i].angle - buf[lastMajorIdx].angle;
            TEST_ASSERT_FLOAT_WITHIN(kFloatTol, expectedDelta, actualDelta);
        }
        lastMajorIdx = i;
    }
    TEST_ASSERT_TRUE(lastMajorIdx >= 0);
}
