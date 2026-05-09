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

- **On M5** (3 buttons, no dedicated Menu key): we adopt M5ez's symmetric A/B/C nav, and use long-press BtnB to enter the menu — borrowed from huVVer-TBX's "hold MODE for 2s" idiom.
- **On huVVer-AVI** (4 buttons): we adopt V.R. Little's TBX vocabulary directly — square = back/menu, round = select, ◀ ▶ = navigate. Long-press the dedicated Menu (☐) key to enter.

The shared menu model behaves identically on both targets; only the entry / exit gesture and live-mode bindings differ by what hardware affords.

## Button mapping

Three M5-Basic / Core2 buttons vs four huVVer-AVI buttons; the menu uses the same model on both, but the entry / exit gesture and live-mode bindings differ by what each hardware affords.

### M5 Basic / Core2 (BtnA, BtnB, BtnC)

Outside the menu (live mode) — **unchanged**:

| Button | Short press | Long press (≥ 600 ms) |
|---|---|---|
| BtnA | brightness halve | (free) |
| BtnB | mode cycle 0 → 4 → 0 | **enter Settings menu** |
| BtnC | brightness double | (free) |
| BtnB at boot | (firmware update — unchanged) | n/a |

Inside the menu:

| Button | Short press | Long press (≥ 600 ms) |
|---|---|---|
| BtnA | move highlight up | (no-op) |
| BtnB | activate highlighted item | **save & exit menu** |
| BtnC | move highlight down | (no-op) |

The M5 has no dedicated Menu key, so the entry / exit gesture is BtnB long-press. Symmetric with M5ez convention.

### huVVer-AVI (BtnA = ☐ Menu, BtnB = ○ Select, BtnC = ▶ Fwd, BtnD = ◀ Back)

Vern Little's TBX firmware ships on this exact hardware and defines a stable button vocabulary that some OnSpeed users already have in muscle memory: **square = back/menu, round = select, triangles = navigate**. We adopt that vocabulary directly so an OnSpeed-display-on-huVVer behaves the way the same hardware behaves under its stock firmware.

Outside the menu (live mode):

| Button | Short press | Long press (≥ 600 ms) |
|---|---|---|
| BtnA (☐ Menu) | (free, see below) | **enter Settings menu** |
| BtnB (○ Select) | mode cycle 0 → 4 → 0 | (free) |
| BtnC (▶ Fwd) | brightness double | (free) |
| BtnD (◀ Back) | brightness halve | (free) |
| BtnB at boot | (firmware update — unchanged) | n/a |

This **promotes BtnD from "unused" to brightness-halve**, freeing BtnA to be the dedicated menu key. (Today on huVVer, BtnA halves brightness — that moves to BtnD where the back-arrow already iconographically suggests "decrease.") Short-press BtnA is left free for now; long-press is the menu entry. Mirrors V.R. Little's TBX where holding the square button has a system-level meaning.

Inside the menu:

| Button | Short press | Long press |
|---|---|---|
| BtnA (☐ Menu) | **back / exit menu** | (no-op) |
| BtnB (○ Select) | activate highlighted item | (no-op) |
| BtnC (▶ Fwd) | move highlight down | (no-op) |
| BtnD (◀ Back) | move highlight up | (no-op) |

