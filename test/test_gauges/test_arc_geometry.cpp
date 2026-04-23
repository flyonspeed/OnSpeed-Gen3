// test_arc_geometry.cpp — unit tests for onspeed::gauges::ArcGeometry.
//
// Locks down the fixes for findings 029 (CCW geometry bug — the original
// code negated cosine, reflecting across Y rather than reversing the
// traversal direction) and 033 (stale end-cap coordinates — the original
// end-cap used loop-local variables that either overshot the final angle
// or stayed at zero if the loop never ran). Also provides indirect
// coverage of finding 047 by exercising the sinf/cosf code path.

#include <unity.h>

#include <gauges/ArcGeometry.h>

#include <cmath>

using namespace onspeed::gauges;

static constexpr float kFloatTol = 1e-6f;

// ---------------------------------------------------------------------------
// CW quad tests
// ---------------------------------------------------------------------------

void test_cw_quad_a_matches_startangle(void)
{
    // At j=0, CW, startAngle=0: edgeA should be at theta=0 => cos=1, sin=0.
    ArcQuad q = ComputeArcQuad(/*startAngle=*/0.0f, /*j=*/0.0f,
                               /*stepRad=*/0.1f, ArcDirection::CW);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 1.0f, q.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 0.0f, q.edgeA.sinV);
}

void test_cw_quad_b_matches_startangle_plus_step(void)
{
    // At j=0, CW, startAngle=0, stepRad=0.1: edgeB at theta=0.1.
    ArcQuad q = ComputeArcQuad(0.0f, 0.0f, 0.1f, ArcDirection::CW);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.1f), q.edgeB.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(0.1f), q.edgeB.sinV);
}

// ---------------------------------------------------------------------------
// CCW quad tests — the Option A semantic
// ---------------------------------------------------------------------------

void test_ccw_quad_a_matches_startangle_minus_j(void)
{
    // At j=0.2, CCW, startAngle=1.0: edgeA at theta = 1.0 - 0.2 = 0.8.
    ArcQuad q = ComputeArcQuad(/*startAngle=*/1.0f, /*j=*/0.2f,
                               /*stepRad=*/0.1f, ArcDirection::CCW);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.8f), q.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(0.8f), q.edgeA.sinV);
}

void test_ccw_quad_b_matches_startangle_minus_j_minus_step(void)
{
    // At j=0.2, CCW, startAngle=1.0, stepRad=0.1: edgeB at theta = 0.7.
    ArcQuad q = ComputeArcQuad(1.0f, 0.2f, 0.1f, ArcDirection::CCW);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.7f), q.edgeB.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(0.7f), q.edgeB.sinV);
}

void test_ccw_cw_same_angular_span_cover_same_arc(void)
{
    // Directly encodes the finding 029 invariant: CCW is direction
    // reversal, not reflection. A CW sweep from startAngle=0 with steps
    // 0, 0.1, 0.2 and a CCW sweep from startAngle=0.3 with the same
    // steps should visit the same unique set of angles, just in
    // opposite order.
    constexpr float stepRad = 0.1f;

    ArcQuad cw0 = ComputeArcQuad(0.0f, 0.0f, stepRad, ArcDirection::CW);
    ArcQuad cw1 = ComputeArcQuad(0.0f, 0.1f, stepRad, ArcDirection::CW);
    ArcQuad cw2 = ComputeArcQuad(0.0f, 0.2f, stepRad, ArcDirection::CW);

    // CW: edgeA angles are 0.0, 0.1, 0.2; edgeB angles are 0.1, 0.2, 0.3.
    // Unique set = {0.0, 0.1, 0.2, 0.3}.
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.0f), cw0.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.1f), cw1.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.2f), cw2.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.3f), cw2.edgeB.cosV);

    ArcQuad ccw0 = ComputeArcQuad(0.3f, 0.0f, stepRad, ArcDirection::CCW);
    ArcQuad ccw1 = ComputeArcQuad(0.3f, 0.1f, stepRad, ArcDirection::CCW);
    ArcQuad ccw2 = ComputeArcQuad(0.3f, 0.2f, stepRad, ArcDirection::CCW);

    // CCW: edgeA angles are 0.3, 0.2, 0.1; edgeB angles are 0.2, 0.1, 0.0.
    // Unique set = {0.0, 0.1, 0.2, 0.3} — same as CW.
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.3f), ccw0.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.2f), ccw1.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.1f), ccw2.edgeA.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(0.0f), ccw2.edgeB.cosV);

    // And critical: CCW sinf matches the non-reflected angle.
    // The original bug kept sinA = +sin(theta) but negated cosA, so
    // sin values would be correct but cos values would be wrong.
    // After the fix, both sin and cos match the correctly-decremented
    // theta. Double-check sin for the CCW sweep here:
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(0.3f), ccw0.edgeA.sinV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(0.2f), ccw1.edgeA.sinV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(0.1f), ccw2.edgeA.sinV);
}

