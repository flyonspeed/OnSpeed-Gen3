// test_display_aoa.cpp — PR-0 baseline assertions for `displayAOA()`
// and its two sub-renderers (`drawAOA`, `drawSlip`).

#include "TestHelpers.h"
#include "goldens.h"

void test_displayAOA_draws_expected_shapes(void)
{
    resetState();
    g_state.AOA                 = 12.0f;   // between fast (10) and slow (15) — green ON-SPEED
    g_state.PercentLift         = 55;
    g_state.displayPercentLift  = 55;
    g_state.displayIAS          = 82;
    g_state.displayVerticalG    = 1.0f;
    g_state.FlapPos             = 0;
    g_state.Slip                = 5;
    g_state.wgtX0               = 109;
    g_state.wgtY0               = 0;
    g_state.wgtWidth            = 102;
    g_state.wgtHeight           = 192;
    g_state.numericDisplay      = true;

    displayAOA();

    // Structural assertions — these stay live even in golden-update mode,
    // acting as a second line of defense for gross regressions.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4, drawEvents().drawCallCount("fillTriangle")); // 2 chevrons * 2 halves
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, drawEvents().drawCallCount("drawRoundRect")); // bounding box
    TEST_ASSERT_EQUAL_UINT32(2, drawEvents().drawCallCount("gauges.drawArc"));           // OnSpeed donut
    // Green band of the chevron/donut fires at this AOA.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, drawEvents().colorHistogram().count(0x07E0)); // TFT_GREEN

    // Anchor: flap triangle center is at (23, 204) in widget mode.
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("fillCircle", { 23, 204, 16 }),
        "flap indicator centre should be at (23,204) r=16");

    // Anchor: the AOA "bullsEye" fillCircle uses the widget center X (160)
    // because wgtX0+wgtWidth/2 = 109+51 = 160.
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("drawRoundRect", { 109, 0, 102, 192, 5 }),
        "AOA gauge bounding rect is (109, 0, 102, 192, r=5)");

    assertOrPrintGolden("GOLDEN_displayAOA", GOLDEN_displayAOA);
}

void test_displayAOA_without_numeric_display(void)
{
    resetState();
    g_state.AOA            = 8.0f;   // below fast → no chevron highlight
    g_state.numericDisplay = false;  // narrow-AOA mode (display type 2)

    displayAOA();

    // No flap circle/triangle when numericDisplay = false.
    TEST_ASSERT_FALSE(drawEvents().containsCall("fillCircle", { 23, 204, 16 }));

    assertOrPrintGolden("GOLDEN_displayAOA_numericless", GOLDEN_displayAOA_numericless);
}

void test_displayAOA_no_chevron_when_aoa_below_tones_on(void)
{
    resetState();
    g_state.AOA = 1.0f;   // well below OnSpeedTonesOnAOA (5)

    displayAOA();

    // The AOA gauge draws greys below tonesOn: all 4 chevrons and the
    // center dot and both arc halves are TFT_DARKGREY. The slip-ball at
    // (200, 221) is the ONLY green draw on the page — it defaults to
    // green and turns red/black only under stall+slip conditions.
    auto greenCount = drawEvents().colorHistogram().count(0x07E0)
                    ? drawEvents().colorHistogram().at(0x07E0) : 0;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(greenCount),
        "only the slip ball should be green when AOA < tonesOn threshold");

    // Confirm chevrons/arcs are all dark grey.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4,
        drawEvents().colorHistogram().at(0x7BEF));       // TFT_DARKGREY
}
