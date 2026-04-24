// test_g_history.cpp — PR-0 baseline assertions for `displayGloadHistory`
// (display mode 4).

#include "TestHelpers.h"
#include "goldens.h"

void test_displayGloadHistory_flat_1g_trace_all_green(void)
{
    resetState();
    // Buffer is already prefilled to 1.0 by resetState().
    g_state.gHistoryIndex = 0;

    displayGloadHistory();

    // 300 circles on the trace (x from 319 down to 20 inclusive).
    TEST_ASSERT_EQUAL_UINT32(300, drawEvents().drawCallCount("fillCircle"));

    // All 1G — must all be green. 300 green fillCircles plus a few other
    // green draws (none exist elsewhere on this page), so histogram[green]
    // equals the fillCircle count.
    TEST_ASSERT_EQUAL_UINT32(300,
        drawEvents().colorHistogram().at(0x07E0));      // TFT_GREEN

    // No yellow and no red when the whole trace is 1g.
    TEST_ASSERT_EQUAL_UINT32(0,
        drawEvents().colorHistogram().count(0xFFE0));    // TFT_YELLOW
    TEST_ASSERT_EQUAL_UINT32(0,
        drawEvents().colorHistogram().count(0xF800));    // TFT_RED

    // Anchor: the "1" label on the pips line is at (18,133).
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsDrawString("1", 18, 133),
        "'1' pip label anchored at (18,133)");

    assertOrPrintGolden("GOLDEN_gload_history_flat", GOLDEN_gload_history_flat);
}

void test_displayGloadHistory_varied_trace_mixes_colors(void)
{
    resetState();
    // Force a mix: first 100 samples at 1.5g (green), next 100 at 0.5g
    // (yellow), last 100 at -0.5g (red).
    for (int i = 0; i < 100;   ++i) g_state.gHistory[i] = 1.5f;
    for (int i = 100; i < 200; ++i) g_state.gHistory[i] = 0.5f;
    for (int i = 200; i < 300; ++i) g_state.gHistory[i] = -0.5f;
    g_state.gHistoryIndex = 0;

    displayGloadHistory();

    // 300 circles; each color band contributes 100.
    TEST_ASSERT_EQUAL_UINT32(100, drawEvents().colorHistogram().at(0x07E0)); // green
    TEST_ASSERT_EQUAL_UINT32(100, drawEvents().colorHistogram().at(0xFFE0)); // yellow
    TEST_ASSERT_EQUAL_UINT32(100, drawEvents().colorHistogram().at(0xF800)); // red

    assertOrPrintGolden("GOLDEN_gload_history_varied", GOLDEN_gload_history_varied);
}
