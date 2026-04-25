# huVVer-AVI Display Integration — Design (revised)

**Date:** 2026-04-24 (revised after master shipped PR #268)
**Author:** Sam Ritchie (with Claude)
**Status:** Draft, pending implementation

## What changed since the original draft

The original spec (committed and then closed) proposed a 7-PR series:

1. PR 0 — render-test harness (RenderShim duplicating renderer bodies for native tests).
2. PR 1 — extract `DrawApi` abstraction, move `GaugeWidgets` to a shared library.
3. PR 2 — extract renderers and `DisplayAppState` into Display-Core.
4. PR 3 — extract `SerialReader` and `OtaServer` into Display-Core.
5. PR 4 — retroactive X-Plane plugin attribution.
6. PR 5 — import the huVVer sketch.
7. PR 6 — docs.
8. PR 7 — bench-test both targets.

While the early PRs were drafted, master shipped **PR #268 ("M5 display: desktop + WebAssembly simulator")**. That PR did most of what PRs 0–3 were going to do, with a different and simpler architecture:

- `#ifdef ESP_PLATFORM` gates the WiFi/Preferences/WebServer blocks. Renderer code compiles unchanged on both ESP32 and host.
- A new `[env:native]` in the M5 sub-project links the same source against M5GFX's SDL panel backend, producing a runnable desktop sim.
- `software/OnSpeed-M5-Display/sim/ArduinoShim.h` provides host stubs for `String`, `Serial`, `Preferences`, `WiFi`, `WebServer`, `Update` — exactly the shape PR 0's `stubs/` directory was creating, but properly maintained as production code.
- `InjectSerialByte()` was extracted so the parser is driveable from any source.

The spec's premise — "renderers can't compile natively, so we need a duplication-shim" — is no longer true. The architectural objective ("M5 and huVVer share renderer code") is now better served by extending master's existing pattern rather than creating a parallel `OnSpeed-Display-Core` library.

This revised spec describes the smaller, simpler plan that drops PRs 0–3.

## Goal

Get the huVVer-AVI display target building from this repo, sharing source with the M5 target via the same `#ifdef`-gated pattern master already uses — minus the `OnSpeed-Display-Core` extraction the original spec proposed.

## Scope

The huVVer-AVI panel is a 320×240 ST7789 SPI panel — exactly the kind of panel `lgfx::Panel_ST7789` in M5GFX's underlying LovyanGFX library is designed for. Rather than introduce a second graphics library (TFT_eSPI) and shim its API to look like M5GFX's, this spec **uses M5GFX everywhere** and instantiates a custom `LGFX_Device` subclass for huVVer's panel + SPI bus + backlight PWM.

This means the renderer code, GaugeWidgets, font symbols, datum enums, and `M5Canvas`/`gdraw` usage are **all unchanged** between targets. Only the board-init layer (panel/bus pin config, button-pin wiring) differs.

1. **Add a huVVer compat header** at `software/OnSpeed-M5-Display/sim/HuvverShim.h` (header-only, ~150-200 LoC), gated on `defined(HUVVER)`:
   - `class Huvver_LGFX : public lgfx::LGFX_Device` — wraps `lgfx::Panel_ST7789` + `lgfx::Bus_SPI` + `lgfx::Light_PWM`, configured for huVVer-AVI's exact pinout (sourced from V.R. Little's hardware reference docs).
   - `class HuvverButton` — mimics M5Unified's `Button_Class` interface (`isPressed()`, `wasPressed()`, `pressedFor()`) over huVVer's GPIO buttons.
   - `struct Huvver_M5` — exposes `Display` (a `Huvver_LGFX`), `BtnA/B/C/EXT` (instances of `HuvverButton`), `update()`, `begin()`, `config()` — only what M5 source actually calls.
   - `using M5GFX = Huvver_LGFX;` and a static `M5` global that the M5 source can use unmodified.
   - `M5Canvas` continues to work because it's `lgfx::LGFX_Sprite` and accepts any `LovyanGFX*` parent.
