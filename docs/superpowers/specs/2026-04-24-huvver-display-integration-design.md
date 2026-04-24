# huVVer AVI Display Integration & `OnSpeed-Display-Core` Extraction — Design

**Date:** 2026-04-24
**Author:** Sam Ritchie (with Claude)
**Status:** Reviewed (agent-reviewed 2026-04-24), pending implementation

## Motivation

The `huVVer/OnSpeed_huVVer_display` repository contains a standalone OnSpeed energy display that targets V.R. Little's **huVVer AVI** avionics-panel instrument (ESP32, 2.4" ST7789 320×240, TFT_eSPI, 4 buttons, dual RS-232, CAN, dual DAC audio). It is functionally a sibling of the M5 display already in this tree — both sketches descend from the same Gen2 codebase, written by V.R. ("Voltar") Little and adapted for OnSpeed by Lenny Rauch in 2021. The two copies have since diverged: the M5 copy has been heavily polished in this repo through PRs #39, #170, #171, #173, and #234–#242 (and 30+ numbered audit findings in `docs/audit/`), while the huVVer copy has remained at roughly the 2021 baseline.

The goal is to bring the huVVer target into this repository under `software/OnSpeed-huVVer-Display/`, on the same release cadence as the M5 target and the X-Plane plugin, without duplicating the ~2,700 lines of renderer + OTA + serial-parser code the two sketches share. That means extracting a shared display-core library first, then landing the huVVer sketch as a thin adapter on top of it.

