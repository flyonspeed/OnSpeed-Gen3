# M5 Settings Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a runtime settings menu to the M5 / huVVer display that lets pilots toggle KTS / MPH at runtime, persisted to NVS — replacing the compile-time `IAS_IN_MPH` flag from issue #419.

**Architecture:** Pure-logic `MenuModel` state machine in `lib/MenuModel/` (peer to `lib/GaugeWidgets/`), platform glue in `SettingsMenu.cpp`, called from `main.cpp` after `M5.update()`. Per-target button vocabularies via `#ifdef HUVVER`: M5 uses BtnB long-press to enter; huVVer uses Vern Little's TBX idiom (square=back/menu, round=select, triangles=navigate). Live serial + render are paused while menu is up.

**Tech Stack:** PlatformIO (espressif32 + native), M5Unified + M5GFX, Unity test framework, ESP32 NVS via `Preferences`. C++17 (project default).

**Companion spec:** `docs/superpowers/specs/2026-05-08-m5-settings-menu-design.md`. Read it first if anything below is ambiguous.

---

## File Structure

**New files:**
- `software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.h` — pure-logic state machine, no Arduino / no M5GFX deps.
- `software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.cpp` — implementation.
- `software/OnSpeed-M5-Display/lib/MenuModel/library.json` — PlatformIO library descriptor.
- `software/OnSpeed-M5-Display/test/test_menu_model/test_menu_model.cpp` — Unity tests.
- `software/OnSpeed-M5-Display/include/SettingsMenu.h` — platform-layer surface.
- `software/OnSpeed-M5-Display/src/SettingsMenu.cpp` — wires `MenuModel` to M5GFX render + NVS + button polling.

**Modified files:**
- `software/OnSpeed-M5-Display/src/main.cpp` — boot-time NVS read of `g_speedInMph`; live-mode `wasPressed()` → `wasClicked()`; menu entry gesture; replace `#ifdef IAS_IN_MPH` with runtime conditional; delete the `#define IAS_IN_MPH` block.
- `software/OnSpeed-M5-Display/sim/HuvverShim.h` — add `wasClicked()` and `wasHold()` to `HuvverButton` matching M5Unified semantics.
- `software/OnSpeed-M5-Display/platformio.ini` — add `[env:m5-native-test]` for running `MenuModel` tests.
- `software/OnSpeed-M5-Display/CLAUDE.md` — document the menu, button changes, file layout.
- `docs/site/docs/installation/external-display.md` — user-facing menu instructions.

---

## Key Spec Facts To Remember

1. **`SpeedMph` default is `true` (MPH)**, not `false`. Master currently has `#define IAS_IN_MPH` enabled, so the runtime default must preserve current behavior for existing pilots' first boot. The spec's earlier wording said "default false" — `true` is correct.
2. **Live `SerialRead()` is paused** while the menu is active. Acceptable (full-screen menu hides the live render, RX FIFO holds ~3 frames, 30s timeout caps the visit).
3. **No "Saving..." banner.** Toggle flips persist immediately on press. Clean cut from menu to live mode on exit.
4. **`MenuModel` is in the M5 sub-project**, NOT in `onspeed_core`. `onspeed_core` is for flight math; UI state is a separate concern.
5. **Per-target buttons:**
   - **M5:** BtnA / BtnC = brightness halve / double, BtnB short = mode cycle, BtnB long-hold = enter menu. Inside menu: BtnA up, BtnB activate (or long-hold to exit), BtnC down.
   - **huVVer:** BtnD = brightness halve (NEW — promoted from "unused"), BtnC = brightness double, BtnB short = mode cycle, BtnA short = (free), BtnA long-hold = enter menu. Inside menu: BtnD up, BtnC down, BtnB activate, BtnA = back/exit (short press).
6. **`wasPressed()` → `wasClicked()` migration is REQUIRED**, not optional. M5Unified's `wasPressed()` fires on press edge — long-press would otherwise fire one mode-cycle on press *and* the menu 600ms later. `wasClicked()` fires on release-after-short-press only and is suppressed if the gesture crossed the hold threshold.

---

## Task 1: MenuModel skeleton, test infrastructure, first wraparound test

**Files:**
- Create: `software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.h`
- Create: `software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.cpp`
- Create: `software/OnSpeed-M5-Display/lib/MenuModel/library.json`
- Create: `software/OnSpeed-M5-Display/test/test_menu_model/test_menu_model.cpp`
- Modify: `software/OnSpeed-M5-Display/platformio.ini`

Lays the groundwork: complete `MenuModel` (small enough to write in one shot), the test env, and the first test case to confirm the toolchain is wired correctly. Subsequent test cases are added in Task 2 — each one a one-shot pass against the implementation written here. This is "test-driven" in the sense that tests are the validation gate before each commit, but not strict red-green-refactor (the model is ~60 lines, smaller than the test suite that exercises it).

- [ ] **Step 1: Create the MenuModel header skeleton**

```cpp
// software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.h
//
// Pure-logic menu state machine. No Arduino, no M5GFX, no <chrono>.
// All time is injected via tick(elapsedMs). Lives inside the M5 sub-project
// (not onspeed_core) because it's a UI concern, not flight math.
#pragma once
#include <cstdint>

namespace m5menu {

enum class ItemType { Toggle, Action, Info };

// Tagged-union item: extra fields are valid only for the matching type.
struct MenuItem {
    const char* label;
    ItemType    type;
    // Toggle-only: pointer to the bool the toggle controls; labels for off/on.
    bool*       toggleValue;
    const char* toggleLabelOff;
    const char* toggleLabelOn;
    // Action-only: callback invoked on activate. May be null for the built-in
    // Exit action (the model returns kExit and the caller handles it).
    void      (*onActivate)();
    // Info-only: callback that returns a string to display on the right.
    const char* (*getInfoValue)();
};

class MenuModel {
public:
    enum class ActivateResult { kStayed, kToggled, kAction, kExit };

    static constexpr uint32_t kIdleTimeoutMs = 30'000;

    // items: pointer to caller-owned array (the M5 sub-project keeps the
    // canonical static array in SettingsMenu.cpp). count: number of items.
    MenuModel(MenuItem* items, int count);

    void onUp();      // wraps to last item from index 0
    void onDown();    // wraps to 0 from last item
    ActivateResult onActivate();    // dispatches by current item's type

    // Called by the platform layer on the entry-symmetric gesture
    // (BtnB long-hold on M5, BtnA click on huVVer).
    void onBackOrLongPressExit();

    // Advances the idle timer. Caller passes elapsed millis since last call.
    // If the timer crosses kIdleTimeoutMs, sets wantsExit() to true.
    void tick(uint32_t elapsedMs);

    bool wantsExit() const  { return wantsExit_; }
    int  currentIndex() const { return currentIndex_; }
    int  itemCount() const    { return count_; }
    const MenuItem& itemAt(int idx) const { return items_[idx]; }

    // Test/reset hook: clear the idle timer and the exit flag. Called by the
    // platform layer when the menu is freshly entered.
    void resetForEntry();

private:
    MenuItem* items_;
    int       count_;
    int       currentIndex_   = 0;
    uint32_t  idleMs_         = 0;
    bool      wantsExit_      = false;
};

}  // namespace m5menu
```

