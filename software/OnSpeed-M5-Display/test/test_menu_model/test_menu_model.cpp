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
      nullptr, nullptr,
      nullptr, 0, nullptr },
    { "Exit",        ItemType::Action, nullptr, nullptr, nullptr,
      nullptr /* null callback = Exit */, nullptr,
      nullptr, 0, nullptr },
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

// Activating a Toggle item flips the bool through the pointer.
void test_toggle_flips_value(void) {
    MenuModel m = make_model();
    TEST_ASSERT_FALSE(g_test_toggle);

    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_TRUE(g_test_toggle);

    r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_FALSE(g_test_toggle);
}

// Activating an Action item with a null callback is the built-in Exit:
// returns kExit and sets wantsExit().
void test_exit_action_sets_wants_exit(void) {
    MenuModel m = make_model();
    m.onDown();   // move to Exit (index 1)
    TEST_ASSERT_FALSE(m.wantsExit());

    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kExit);
    TEST_ASSERT_TRUE(m.wantsExit());
}

// 30-second idle timeout: tick(elapsedMs) accumulating past kIdleTimeoutMs
// auto-flips wantsExit().
void test_idle_timeout_exits(void) {
    MenuModel m = make_model();
    m.tick(29'999);
    TEST_ASSERT_FALSE(m.wantsExit());
    m.tick(2);  // crosses 30000
    TEST_ASSERT_TRUE(m.wantsExit());
}

// Any input resets the idle timer.
void test_input_resets_idle_timer(void) {
    MenuModel m = make_model();
    m.tick(20'000);
    m.onDown();             // resets idleMs_ to 0
    m.tick(20'000);         // idle is 20s now, well under 30s
    TEST_ASSERT_FALSE(m.wantsExit());
}

// onBackOrLongPressExit() forces wantsExit() regardless of cursor position.
void test_back_or_longpress_exits_immediately(void) {
    MenuModel m = make_model();
    TEST_ASSERT_FALSE(m.wantsExit());

    m.onBackOrLongPressExit();
    TEST_ASSERT_TRUE(m.wantsExit());
}

// Activating an Info item does nothing visible — no toggle, no exit, cursor
// stays put.
void test_info_activation_is_noop(void) {
    static const char* k_info_value = "v1.2.3";
    auto info_getter = []() -> const char* { return k_info_value; };
    static MenuItem info_items[] = {
        { "Version", ItemType::Info, nullptr, nullptr, nullptr, nullptr,
          info_getter,
          nullptr, 0, nullptr },
        { "Exit",    ItemType::Action, nullptr, nullptr, nullptr, nullptr,
          nullptr,
          nullptr, 0, nullptr },
    };
    MenuModel m(info_items, 2);

    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kStayed);
    TEST_ASSERT_FALSE(m.wantsExit());
    TEST_ASSERT_EQUAL_INT(0, m.currentIndex());
}

// resetForEntry() clears all entry-time state: cursor returns to 0,
// idle counter zeroed, exit flag cleared.
void test_reset_for_entry_clears_state(void) {
    MenuModel m = make_model();
    m.onDown();                             // cursor moves off 0
    TEST_ASSERT_EQUAL_INT(1, m.currentIndex());

    m.tick(40'000);
    TEST_ASSERT_TRUE(m.wantsExit());

    m.resetForEntry();
    TEST_ASSERT_EQUAL_INT(0, m.currentIndex());   // cursor reset
    TEST_ASSERT_FALSE(m.wantsExit());

    m.tick(20'000);
    TEST_ASSERT_FALSE(m.wantsExit());   // idle reset, still under 30s
}

// Activating a Choice item cycles through its options modulo choiceCount.
// With choiceCount = 3, four activates produce 1, 2, 0, 1.
void test_activate_cyclesThroughChoices(void) {
    static int choice = 0;
    static const char* const labels[] = { "A", "B", "C" };
    static MenuItem items[] = {
        { "Mode", ItemType::Choice, nullptr, nullptr, nullptr, nullptr, nullptr,
          &choice, 3, labels },
    };
    MenuModel m(items, 1);

    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_EQUAL_INT(1, choice);

    r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_EQUAL_INT(2, choice);

    r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_EQUAL_INT(0, choice);

    r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_EQUAL_INT(1, choice);
}

// A Choice item with a single option still reports kToggled (the caller
// cares only that the value may have changed and should be persisted),
// but the value stays at 0.
void test_choiceWithSingleOption(void) {
    static int choice = 0;
    static const char* const labels[] = { "Only" };
    static MenuItem items[] = {
        { "Mode", ItemType::Choice, nullptr, nullptr, nullptr, nullptr, nullptr,
          &choice, 1, labels },
    };
    MenuModel m(items, 1);

    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_EQUAL_INT(0, choice);
}

// A Choice item with a null choiceValue pointer is a no-op: returns the
// "did nothing" sentinel, matches the convention used for null
// toggleValue / null getInfoValue.
void test_choiceWithNullPointer(void) {
    static const char* const labels[] = { "A", "B" };
    static MenuItem items[] = {
        { "Mode", ItemType::Choice, nullptr, nullptr, nullptr, nullptr, nullptr,
          nullptr, 2, labels },
    };
    MenuModel m(items, 1);

    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kStayed);
}

// Navigation works correctly across an array mixing all item types:
// Toggle flips a bool, Choice cycles its int, Action with null callback
// sets wantsExit().
void test_mixedItemsArray(void) {
    static bool mixed_toggle = false;
    static int  mixed_choice = 0;
    static const char* const mixed_labels[] = { "X", "Y", "Z" };
    static MenuItem mixed_items[] = {
        { "Toggle", ItemType::Toggle, &mixed_toggle, "OFF", "ON",
          nullptr, nullptr,
          nullptr, 0, nullptr },
        { "Choice", ItemType::Choice, nullptr, nullptr, nullptr,
          nullptr, nullptr,
          &mixed_choice, 3, mixed_labels },
        { "Exit",   ItemType::Action, nullptr, nullptr, nullptr,
          nullptr, nullptr,
          nullptr, 0, nullptr },
    };
    MenuModel m(mixed_items, 3);

    // Index 0: Toggle. Activate flips bool, returns kToggled.
    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_TRUE(mixed_toggle);

    // Down to index 1: Choice. Activate cycles int.
    m.onDown();
    TEST_ASSERT_EQUAL_INT(1, m.currentIndex());
    r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kToggled);
    TEST_ASSERT_EQUAL_INT(1, mixed_choice);

    // Down to index 2: Exit action. Activate sets wantsExit, returns kExit.
    m.onDown();
    TEST_ASSERT_EQUAL_INT(2, m.currentIndex());
    r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kExit);
    TEST_ASSERT_TRUE(m.wantsExit());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cursor_wraps_both_directions);
    RUN_TEST(test_toggle_flips_value);
    RUN_TEST(test_exit_action_sets_wants_exit);
    RUN_TEST(test_idle_timeout_exits);
    RUN_TEST(test_input_resets_idle_timer);
    RUN_TEST(test_back_or_longpress_exits_immediately);
    RUN_TEST(test_info_activation_is_noop);
    RUN_TEST(test_reset_for_entry_clears_state);
    RUN_TEST(test_activate_cyclesThroughChoices);
    RUN_TEST(test_choiceWithSingleOption);
    RUN_TEST(test_choiceWithNullPointer);
    RUN_TEST(test_mixedItemsArray);
    return UNITY_END();
}