Symmetric pair (◀ ▶) for navigation, dedicated back button — exactly the TBX idiom. No long-press needed inside the menu since BtnA is right there as a one-press exit. (The shared `MenuModel`'s `onBackOrLongPressExit()` is invoked on M5 from BtnB-hold and on huVVer from BtnA-click; same effect.)

### Event semantics for mode-cycle vs hold-to-enter

The current firmware uses `wasPressed()` for the live-mode buttons (`main.cpp:534/535/552`), which fires on the **press edge** — meaning a long-press would trigger the press-edge action *and* the menu, 600 ms apart. To avoid this collision, this PR migrates the live-mode handlers from `wasPressed()` to `wasClicked()`. M5Unified's `wasClicked()` fires on **release after a short press** and is guaranteed not to fire on a release that came after a hold (the internal `_press` state is set to 2 once held, and the click branch only triggers when `oldPress == 1`). So on M5:

- Short click of BtnB → release fires `wasClicked()` → mode cycles.
- Long press of BtnB → at 600 ms held, `wasHold()` fires once → menu opens. Subsequent release fires neither `wasClicked()` nor anything else.

The migration applies to all three (M5) / four (huVVer) live-mode buttons for consistency. The huVVer `HuvverButton` class needs matching `wasClicked()` and `wasHold()` methods with the same suppress-release-after-hold semantics (implementation in §"Cross-target compatibility").

Wraparound inside the menu: up from item 0 wraps to last item; down from last wraps to 0.

**Exit semantics:**
- **M5:** activate the `Exit` menu item, long-press BtnB anywhere, or 30-second idle timeout.
- **huVVer:** press BtnA (☐ Menu) anywhere, activate the `Exit` menu item, or 30-second idle timeout.

## Item types

Three item kinds, each rendered as a single row in the list:

```
+----------------------------------------+
|   Speed Units            [ KTS ]       |   <-- ToggleItem
| > Exit                                 |   <-- ActionItem (highlighted)
+----------------------------------------+
```

- **ToggleItem** — two values (e.g. `KTS / MPH`). Activate flips the value, persists immediately to NVS, stays on the menu. The new value is rendered live in the right-hand `[ ]` slot so the pilot sees the change instantly. No "edit mode," no confirmation dialog.
- **ActionItem** — activate runs a callback. Used for `Exit` today; `Restart` and `Factory Reset` are obvious follow-ups.
- **InfoItem** — read-only readout (version string, IP address, etc.). Activate does nothing. Not used in the v1 menu but defined so the framework is complete.

(Throughout this section, "activate" = the select button — BtnB on M5, BtnB / ○ Select on huVVer.)

A future `IntItem` (with a sub-edit mode where activate enters edit, the up / down navigation buttons change the value, and activate exits edit) is intentionally **not** implemented in this PR. Scope discipline: we don't have a setting that needs it yet.

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
- **Footer:** button hints, drawn from primitives (`gdraw.fillTriangle`, `gdraw.fillCircle`, `gdraw.drawRect` for the square). Per-target text:
  - M5: `▲ UP   ● SEL   ▼ DOWN` (BtnA, BtnB, BtnC).
  - huVVer: `☐ BACK   ○ SEL   ◀ UP   ▶ DOWN` (BtnA, BtnB, BtnD, BtnC) — four hints fit; BtnD label sits on the left edge to mirror the physical button position.
  - Footer divider at `y=200`, hint text at `y=220`. The footer is in v1 to aid discovery on a panel with no manual; we'll consider removing it once the bindings are settled.
- **Exit transition:** on exit, the menu deletes its sprite and the next `loop()` iteration renders the live mode normally. No "Saving..." banner — toggle-flips already persisted on press, so the banner would be symbolic. Clean cut from menu to live mode.
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

Three new files, split by platform-dependence:

- `software/OnSpeed-M5-Display/lib/MenuModel/MenuModel.h/.cpp` — pure-logic state machine. **Lives inside the M5 sub-project**, peer to `lib/GaugeWidgets/`, *not* in `onspeed_core`. Even though `MenuModel` happens to be platform-independent (no Arduino, no M5GFX, time injected via `tick(elapsedMs)`), it's a UI concern, not a flight-math concern. `onspeed_core` is for AOA / EKF / Kalman / Madgwick — code that defines aircraft behavior. A menu state machine doesn't belong there. Keeping it in the M5 sub-project also means the main firmware doesn't carry the dead weight (the sketch firmware has no menu and never will). Public surface:
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
      void onBackOrLongPressExit();           // BtnA on huVVer, BtnB-hold on M5
      bool wantsExit() const;
      int  currentIndex() const;
  };
  ```
- `software/OnSpeed-M5-Display/include/SettingsMenu.h` — thin platform layer:
  ```cpp
  void enterSettingsMenu();              // call from loop() when entry gesture fires
  void tickSettingsMenu();               // call from loop() each iteration when active
  bool isSettingsMenuActive();
  extern bool g_speedInMph;              // read by main.cpp's IAS-render path
  ```
- `software/OnSpeed-M5-Display/src/SettingsMenu.cpp` — wires `MenuModel` to M5GFX render + NVS read/write + button polling. The static `MenuItem[]` array and the toggle-flip / exit callbacks live here so they have access to `g_speedInMph` and `preferences`. Per-target button polling lives behind a single `#ifdef HUVVER` block (M5 polls BtnA/BtnB/BtnC, huVVer polls BtnA/BtnB/BtnC/BtnD with the assignments from §"Button mapping").

