// test_bar_range_scale.cpp — unit tests for onspeed::gauges::ScaleBarRanges.
//
// These tests lock down the fix for finding 024: vBarGraph / hBarGraph used
// to mutate the caller-owned rangeTop[] / rangeBot[] member arrays in place,
// so a second draw call would compound the scaling and produce garbage.
// The extracted function takes separate src and dst buffers; the tests
// below verify that (a) src is never written, (b) successive calls produce
// identical dst (idempotency), and (c) scaling + clamping behave as
// specified.

#include <unity.h>

#include <gauges/BarRangeScale.h>

using namespace onspeed::gauges;

void test_vbar_range_idempotent_when_called_twice(void)
{
    // The canonical finding-024 guard: if the function mutated src, the
    // second call would produce a doubly-scaled top and a changed src.
    RangeBand src[1];
    src[0].valid = true;
    src[0].top   = 400;
    src[0].bot   = 100;
    src[0].color = 0x1234;

    ScaledRangeBand dst1[1] = {};
    ScaledRangeBand dst2[1] = {};

    ScaleBarRanges(src, dst1, 1, 4095, 4096, 400, 0);
    ScaleBarRanges(src, dst2, 1, 4095, 4096, 400, 0);

    // Second call must produce identical output to the first.
    TEST_ASSERT_EQUAL_INT32(dst1[0].top, dst2[0].top);
    TEST_ASSERT_EQUAL_INT32(dst1[0].bot, dst2[0].bot);

    // And src must be unchanged.
    TEST_ASSERT_EQUAL_INT32(400, src[0].top);
    TEST_ASSERT_EQUAL_INT32(100, src[0].bot);
}

void test_vbar_range_scaled_values_match_reference(void)
{
    // normAxis == scaleUp == 4096: identity transform.
    RangeBand src[1];
    src[0].valid = true;
    src[0].top   = 80;
    src[0].bot   = 20;

    ScaledRangeBand dst[1] = {};
    ScaleBarRanges(src, dst, 1, 4096, 4096, 100, 0);

    TEST_ASSERT_EQUAL_INT32(80, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(20, dst[0].bot);

    // Halve normAxis -> halved output.
    ScaleBarRanges(src, dst, 1, 2048, 4096, 100, 0);
    TEST_ASSERT_EQUAL_INT32(40, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(10, dst[0].bot);
}

void test_vbar_range_clamps_to_maxscaled(void)
{
    // Scaled top (200) exceeds maxScaled (100) -> clamp to 100.
    RangeBand src[1];
    src[0].valid = true;
    src[0].top   = 200;
    src[0].bot   = 10;

    ScaledRangeBand dst[1] = {};
    ScaleBarRanges(src, dst, 1, 4096, 4096, 100, 0);

    TEST_ASSERT_EQUAL_INT32(100, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(10,  dst[0].bot);
}

void test_vbar_range_clamps_to_minscaled(void)
{
    // Scaled bot (-50) below minScaled (0) -> clamp to 0.
    RangeBand src[1];
    src[0].valid = true;
    src[0].top   = 30;
    src[0].bot   = -50;

    ScaledRangeBand dst[1] = {};
    ScaleBarRanges(src, dst, 1, 4096, 4096, 100, 0);

    TEST_ASSERT_EQUAL_INT32(30, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(0,  dst[0].bot);
}

void test_vbar_range_zero_span_produces_zero_output(void)
{
    // top == bot: the band has zero height. Both scale the same.
    RangeBand src[1];
    src[0].valid = true;
    src[0].top   = 50;
    src[0].bot   = 50;

    ScaledRangeBand dst[1] = {};
    ScaleBarRanges(src, dst, 1, 4096, 4096, 100, 0);

    TEST_ASSERT_EQUAL_INT32(50, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(50, dst[0].bot);
    TEST_ASSERT_EQUAL_INT32(0,  dst[0].top - dst[0].bot);
}

void test_vbar_range_valid_false_still_writes_dst(void)
{
    // The scaler is unconditional; the caller decides whether to draw based
    // on the valid flag. Verify dst is written even when valid == false.
    RangeBand src[1];
    src[0].valid = false;
    src[0].top   = 200;
    src[0].bot   = 50;

    ScaledRangeBand dst[1];
    dst[0].top = -999;
    dst[0].bot = -999;

    ScaleBarRanges(src, dst, 1, 4096, 4096, 100, 0);

    // Clamped from 200 to maxScaled (100), and from 50 -> 50.
    TEST_ASSERT_EQUAL_INT32(100, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(50,  dst[0].bot);
}

void test_vbar_range_multiple_bands_independent(void)
{
    // Three bands with different top/bot values; each must scale
    // independently and must not perturb its neighbours.
    RangeBand src[3];
    src[0] = {true, 80, 40, 0x0001};
    src[1] = {true, 60, 20, 0x0002};
    src[2] = {true, 30, 10, 0x0003};

    ScaledRangeBand dst[3] = {};
    ScaleBarRanges(src, dst, 3, 4096, 4096, 100, 0);

    TEST_ASSERT_EQUAL_INT32(80, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(40, dst[0].bot);
    TEST_ASSERT_EQUAL_INT32(60, dst[1].top);
    TEST_ASSERT_EQUAL_INT32(20, dst[1].bot);
    TEST_ASSERT_EQUAL_INT32(30, dst[2].top);
    TEST_ASSERT_EQUAL_INT32(10, dst[2].bot);
}

void test_vbar_normaxis_zero_does_not_crash(void)
{
    // normAxis = 0 is the degenerate case GaugeWidgets guards with
    // _normAxis = 1 when (maxDisplay - minDisplay) == 0. The extracted
    // function still handles 0 gracefully: 0 * top / scaleUp == 0.
    RangeBand src[1];
    src[0].valid = true;
    src[0].top   = 500;
    src[0].bot   = 100;

    ScaledRangeBand dst[1];
    dst[0].top = 999;
    dst[0].bot = 999;

    ScaleBarRanges(src, dst, 1, 0, 4096, 50, -50);

    // 0 clamped to [-50, 50] is 0.
    TEST_ASSERT_EQUAL_INT32(0, dst[0].top);
    TEST_ASSERT_EQUAL_INT32(0, dst[0].bot);
}
