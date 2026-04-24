// test_decel_gauge.cpp — PR-0 baseline assertions for `displayDecelGauge`
// (display mode 3).

#include "TestHelpers.h"
#include "goldens.h"

void test_displayDecelGauge_zero_decel_centers_indicator(void)
{
    resetState();
    g_state.SmoothedDecelRate = 0.0f;
    g_state.displayIAS        = 80;
    g_state.displayDecelRate  = 0.0f;
    g_state.iVSI              = 0.0f;

    displayDecelGauge();

    // Gauge shell: red fillRoundRect + green band fillRect + grey outline.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, drawEvents().colorHistogram().count(0xF800)); // TFT_RED
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, drawEvents().colorHistogram().count(0x07E0)); // TFT_GREEN

    // Anchor: the indicator pointer is a 7-tall white fillRect starting
    // at x=109, width=102. At zero decel the computed index is
    // int(35.143*0 + 141.48 - 3.5) = 137.
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("fillRect", { 109, 137, 102, 7 }),
        "decel pointer rectangle at (109,137,102,7) for zero decel");

    // Anchor: 5 pip lines on the tick ladder.
    TEST_ASSERT_TRUE(drawEvents().containsCall("drawLine", { 99, 106, 107, 106 }));
    TEST_ASSERT_TRUE(drawEvents().containsCall("drawLine", { 99,  72, 107,  72 }));
    TEST_ASSERT_TRUE(drawEvents().containsCall("drawLine", { 99, 141, 107, 141 }));

    assertOrPrintGolden("GOLDEN_decel_gauge_zero", GOLDEN_decel_gauge_zero);
}

void test_displayDecelGauge_negative_decel_moves_indicator_up(void)
{
    resetState();
    g_state.SmoothedDecelRate = -2.0f;
    g_state.displayIAS        = 65;
    g_state.displayDecelRate  = -2.0f;

    displayDecelGauge();

    // decelIndex = int(35.143*-2 + 141.48 - 3.5) = int(67.694) = 67
    // clamped to [2,205] → 67.
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("fillRect", { 109, 67, 102, 7 }),
        "decel pointer moves up to y=67 for -2 kt/s");

    assertOrPrintGolden("GOLDEN_decel_gauge_negative", GOLDEN_decel_gauge_negative);
}
