# M5 settings menu — design

Issue: [#419](https://github.com/flyonspeed/OnSpeed-Gen3/issues/419) — M5 display: promote `IAS_IN_MPH` from build flag to runtime preference.

## Problem

`software/OnSpeed-M5-Display/src/main.cpp` picks knots vs MPH for the IAS readout via a compile-time `#define IAS_IN_MPH` (line 47). Pilots flying experimental aircraft in the US frequently want MPH; pilots elsewhere want knots. With the current setup they have to build their own firmware to flip it. That's a runtime preference, not a build choice.

There is no in-flight settings UI on the M5 today. The `#define` is the only "configuration" the firmware exposes outside of brightness (BtnA / BtnC, persisted) and the firmware-update WiFi AP gated on BtnB-held-at-boot.

## Goal

Add a small, in-flight settings menu reachable from live-mode operation. Persist the speed-units choice to NVS so it survives power-cycles. Build the menu as a list-driven framework so future settings (brightness default, mode-skip, etc.) are one-line additions, but **the only setting shipping in this PR is `Speed Units: KTS / MPH`**.

Once this lands, the `#define IAS_IN_MPH` block (lines 43–47) and the `#ifdef IAS_IN_MPH` gate (lines 540–544) are deleted.

## Prior art

Two anchors, both from this hardware ecosystem:

**huVVer-AVI TBX firmware** ([V5A manual](https://www.huvver.tech/wp-content/uploads/huVVer-AVI-TBX-V5A.pdf)) — V.R. Little's stock app, runs on the same physical hardware some OnSpeed users have (we already build for `huvver-avi` from this codebase). Its menu pattern: hold the right-triangle MODE button for 2 s to enter System Setup; inside, square = back, round = select, triangles = navigate. Boot-time chord (Menu+Select held at power-up) = factory reset. We already document and rely on this same boot-chord in `OnSpeed-M5-Display/CLAUDE.md`.

**M5ez** ([github.com/M5ez/M5ez](https://github.com/M5ez/M5ez)) — canonical M5Stack 3-button menu library. Default mapping: `BtnA = up, BtnB = select, BtnC = down`. Symmetric, easy to internalize.

We adopt M5ez's symmetric A/B/C nav (matches the existing brightness-button symmetry) and the huVVer "long-press to enter" entry gesture.

## Button mapping

Outside the menu (live mode) — **unchanged**:

| Button | Short press | Long press (≥ 600 ms) |
|---|---|---|
| BtnA | brightness halve | (free) |
| BtnB | mode cycle 0 → 4 → 0 | **enter Settings menu** |
| BtnC | brightness double | (free) |
| BtnB at boot | (firmware update — unchanged) | n/a |

**Event semantics for mode-cycle vs hold-to-enter.** The current firmware uses `BtnB.wasPressed()` for the mode-cycle (`main.cpp:552`), which fires on the **press edge** — meaning a long-press would trigger one mode-cycle *and* the menu, 600 ms apart. To avoid this collision, this PR migrates the mode-cycle from `wasPressed()` to `wasClicked()`. M5Unified's `wasClicked()` fires on **release after a short press** and is guaranteed not to fire on a release that came after a hold (the internal `_press` state is set to 2 once held, and the click branch only triggers when `oldPress == 1`). So:

- Short click of BtnB → release fires `wasClicked()` → mode cycles.
- Long press of BtnB → at 600 ms held, `wasHold()` fires once → menu opens. Subsequent release fires neither `wasClicked()` nor anything else.

For consistency, BtnA and BtnC migrate from `wasPressed()` to `wasClicked()` too. They have no long-press behavior today, so the migration is behavior-neutral now and protects against future long-press-on-A/C semantics. The huVVer `HuvverButton` class needs matching `wasClicked()` and `wasHold()` methods with the same suppress-release-after-hold semantics (implementation in §"Cross-target compatibility").

Inside the menu — **M5ez-style** with dual-purpose BtnB:

| Button | Short press | Long press (≥ 600 ms) |
|---|---|---|
| BtnA | move highlight up | (no-op) |
| BtnB | activate highlighted item | **save & exit menu** |
| BtnC | move highlight down | (no-op) |

Wraparound: up from item 0 wraps to last item; down from last wraps to 0.

**Exit semantics** — three ways out, all safe:
1. Activate the explicit `Exit` menu item.
2. Long-press BtnB anywhere in the menu (mirrors the entry gesture).
3. 30-second idle timeout — any frame where `millis() - lastButtonMs > 30000` while in the menu auto-exits to live mode.

## Item types

Three item kinds, each rendered as a single row in the list:

```
+----------------------------------------+
|   Speed Units            [ KTS ]       |   <-- ToggleItem
| > Exit                                 |   <-- ActionItem (highlighted)
+----------------------------------------+
```

- **ToggleItem** — two values (e.g. `KTS / MPH`). BtnB-activate flips the value, persists immediately to NVS, stays on the menu. The new value is rendered live in the right-hand `[ ]` slot so the pilot sees the change instantly. No "edit mode," no confirmation dialog.
- **ActionItem** — BtnB-activate runs a callback. Used for `Exit` today; `Restart` and `Factory Reset` are obvious follow-ups.
- **InfoItem** — read-only readout (version string, IP address, etc.). BtnB-activate does nothing. Not used in the v1 menu but defined so the framework is complete.

A future `IntItem` (with a sub-edit mode where BtnB enters edit, BtnA/BtnC change value, BtnB exits edit) is intentionally **not** implemented in this PR. Scope discipline: we don't have a setting that needs it yet.

## Visual layout

Avionics style — dark background, bright text, simple list. Reuses the FSS12 / FSSB12 fonts already loaded by `main.cpp`.

```
+----------------------------------------+    <- 320×240, M5 Basic / Core2
|                                        |
|           OnSpeed Settings             |    title, FSSB12, white,
|         ──────────────────             |    centered y=20, divider y=38
|                                        |
|   Speed Units            [ KTS ]       |    items, FSS12, y=70 then +30/row
|                                        |
| > Exit                                 |    highlighted: "> " prefix +
|                                        |    cyan background band on the row
|                                        |
|                                        |
|         ────────────────              |    divider y=200
|     ▲           ●           ▼          |    button hints, FSS12 grey, y=220
|    UP        SELECT       DOWN         |
+----------------------------------------+
```

- **Title bar:** `"OnSpeed Settings"` in `FSSB12` white, centered, `y=20`. Thin grey divider underneath at `y=38`.
- **Item rows:** label left-anchored at `x=20`, current value right-anchored at `x=300`, `FSS12`. Highlighted item renders with a `>` prefix (visible without color) plus a thin cyan background band (color reinforcement for sighted-color pilots, redundancy for sunlit cockpits). 30-pixel row spacing fits up to 4 rows above the footer.
- **Footer:** button hints `▲ UP   ● SELECT   ▼ DOWN`. Triangles and circle drawn from primitives (`gdraw.fillTriangle`, `gdraw.fillCircle`) — Free_Fonts may not carry those Unicode glyphs, and we don't want to hunt for fallbacks. Footer divider at `y=200`, hint text at `y=220`.
- **Save flash:** on long-press-exit, a brief `Saving...` banner shows for ~300 ms before the menu deletes its sprite. (Toggle-flips already persist immediately, so this banner is symbolic, not load-bearing.)
- **huVVer (240×320 portrait):** same layout, just rotated. List menus tolerate aspect-ratio change well; row spacing stays at 30 px and the footer drops to the bottom of the rotated frame.

## Persistence

Reuse the existing `"OnSpeed"` NVS namespace, opened today by `setup()` (line 426) and `serialSetup()` (in `SerialRead.cpp`).

New key: `SpeedMph` (boolean, default `false` = knots).

```cpp
// At setup(), after the existing Brightness read:
preferences.begin("OnSpeed", true);  // read-only
g_speedInMph = preferences.getBool("SpeedMph", false);
preferences.end();

// On toggle flip (inside SettingsMenu callback):
preferences.begin("OnSpeed", false);
preferences.putBool("SpeedMph", g_speedInMph);
preferences.end();
```

Same pattern as the brightness-write at `main.cpp:489`. M5Unified's NVS handles wear-leveling; toggle cadence is far below rewrite limits.

The IAS-render block changes from:

```cpp
#ifdef IAS_IN_MPH
    displayIAS = IAS * 1.15078f;
#else
    displayIAS = IAS;
#endif
```

to:

```cpp
displayIAS = g_speedInMph ? IAS * 1.15078f : IAS;
```

`displayIAS` is consumed in 3 render paths (Mode 0 line 648, Mode 2 line 842, Mode 3 line 1509), all reading from the same global, so the single conditional-assignment site at the numbers-update block (line 538) covers all three.

## Code organization

Three new files split by platform-dependence (so the pure-logic core is unit-testable from the main firmware's native env):

- `software/Libraries/onspeed_core/menu/MenuModel.h/.cpp` — pure-logic state machine, no M5GFX or Arduino deps. Already established `onspeed_core` pattern for code shared between firmware and the M5 sub-project. Public surface:
  ```cpp
  enum class ItemType { Toggle, Action, Info };
  struct MenuItem {
      const char* label;
      ItemType    type;
      // For Toggle: bool* value, const char* labelOff, const char* labelOn
      // For Action: void (*onActivate)()
      // For Info:   const char* (*getValue)()
  };
  class MenuModel {
  public:
      MenuModel(MenuItem* items, int count);
      void onUp();
      void onDown();
      enum class ActivateResult { kStayed, kToggled, kExit, kAction };
      ActivateResult onActivate();
      void tick(uint32_t elapsedMs);          // for idle timeout
      void onLongPressExit();                 // called by long-press shortcut
      bool wantsExit() const;
      int  currentIndex() const;
  };
  ```
- `software/OnSpeed-M5-Display/include/SettingsMenu.h` — thin platform layer:
  ```cpp
  void enterSettingsMenu();              // call when BtnB long-press detected
  void tickSettingsMenu();               // call from loop() each iteration when active
  bool isSettingsMenuActive();
  extern bool g_speedInMph;              // read by main.cpp's IAS-render path
  ```
- `software/OnSpeed-M5-Display/src/SettingsMenu.cpp` — wires `MenuModel` to M5GFX render + NVS read/write + button polling. The static `MenuItem[]` array and the toggle-flip / exit callbacks live here so they have access to `g_speedInMph` and `preferences`.

`main.cpp` changes:

- New include `"SettingsMenu.h"`.
- Inside `setup()`, after the brightness read, add the `g_speedInMph` read.
- Migrate the three live-mode button handlers from `wasPressed()` to `wasClicked()`:
  - `main.cpp:534` `BtnC.wasPressed() → wasClicked()` (brightness up)
  - `main.cpp:535` `BtnA.wasPressed() → wasClicked()` (brightness down)
  - `main.cpp:552` `BtnB.wasPressed() → wasClicked()` (mode cycle)
  - The boot-time `BtnB.isPressed()` (line 348) and the firmware-update-cancel `BtnC.wasPressed()` (line 510) stay as they are — both run before the menu exists.
- Inside `loop()`, near the top (after `M5.update()` and before `SerialRead()`):
  ```cpp
  if (isSettingsMenuActive()) { tickSettingsMenu(); return; }
  if (M5.BtnB.wasHold()) { enterSettingsMenu(); return; }
  ```
  Short-circuits the brightness handling and the mode-cycle while the menu is up. Live serial-data reads pause for the duration of the menu (the pilot is on the ground / parked anyway when configuring; the few seconds without IAS readout are acceptable).
- Delete the `#ifdef IAS_IN_MPH` gate; replace with `g_speedInMph ? IAS * 1.15078f : IAS`.

## Cross-target compatibility

**M5Unified (M5 Basic / Core2):** native support for `wasHold()` — no shim needed.

**huVVer-AVI:** add `wasHold()` and `wasClicked()` methods to `HuvverButton` in `sim/HuvverShim.h`. `wasHold()` fires true exactly once per gesture, the first poll where `pressedFor(600)` is true; track a `holdFired_` flag cleared on release. `wasClicked()` fires true exactly once on release **only if the gesture never crossed the hold threshold**; track an `everHeld_` flag set when the press passes 600 ms, and gate the click-on-release on `everHeld_ == false`. Matches `M5Unified::Button_Class` semantics so the call sites in `main.cpp` are target-agnostic.

**Native sim (SDL2):** Panel_sdl maps Left / Down / Right keys to BtnA / BtnB / BtnC. Holding the Down arrow key produces a continuous press, so `wasHold()` works in the sim. The shimmed `Preferences` does not persist across sim runs, but the menu is fully testable end-to-end.

**X-Plane plugin:** the plugin builds `main.cpp` with `XPLANE_PLUGIN_BUILD` and never calls `M5.begin()` / `M5.update()`. It also has no NVS. Two requirements:
1. Gate the `enterSettingsMenu()` call site behind `#ifndef XPLANE_PLUGIN_BUILD` so the plugin never inspects an uninitialized button.
2. `g_speedInMph` defaults to `false` (knots) from its runtime initializer, which is what the plugin needs — no special case required.

## Testing

**Unit tests** for the pure-logic `MenuModel` live in the main firmware's existing native-test tree at `OnSpeed-Gen3/test/test_menu_model/`, alongside the 12 existing test suites. `MenuModel` has no M5GFX dependency, so it links cleanly against the main firmware's `[env:native]` toolchain. To make `MenuModel` reachable from both targets, it lives at `software/Libraries/onspeed_core/menu/MenuModel.{h,cpp}` (header-only-friendly, no platform deps), and `OnSpeed-M5-Display/src/SettingsMenu.cpp` includes it. The platform-specific render and NVS code stay in the M5 sub-project where they belong.

Test coverage:

- Cursor wraparound: up from index 0 lands at last index; down from last lands at 0.
- Toggle activation flips the toggle value.
- Exit-action activation sets `wantsExit() == true`.
- Idle timeout: `tick(31'000)` after no input flips `wantsExit()`.
- Long-press-during-menu sets `wantsExit()` immediately regardless of cursor position.
- Activating an `InfoItem` is a no-op (cursor stays, no exit, no toggle change).

The M5 sub-project itself has no test tree today and gains none in this PR; its native SDL build serves as the integration-test surface (next paragraph).

**Sim integration:** native SDL build runs end-to-end. Boot, hold the Down arrow >600 ms to enter, cycle items with Left / Right, flip the toggle with Down (short), long-press Down to exit. Confirm the IAS readout switches between knots and MPH digits live.

**Hardware:** M5 Basic + huVVer-AVI bench test using the existing `tools/m5-replay/replay.py` to stream synthetic frames. Verify (a) the gesture works without conflicting with brightness or mode-cycle, (b) the toggle persists across power-cycle, (c) the idle timeout returns the pilot to live mode.

## Documentation

- `software/OnSpeed-M5-Display/CLAUDE.md`:
  - Add a "Settings menu" section describing the long-press-BtnB gesture, the current items list, and the recipe for adding a new item (extend the `MenuItem` array in `SettingsMenu.cpp`).
- `docs/site/docs/installation/external-display.md`:
  - User-facing description: "Hold the middle button for ~1 second during normal flight to access settings. Use the left and right buttons to navigate, the middle button to change a value, and select 'Exit' to return to flight mode."

## Out of scope

- **`IntItem` with sub-edit mode.** No setting needs it yet; framework leaves a clean place for it.
- **Hold-to-repeat scrolling** on BtnA / BtnC inside the menu. With ≤ 5 items planned, single-press scrolling is fine.
- **Mode-skip setting.** Some pilots may want to disable Mode 4 (G-history) from the cycle; that's a future menu item, not this PR.
- **Touch input on Core2.** Core2 has a capacitive touch panel that the firmware already ignores everywhere else. Keeping that pattern.
- **Vendoring M5ez or M5StackSAM.** The 2-item menu we need is ~150 LOC. M5ez is 6,000 LOC and pulls in WiFi / WebServer machinery the M5 firmware doesn't want in its bundle.
- **Removing `REPEATER_MODE` / `VAC_MODE`.** Those were already deleted before this work. The only build flag this PR retires is `IAS_IN_MPH`.
- **Brightness NVS persistence.** Issue #419's "Related" section mentioned a separate item for this — it's already done (brightness writes through to the `"OnSpeed"` namespace at `main.cpp:489`). This PR adds `SpeedMph` alongside the existing `Brightness` key; no other persistence work needed.

## Open questions

None — design is locked. Implementation plan to follow via writing-plans.