2. **Two ~3-line `#if defined(HUVVER)` blocks** in shared M5 source: the `#include <M5Unified.h>` directive in `main.cpp` and `SerialRead.cpp` becomes a conditional include that picks `HuvverShim.h` instead. Total: 6 lines of guards in shared source.
3. **Add `software/OnSpeed-huVVer-Display/`** as a new PIO sub-project. Contents:
   - `platformio.ini` — `[env:huvver-avi]` with `platform = espressif32@6.9.0`, `board = esp-wrover-kit` (4MB classic ESP32, closest to huVVer-AVI's hardware), `lib_deps += m5stack/M5Unified@^0.2.13` (we pull M5Unified's M5GFX dep but never call into M5_t auto-detect — our `Huvver_M5` shim replaces it), `build_flags += -DHUVVER`, `build_src_filter` reaching into `../OnSpeed-M5-Display/src/`.
   - `huvver_main.cpp` — huVVer-specific board-init helpers if needed (button GPIO setup before `setup()` runs, etc.). Most likely empty or near-empty.
   - `CREDITS.md` — names V.R. Little (upstream author of GaugeWidgets and the original huVVer-AVI hardware/firmware), Lenny Rauch (2021 OnSpeed adapter for huVVer). Notes Vern's non-commercial license terms apply to derived files. **Note: this design path does NOT use V.R. Little's TFT_eSPI-based OnSpeed sketch as a source.** We re-implement the equivalent functionality on top of master's M5 source. Vern's GaugeWidgets contribution is still attributed (we use it via `lib/GaugeWidgets/`); his huVVer sketch's source we do not import.