- [ ] **Step 2: Create the (initially empty-ish) MenuModel implementation**

```cpp
// software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.cpp
#include "MenuModel.h"

namespace m5menu {

MenuModel::MenuModel(MenuItem* items, int count)
    : items_(items), count_(count) {}

void MenuModel::onUp() {
    idleMs_ = 0;
    if (count_ <= 0) return;
    currentIndex_ = (currentIndex_ - 1 + count_) % count_;
}

void MenuModel::onDown() {
    idleMs_ = 0;
    if (count_ <= 0) return;
    currentIndex_ = (currentIndex_ + 1) % count_;
}

MenuModel::ActivateResult MenuModel::onActivate() {
    idleMs_ = 0;
    if (count_ <= 0) return ActivateResult::kStayed;
    const MenuItem& item = items_[currentIndex_];
    switch (item.type) {
        case ItemType::Toggle:
            if (item.toggleValue) *item.toggleValue = !*item.toggleValue;
            return ActivateResult::kToggled;
        case ItemType::Action:
            if (item.onActivate) {
                item.onActivate();
                return ActivateResult::kAction;
            }
            // Action with no callback = built-in Exit.
            wantsExit_ = true;
            return ActivateResult::kExit;
        case ItemType::Info:
            return ActivateResult::kStayed;
    }
    return ActivateResult::kStayed;
}

void MenuModel::onBackOrLongPressExit() {
    wantsExit_ = true;
}

void MenuModel::tick(uint32_t elapsedMs) {
    idleMs_ += elapsedMs;
    if (idleMs_ >= kIdleTimeoutMs) wantsExit_ = true;
}

void MenuModel::resetForEntry() {
    idleMs_    = 0;
    wantsExit_ = false;
}

}  // namespace m5menu
```

- [ ] **Step 3: Create the library.json so PlatformIO finds it**

```json
{
    "name": "MenuModel",
    "version": "1.0.0",
    "description": "Pure-logic 3/4-button menu state machine for the OnSpeed M5 display",
    "build": {
        "flags": ["-std=gnu++17"]
    }
}
```

- [ ] **Step 4: Create the test file with the wraparound test**

```cpp
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
```

- [ ] **Step 5: Add the m5-native-test env to platformio.ini**

Append the following to `software/OnSpeed-M5-Display/platformio.ini` after the existing `[env:native]` section (file currently ends at line 137 with the `[env:native]` block — append directly after).

```ini

; -----------------------------------------------------------------------
; Native unit-test environment for MenuModel and any future pure-logic
; libs that live under lib/. PlatformIO's test runner skips src/ by default
; (test_build_src defaults to false), so we only need a clean native env
; with Unity wired in.
;
; Build & run: pio test -e m5-native-test --project-dir software/OnSpeed-M5-Display
[env:m5-native-test]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -Wall
    -Wextra
    -Wshadow
lib_deps =
    throwtheswitch/Unity@^2.5.2
```

- [ ] **Step 6: Run the test and verify it passes**

```bash
cd software/OnSpeed-M5-Display
pio test -e m5-native-test
```

Expected: `1 Tests 0 Failures 0 Ignored` and overall `PASSED`. The test exercises the wraparound branches (up→last, down→0).

- [ ] **Step 7: Commit**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec
git add software/OnSpeed-M5-Display/lib/MenuModel/ \
        software/OnSpeed-M5-Display/test/test_menu_model/ \
        software/OnSpeed-M5-Display/platformio.ini
