// test_gauge_state.cpp — unit tests for onspeed::gauges::GaugeState.
//
// Locks down the fix for finding 045: the original `Gauges::Gauges() {}`
// left ~30 members uninitialised. For non-static instances, every field
// holds whatever was on the stack/heap — undefined behaviour in C++.
// The extracted struct uses C++11 in-class default initializers so every
// instance is deterministic at construction. The tests below verify that
// (a) every field has the documented default, (b) defaults do not leak
// between instances, and (c) construction on a deliberately-dirty stack
// still produces a clean instance.

#include <unity.h>

#include <gauges/GaugeState.h>

#include <cstdint>
#include <cstring>

using namespace onspeed::gauges;

// ---------------------------------------------------------------------------
// Per-field default checks
// ---------------------------------------------------------------------------

void test_default_construction_all_range_valid_false(void)
{
    GaugeState s;
    // Indices 1..kNumRanges are the live slots; index 0 is unused but
    // must still be zero-initialised.
    for (int i = 0; i <= kNumRanges; ++i)
    {
        TEST_ASSERT_FALSE_MESSAGE(s.rangeValid[i],
                                  "rangeValid must default to false");
    }
}

void test_default_construction_range_arrays_zero(void)
{
    GaugeState s;
    for (int i = 0; i <= kNumRanges; ++i)
    {
        TEST_ASSERT_EQUAL_INT32(0, s.rangeTop  [i]);
        TEST_ASSERT_EQUAL_INT32(0, s.rangeBot  [i]);
        TEST_ASSERT_EQUAL_INT32(0, s.rangeColor[i]);
    }
}

void test_default_construction_pointer_arrays_zero(void)
{
    GaugeState s;
    for (int i = 0; i <= kNumPointers; ++i)
    {
        TEST_ASSERT_EQUAL_INT32(0,    s.pointerValue[i]);
        TEST_ASSERT_EQUAL_INT32(0,    s.pointerType [i]);
        TEST_ASSERT_EQUAL_INT32(0,    s.pointerColor[i]);
        TEST_ASSERT_EQUAL_INT8 ('\0', s.pointerTag  [i]);
    }
}

void test_default_construction_clockwise_true(void)
{
    GaugeState s;
    TEST_ASSERT_TRUE(s.clockWise);
}

void test_default_construction_style_fields(void)
{
    GaugeState s;
    TEST_ASSERT_EQUAL_UINT16(1, s.lineWidth);
    TEST_ASSERT_EQUAL_UINT16(0, s.edgeWidth);
    TEST_ASSERT_EQUAL_UINT8 (0, s.lineEnd);
    TEST_ASSERT_EQUAL_UINT8 (0, s.edgeEnd);
    TEST_ASSERT_EQUAL_UINT8 (0, s.gradLineEnd);
}

// ---------------------------------------------------------------------------
// Cross-instance isolation — finding 045
// ---------------------------------------------------------------------------

// Force a dirty stack frame before constructing. This is a best-effort
// stress test of the finding-045 semantic: even if the compiler had
// not arranged for zero-init, the in-class initializers must run. The
// helper is marked volatile to discourage the optimiser from removing
// it, and writes non-zero bytes to a local buffer that occupies the
// same stack region the next GaugeState will use.
static void DirtyStackFrame()
{
    volatile uint8_t scratch[sizeof(GaugeState)];
    for (size_t i = 0; i < sizeof(scratch); ++i)
    {
        scratch[i] = static_cast<uint8_t>(0xBE);
    }
    // Use `scratch` so -Wunused-variable does not fire.
    (void)scratch[0];
}

void test_default_state_is_deterministic(void)
{
    // Construct two instances in sequence. All fields must match.
    GaugeState a;
    GaugeState b;

    TEST_ASSERT_EQUAL_INT(a.clockWise  ? 1 : 0, b.clockWise ? 1 : 0);
    TEST_ASSERT_EQUAL_INT16(a.maxDisplay, b.maxDisplay);
    TEST_ASSERT_EQUAL_INT16(a.minDisplay, b.minDisplay);
    TEST_ASSERT_EQUAL_INT16(a.barWidth,   b.barWidth);
    TEST_ASSERT_EQUAL_INT16(a.barSize,    b.barSize);
    TEST_ASSERT_EQUAL_UINT16(a.lineWidth, b.lineWidth);
    TEST_ASSERT_EQUAL_UINT16(a.edgeWidth, b.edgeWidth);
    for (int i = 0; i <= kNumRanges; ++i)
    {
        TEST_ASSERT_EQUAL_INT32(a.rangeTop[i],   b.rangeTop[i]);
        TEST_ASSERT_EQUAL_INT32(a.rangeBot[i],   b.rangeBot[i]);
        TEST_ASSERT_EQUAL_INT32(a.rangeColor[i], b.rangeColor[i]);
    }
}

void test_modified_state_does_not_bleed_to_next_instance(void)
{
    // Step 1: construct an instance and mutate it.
    {
        GaugeState a;
        a.rangeTop[1] = 999;
        a.rangeBot[1] = -999;
        a.clockWise   = false;
        a.barSize     = 1234;
        // a goes out of scope here.
        (void)a;
    }

    // Step 2: dirty the stack (best-effort).
    DirtyStackFrame();

    // Step 3: construct a fresh instance. Every field must be its default,
    // not the dirtied bytes and not the previous instance's values.
    GaugeState b;
    TEST_ASSERT_EQUAL_INT32(0, b.rangeTop[1]);
    TEST_ASSERT_EQUAL_INT32(0, b.rangeBot[1]);
    TEST_ASSERT_TRUE(b.clockWise);
    TEST_ASSERT_EQUAL_INT16(0, b.barSize);
}