`onspeed_core` stays untouched by this PR — no menu code lands there.

`main.cpp` changes:

- New include `"SettingsMenu.h"`.
- Inside `setup()`, after the brightness read, add the `g_speedInMph` read.
- Migrate live-mode button handlers from `wasPressed()` to `wasClicked()`:
  - `main.cpp:534` `BtnC.wasPressed() → wasClicked()` (brightness up — both targets)
  - `main.cpp:552` `BtnB.wasPressed() → wasClicked()` (mode cycle — both targets)
  - `main.cpp:535` brightness-halve: on M5 stays on `BtnA.wasClicked()`; on huVVer **moves to `BtnD.wasClicked()`** under `#ifdef HUVVER`. (BtnA on huVVer becomes the dedicated Menu key with long-press-to-enter.)
  - The boot-time `BtnB.isPressed()` (line 348) and the firmware-update-cancel `BtnC.wasPressed()` (line 510) stay as they are — both run before the menu exists.
- Inside `loop()`, after `M5.update()` and `SerialRead()`:
  ```cpp
  if (isSettingsMenuActive()) { tickSettingsMenu(); return; }
  #if defined(HUVVER)
      if (M5.BtnA.wasHold()) { enterSettingsMenu(); return; }   // ☐ Menu key
  #else
      if (M5.BtnB.wasHold()) { enterSettingsMenu(); return; }   // round/middle button
  #endif
  ```
  Short-circuits the live-mode render and the brightness/mode-cycle handlers while the menu is up. `SerialRead()` runs every iteration (above the gate) so the UART RX FIFO keeps draining — the OnSpeed wire pushes 77 B × 20 Hz ≈ 1540 B/s, the ESP32 RX FIFO is 256 B, and pausing the drain across a 30-second menu visit would overflow within ~200 ms. The newest frame is always one tick stale at most, so live-mode resumes cleanly on menu exit.
- Delete the `#ifdef IAS_IN_MPH` gate; replace with `g_speedInMph ? IAS * 1.15078f : IAS`.

## Cross-target compatibility

**M5Unified (M5 Basic / Core2):** native support for `wasHold()` — no shim needed.

**huVVer-AVI:** add `wasHold()` and `wasClicked()` methods to `HuvverButton` in `sim/HuvverShim.h`. `wasHold()` fires true exactly once per gesture, the first poll where `pressedFor(600)` is true; track a `holdFired_` flag cleared on release. `wasClicked()` fires true exactly once on release **only if the gesture never crossed the hold threshold**; track an `everHeld_` flag set when the press passes 600 ms, and gate the click-on-release on `everHeld_ == false`. Matches `M5Unified::Button_Class` semantics so the call sites in `main.cpp` are target-agnostic.

**Native sim (SDL2):** Panel_sdl maps Left / Down / Right keys to BtnA / BtnB / BtnC. Holding the Down arrow key produces a continuous press, so `wasHold()` works in the sim. The shimmed `Preferences` does not persist across sim runs, but the menu is fully testable end-to-end.

**X-Plane plugin:** the plugin builds `main.cpp` with `XPLANE_PLUGIN_BUILD` and never calls `M5.begin()` / `M5.update()`. It also has no NVS. Two requirements:
1. Gate the `enterSettingsMenu()` call site behind `#ifndef XPLANE_PLUGIN_BUILD` so the plugin never inspects an uninitialized button.
2. `g_speedInMph` defaults to `false` (knots) from its runtime initializer, which is what the plugin needs — no special case required.

## Testing

`MenuModel` is pure logic (no M5GFX, no Arduino, time injected via `tick(elapsedMs)`), so it can compile and run under the existing M5 native env (`[env:native]` in `OnSpeed-M5-Display/platformio.ini` — already set up for SDL2-based desktop builds). This PR adds a `test/` directory and a small native-test entry point to the M5 sub-project, mirroring the main firmware's `pio test -e native` pattern.

**New test infrastructure** (first test suite for the M5 sub-project):