git commit -m "M5 menu: MenuModel skeleton + first wraparound test (#419)"
```

---

## Task 2: MenuModel — toggle / activate / exit / idle / info / back tests

**Files:**
- Modify: `software/OnSpeed-M5-Display/test/test_menu_model/test_menu_model.cpp`

Add the remaining test coverage from the spec's testing section. We're TDD-ing in the order test-first → already-passes (the implementation in Task 1 is complete), so each new test should pass on first run. If any fails, fix the implementation in `MenuModel.cpp`.

- [ ] **Step 1: Add toggle-flip test**

Insert before `int main(...)`:

```cpp
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
```

- [ ] **Step 2: Add Exit-action test**

```cpp
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
```

- [ ] **Step 3: Add idle-timeout test**

```cpp
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
```

- [ ] **Step 4: Add long-press-exit test**

```cpp
// onBackOrLongPressExit() forces wantsExit() regardless of cursor position.
void test_back_or_longpress_exits_immediately(void) {
    MenuModel m = make_model();
    TEST_ASSERT_FALSE(m.wantsExit());

    m.onBackOrLongPressExit();
    TEST_ASSERT_TRUE(m.wantsExit());
}
```

- [ ] **Step 5: Add Info-item no-op test**

```cpp
// Activating an Info item does nothing visible — no toggle, no exit, cursor
// stays put.
void test_info_activation_is_noop(void) {
    static const char* k_info_value = "v1.2.3";
    auto info_getter = []() -> const char* { return k_info_value; };
    static MenuItem info_items[] = {
        { "Version", ItemType::Info, nullptr, nullptr, nullptr, nullptr,
          info_getter },
        { "Exit",    ItemType::Action, nullptr, nullptr, nullptr, nullptr,
          nullptr },
    };
    MenuModel m(info_items, 2);

    auto r = m.onActivate();
    TEST_ASSERT_TRUE(r == MenuModel::ActivateResult::kStayed);
    TEST_ASSERT_FALSE(m.wantsExit());
    TEST_ASSERT_EQUAL_INT(0, m.currentIndex());
}
```

- [ ] **Step 6: Add resetForEntry test**

```cpp
// resetForEntry() clears the idle counter and the exit flag — ready to
// re-use the model after a previous menu visit ended.
void test_reset_for_entry_clears_state(void) {
    MenuModel m = make_model();
    m.tick(40'000);
    TEST_ASSERT_TRUE(m.wantsExit());

    m.resetForEntry();
    TEST_ASSERT_FALSE(m.wantsExit());

    m.tick(20'000);
    TEST_ASSERT_FALSE(m.wantsExit());   // idle reset, still under 30s
}
```

- [ ] **Step 7: Register the new tests in main()**

Replace the existing `main()` body with:

```cpp
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
    return UNITY_END();
}
```

- [ ] **Step 8: Run and verify all 8 tests pass**

```bash
cd software/OnSpeed-M5-Display
pio test -e m5-native-test
```

Expected: `8 Tests 0 Failures 0 Ignored` and `PASSED`.

- [ ] **Step 9: Commit**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec
git add software/OnSpeed-M5-Display/test/test_menu_model/test_menu_model.cpp
git commit -m "M5 menu: full MenuModel test coverage (#419)"
```

---

## Task 3: HuvverShim — add `wasClicked()` and `wasHold()` matching M5Unified semantics

**Files:**
- Modify: `software/OnSpeed-M5-Display/sim/HuvverShim.h:174-219` (the `HuvverButton` class).

The huVVer shim today only implements `isPressed()`, `wasPressed()`, `pressedFor()`. We need `wasClicked()` (release after short press, suppressed by hold) and `wasHold()` (edge-once, fires when the press crosses 600ms). Both match `M5Unified::Button_Class` semantics so the call sites in `main.cpp` are target-agnostic.

- [ ] **Step 1: Read the current HuvverButton definition**

```bash
sed -n '160,220p' software/OnSpeed-M5-Display/sim/HuvverShim.h
```

Confirm the class has the fields: `stable_`, `prevStable_`, `candidate_`, `candidateSince_`, `stableSince_`, and the `poll()` / `isPressed()` / `wasPressed()` / `pressedFor()` methods.

- [ ] **Step 2: Replace the HuvverButton class definition**

The new definition adds `holdFired_` and `everHeld_` flags to track per-gesture state. `wasHold()` returns true exactly once per gesture (first poll where the press passes 600ms). `wasClicked()` returns true on release **only if the gesture never crossed the hold threshold**. Behavior matches M5Unified's `Button_Class`.

Find the existing class (currently lines 174-219 — verify with `grep -n "class HuvverButton" software/OnSpeed-M5-Display/sim/HuvverShim.h`) and replace the entire class block with:

```cpp
class HuvverButton {
private:
    static constexpr uint32_t kDebounceMs  = 50;
    static constexpr uint32_t kHoldMs      = 600;

    int      pin_;
    bool     stable_         = false;
    bool     prevStable_     = false;
    bool     candidate_      = false;
    uint32_t candidateSince_ = 0;
    uint32_t stableSince_    = 0;
    // Per-gesture state for wasHold() / wasClicked():
    bool     holdFired_      = false;  // wasHold() already returned true this gesture
    bool     everHeld_       = false;  // gesture has crossed the hold threshold
    bool     wasClickedFlag_ = false;  // latched on release-after-short-press; cleared by poll()

public:
    explicit HuvverButton(int pin) : pin_(pin) {}

    void poll() {
        const uint32_t now = millis();

        prevStable_     = stable_;
        wasClickedFlag_ = false;  // edge flags clear on each poll; latched again below if applicable

        const bool reading = (digitalRead(pin_) == LOW);

        if (reading != candidate_) {
            candidate_      = reading;
            candidateSince_ = now;
        } else if (reading != stable_ && (now - candidateSince_) >= kDebounceMs) {
            stable_      = reading;
            stableSince_ = now;
            if (!stable_) {
                // Falling edge of the press = release.
                // Latch wasClicked() ONLY if the gesture was a short press
                // (never crossed the hold threshold).
                if (!everHeld_) wasClickedFlag_ = true;
                // Clear per-gesture state for the next press.
                holdFired_ = false;
                everHeld_  = false;
            }
        }
    }

    bool isPressed()  const { return stable_; }

    // Press edge — true on exactly one poll after a press starts.
    bool wasPressed() const { return stable_ && !prevStable_; }

    bool pressedFor(uint32_t ms) const {
        return stable_ && (millis() - stableSince_) >= ms;
    }

    // Release after a short press (gesture < 600ms). Mutually exclusive with
    // wasHold() on the same gesture. Latched by poll() and cleared by the
    // next poll().
    bool wasClicked() const { return wasClickedFlag_; }

    // Edge-once: returns true exactly once when the press crosses kHoldMs.
    // Subsequent polls during the same gesture return false. Mutating because
    // it has to record that it fired — but the call site reads it like a
    // method query, so we mark it mutable-via-mutable-fields.
    bool wasHold() {
        if (!stable_) return false;
        if (holdFired_) return false;
        if ((millis() - stableSince_) < kHoldMs) return false;
        holdFired_ = true;
        everHeld_  = true;  // suppress wasClicked() on this gesture's release
        return true;
    }
};
```

- [ ] **Step 3: Verify the M5Unified equivalents in real hardware code**

Confirm `wasHold()` is non-const in the shim (matches it being called from `loop()`). Confirm `wasClicked()` is `const` (matches the M5Unified surface — `bool wasClicked(void) const`).

```bash
grep -n "wasClicked\|wasHold" software/OnSpeed-M5-Display/.pio/libdeps/m5stack-core-esp32/M5Unified/src/utility/Button_Class.hpp
```

Expected output (M5Unified):
```
22:    bool wasClicked(void)  const { return _currentState == state_clicked; }
25:    bool wasHold(void)     const { return _currentState == state_hold; }
```

Note: M5Unified's `wasHold()` is `const` because the underlying state machine fires it once per `update()`-cycle naturally. The huVVer shim makes `wasHold()` non-const because it needs to latch `holdFired_` itself; this is invisible to call sites (they call it the same way).

- [ ] **Step 4: Compile-check the huVVer build**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec
pio run -e huvver-avi --project-dir software/OnSpeed-M5-Display
```

Expected: builds successfully (uses `wasPressed()` only at this point — adding new methods doesn't break existing call sites).

If `huvver-avi` env requires CH340 USB-serial dongle to upload, that's fine — we only need the build, not the upload. `pio run` (without `-t upload`) builds only.

- [ ] **Step 5: Commit**

```bash
git add software/OnSpeed-M5-Display/sim/HuvverShim.h
git commit -m "huVVer: add wasClicked() / wasHold() to button shim (#419)"
```

---

## Task 4: SettingsMenu platform layer — header

**Files:**
- Create: `software/OnSpeed-M5-Display/include/SettingsMenu.h`

Lays out the M5GFX-side surface that `main.cpp` will call into. Implementation is in Task 5.

- [ ] **Step 1: Write the header**

```cpp
// software/OnSpeed-M5-Display/include/SettingsMenu.h
//
// In-flight settings menu for the OnSpeed display. Implements issue #419
// — runtime KTS/MPH toggle persisted to NVS — and provides a list-driven
// framework for future settings.
//
// The pure-logic state machine lives in lib/MenuModel/. This file is the
// platform glue: M5GFX render, NVS read/write, button polling, lifecycle.
//
// Lifecycle (called from main.cpp's loop()):
//   1. After M5.update(), if isSettingsMenuActive() returns true, the
//      caller should call tickSettingsMenu() and return early — the menu
//      owns the screen and SerialRead is paused for the duration.
//   2. Otherwise, on the platform-specific entry gesture (BtnB long-hold
//      on M5, BtnA long-hold on huVVer), call enterSettingsMenu().
#pragma once

// Initialize from NVS. Call from setup() after M5.begin() and after the
// existing brightness / displayType reads. Reads the persisted SpeedMph
// preference; default true (preserves the current MPH-by-default behavior
// that the deleted IAS_IN_MPH define had on master).
void initSettingsMenu();

// Enter the menu: builds the items array, resets MenuModel state. Allocates
// the menu's off-screen sprite. Idempotent if already active.
void enterSettingsMenu();

// Per-loop tick while the menu is active. Polls M5 buttons (M5.BtnA/B/C
// and BtnD on huVVer), advances MenuModel idle timer, renders the frame,
// handles the "wants exit" flag (deletes the sprite, sets active=false).
void tickSettingsMenu();

// True between enterSettingsMenu() and the tick that processes wantsExit().
bool isSettingsMenuActive();

// The runtime preference. Read by main.cpp's IAS-render block. Default true
// (MPH) on a fresh device with no saved preference, matching the current
// build's IAS_IN_MPH behavior.
extern bool g_speedInMph;
```

- [ ] **Step 2: Commit**

```bash
git add software/OnSpeed-M5-Display/include/SettingsMenu.h
git commit -m "M5 menu: SettingsMenu.h public surface (#419)"
```

---

## Task 5: SettingsMenu platform layer — implementation

**Files:**
- Create: `software/OnSpeed-M5-Display/src/SettingsMenu.cpp`

Wires `MenuModel` to M5GFX rendering, NVS, and the per-target button polling.

- [ ] **Step 1: Write the implementation**

```cpp
// software/OnSpeed-M5-Display/src/SettingsMenu.cpp
//
// Platform layer: drives the pure MenuModel from lib/MenuModel/ against
// M5GFX rendering, the M5Unified Preferences NVS shim, and per-target
// button polling. See SettingsMenu.h for lifecycle.

#include "SettingsMenu.h"

// Mirror main.cpp's include pattern: GaugeWidgets pulls in <M5GFX.h>
// (where M5Canvas / fonts come from) regardless of target.
#include <GaugeWidgets.h>
#if defined(HUVVER)
#include "../sim/HuvverShim.h"
#else
#include <M5Unified.h>
#endif

#include <Free_Fonts.h>

// On the desktop SDL build, the ESP32 Preferences class doesn't exist.
// The M5 sub-project's existing pattern is to include <Preferences.h>
// only behind ESP_PLATFORM and stub it out elsewhere via the sim shim.
#if defined(ESP_PLATFORM)
#include <Preferences.h>
extern Preferences preferences;   // defined in main.cpp
#endif

#include "MenuModel.h"

using m5menu::MenuModel;
using m5menu::MenuItem;
using m5menu::ItemType;

// Off-screen sprite shared with main.cpp. Defined in main.cpp at top scope.
extern M5Canvas gdraw;

// Public global — read by main.cpp's IAS render block. Default true (MPH)
// to match the current IAS_IN_MPH build's behavior on first boot.
bool g_speedInMph = true;

namespace {

// ---------------------------------------------------------------------------
// Menu item table.
//
// Caller-owned static storage: MenuModel just holds a pointer. The Exit
// action has a null callback — MenuModel treats that as the built-in Exit.
// To add a new setting, add an entry here and (if it's a Toggle) wire the
// flip in toggleCallback below.
// ---------------------------------------------------------------------------
MenuItem g_items[] = {
    { "Speed Units", ItemType::Toggle, &g_speedInMph, "KTS", "MPH",
      nullptr, nullptr },
    { "Exit",        ItemType::Action, nullptr, nullptr, nullptr,
      nullptr, nullptr },
};
constexpr int kItemCount = sizeof(g_items) / sizeof(g_items[0]);

MenuModel g_model{g_items, kItemCount};
bool      g_active = false;

// Track last tick time for MenuModel::tick(elapsedMs).
uint32_t  g_lastTickMs = 0;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
void persistSpeedUnits() {
#if defined(ESP_PLATFORM)
    preferences.begin("OnSpeed", false);
    preferences.putBool("SpeedMph", g_speedInMph);
    preferences.end();
#endif
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
constexpr int kTitleY      = 20;
constexpr int kDividerTopY = 38;
constexpr int kFirstRowY   = 70;
constexpr int kRowSpacing  = 30;
constexpr int kDividerBotY = 200;
constexpr int kFooterY     = 220;

constexpr uint16_t kColorTitle      = TFT_WHITE;
constexpr uint16_t kColorBody       = TFT_WHITE;
constexpr uint16_t kColorHighlight  = 0x07FF;   // cyan band
constexpr uint16_t kColorFooterText = 0x7BEF;   // grey
constexpr uint16_t kColorDivider    = 0x4208;   // darker grey

const char* toggleValueLabel(const MenuItem& item) {
    if (item.type != ItemType::Toggle || !item.toggleValue) return "";
    return *item.toggleValue ? item.toggleLabelOn : item.toggleLabelOff;
}

void renderMenu() {
    gdraw.setColorDepth(8);
    gdraw.createSprite(320, 240);
    gdraw.fillSprite(TFT_BLACK);
    gdraw.setTextDatum(textdatum_t::baseline_left);

    // Title
    gdraw.setFont(FSSB12);
    gdraw.setTextColor(kColorTitle);
    {
        const char* title = "OnSpeed Settings";
        int w = gdraw.textWidth(title);
        gdraw.setCursor((320 - w) / 2, kTitleY);
        gdraw.print(title);
    }
    gdraw.drawFastHLine(40, kDividerTopY, 240, kColorDivider);

    // Items
    gdraw.setFont(FSS12);
    for (int i = 0; i < g_model.itemCount(); ++i) {
        int y = kFirstRowY + i * kRowSpacing;
        const MenuItem& item = g_model.itemAt(i);

        if (i == g_model.currentIndex()) {
            // Cyan band fills the row to the divider edges.
            gdraw.fillRect(20, y - 18, 280, 24, kColorHighlight);
            gdraw.setTextColor(TFT_BLACK);
        } else {
            gdraw.setTextColor(kColorBody);
        }

        gdraw.setCursor(28, y);
        gdraw.print(item.label);

        const char* rhs = nullptr;
        char rhs_buf[32];
        if (item.type == ItemType::Toggle) {
            snprintf(rhs_buf, sizeof(rhs_buf), "[ %s ]",
                     toggleValueLabel(item));
            rhs = rhs_buf;
        } else if (item.type == ItemType::Info && item.getInfoValue) {
            rhs = item.getInfoValue();
        }
        if (rhs) {
            int rhsW = gdraw.textWidth(rhs);
            gdraw.setCursor(312 - rhsW, y);
            gdraw.print(rhs);
        }
    }

    // Footer
    gdraw.drawFastHLine(40, kDividerBotY, 240, kColorDivider);
    gdraw.setTextColor(kColorFooterText);
    gdraw.setFont(FSS12);

    // Glyph-from-primitives helpers (FreeFonts may not carry ▲ ● ▼ ☐ ◀ ▶)
    auto drawTriangleUp   = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx - s, cy + s, cx + s, cy + s, cx, cy - s,
                           kColorFooterText);
    };
    auto drawTriangleDown = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx - s, cy - s, cx + s, cy - s, cx, cy + s,
                           kColorFooterText);
    };
    auto drawTriangleLeft = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx + s, cy - s, cx + s, cy + s, cx - s, cy,
                           kColorFooterText);
    };
    auto drawTriangleRight = [](int cx, int cy, int s) {
        gdraw.fillTriangle(cx - s, cy - s, cx - s, cy + s, cx + s, cy,
                           kColorFooterText);
    };
    auto drawCircle = [](int cx, int cy, int r) {
        gdraw.fillCircle(cx, cy, r, kColorFooterText);
    };
    auto drawSquare = [](int cx, int cy, int s) {
        gdraw.drawRect(cx - s, cy - s, 2 * s + 1, 2 * s + 1, kColorFooterText);
    };

