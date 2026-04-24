// test_splash.cpp — PR-0 baseline assertions for `displaySplashScreen()`.

#include "TestHelpers.h"
#include "goldens.h"

void test_displaySplashScreen_draws_title_and_version(void)
{
    resetState();
    displaySplashScreen();

    // "Fly OnSpeed" title is drawn once.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, drawEvents().drawCallCount("drawString"));

    // Text color is white.
    TEST_ASSERT_TRUE(drawEvents().containsCall("setTextColor", { 0xFFFF })); // TFT_WHITE

    // pushSprite(0, 0) terminates the frame.
    TEST_ASSERT_TRUE_MESSAGE(
        drawEvents().containsCall("pushSprite", { 0, 0 }),
        "splash ends with pushSprite(0,0)");
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, drawEvents().drawCallCount("deleteSprite"));

    assertOrPrintGolden("GOLDEN_splash_screen", GOLDEN_splash_screen);
}