- `software/OnSpeed-M5-Display/test/test_menu_model/test_menu_model.cpp` — Unity tests against `MenuModel`.
- An `[env:m5-native-test]` env is added to `platformio.ini` that builds **only** the test source + `lib/MenuModel/` (excluding `src/main.cpp` and the SDL panel deps via `build_src_filter`). Run with `pio test -e m5-native-test --project-dir software/OnSpeed-M5-Display`. CI integration (`.github/workflows/ci.yml`) follows in a side change once this PR's tests are stable locally.

**Test coverage:**

- Cursor wraparound: up from index 0 lands at last index; down from last lands at 0.
- Toggle activation flips the toggle value.
- Exit-action activation sets `wantsExit() == true`.
- Idle timeout: `tick(31'000)` after no input flips `wantsExit()`.
- `onBackOrLongPressExit()` sets `wantsExit()` immediately regardless of cursor position.
- Activating an `InfoItem` is a no-op (cursor stays, no exit, no toggle change).

**Sim integration:** native SDL build (M5 mapping) runs end-to-end. SDL keys map Left / Down / Right → BtnA / BtnB / BtnC. Hold Down >600 ms to enter the menu, cycle items with Left / Right, flip the toggle with Down (short), long-press Down to exit. Confirm the IAS readout switches between knots and MPH digits live. The huVVer button vocabulary is not exercised in the sim today (would require adding a 4th key binding); huVVer behavior is verified on hardware.

**Hardware:**
- **M5 Basic / Core2:** bench test using `tools/m5-replay/replay.py` to stream synthetic frames. Verify (a) BtnB long-press enters without firing a stray mode-cycle, (b) the toggle persists across power-cycle, (c) the 30 s idle timeout returns the pilot to live mode.
- **huVVer-AVI:** same as above, but exercise the Vern-style button vocabulary — long-press BtnA (☐) to enter, ◀ ▶ to navigate, ○ to activate, BtnA short-press to back out. Verify the BtnD-now-halves-brightness change doesn't surprise pilots already running this firmware (note in release notes; only impacts huVVer users).

## Documentation

- `software/OnSpeed-M5-Display/CLAUDE.md`:
  - Add a "Settings menu" section describing both gesture sets (M5 BtnB long-press; huVVer BtnA long-press), the current items list, the file layout (`lib/MenuModel/` for logic, `SettingsMenu.cpp` for platform glue), and the recipe for adding a new menu item.
  - Update the existing huVVer button table to reflect BtnD's promotion from "unused" to brightness-halve.
- `docs/site/docs/installation/external-display.md`:
  - Two short user-facing blurbs, one per target:
    - **M5 Basic / Core2:** "Hold the middle button for ~1 second during normal flight to access settings. Use the left and right buttons to navigate, the middle button to change a value, and select 'Exit' to return to flight mode."
    - **huVVer-AVI:** "Hold the square (☐) button for ~1 second to access settings. Use ◀ and ▶ to navigate, the round (○) button to change a value, and the square button again to return to flight mode."

## Out of scope

- **`IntItem` with sub-edit mode.** No setting needs it yet; framework leaves a clean place for it.
- **Hold-to-repeat scrolling** on the navigation buttons inside the menu. With ≤ 5 items planned, single-press scrolling is fine.
- **Mode-skip setting.** Some pilots may want to disable Mode 4 (G-history) from the cycle; that's a future menu item, not this PR.
- **Touch input on Core2.** Core2 has a capacitive touch panel that the firmware already ignores everywhere else. Keeping that pattern.
- **Vendoring M5ez or M5StackSAM.** The 2-item menu we need is ~150 LOC. M5ez is 6,000 LOC and pulls in WiFi / WebServer machinery the M5 firmware doesn't want in its bundle.
- **Removing `REPEATER_MODE` / `VAC_MODE`.** Those were already deleted before this work. The only build flag this PR retires is `IAS_IN_MPH`.
- **Brightness NVS persistence.** Issue #419's "Related" section mentioned a separate item for this — it's already done (brightness writes through to the `"OnSpeed"` namespace at `main.cpp:489`). This PR adds `SpeedMph` alongside the existing `Brightness` key; no other persistence work needed.

## Open questions

None — design is locked. Implementation plan to follow via writing-plans.