#if defined(HUVVER)
    // ☐ BACK    ◀ UP    ○ SEL    ▶ DOWN
    constexpr int kIconY = kFooterY - 6;
    drawSquare      ( 30, kIconY, 6);
    gdraw.setCursor( 44, kFooterY); gdraw.print("BACK");
    drawTriangleLeft( 110, kIconY, 6);
    gdraw.setCursor(124, kFooterY); gdraw.print("UP");
    drawCircle      (170, kIconY, 6);
    gdraw.setCursor(184, kFooterY); gdraw.print("SEL");
    drawTriangleRight(240, kIconY, 6);
    gdraw.setCursor(254, kFooterY); gdraw.print("DOWN");
#else
    // ▲ UP        ● SEL        ▼ DOWN
    constexpr int kIconY = kFooterY - 6;
    drawTriangleUp  ( 50, kIconY, 6);
    gdraw.setCursor( 64, kFooterY); gdraw.print("UP");
    drawCircle      (148, kIconY, 6);
    gdraw.setCursor(162, kFooterY); gdraw.print("SEL");
    drawTriangleDown(240, kIconY, 6);
    gdraw.setCursor(254, kFooterY); gdraw.print("DOWN");
#endif

    gdraw.pushSprite(0, 0);
    gdraw.deleteSprite();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