// ---------------------------------------------------------------------------
// End-cap tests — finding 033
// ---------------------------------------------------------------------------

void test_endcap_cw_lands_at_start_plus_arcangle(void)
{
    const float pi_over_2 = 3.14159265358979323846f / 2.0f;
    ArcEdge e = ComputeEndCapAngle(/*startAngle=*/0.0f,
                                   /*arcAngle=*/pi_over_2,
                                   ArcDirection::CW);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(pi_over_2), e.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(pi_over_2), e.sinV);
}

void test_endcap_ccw_lands_at_start_minus_abs_arcangle(void)
{
    // Caller passes negative arcAngle to signal CCW intent. Final angle
    // is startAngle + arcAngle = PI + (-PI/2) = PI/2.
    const float pi        = 3.14159265358979323846f;
    const float pi_over_2 = pi / 2.0f;
    ArcEdge e = ComputeEndCapAngle(/*startAngle=*/pi,
                                   /*arcAngle=*/-pi_over_2,
                                   ArcDirection::CCW);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(pi_over_2), e.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(pi_over_2), e.sinV);
}

void test_endcap_zero_arcangle_equals_startangle(void)
{
    // The zero-arcAngle case: where the original code used stale
    // zero-initialised loop-local variables, the extracted function
    // returns the geometrically correct start-arc position.
    const float startAngle = 1.234f;
    ArcEdge e = ComputeEndCapAngle(startAngle, /*arcAngle=*/0.0f,
                                   ArcDirection::CW);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(startAngle), e.cosV);
    TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(startAngle), e.sinV);
}

// ---------------------------------------------------------------------------
// Precision / invariants
// ---------------------------------------------------------------------------

void test_endcap_uses_float_trig_not_double(void)
{
    // Indirect validation: compare the extracted function's output to
    // reference sinf/cosf. If the implementation were using double
    // precision, the results would still be within ~1 ULP of sinf and
    // thus pass this test, but re-visiting this test during code review
    // is what really locks down the single-precision choice. The
    // finding-047 documentation test.
    for (float t = -3.14f; t <= 3.14f; t += 0.2f)
    {
        ArcEdge e = ComputeEndCapAngle(/*startAngle=*/0.0f,
                                       /*arcAngle=*/t,
                                       ArcDirection::CW);
        TEST_ASSERT_FLOAT_WITHIN(kFloatTol, cosf(t), e.cosV);
        TEST_ASSERT_FLOAT_WITHIN(kFloatTol, sinf(t), e.sinV);
    }
}

void test_cw_quad_magnitude_is_one(void)
{
    // Unit-circle invariant: cos^2 + sin^2 == 1 for every angle the
    // function emits. Guards against any future "scaling" bug.
    for (int step = 0; step < 10; ++step)
    {
        const float j = 0.1f * static_cast<float>(step);

        ArcQuad cw = ComputeArcQuad(/*startAngle=*/0.5f, j,
                                    /*stepRad=*/0.1f, ArcDirection::CW);
        const float magCwA = cw.edgeA.cosV * cw.edgeA.cosV
                           + cw.edgeA.sinV * cw.edgeA.sinV;
        const float magCwB = cw.edgeB.cosV * cw.edgeB.cosV
                           + cw.edgeB.sinV * cw.edgeB.sinV;
        TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 1.0f, magCwA);
        TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 1.0f, magCwB);

        ArcQuad ccw = ComputeArcQuad(/*startAngle=*/0.5f, j,
                                     /*stepRad=*/0.1f, ArcDirection::CCW);
        const float magCcwA = ccw.edgeA.cosV * ccw.edgeA.cosV
                            + ccw.edgeA.sinV * ccw.edgeA.sinV;
        const float magCcwB = ccw.edgeB.cosV * ccw.edgeB.cosV
                            + ccw.edgeB.sinV * ccw.edgeB.sinV;
        TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 1.0f, magCcwA);
        TEST_ASSERT_FLOAT_WITHIN(kFloatTol, 1.0f, magCcwB);
    }
}