4. **Shared sources stay in `software/OnSpeed-M5-Display/src/`.** No rename, no extraction. The M5 sub-project becomes the canonical location for shared display code; huVVer reaches into it via PIO's `build_src_filter`.
5. **Retroactive X-Plane attribution** lands separately (see PR #286, already open). Independent of huVVer work.
6. **CI** — add a `build-huvver-display` job to `.github/workflows/ci.yml` mirroring `build-m5-display`.
7. **Docs** — extend `docs/site/docs/installation/external-display.md` to cover both M5 and huVVer. Site-level credits section listing external contributors.

## Non-goals (unchanged from original spec)

- Preserving huVVer upstream git history via `git subtree add`. Fresh copy with explicit per-file attribution headers.
- ESP32-S3 huVVer support (`HUVVER_AVI_S3`). V.R. Little's setup file reserves the define but provides no pin map.
- CAN bus / OC-driver / analog-input support on huVVer. Neither the OnSpeed firmware nor the original huVVer OnSpeed sketch drives them.
- Relicensing V.R. Little's code to MIT. Non-commercial license is preserved verbatim per Vern's terms.

## Architecture: ASCII

```
                       software/OnSpeed-M5-Display/
                       ├── src/
                       │   ├── main.cpp            ← shared source
                       │   ├── SerialRead.cpp      ← shared source
                       │   └── ...
                       ├── lib/GaugeWidgets/       ← shared library
                       └── sim/
                           ├── ArduinoShim.h       ← native host shim (today)
                           └── HuvverShim.h        ← NEW: huVVer compat (this spec)
                                  ▲          ▲
                                  │          │
                       defined(HUVVER) │          │ defined(NATIVE_BUILD), no ESP_PLATFORM
                                  │          │
       ┌──────────────────────────┘          │
       │                                     │
software/OnSpeed-huVVer-Display/    [env:native] runs the SDL2 sim;
├── platformio.ini                  no production target.
├── huvver_main.cpp     ← target-specific setup()
├── tft_user_setup.h    ← TFT_eSPI User_Setup
└── CREDITS.md
```

The three real backends:

| Backend | Defined macros | Display | Audio | Buttons |
|---|---|---|---|---|
| ESP32+M5GFX (M5 production) | `ESP_PLATFORM`, `ARDUINO`, `M5STACK_*` | M5GFX → ILI9342C | I²S DAC | M5.BtnA/B/C |
| ESP32+TFT_eSPI (huVVer production) | `ESP_PLATFORM`, `ARDUINO`, `HUVVER` | TFT_eSPI → ST7789 | DAC pin 25/26 | 4 GPIO buttons |
| Host+SDL (M5 sim) | (no `ESP_PLATFORM`), `NATIVE_BUILD` | M5GFX → SDL window | (none) | Keyboard arrow keys |

## Architectural alternatives considered

| Path | Approach | Verdict |
|---|---|---|
| **A** | Extract a `DrawApi` virtual interface; both M5 and huVVer implement it. | Rejected — adds a virtual dispatch layer master doesn't have, and M5GFX/LovyanGFX already provide the abstraction we need. |
| **B** | Keep the upstream huVVer sketch's TFT_eSPI dep; shim `M5Canvas` → `TFT_eSprite` via typedef + compat header. | Rejected after prototyping — TFT_eSPI's font-pointer types (`GFXfont*` vs M5GFX's `lgfx::IFont*`) and datum-enum values are non-trivially different. Adds ~150 LoC of compat per call site. |
| **C** | Duplicate the M5 source tree, port to huVVer with TFT_eSPI directly. | Rejected — guarantees drift over time. |
| **D** *(this spec)* | Use M5GFX/LovyanGFX everywhere; instantiate `lgfx::Panel_ST7789` directly for huVVer's hardware via a custom `LGFX_Device` subclass. | **Selected.** Smallest delta to shared source. Same renderer code on both targets. |

## Risks

**Risk: M5GFX cannot be built without M5Unified's auto-detect logic.**
*Mitigation*: M5Unified's `M5_t::begin()` runs board auto-detect; if our huVVer hardware doesn't match any known M5 board ID, M5_t may panic or produce wrong defaults. We sidestep this by **not calling `M5.begin()` at all** on huVVer — our `Huvver_M5::begin()` initializes the custom LGFX directly, calling neither `M5Unified` nor M5GFX's auto-detect path. M5Unified is pulled in only for its types (`Button_Class` etc., from which we mimic the API).

**Risk: huVVer hardware revision mismatch.**
huVVer is on its V5+ PCB rev (per the docs Vern shared); the upstream sketch's `my_custom_setup.h` is from an earlier rev. First-light flash is part of PR 5 acceptance — if the board doesn't boot, we pause and check pin map against current rev before merging.

**Risk: 4MB partition too small for TFT_eSPI + WiFi + our sketch.**
Same risk as the original spec. Measure binary size during PR 5; switch to `min_spiffs.csv` (1.9 MB app slot) if needed.

**Risk: We end up duplicating master's M5 source directory pattern in unhelpful ways (e.g. `huvver_main.cpp` re-declares too many globals already in `main.cpp`).**
*Mitigation*: minimize what `huvver_main.cpp` defines. It should be ~50 LoC max — only `setup()` and the OTA web handlers (the main.cpp parts that are `#ifdef ESP_PLATFORM`-gated for the M5 target need `#elif defined(HUVVER)` branches with huVVer's WiFi credentials and TFT-specific init).

## PR sequence (revised)

| PR | Description | Status |
|---|---|---|
| **PR 4** (was PR 4) | X-Plane attribution headers + CREDITS.md. | **Open**: #286 |
| **PR 1** | huVVer compat layer (HuvverShim.h) + 2 conditional-include guards in shared M5 source. No production behavior change for the M5 target. | Pending |
| **PR 5** | Add `software/OnSpeed-huVVer-Display/` sub-project with `[env:huvver-avi]`. First-light flash on real hardware. | Blocked by PR 1 |
| **PR 6** | Docs (`external-display.md` covers both targets). Site-level Credits page. | Blocked by PR 5 |
| **PR 7** | Bench-test both targets with `tools/m5-replay/replay.py`. | Blocked by PR 5 |

The original PRs 0, 2, and 3 are dropped — superseded by master's PR #268. PR 1's scope shrinks dramatically (no library extraction, no DisplayAppState struct, just a compat header that mimics the M5_t public surface using M5GFX's underlying LovyanGFX layer).

## Spec review

This document supersedes the prior spec at the same path. Implementation begins after the prototype agent's feasibility-report lands; if Path B (typedef compat) is blocked, we revisit with a Path A (DrawApi abstraction) revision.