//
// Polls per-target buttons for menu navigation. Returns true if any input
// was processed (used to decide whether render is needed this tick — but
// we always render at the renderer cadence anyway).
void pollMenuInput() {
#if defined(HUVVER)
    // huVVer: BtnA = back/exit, BtnB = activate, BtnC = down, BtnD = up
    if (M5.BtnA.wasClicked()) g_model.onBackOrLongPressExit();
    if (M5.BtnB.wasClicked()) {
        auto r = g_model.onActivate();
        if (r == MenuModel::ActivateResult::kToggled) persistSpeedUnits();
    }
    if (M5.BtnC.wasClicked()) g_model.onDown();
    if (M5.BtnD.wasClicked()) g_model.onUp();
#else
    // M5: BtnA = up, BtnB = activate (long-hold = exit), BtnC = down
    if (M5.BtnA.wasClicked()) g_model.onUp();
    if (M5.BtnC.wasClicked()) g_model.onDown();
    if (M5.BtnB.wasHold())    g_model.onBackOrLongPressExit();
    else if (M5.BtnB.wasClicked()) {
        auto r = g_model.onActivate();
        if (r == MenuModel::ActivateResult::kToggled) persistSpeedUnits();
    }
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

void initSettingsMenu() {
#if defined(ESP_PLATFORM)
    preferences.begin("OnSpeed", true);   // read-only
    g_speedInMph = preferences.getBool("SpeedMph", true);   // default MPH
    preferences.end();
#endif
}

void enterSettingsMenu() {
    if (g_active) return;
    g_model.resetForEntry();
    g_lastTickMs = millis();
    g_active = true;
}

void tickSettingsMenu() {
    if (!g_active) return;

    uint32_t now = millis();
    uint32_t elapsed = now - g_lastTickMs;
    g_lastTickMs = now;

    pollMenuInput();
    g_model.tick(elapsed);
    renderMenu();

    if (g_model.wantsExit()) {
        g_active = false;
        // Sprite was already pushed and deleted by renderMenu().
        // Next loop() iteration will fall through to live-mode rendering.
    }
}

bool isSettingsMenuActive() {
    return g_active;
}
```

- [ ] **Step 2: Build for M5 to confirm it compiles**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec
pio run -e m5stack-core-esp32 --project-dir software/OnSpeed-M5-Display
```

Expected: builds with no warnings (zero-warning policy enforced via `-Werror`). If it fails on missing `Free_Fonts.h` symbols (`FSS12`, `FSSB12`), check that `<Free_Fonts.h>` is on the include path — it's a `lib/GaugeWidgets/` header.

- [ ] **Step 3: Build for huVVer to confirm cross-target compile**

```bash
pio run -e huvver-avi --project-dir software/OnSpeed-M5-Display
```

Expected: builds. The HUVVER branch in `pollMenuInput` references `M5.BtnD` which exists on `HuvverShim::M5_Class` (verified in Task 3).

- [ ] **Step 4: Build for native sim**

```bash
pio run -e native --project-dir software/OnSpeed-M5-Display
```

Expected: builds. The native build's `ArduinoShim.h` provides a stub Preferences (verify via `grep -n Preferences software/OnSpeed-M5-Display/sim/ArduinoShim.h`); the `ESP_PLATFORM` guard skips the NVS calls anyway in the sim.

- [ ] **Step 5: Commit**

```bash
git add software/OnSpeed-M5-Display/src/SettingsMenu.cpp
git commit -m "M5 menu: SettingsMenu platform layer (render, NVS, input) (#419)"
```

---

## Task 6: main.cpp — wire the menu into the loop, replace IAS_IN_MPH

**Files:**
- Modify: `software/OnSpeed-M5-Display/src/main.cpp`

The biggest functional change. Reads NVS at boot, migrates `wasPressed()` → `wasClicked()` for live-mode buttons, gates the menu entry, replaces the compile-time IAS units block with a runtime conditional, and deletes the `#define IAS_IN_MPH` block.

Before starting: verify the line numbers below by running `grep -n` against the file. They were captured against current master at the start of this plan but `main.cpp` is large and may shift if you've edited it.

- [ ] **Step 1: Add the SettingsMenu include**

Locate the existing block of includes near the top of `main.cpp` (after the `<cmath>` include around line 43). Add:

```cpp
#include "SettingsMenu.h"
```

After the existing `#include "SerialRead.h"` line (around line 124).

- [ ] **Step 2: Delete the `#define IAS_IN_MPH` block**

Find the block (around lines 48-52, but verify):

```cpp
// IAS_IN_MPH gates the IAS units readout (MPH default, knots if
// undefined).  Tracked for runtime promotion in issue #419 — once that
// lands, the `#define` goes away and pilots flip units at runtime.
// Until then, comment out to rebuild with knots.
#define IAS_IN_MPH
```

Delete the entire block (5 lines: the four comment lines plus the `#define`).

- [ ] **Step 3: Replace the runtime IAS unit conditional**

Find the `#ifdef IAS_IN_MPH` block (around lines 603-607, but verify with `grep -n IAS_IN_MPH`):

```cpp
#ifdef IAS_IN_MPH
            displayIAS         = IAS * 1.15078;
#else
            displayIAS         = IAS;
#endif
```

Replace with:

```cpp
            displayIAS         = g_speedInMph ? IAS * 1.15078f : IAS;
```

(Note the `f` suffix on the literal — current code uses an implicit `double` which the compiler should silently convert. The new code is explicit. Functionally identical.)

- [ ] **Step 4: Verify there are no other `IAS_IN_MPH` references**

```bash
grep -n IAS_IN_MPH software/OnSpeed-M5-Display/src/main.cpp
```

Expected: no output. If there's a stale comment elsewhere, delete it.

- [ ] **Step 5: Add initSettingsMenu() call to setup()**

Find the existing brightness/displayType read block in `setup()` (around lines 477-482, inside an `#if defined(ESP_PLATFORM)` block that closes on line 484):

```cpp
    preferences.begin("OnSpeed", true);  // read-only
    displayBrightness = preferences.getUShort("Brightness", 255);
    displayType       = preferences.getShort("DisplayType", 0);
    preferences.end();
    displayBrightness = constrain(displayBrightness, 1, 255);
    displayType       = constrain(displayType, 0, 4);
#endif
```

Insert the new call **before** the `#endif` so it sits inside the `ESP_PLATFORM` block alongside the other NVS reads:

```cpp
    preferences.begin("OnSpeed", true);  // read-only
    displayBrightness = preferences.getUShort("Brightness", 255);
    displayType       = preferences.getShort("DisplayType", 0);
    preferences.end();
    displayBrightness = constrain(displayBrightness, 1, 255);
    displayType       = constrain(displayType, 0, 4);

    initSettingsMenu();   // reads SpeedMph from the same NVS namespace
#endif
```

(`initSettingsMenu` opens its own `preferences.begin("OnSpeed", true)` block; the previous block is closed by the `preferences.end()` above, so there's no namespace-handle overlap.)

- [ ] **Step 6: Migrate live-mode `wasPressed()` to `wasClicked()` and gate the menu**

Find the live-mode button block (around lines 533-558). Current code:

```cpp
    const uint16_t prevBrightness = displayBrightness;
    if (M5.BtnC.wasPressed()) displayBrightness *= 2;  // brightness up
    if (M5.BtnA.wasPressed()) displayBrightness /= 2;  // brightness down

    displayBrightness = constrain(displayBrightness, 1, 255);

    M5.Display.setBrightness(displayBrightness);

#if defined(ESP_PLATFORM)
    if (displayBrightness != prevBrightness)
    {
        preferences.begin("OnSpeed", false);
        preferences.putUShort("Brightness", displayBrightness);
        preferences.end();
    }
#else
    (void)prevBrightness;
#endif

    if (M5.BtnB.wasPressed())
    {
        gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
        gdraw.createSprite(WIDTH, HEIGHT);
        gdraw.fillSprite (TFT_BLACK);
        displayType ++;
        if (displayType > 4) displayType = 0; // type of display
```

Replace the entire block (down to and including the `if (displayType > 4) displayType = 0;` line) with:

```cpp
    // ---- Settings menu gate (firmware-only path) ----
    // The X-Plane plugin builds main.cpp without M5.begin() and without
    // M5.update(); button state is uninitialized there. Gate the menu
    // behind the same XPLANE_PLUGIN_BUILD guard that the M5.update() call
    // above uses. While the menu is up it owns the screen and consumes
    // button events; skip live-mode handling and SerialRead() entirely
    // for this iteration.
#ifndef XPLANE_PLUGIN_BUILD
    if (isSettingsMenuActive())
    {
        tickSettingsMenu();
        return;
    }

    // ---- Menu entry gesture ----
    // M5: long-hold BtnB. huVVer: long-hold BtnA (☐ Menu key).
    // wasHold() fires once at the moment the press passes ~600ms.
#if defined(HUVVER)
    if (M5.BtnA.wasHold()) { enterSettingsMenu(); return; }
#else
    if (M5.BtnB.wasHold()) { enterSettingsMenu(); return; }
#endif
#endif // XPLANE_PLUGIN_BUILD

    // ---- Live-mode button handlers ----
    // wasClicked() fires on release-after-short-press only; suppressed
    // automatically when the press crossed the hold threshold (so the
    // menu-entry hold above doesn't *also* cycle the mode here).
    const uint16_t prevBrightness = displayBrightness;
#if defined(HUVVER)
    // huVVer: ▶ (BtnC) = up, ◀ (BtnD) = down — mirrors V.R. Little's TBX.
    if (M5.BtnC.wasClicked()) displayBrightness *= 2;
    if (M5.BtnD.wasClicked()) displayBrightness /= 2;
#else
    // M5 Basic / Core2: BtnC = up, BtnA = down (unchanged from prior code).
    if (M5.BtnC.wasClicked()) displayBrightness *= 2;
    if (M5.BtnA.wasClicked()) displayBrightness /= 2;
#endif

    displayBrightness = constrain(displayBrightness, 1, 255);

    M5.Display.setBrightness(displayBrightness);

#if defined(ESP_PLATFORM)
    if (displayBrightness != prevBrightness)
    {
        preferences.begin("OnSpeed", false);
        preferences.putUShort("Brightness", displayBrightness);
        preferences.end();
    }
#else
    (void)prevBrightness;
#endif

    if (M5.BtnB.wasClicked())
    {
        gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
        gdraw.createSprite(WIDTH, HEIGHT);
        gdraw.fillSprite (TFT_BLACK);
        displayType ++;
        if (displayType > 4) displayType = 0; // type of display
```

(Everything from `if (displayType > 4)...` onwards stays exactly as it was — including the existing `preferences.putShort("DisplayType",...)` block.)

- [ ] **Step 7: Build for M5**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec
pio run -e m5stack-core-esp32 --project-dir software/OnSpeed-M5-Display
```

Expected: builds with zero warnings. The build flow auto-runs `generate_buildinfo.py`.

- [ ] **Step 8: Build for huVVer**

```bash
pio run -e huvver-avi --project-dir software/OnSpeed-M5-Display
```

Expected: builds. Touches the `#if defined(HUVVER)` branches — verifies the BtnD reference and the BtnA-as-menu-key mapping compile.

- [ ] **Step 9: Build for native sim**

```bash
pio run -e native --project-dir software/OnSpeed-M5-Display
```

Expected: builds. The sim's `Preferences` shim makes `initSettingsMenu()` a no-op (the `ESP_PLATFORM` guard inside `initSettingsMenu` skips the body), so `g_speedInMph` retains its `true` initializer.

- [ ] **Step 10: Run MenuModel unit tests one more time as a sanity check**

```bash
pio test -e m5-native-test --project-dir software/OnSpeed-M5-Display
```

Expected: 8/8 still pass. (Should be unchanged — Task 6 didn't touch MenuModel — but worth confirming we haven't broken the test env's build_src_filter or lib resolution.)

- [ ] **Step 11: Commit**

```bash
git add software/OnSpeed-M5-Display/src/main.cpp
git commit -m "M5 menu: wire menu into loop, runtime KTS/MPH toggle (#419)"
```

---

## Task 7: Native SDL sim end-to-end smoke test

**Files:** none modified — this task verifies behavior in the desktop simulator.

The SDL native env runs the full M5 source against an SDL2-emulated 320×240 panel. Keyboard maps Left/Down/Right → BtnA/BtnB/BtnC. We use this to confirm the menu actually renders and the toggle round-trips through the IAS readout.

- [ ] **Step 1: Build and run the sim**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec
pio run -e native --project-dir software/OnSpeed-M5-Display
./software/OnSpeed-M5-Display/.pio/build/native/program
```

Expected: SDL window opens showing the OnSpeed splash → live indexer with synthetic AOA ramp (the native build sets `-DDUMMY_SERIAL_DATA`).

- [ ] **Step 2: Enter the menu via long-press**

Hold the **Down arrow key** for ≥1 second (kHoldMs is 600ms but give it a generous press). Expected: live render is replaced by the OnSpeed Settings screen showing two rows — `Speed Units    [ MPH ]` (highlighted in cyan) and `Exit`. Footer shows `▲ UP   ● SEL   ▼ DOWN`.

If you see a one-tick mode-cycle flash before the menu appears, the `wasClicked()` migration didn't take — go back to Task 6 Step 6 and verify the live-mode handlers are using `wasClicked()`, not `wasPressed()`.

- [ ] **Step 3: Toggle the units**

Press Down (BtnB) once. Expected: the toggle flips to `[ KTS ]` immediately, no banner, cursor stays on Speed Units.

- [ ] **Step 4: Navigate to Exit**

Press Right (BtnC) once. Expected: highlight moves to the Exit row.

- [ ] **Step 5: Exit via the Exit item**

Press Down (BtnB) once. Expected: menu disappears, live indexer renders again. The IAS readout should now display the value as KTS (the synthetic ramp is small numbers — confirm the IAS digit is *not* multiplied by 1.15).

- [ ] **Step 6: Re-enter and exit via long-press**

Hold Down arrow for ≥1 second. Menu opens. Hold Down again for ≥1 second. Menu should close (long-press BtnB triggers `onBackOrLongPressExit`). Confirm the IAS units state persisted from Step 3 — still KTS.

- [ ] **Step 7: Test the idle timeout**

Re-enter the menu. Don't press any keys. After 30 seconds the menu should auto-exit back to the live indexer.

If any of these steps fail, the implementation has a bug — debug before proceeding to Task 8. Common failure modes:
- Menu doesn't enter: `M5.BtnB.wasHold()` not firing on the SDL panel. Check `M5Unified`'s SDL panel implementation handles the long-press path; if not, the test plan needs to be revised to use a different key or test on hardware.
- Menu enters but immediately closes: `wasHold()` AND `wasClicked()` both firing — the suppression logic in HuvverShim is wrong, OR M5Unified's behavior diverges from spec.
- Toggle works but doesn't persist: NVS write isn't happening (check `pollMenuInput` — `r == kToggled` should call `persistSpeedUnits()`).

- [ ] **Step 8: Commit any fixes from this task**

If you didn't need to make changes (sim worked first try), skip the commit. Otherwise:

```bash
git add -A
git commit -m "M5 menu: sim fixes from end-to-end smoke (#419)"
```

---

## Task 8: Documentation

**Files:**
- Modify: `software/OnSpeed-M5-Display/CLAUDE.md`
- Modify: `docs/site/docs/installation/external-display.md`

- [ ] **Step 1: Update the M5 sub-project CLAUDE.md**

Open `software/OnSpeed-M5-Display/CLAUDE.md`. Find the existing "Buttons" section for huVVer (search for "BtnD (the 4th huVVer button) is unused"). Update that context:
- Note that BtnD now halves brightness on huVVer.
- Note that BtnA on huVVer is now the dedicated Menu key (long-press to enter the settings menu).

After the existing "## Bench testing" section at the end of the file, append a new section:

```markdown
## Settings menu

The display has an in-flight settings menu reachable from live mode.
First setting is `Speed Units: KTS / MPH`, replacing the compile-time
`IAS_IN_MPH` flag (issue #419).

**Entry gesture:**
- M5 Basic / Core2: hold BtnB (round/middle) for ≥600 ms.
- huVVer-AVI: hold BtnA (☐ Menu, square top-left) for ≥600 ms.

**Inside the menu:**
- M5: BtnA = up, BtnC = down, BtnB = activate (or hold ≥600 ms to exit).
- huVVer: ◀ (BtnD) = up, ▶ (BtnC) = down, ○ (BtnB) = activate, ☐ (BtnA) = back/exit.
- 30-second idle timeout exits automatically.

**Adding a setting:** extend `g_items[]` in `src/SettingsMenu.cpp`. For
toggles, add a `bool` global, point the item at it, and add a callback
to persist it. The pure-logic state machine in `lib/MenuModel/` is
unchanged — it dispatches based on `ItemType`.

**Persistence:** `Preferences` namespace `"OnSpeed"`, key `SpeedMph`.
Default `true` (MPH) on a fresh device, matching the previous
build-time default.

**Tests:** `pio test -e m5-native-test --project-dir software/OnSpeed-M5-Display`.
```

- [ ] **Step 2: Update the installation docs site**

Open `docs/site/docs/installation/external-display.md`. Find the section that currently describes the button mappings (probably has "BtnA / BtnB / BtnC" or "left / middle / right" wording). Add a new subsection:

```markdown
## Settings menu

The display has a built-in settings menu for runtime preferences (units,
brightness defaults, etc.). Currently one setting: speed units (KTS or
MPH).

### M5 Basic / Core2

Hold the **middle button** for about a second during normal flight to
open the menu. Use the **left** and **right** buttons to navigate, the
**middle button** to change a value, and select **Exit** to return to
flight mode. The menu closes automatically after 30 seconds of no input.

### huVVer-AVI

Hold the **square (☐) Menu button** for about a second to open the menu.
Use **◀** and **▶** to navigate, the **round (○) Select button** to
change a value, and the **square button** again to return to flight
mode. The menu closes automatically after 30 seconds of no input.

### Speed units

Selecting **Speed Units** flips the IAS readout between knots (KTS) and
miles per hour (MPH). The change takes effect immediately and persists
across power cycles.
```

- [ ] **Step 3: Commit**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec
git add software/OnSpeed-M5-Display/CLAUDE.md docs/site/docs/installation/external-display.md
git commit -m "docs: M5 settings menu (#419)"
```

---

## Task 9: PR prep

**Files:** none modified — this task validates the branch is ready to push.

- [ ] **Step 1: Run the full test matrix one more time**

```bash
cd /Users/sritchie/code/onspeed-worktrees/m5-settings-menu-spec

# Unit tests
pio test -e m5-native-test --project-dir software/OnSpeed-M5-Display

# Builds
pio run -e m5stack-core-esp32 --project-dir software/OnSpeed-M5-Display
pio run -e m5stack-core2     --project-dir software/OnSpeed-M5-Display
pio run -e huvver-avi        --project-dir software/OnSpeed-M5-Display
pio run -e native            --project-dir software/OnSpeed-M5-Display
```

Expected: 8/8 tests pass; all 4 builds succeed with zero warnings.

- [ ] **Step 2: Confirm the diff is what you expect**

```bash
git log --oneline origin/master..HEAD
```

Expected: a sequence of commits matching Tasks 1-8. (Plus the two earlier spec commits that started this branch.)

```bash
git diff --stat origin/master
```

Expected (approximate):
- `docs/site/docs/installation/external-display.md` — modified (+~25 lines)
- `docs/superpowers/plans/2026-05-09-m5-settings-menu.md` — added (this file)
- `docs/superpowers/specs/2026-05-08-m5-settings-menu-design.md` — added
- `software/OnSpeed-M5-Display/CLAUDE.md` — modified (+~30 lines)
- `software/OnSpeed-M5-Display/include/SettingsMenu.h` — added
- `software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.cpp` — added
- `software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.h` — added
- `software/OnSpeed-M5-Display/lib/MenuModel/library.json` — added
- `software/OnSpeed-M5-Display/platformio.ini` — modified (+~15 lines for the test env)
- `software/OnSpeed-M5-Display/sim/HuvverShim.h` — modified (+~30 lines)
- `software/OnSpeed-M5-Display/src/SettingsMenu.cpp` — added
- `software/OnSpeed-M5-Display/src/main.cpp` — modified (+~25 lines, -7 lines for IAS_IN_MPH delete)
- `software/OnSpeed-M5-Display/test/test_menu_model/test_menu_model.cpp` — added

- [ ] **Step 3: Push the branch**

```bash
git push -u origin feature/m5-settings-menu-spec
```

- [ ] **Step 4: Open the PR**

Per project convention (the parent dir contains untracked files, so `gh pr create` needs `--head`):

```bash
gh pr create --head feature/m5-settings-menu-spec \
  --title "M5 menu: runtime KTS/MPH toggle, replaces IAS_IN_MPH (#419)" \
  --body "$(cat <<'EOF'
## M5 settings menu — runtime KTS/MPH toggle (#419)

Promotes `IAS_IN_MPH` from a compile-time `#define` to a per-display NVS
preference, accessed through a small in-flight settings menu. The menu
is list-driven — adding a new setting in the future is a one-line entry
in `g_items[]` plus a persist callback.

### Changes

- `lib/MenuModel/` — pure-logic 3/4-button menu state machine, peer to
  `lib/GaugeWidgets/`. Unit-tested via the new `[env:m5-native-test]`.
- `SettingsMenu.{h,cpp}` — M5GFX render + NVS read/write + per-target
  button polling. M5: BtnB long-hold to enter, BtnA/BtnC navigate, BtnB
  activate. huVVer: V.R. Little's TBX vocabulary — BtnA = ☐ Menu,
  BtnB = ○ Select, ◀ ▶ navigate, BtnD promoted from "unused" to
  brightness-halve.
- `main.cpp` — wires the menu into `loop()`, migrates live-mode buttons
  from `wasPressed()` to `wasClicked()` (so the long-press-to-menu hold
  doesn't double-trigger as a press-edge action), replaces the
  `#ifdef IAS_IN_MPH` block with `g_speedInMph ? IAS * 1.15078f : IAS`,
  deletes the `#define`.
- `HuvverShim.h` — adds `wasClicked()` and `wasHold()` to `HuvverButton`
  matching M5Unified's edge semantics.

### Testing

```bash
pio test -e m5-native-test --project-dir software/OnSpeed-M5-Display
pio run -e m5stack-core-esp32 --project-dir software/OnSpeed-M5-Display
pio run -e m5stack-core2     --project-dir software/OnSpeed-M5-Display
pio run -e huvver-avi        --project-dir software/OnSpeed-M5-Display
pio run -e native            --project-dir software/OnSpeed-M5-Display
```

Native sim verified end-to-end (enter via Down-arrow long-press, toggle
via short Down, exit via Down-arrow long-press or 30s timeout). Hardware
bench test on M5 Basic + huVVer-AVI follows.

Closes #419.
EOF
)"
```

Expected: PR opens at `https://github.com/flyonspeed/OnSpeed-Gen3/pull/<n>`. Return that URL.

---

## Self-Review Notes

This plan implements the spec at `docs/superpowers/specs/2026-05-08-m5-settings-menu-design.md` end-to-end:

| Spec section | Tasks |
|---|---|
| Goal (delete `#define IAS_IN_MPH`, runtime toggle) | T6 |
| Button mapping (M5 + huVVer) | T6 (live mode), T5 (in-menu) |
| `wasClicked()` migration | T6 |
| Item types (Toggle, Action, Info) | T1 (struct + dispatch), T2 (test coverage) |
| Visual layout (title, items, footer, no banner) | T5 |
| Persistence (`SpeedMph` key, default true) | T5 (read), T5 (write), T6 (init) |
| `MenuModel` in `lib/MenuModel/`, NOT `onspeed_core` | T1 |
| `SettingsMenu.h/cpp` thin platform layer | T4, T5 |
| huVVer `wasClicked` / `wasHold` shim | T3 |
| Native sim compat | T7 |
| X-Plane plugin compat (M5 buttons not touched on plugin path) | already gated by existing `#ifndef XPLANE_PLUGIN_BUILD` around `M5.update()` |
| Unit tests (6 cases from spec) | T2 (covers all 6 + 2 extra: input-resets-idle and reset-for-entry) |
| Sim integration test | T7 |
| CLAUDE.md + external-display.md docs | T8 |
| PR prep | T9 |

The plan does **not** include CI integration (the spec said "follows in a side change once this PR's tests are stable locally") — `[env:m5-native-test]` exists and works locally; wiring it to GitHub Actions is intentionally deferred.

The plan does **not** include the boot-time chord behavior or any new boot menu — out of scope per spec.

Hardware bench testing is described in the spec but not as a plan task because it requires physical hardware. The PR description (T9) signals it's a follow-up before merge.