A secondary outcome is closing an attribution gap we inadvertently created when we imported the X-Plane plugin via `git subtree add` (PR #256): the source files name neither Topher Timemachine nor Mrcoole7890, even though `git log` still attributes the lines to them. This spec covers fixing that at the same time as adding analogous attribution for V.R. Little and Lenny Rauch on the huVVer/M5 code.

## Licensing landscape (read this before editing any attribution)

The repo root is **MIT**. However, the following file trees carry V.R. Little's **custom non-commercial license** (not MIT — the text reads "for non-commercial purposes"):

- `software/OnSpeed-M5-Display/lib/GaugeWidgets/GaugeWidgets.h/cpp`
- `software/Libraries/onspeed_core/src/gauges/` (7 files — already in the tree, pre-dates this spec)
- The huVVer sketch and its `GaugeWidgets.cpp/h` in the huVVer upstream repo
- Any code we extract from these sources into `Libraries/OnSpeed-Display-Core/` or `Libraries/GaugeWidgets/`

**Policy for this PR series (direction from Sam, 2026-04-24):** do not attempt to relicense. Preserve Vern's original non-commercial copyright headers verbatim on every derived file. Per-file attribution banners say "Licensed under V.R. Little's non-commercial license — see original notice below," not "MIT." A follow-up conversation with Vern about an MIT relicense is out of band; this PR series does not block on it.

Separately, `Button.cpp/h` (used in the huVVer target) is **CC BY-SA 3.0** (Jack Christensen). Share-alike means we cannot re-cover it under MIT; its original license header stays intact, and it gets a separate note in `software/OnSpeed-huVVer-Display/CREDITS.md` flagging the sub-license. If we later want to remove the SA obligation from the target distribution, the path is swapping to a MIT-licensed button library, not relicensing.

`software/LICENSES/` (new directory) holds verbatim copies of the two non-MIT licenses the repo now ships:
- `LICENSES/VR-Little-non-commercial.txt` — full text of Vern's custom license.
- `LICENSES/CC-BY-SA-3.0.txt` — full text of the Creative Commons Attribution-ShareAlike 3.0 Unported license.

The repo root `LICENSE` file stays MIT. We add a paragraph to the repo root `README.md` noting the mixed-license reality and pointing readers at `software/LICENSES/` plus per-subproject `CREDITS.md`.

## Scope

1. Extract a new platform-library `software/Libraries/OnSpeed-Display-Core/` containing:
   - A `DrawApi` abstract interface (see "DrawApi interface shape" below for the concrete surface; it is larger than the original draft estimated).
   - All five display-mode renderers (`drawAOA`, `drawSlip`, `AiGraph`, `pitchGraph`, `displayDecelGauge`, `displayGloadHistory`) plus `displaySplashScreen` and the `displayAOA` composite, parameterized on `DrawApi&` + `const DisplayAppState&`. `drawAOA`/`drawSlip` are exposed as internal helpers of the `displayAOA` composite, not as separately-addressable public renderers.
   - A typed `DisplayAppState` struct replacing the ~30 file-scope globals in each sketch.
   - A shared `SerialReader` that accepts a list of `SerialPortCandidate`s, runs port-autodetect, and parses `#1` frames via the existing `onspeed_core::proto::ParseDisplayFrame`.
   - A shared `OtaServer` implementing the WiFi-AP firmware upload flow.
2. Move `GaugeWidgets` from `software/OnSpeed-M5-Display/lib/GaugeWidgets/` to `software/Libraries/GaugeWidgets/`. Reparameterize its drawing primitives from `M5Canvas&` to `DrawApi&` so it can serve both the M5 (M5GFX backend) and huVVer (TFT_eSPI backend) targets from a single source copy.
3. Refactor `software/OnSpeed-M5-Display/` to consume the new `OnSpeed-Display-Core` library. Its `main.cpp` shrinks from ~1,600 lines to ~400, keeping only M5-specific glue (setup, button wiring, loop dispatch, DAC mute, backlight PWM init).
4. Add `software/OnSpeed-huVVer-Display/` as a new target:
   - Thin `main.cpp` that mirrors the refactored M5 structure.
   - `TftESpiDrawApi.cpp` wrapping a `TFT_eSprite&` into the `DrawApi` interface.
   - `my_custom_setup.h` (imported from huVVer's repo) configuring TFT_eSPI for huVVer AVI's ST7789 panel.
   - `platformio.ini` with `[env:huvver-avi]` (ESP32 classic only; no S3 variant).
   - `Button.cpp/h` imported from huVVer's repo, original CC BY-SA 3.0 header preserved.
5. Add per-source-file attribution banners to imported files in all three targets (M5, huVVer, X-Plane), preserving each file's original copyright verbatim. Add `CREDITS.md` files to: `OnSpeed-M5-Display/`, `OnSpeed-huVVer-Display/`, `OnSpeed-XPlane-Plugin/`, `Libraries/GaugeWidgets/`, `Libraries/OnSpeed-Display-Core/`.
6. Add `software/LICENSES/VR-Little-non-commercial.txt` and `software/LICENSES/CC-BY-SA-3.0.txt`. Amend `README.md` at repo root with a "Licensing" section explaining the mixed-license reality.
7. Extend the documentation site (`docs/site/docs/installation/external-display.md`) to cover the huVVer target alongside M5, and add a "Credits" section listing external contributors.
8. Add CI: a `build-huvver-display` matrix job in `.github/workflows/ci.yml` following the `build-m5-display` pattern. Add huVVer artifact rows to the PR-comment table and to release uploads.
9. Add a **structural** render-test harness (see "Testing strategy" — *not* byte-exact golden traces) in `test/` that asserts per-renderer draw-call counts, color histograms, and a coordinate-sequence hash with `--update-golden` support.

## Non-goals

- **Preserving upstream git history via `git subtree add`.** User direction: fresh copy with explicit in-file attribution is more transparent for a casual reader browsing master than a merge commit only visible via `git log --all`.
- **Shipping huVVer hardware.** The mid-term goal is feature parity between M5 and huVVer so a future production run is a firmware-flash away; whether/when to actually manufacture more huVVer AVI instruments is out of scope here.
- **ESP32-S3 huVVer support (`HUVVER_AVI_S3`).** V.R. Little's `my_custom_setup.h` reserves this define for future use but provides no pin map. If the S3 variant ever ships, it gets its own pin map and PIO env. Not blocking.
- **CAN bus / relay-driver / analog-input support.** huVVer AVI exposes CAN + 2 OC drivers + 2 analog inputs. Neither the OnSpeed firmware nor the original huVVer OnSpeed sketch drives these. Keep it that way.
- **Relicensing any of V.R. Little's code to MIT.** Policy decision above; would require Vern's written agreement and is out of band.
- **Porting huVVer's `REPEATER_MODE` and `VAC_MODE` build variants.** Both exist in the M5 sketch too (same source). The Display-Core extraction preserves them via compile-time `#ifdef`s passed through to the per-target `main.cpp`. No new features here.
- **Applying all 30+ M5 audit findings (021–053) to the huVVer code in PR 5.** PR 5 delivers a buildable + first-light huVVer target. Audit fixes are a follow-on series, with the ~5–7 highest-severity findings blocking PR 5 merge (see "PR sequence — PR 5 blocking audit fixes").
- **Translating the M5's simulator page to cover huVVer.** The docs page already includes an iframe-embedded M5 pixel sim (PR #271). A huVVer sim is valuable but a separate project.

## Key technical decisions

### `DrawApi` interface shape — concrete method list

Header-only pure-virtual class. Two concrete subclasses: `M5DrawApi` wraps `M5Canvas&`, `TftESpiDrawApi` wraps `TFT_eSprite&`. Method list derived from `grep -oE '\bgdraw\.[a-zA-Z_]+' software/OnSpeed-M5-Display/src/main.cpp software/OnSpeed-M5-Display/lib/GaugeWidgets/GaugeWidgets.cpp | sort -u` (run at implementation time to verify nothing is missed):

**Sprite lifecycle** (5): `setColorDepth(bits)`, `createSprite(w,h)`, `deleteSprite()`, `fillSprite(color)`, `pushSprite(x,y)`.

**Rect primitives** (4): `fillRect`, `drawRect`, `fillRoundRect`, `drawRoundRect`.

**Line / shape primitives** (8): `drawLine`, `drawFastHLine`, `drawFastVLine`, `fillTriangle`, `drawTriangle`, `fillCircle`, `drawCircle`, `drawPixel`.

**Text** (10): `setFont(const GFXfont*)` (canonical name; `setFreeFont` is the TFT_eSPI spelling — `TftESpiDrawApi::setFont` forwards to `sprite.setFreeFont`), `setTextColor(fg)`, `setTextColor(fg, bg)`, `setTextDatum(TextDatum)`, `setCursor(x,y)`, `print(str/int/float)`, `printf(fmt, ...)`, `drawString(str, x, y)`, `textWidth(str)`, `fontHeight()`.

**Panel control** (1): `setBrightness(level)`.

**Total: 28 methods** (not 40). The earlier undercount/overcount was based on the pre-refactor extern-global style; the actual unique surface is smaller because many of the call sites share the same method.

**Textdatum enum mapping.** Expose `onspeed::display::TextDatum` with values matching M5GFX's bit-packed integer encoding:

| Our enum | M5GFX `textdatum_t` | TFT_eSPI `*_DATUM` |
|--|--|--|
| `top_left` = 0 | `top_left` = 0 | `TL_DATUM` = 0 |
| `top_center` = 1 | `top_center` = 1 | `TC_DATUM` = 1 |
| `top_right` = 2 | `top_right` = 2 | `TR_DATUM` = 2 |
| `middle_left` = 4 | `middle_left` = 4 | `ML_DATUM` = 3 ⚠ |
| `middle_center` = 5 | `middle_center` = 5 | `MC_DATUM` = 4 ⚠ |
| `middle_right` = 6 | `middle_right` = 6 | `MR_DATUM` = 5 ⚠ |
| `bottom_left` = 8 | `bottom_left` = 8 | `BL_DATUM` = 6 ⚠ |
| `bottom_center` = 9 | `bottom_center` = 9 | `BC_DATUM` = 7 ⚠ |
| `bottom_right` = 10 | `bottom_right` = 10 | `BR_DATUM` = 8 ⚠ |
| `baseline_left` = 16 | `baseline_left` = 16 | `L_BASELINE` = 9 ⚠ |
| `baseline_center` = 17 | `baseline_center` = 17 | `C_BASELINE` = 10 ⚠ |
| `baseline_right` = 18 | `baseline_right` = 18 | `R_BASELINE` = 11 ⚠ |

**The TFT_eSPI integer values do not match M5GFX's.** `M5DrawApi::setTextDatum(TextDatum d)` is a `reinterpret_cast` (same values); `TftESpiDrawApi::setTextDatum(TextDatum d)` does a `switch` translating to the TFT_eSPI integer. Renderer code and GaugeWidgets pass `TextDatum`, not integers. Existing M5 `main.cpp` calls like `gdraw.setTextDatum(textdatum_t::baseline_left)` get rewritten to `draw.setTextDatum(TextDatum::baseline_left)`.

**Font pointers** (`const GFXfont*`) are Adafruit-compatible struct on both backends. `Free_Fonts.h` symbols (`FSS12`, `FSSB18`, etc.) work bit-for-bit on both. No translation layer.

**Colors** are RGB565 `uint16_t`. `TFT_BLACK=0x0000`, `TFT_WHITE=0xFFFF`, etc. are identical constants on both backends. Our `Libraries/OnSpeed-Display-Core/include/DisplayColors.h` defines them once; the renderers include only that header.

**Virtual-dispatch cost:** one indirect call per primitive. At 20 Hz with sprite batching we are CPU-idle most of the frame. Negligible.

**Not in `DrawApi`:** buttons (huge hardware diff between M5's 3 touch/tactile and huVVer's 4 tactile + 2 external), DAC mute, backlight PWM init, WiFi stack. Those are per-target `main.cpp`.

### `DisplayAppState` struct

Replaces the ~30 `extern` globals in each sketch with one typed struct, owned by the per-target `main.cpp`, passed by `const&` to every renderer. `SerialReader` and the main loop hold a non-const reference via a `DisplayAppState& state_` member. Fields covered: all parsed #1-frame values, AOA setpoints, derived/smoothed values, G-history ring buffer (300 samples = 60 s at 5 Hz), UI state (display mode, flash flag, numeric-display flag), rate-limited "display" copies of the noisy numbers, and `lastFrameMillis` for stale-data detection.

### `SerialReader` API

```cpp
struct SerialPortCandidate {
  HardwareSerial* port;
  int  rx, tx;
  bool inverted;       // true = RS-232 buffered, false = TTL
  const char* label;   // short stable identifier (for logging only — NOT a Preferences key)
};

class SerialReader {
public:
  SerialReader(DisplayAppState& state,
               const SerialPortCandidate* candidates, size_t count);
  unsigned int autoDetect(uint32_t timeoutMs = 30000);  // 0 = not found
  void         begin(unsigned int portIndex);
  void         tick();   // call once per main loop()
};
```

**Preferences key compatibility.** Both M5 and huVVer sketches currently use `preferences.begin("OnSpeed", false)` with the `uint` key `"SerialPort"`. `SerialReader` uses the same namespace + key verbatim, so an existing device upgrading over OTA does NOT lose its port setting. This is a hard constraint — any change to the namespace/key name would force a rescan on every OTA upgrade. Value encoding: port index (1-based; 0 means "not detected"), identical to current behavior.

**Autodetect responsiveness.** The current M5 `serialSetup()` blocks `setup()` for up to 30 s (audit finding 038 noted this but did not fully fix it). The extracted `autoDetect()` takes a callback or requires the caller to interleave `autoDetect()` short-timeout calls with `ButtonUpdate()` in a polling loop. Concrete shape:

```cpp
for (uint32_t start = millis(); millis() - start < 30000; ) {
  ButtonUpdate();                              // responsiveness
  unsigned int port = reader.autoDetect(500);  // 500 ms per probe
  if (port) break;
}
```

Documented in `SerialReader.h`; reflected in M5 and huVVer `main.cpp` alike.

### `GaugeWidgets` migration

Current M5 `GaugeWidgets.cpp` takes `M5Canvas&` (as `extern M5Canvas gdraw` — a global reference). Post-refactor: `Gauges` holds a `DrawApi*` set via `setDrawApi(DrawApi&)` during target `setup()`. Drawing methods use `draw_->fillTriangle(...)`, etc.

**Critical: both GaugeWidgets AND the renderers in `main.cpp` use the same `DrawApi`.** The ~30 `gdraw.*` call sites in `GaugeWidgets.cpp` plus the ~100+ `gdraw.*` call sites across the renderer functions in `main.cpp` all go through `DrawApi`. The renderers take a `DrawApi&` parameter and use `draw.fillTriangle(...)` etc. directly. GaugeWidgets holds its own `DrawApi*` because its public methods don't currently pass a draw surface.

Per-file platform-freeness check is NOT extended to `software/Libraries/GaugeWidgets/` — it still pulls in Arduino (for `String`, `millis`, `PI`), unlike `onspeed_core/`. This is acceptable; GaugeWidgets is a display library, and Arduino coupling is the point. `check_core_purity.sh` only gates `onspeed_core/`.

### Floating-point determinism for render tests

Render functions currently use `sin()` / `cos()` (double-precision) at multiple call sites in `main.cpp`. Double-precision trig runs in software on Xtensa (no FPU) and in hardware on x86, producing slightly different low-order bits — enough to change `int16_t(x + radius * sin(θ))` by ±1 pixel at some angles. `onspeed_core/src/gauges/ArcGeometry.h` already canonicalized to `sinf/cosf` to fix this (finding 047).

**Canonicalization of trig in the renderers is a prerequisite to PR 2's test harness.** As part of PR 2 (or a preparatory commit inside it), every `sin()` / `cos()` / `atan2()` call in the extracted renderer functions becomes `sinf/cosf/atan2f`. Golden trace capture happens after this canonicalization, so the recorded coordinates are reproducible across build platforms.

**Golden traces are captured and compared on the native (x86) CI runner only.** `pio test -e native` is the only place these tests run. `ci.yml` already pins `ubuntu-latest`, which is x86-64. Tests are not cross-compiled for ESP32.

### Attribution scheme

Every imported source file gets a header block like:

```cpp
/*
 * Originally: <upstream project name>
 *   <upstream GitHub URL>, commit <sha> imported YYYY-MM-DD.
 *   Authored by <real name> <handle or email>.
 *
 * Ported into the OnSpeed-Gen3 tree and adapted to:
 *   - <one line per adaptation>
 *
 * Licensed under <license name — MIT / V.R. Little non-commercial /
 * CC BY-SA 3.0> — see LICENSES/<file>.txt or original notice below.
 */

/*
<verbatim original copyright + license text>
*/
```

Each subproject directory gets `CREDITS.md` with:
- Upstream repo URL.
- Commit hash at import time.
- Named authors (GitHub handle + real name where known).
- License of each imported component.
- One paragraph summarizing post-import changes.

Files getting attribution touch-ups:
- **X-Plane** (retroactive): `src/aoa_audio.cpp` (name TopherTimemachine + Mrcoole7890), `CREDITS.md` new. This is PR 4, independent.
- **M5** (incremental): `SerialRead.cpp/h` gets a "ported from Gen2 OnSpeed_M5_display, see Vern's non-commercial copyright in GaugeWidgets.h" banner. `CREDITS.md` new; `main.cpp` already has Vern + Lenny attribution from prior PRs.
- **huVVer** (new): all imported files get full banners preserving Vern's non-commercial license verbatim. `Button.cpp/h` keeps its CC BY-SA 3.0 header intact. `CREDITS.md` names V.R. Little (upstream author), Lenny Rauch (2021 OnSpeed adapter), and Jack Christensen (Button library, CC BY-SA 3.0 — flagged as sub-license).
- **Libraries/GaugeWidgets** (new): `CREDITS.md` explicitly cross-references V.R. Little, links his huVVer.tech original, notes our fork history, flags the non-commercial license.
- **Libraries/OnSpeed-Display-Core** (new): `CREDITS.md` credits the renderer math/layout to V.R. Little + Lenny Rauch (non-commercial license applies where derived), notes that the `DrawApi` abstraction and extraction itself is OnSpeed-Gen3 work (MIT).

### Build system

- New `software/Libraries/OnSpeed-Display-Core/` with `library.json`, `library.properties`, `src/`. Both M5 and huVVer targets pick it up via existing `lib_extra_dirs = ../Libraries` and `lib_deps = OnSpeed-Display-Core`.
- `software/Libraries/GaugeWidgets/` replaces `OnSpeed-M5-Display/lib/GaugeWidgets/`. Identical include style (`#include <GaugeWidgets.h>`). Both targets depend on it.
- New `software/OnSpeed-huVVer-Display/platformio.ini`:
  - `platform = espressif32@6.9.0` (same as M5; huVVer uses ESP32 classic).
  - `framework = arduino`.
  - **`board = esp-wrover-kit`** — ESP32 classic, 4MB flash, no PSRAM. The default partition CSV for this board is `default.csv` which allocates ~1.3 MB per OTA slot. TFT_eSPI + WiFi + our sketch is likely to approach but not exceed this. **Before PR 5 submission:** build the huVVer target and measure `.pio/build/huvver-avi/firmware.bin` size; if >1.2 MB, add `board_build.partitions = min_spiffs.csv` (1.9 MB app slot, no SPIFFS) or a custom partition CSV. Document the chosen partition in the `platformio.ini` with a comment explaining the decision.
  - `build_flags += -I "$PROJECT_DIR/src"` so TFT_eSPI's `User_Setup_Select.h` finds our `my_custom_setup.h` (or we use `-DUSER_SETUP_LOADED -include src/my_custom_setup.h`; decide during PR 5 prep based on whether TFT_eSPI honors `-include` for its Setup selection).
  - Strict-warning flags mirrored from M5 (`-Wall -Wextra -Werror -Wshadow -Wformat=2 -Wunreachable-code -Wnull-dereference`), with `-Wno-error=format-nonliteral` downgraded up front because WebServer headers trip it. Other `-Wno-error=` flags are added one at a time only if TFT_eSPI's headers force them, each with a comment explaining the upstream file. If more than 3 such flags become necessary, escalate to dropping `-Werror` on the huVVer src-tree and filing a tech-debt issue.
  - `extra_scripts = pre:../../scripts/generate_buildinfo.py` so huVVer OTA upgrades show the same version string as firmware/M5/X-Plane builds.
- CI: new `build-huvver-display` job in `.github/workflows/ci.yml` under the existing matrix; follows `build-m5-display` structure verbatim. Release workflow (`release.yml`) gets a huVVer artifact row. PR-comment sticky-table updated.

### Testing strategy

**Render regression via structural assertions (not byte-exact traces).** Under `test/test_display_renderers/`:

- `MockDrawApi.h` records every draw call as a structured event (method name, arg tuple, for each call).
- Assertions per renderer fixture, NOT a byte-exact `.golden` file:
  - `EXPECT_EQ(mock.drawCallCount("fillTriangle"), N)` — counts per method type.
  - `EXPECT_EQ(mock.colorHistogram()[TFT_RED], N)` — number of red pixel-emitting calls.
  - `EXPECT_EQ(mock.coordHash(), "0xABCD...")` — MurmurHash3 of the (method, x, y, ...) sequence, regeneratable with `--update-golden` flag.
  - `EXPECT_PRESENT(mock, "fillCircle", x=160, y=120, r=2)` — specific critical anchor checks (widget center, known hotspots).
- Traces are captured and compared on **native x86 only** (`pio test -e native`). CI pins ubuntu-latest which is x86-64. Tests are not cross-compiled for ESP32.
- Only the renderer's output is checked. Randomized or time-varying inputs (`millis()`, `rand()`) are mocked to stable values in each fixture.

This is lighter than a byte-exact golden (a regenerated `.golden` is a hash rewrite, not a 500-line diff) and still catches structural regressions during extraction. **First deliberate layout change** requires updating: the coord hash (`--update-golden`), any affected per-call-type counts, and any affected anchor assertions — typically 3-5 test lines. Manageable.

**Bench replay.** `tools/m5-replay/replay.py` already drives M5 from CSV logs. Extension for huVVer is a TTL-to-RS-232 adapter, not a code change. Same logs, same inputs, visual comparison on two screens side-by-side.

**Manual verification.**
- After PR 1 (DrawApi + GaugeWidgets move): flash current M5, confirm identical visual behavior.
- After PR 2 (renderer extraction): flash current M5, confirm identical visual behavior.
- After PR 3 (SerialReader/OtaServer extraction): flash M5, confirm serial-port autodetect still works, OTA upgrade page still loads.
- After PR 5 (huVVer): flash huVVer AVI hardware, confirm all 5 modes render, button nav works, OTA works.
- After PR 7: bench replay on both simultaneously with the same input log.

### PR sequence

**PR 0 — Render-test harness and baseline capture.** NEW. Creates `test/test_display_renderers/` with `MockDrawApi`, per-renderer fixtures, and initial assertions + hashes captured against the *current* M5 `main.cpp` code (pre-refactor). Prerequisite for PR 1 — without it, we have no safety net for the DrawApi substitution. Uses a temporary shim (`MockDrawApiShim : public M5Canvas`) that masquerades as the current `M5Canvas&` so renderers can be invoked unmodified.

**PR 1 — Extract `DrawApi`, move GaugeWidgets to `Libraries/`.** Rewrite `GaugeWidgets.cpp` to take `DrawApi&` instead of `M5Canvas&`. Ship `M5DrawApi` in `OnSpeed-Display-Core/src/backends/`. M5 sketch builds unchanged externally; internal `gdraw` references in `main.cpp` rewritten to route through a `DrawApi&` parameter. Render tests from PR 0 must pass.

**PR 2 — Extract renderers + `DisplayAppState` into Display-Core.** Pull five mode renderers plus `displayAOA`/`displaySplashScreen` into `OnSpeed-Display-Core/src/render/`. Replace M5 globals with a struct. `sin/cos` → `sinf/cosf` canonicalization at the same time (required for test reproducibility — finding 047 of the M5 audit already did this for `ArcGeometry.h`, this PR extends it to the renderer code). M5 `main.cpp` drops from ~1,600 → ~400 LoC. Render tests updated to use the new API and re-hashed against the refactored-but-functionally-identical code.

**PR 3 — Extract `SerialReader` + `OtaServer` to Display-Core.** M5 uses them via the new API; still behaves identically. Preferences key name preserved (`OnSpeed` / `SerialPort`).

**PR 4 — X-Plane attribution fix (parallel with PR 1–3).** Add header banner on `aoa_audio.cpp`, add `OnSpeed-XPlane-Plugin/CREDITS.md`. Fully independent, no file overlap with PRs 1–3. **Can ship first** if PRs 1–3 are delayed.

**PR 5 — Import huVVer sketch, first-light build.** Fresh copy of `OnSpeed_huVVer_display.ino`, `Button.cpp/h`, `SerialRead.h`, `my_custom_setup.h`, `Free_Fonts.h` (diff against M5's; if bit-identical, promote to a shared location; if not, note divergence in `CREDITS.md` and keep separate). Full attribution headers with verbatim original license text. New PIO env. Partition sizing confirmed. CI job added. **Blocking audit fixes (must land in or before PR 5):**
  - Finding 021 — `PitchStr[4]` buffer overflow in Mode 1.
  - Finding 022 — `FlapsChar[2]` buffer overflow.
  - Finding 023 — CRC verification dereferences dangling `String` pointer.
  - Finding 024 — `vBarGraph`/`hBarGraph` mutate member `rangeTop[]` every frame.
  - Finding 029 — `fillArc` CCW branch mirror-reflection bug.
  - Finding 035 — `SerialReader` frame-dt missing lower clamp.
  - Finding 047 — `fillArc` double-precision `sin/cos` on Xtensa (incoherent with PR 2's canonicalization).
  All seven are memory-safety, protocol-correctness, or floating-point-determinism bugs that would corrupt the huVVer first-light experience. Remaining findings (026, 027, 030, 031, 032, 033, 036, 037, 038, 039, 040, 041, 042, 043, 044, 045, 046, 048–053) filed as a follow-on PR series "huVVer audit cleanup," each referencing the M5 audit-finding file it's porting.

**PR 6 — Docs integration.** Extend `docs/site/docs/installation/external-display.md` to cover huVVer (datasheet summary, mounting options, panel cutout, wiring, button map, OTA upgrade). Add a site-wide Credits section. Update project `CLAUDE.md` to describe the new shared Display-Core layout.

**PR 7 — Bench-test both targets, capture evidence.** Run `tools/m5-replay/replay.py` against both targets with a representative flight log. Capture photos of huVVer running, add to docs.

**Dependency graph:**
- PR 0 → PR 1 → PR 2 → PR 3 (strictly serial).
- PR 4 can land any time (fully independent).
- PR 5 depends on PR 3.
- PR 6, PR 7 depend on PR 5.

## Risks and mitigations

**Risk: GaugeWidgets reparameterization breaks M5 visually.** Mitigation: PR 0 locks in baseline renderer traces via `MockDrawApiShim` before PR 1 touches anything. PR 1 is tightly scoped to the interface change; no renderer logic changes. If PR 1's trace diverges from PR 0's baseline, the test catches it.

**Risk: The TextDatum enum mapping contains a subtle bug (off-by-one on one of the 12 values).** Mitigation: the mapping table in the "DrawApi interface shape" section is checked into the spec as the authoritative reference. Implementation copies the table into `TftESpiDrawApi.cpp` comments for visual verification during code review.

**Risk: TFT_eSPI warns heavily on huVVer, forcing many `-Wno-error=` flags that dilute the zero-warning policy.** Mitigation: enumerate TFT_eSPI warnings as a single research spike during PR 5 prep. If the count is small (≤3 flags), add each with a specific comment. If ≥10, drop `-Werror` on the huVVer src-tree and file a tech-debt issue — don't paper over.

**Risk: huVVer hardware I have on hand is a revision mismatch vs `my_custom_setup.h`.** Mitigation: first-light flash is part of PR 5 acceptance, not a post-merge activity. If the board fails to boot, we pause PR 5, confirm the revision with V.R. Little or the makerplane community, and adjust the pin map before merging.

**Risk: huVVer 4MB flash partition isn't large enough for TFT_eSPI + WiFi + our sketch.** Mitigation: binary size measurement is an explicit gate before PR 5 submission. If we exceed ~1.2 MB in the default 1.3 MB OTA slot, switch to `min_spiffs.csv` (1.9 MB slot, sacrificing SPIFFS) with a documented comment explaining why.

**Risk: Display-Core ends up with circular dependency on GaugeWidgets, or vice versa.** Mitigation: enforce layering — `OnSpeed-Display-Core` depends on `GaugeWidgets`, not the other way around. GaugeWidgets only knows about `DrawApi` (via a forward declaration in its header, full include only in the `.cpp`). Display-Core's renderers hold `Gauges` instances internally.

**Risk: Attribution fix (PR 4) is perceived as controversial retroactive author-assignment.** Mitigation: the X-Plane plugin is MIT-licensed on both ends, and what we are adding is *credit where credit is due*, not a license change. The CREDITS.md and file banners reference the upstream commit SHA (`fff96202fc82a9e8e0e2fda7d62ee92237c18a05`) so the provenance is auditable. Notify Topher via a PR comment tag before merge.

**Risk: huVVer `Button.cpp/h`'s CC BY-SA 3.0 license is incompatible with MIT redistribution.** Mitigation: CC BY-SA 3.0 does not prevent inclusion in a larger MIT-licensed distribution — it requires the specific CC BY-SA file itself remain under CC BY-SA 3.0 in the derivative. We preserve the header verbatim, flag the sub-license in `software/OnSpeed-huVVer-Display/CREDITS.md`, and ship `LICENSES/CC-BY-SA-3.0.txt`. If someone later wants the target MIT-clean, swap Button for a MIT-compat button library.

**Risk: PR 5's blocking audit fixes leak scope.** Mitigation: the 7 blocking findings are enumerated in the PR sequence above. Anything else discovered during the import is filed as a new audit finding and deferred to the "huVVer audit cleanup" series. PR reviewer is instructed to reject scope creep.

## Open questions to resolve during implementation

1. **`Free_Fonts.h` — symlink or separate copy?** huVVer's version is 377 lines, M5's is 377 lines, and both derive from Adafruit GFX's `Fonts/` set. If a `diff` shows them bit-identical, promote to `software/Libraries/FreeFonts/`. If not, keep separate and note the divergence in `CREDITS.md`. Resolve during PR 5 prep.
2. **TFT_eSPI `my_custom_setup.h` integration with PIO.** TFT_eSPI typically uses `User_Setup_Select.h` to dispatch between setup files. Option A: `-DUSER_SETUP_LOADED -include $PROJECT_DIR/src/my_custom_setup.h`. Option B: copy `my_custom_setup.h` contents into `User_Setup.h` of the vendored TFT_eSPI. Option A is preferred; confirm it works during PR 5 prep.
3. **CAN / OC / analog pins on huVVer.** Leave `pinMode` as INPUT for OC pins (match the imported sketch), don't touch CAN. Document in `OnSpeed-huVVer-Display/CLAUDE.md` post-import.
4. **Partition scheme exact choice.** Default (`default.csv`, 1.3 MB app slot) vs `min_spiffs.csv` (1.9 MB slot). Decide by measurement during PR 5. If default fits with >200 KB headroom, use it (keeps SPIFFS available for future features). Otherwise switch.

## Alternatives considered

- **Duplicate the huVVer sketch wholesale (Approach C).** Rejected. Two copies of a 2,300-line renderer will drift, and every M5 audit fix would need re-applying to huVVer by hand. We'd have this same conversation in 6 months.
- **Share GaugeWidgets only, keep two main.cpp files (Approach B).** Rejected. Gets us building fast but leaves the renderers duplicated — and the renderers are where the divergence actually matters over time.
- **Preserve huVVer upstream history via `git subtree add` (like X-Plane).** Rejected per user direction. Fresh copy with explicit in-file attribution is more transparent for a casual reader browsing master.
- **Relicense V.R. Little code to MIT via consent email.** Parallel out-of-band effort — does NOT block this PR series. Spec assumes non-commercial license is preserved verbatim.

## References

- huVVer upstream: `https://github.com/huVVer/OnSpeed_huVVer_display`, last push 2025-07-17.
- huVVer AVI datasheet: `https://makerplane.org/huvver-avi-instruments/`.
- X-Plane plugin move plan (analogous playbook): `docs/XPLANE_PLUGIN_MOVE_PLAN.md`.
- M5 display CLAUDE.md (text-layout rules carry over to huVVer): `software/OnSpeed-M5-Display/CLAUDE.md`.
- M5 audit findings 021–053 in `docs/audit/` — most apply to the huVVer code verbatim, since both share the Gen2 origin. The seven blocking-for-PR-5 findings are enumerated above.
- Existing `onspeed_core` modules that the huVVer target will consume: `proto/DisplaySerial.h`, `filters/SavGolDerivative.h`, `gauges/ArcGeometry.h`, `gauges/BarRangeScale.h`.

## Review trail

This spec was reviewed by an ultrathink-tier agent on 2026-04-24. Ten findings resolved inline in this revision; see commit history for the pre-review draft. Summary of material changes from review:
- License recharacterization (Vern's code is non-commercial, not MIT). Section "Licensing landscape" added. Attribution banner template corrected.
- DrawApi method count corrected from ~40 to 28, with enumeration. TextDatum integer mapping table added (M5GFX ≠ TFT_eSPI).
- PR 0 added (baseline test harness before any code moves).
- Render tests downgraded from byte-exact golden traces to structural assertions + coordinate-sequence hash (maintainability).
- Floating-point determinism section added (native-only tests; `sin/cos` → `sinf/cosf` canonicalization as part of PR 2).
- Button library CC BY-SA 3.0 treatment made explicit.
- Partition sizing gate added to PR 5.
- Preferences key-compatibility constraint made explicit (OTA upgrade must not lose port setting).
- PR 5 "blocking audit fixes" list enumerated (7 findings); follow-on series for remaining 23.
