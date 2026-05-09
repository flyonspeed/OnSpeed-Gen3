// software/OnSpeed-M5-Display/test/test_menu_model/test_menu_model.cpp
//
// Unity tests for the pure-logic MenuModel. Runs under [env:m5-native-test].
// Mirrors the main firmware's test/ tree pattern (Unity, void setUp/tearDown,
// RUN_TEST in main).
#include <unity.h>
#include "MenuModel.h"

using m5menu::MenuModel;
using m5menu::MenuItem;
using m5menu::ItemType;

void setUp(void) {}
void tearDown(void) {}

// Build a canonical 2-item test menu: one toggle, one Exit action.
// Caller-owned storage (the production code does the same thing).
static bool g_test_toggle = false;
static MenuItem g_test_items[] = {
    { "Speed Units", ItemType::Toggle, &g_test_toggle, "KTS", "MPH",
      nullptr, nullptr },
    { "Exit",        ItemType::Action, nullptr, nullptr, nullptr,
      nullptr /* null callback = Exit */, nullptr },
};

static MenuModel make_model() {
    g_test_toggle = false;
    return MenuModel(g_test_items, 2);
}

// Up from index 0 wraps to the last item; down from last wraps to 0.
void test_cursor_wraps_both_directions(void) {
    MenuModel m = make_model();
    TEST_ASSERT_EQUAL_INT(0, m.currentIndex());

    m.onUp();
    TEST_ASSERT_EQUAL_INT(1, m.currentIndex());  // wrapped up to last

    m.onDown();
    TEST_ASSERT_EQUAL_INT(0, m.currentIndex());  // wrapped down to 0
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cursor_wraps_both_directions);
    return UNITY_END();
}
