# X-Plane plugin: embedded M5 indexer

## Goal

Show the OnSpeed M5 indexer **inside an X-Plane window**, driven by
X-Plane datarefs, with all five M5 modes (AOA indexer, attitude,
narrow AOA, decel gauge, G-load history) selectable. Same renderer
the real M5 hardware runs.

A follow-up (later, not in this PR) lets the same data path also
drive a real M5 plugged into the laptop over USB serial.

## v2: rewritten in response to bulldog review

The first draft proposed a "headless M5Unified, no SDL" architecture.
Code review demolished it: `lgfx::v1::millis/micros/delay` only exist
inside `M5GFX/src/lgfx/v1/platforms/sdl/common.cpp` (gated `#if
defined(SDL_h_)`), and the M5 firmware calls `millis()` ~10 times per
loop iteration. Going SDL-free is a research project, not an overnight
build.

This rewrite commits to **SDL2 as a runtime dependency** and gets the
sane benefits: the M5 firmware already runs end-to-end on macOS via
the existing `software/OnSpeed-M5-Display/sim/` build, proven by
`pio run -e native`.  We're not inventing anything; we're moving the
already-working desktop sim into the X-Plane plugin process.

## Architectural decision

The plugin **links the M5 firmware sources directly** as additional
C++ TUs alongside M5GFX, M5Unified, and SDL2. We replace M5Unified's
default `Panel_sdl` with our own `Panel_PluginCanvas` — a custom
`lgfx::v1::Panel_Device` whose `writeBlock`/`writePixels` copy into a
plugin-owned RGB565 framebuffer instead of an SDL window.

The result: the M5 firmware runs unchanged, calls `pushSprite()` at
the end of every `loop()`, that `pushSprite` populates our framebuffer
via the custom panel, and the X-Plane plugin uploads our framebuffer
to a GL texture and draws it into an `XPLMWindow`.

Why a custom panel instead of using `Panel_sdl`:
- `Panel_sdl` opens an OS window. We don't want a second window
  appearing alongside X-Plane.
- `Panel_sdl::_texturebuf` is private; reading it back would require
  a patch to vendored M5GFX.
- A custom `Panel_FrameBufferBase` subclass is the canonical M5GFX
  extension point for software framebuffers — that's exactly what
  `Panel_sdl` is.  We're following its model (subclass
  `Panel_FrameBufferBase`, override `initFrameBuffer` + `display`)
  but writing to a plugin-owned buffer instead of an SDL texture.

SDL2 stays linked because `lgfx::millis/micros/delay/spi/i2c` from
`common.cpp` need it. SDL is initialized in audio-only mode
(`SDL_INIT_TIMER`) — no video subsystem, no window, no event pump.

Net code added in the plugin: ~400 lines (custom panel, plugin
wrapper, dataref adapter, OpenGL plumbing, CMakeLists changes).
Net code reused: the entire ~1700-line M5 renderer + ~3000 lines of
M5GFX + M5Unified + onspeed_core.

## Files

**Reused unchanged from `software/OnSpeed-M5-Display/`:**

- `src/main.cpp` — `setup()` + `loop()`, all five mode renders.
- `src/SerialRead.cpp` — `InjectSerialByte()`, frame parsing.
- `lib/GaugeWidgets/GaugeWidgets.cpp` — gauge primitives.
- `sim/ArduinoShim.h` — `millis()`, `Serial`, etc.

**Reused from `onspeed_core`:**

- `proto/DisplaySerial.{h,cpp}` — `DisplayBuildInputs`,
  `BuildDisplayFrame`, `kDisplayFrameSizeBytes`.

**Reused from third-party:**

- M5Unified, M5GFX, Free_Fonts (sources globbed from
  `software/OnSpeed-M5-Display/.pio/libdeps/native/`, same set the
  WASM build uses — see `sim/build_wasm.sh` for the exact glob).
- SDL2 (system framework on macOS, libsdl2-dev on Linux, vcpkg-static
  on Windows).
- tinyxml2 (transitively required by `onspeed_core/proto/LogCsv.cpp`;
  one .cpp added to the source list).

**New in `software/OnSpeed-XPlane-Plugin/`:**

- `src/m5_indexer/Panel_PluginCanvas.{h,cpp}` (~80 lines)
  - Subclass of `lgfx::v1::Panel_FrameBufferBase` — same base class
    `Panel_sdl` extends.  Per `Panel_sdl::initFrameBuffer` (see
    `M5GFX/src/lgfx/v1/platforms/sdl/Panel_sdl.cpp:644`), this base
    class implements every pixel op (`writePixels`, `writeBlock`,
    `writeFillRectPreclipped`, `drawPixelPreclipped`, `writeImage`,
    `setWindow`, `readRect`, `copyRect`) against an internal
    `_lines_buffer` of row pointers — we don't write any of them.
  - Overrides we DO write:
    - `initFrameBuffer(width, height)` — allocates one
      `width*height*2` byte buffer (RGB565), populates `_lines_buffer`
      with row pointers as Panel_sdl does.
    - `display(x, y, w, h)` — invoked by M5GFX after a region has been
      written.  Marks the X-Plane texture dirty so the draw callback
      uploads it on its next pass.  No SDL calls.
  - `M5.Display.setColorDepth(rgb565_2Byte)` is called from our
    Init() before any rendering, so `_lines_buffer` is sized for
    16bpp and renderer writes go straight in as RGB565 host-endian.
  - Provides `CopyToRGBA8888(uint32_t* dst)` — a 320×240 RGB565→RGBA8888
    conversion used by the GL upload path.  Mutex-guarded.

- `src/m5_indexer/IndexerWindow.{h,cpp}` (~250 lines)
  - `Init()`: initializes SDL (timer subsystem only), constructs the
    `Panel_PluginCanvas` and registers it with M5Unified, calls
    `setup()`, creates the X-Plane window with a draw callback,
    creates the GL texture.
  - `Tick()`: per X-Plane flight loop. Builds `DisplayBuildInputs`
    from datarefs, calls `BuildDisplayFrame`, feeds bytes through
    `InjectSerialByte`, calls the M5's `loop()`. After `loop()`
    returns, the panel has been written.
  - `XPLMDraw_f` callback: under a mutex, calls
    `Panel_PluginCanvas::CopyToRGBA8888` into a scratch buffer,
    `glTexSubImage2D` upload, draws a textured quad covering the
    window. Restores all GL state we touched.
  - `Shutdown()`: stops the X-Plane window, deletes the texture,
    calls (a no-op for now) M5 teardown, releases SDL.

- `src/m5_indexer/DataRefAdapter.{h,cpp}` (~150 lines)
  - `BuildInputsFromDatarefs(out)` populates the 22-field
    `DisplayBuildInputs` from X-Plane datarefs. **Complete table
    below in the "DataRef mapping" section** — no defaulting hand-wave.
  - One-time alpha_0 / alpha_stall derivation at Save time so
    per-frame `ComputePercentLift` calls don't repeat the lift-curve
    fit.

- Updates to `CMakeLists.txt` (~80 lines) — additional source globs
  for M5GFX (lgfx/v1, lgfx/Fonts, lgfx/utility, sdl platform), for
  M5Unified (top-level + utility), for the M5 firmware sources, for
  GaugeWidgets, for tinyxml2. Patterns lifted mechanically from
  `sim/build_wasm.sh:80-122`.

**Modified:**

- `software/OnSpeed-M5-Display/src/main.cpp` — small `#ifdef
  XPLANE_PLUGIN_BUILD` block at three call sites (see "main.cpp edits"
  section for the exact list and rationale).

## DataRef mapping (complete)

`DisplayBuildInputs` has 22 fields. Source per field:

| Field | X-Plane source / formula | Notes |
|---|---|---|
| `pitchDeg` | `sim/flightmodel/position/theta` | Direct |
| `rollDeg` | `sim/flightmodel/position/phi` | Direct |
| `iasKt` | `sim/flightmodel/position/indicated_airspeed` | Already exists in plugin |
| `paltFt` | `sim/flightmodel2/position/pressure_altitude` if present, else fall back to `sim/flightmodel/misc/h_ind` | Existence-check at Init; only the former is truly pressure altitude (29.92") |
| `turnRateDps` | `sim/cockpit2/gauges/indicators/turn_rate_heading_deg_per_sec_pilot` | Already deg/s; X-Plane's `position/R` is also deg/s but bulldog flagged ambiguity — use the unambiguous cockpit2 dataref |
| `lateralG` | `−sim/flightmodel/forces/g_side` | Negate to get ball-frame (positive = leftward, per DisplayBuildInputs convention at h:100-117) |
| `verticalGScaled10` | `sim/flightmodel/forces/g_nrml × 10`, rounded | Wire wants tenths × 10 as raw int (stored as float in struct) |
| `percentLift` | `ComputePercentLift(smoothedAoa, flapCfg, iasValid)` | flapCfg = single derived from plugin setpoints; see "alpha_0 derivation" below |
| `vsiFpm10` | `sim/flightmodel/position/vh_ind_fpm / 10`, floored | Wire wants fpm/10, range −999..+999 |
| `oatC` | `sim/cockpit2/temperature/outside_air_temp_degc` | Direct |
| `flightPathDeg` | `sim/flightmodel/position/vpath` (vertical path angle, deg) | Direct |
| `flapsDeg` | `sim/cockpit2/controls/flap_handle_deploy_ratio × max_flap_degrees` | Round to int |
| `tonesOnPctLift` | `ComputePercentLift(plugin.fLDMAXAOA, flapCfg, iasValid)` | Snapped per current flap detent (operational gate) |
| `onSpeedFastPctLift` | `ComputePercentLift(plugin.fONSPEEDFASTAOA, flapCfg, iasValid)` | |
| `onSpeedSlowPctLift` | `ComputePercentLift(plugin.fONSPEEDSLOWAOA, flapCfg, iasValid)` | |
| `stallWarnPctLift` | `ComputePercentLift(plugin.fSTALLWARNAOA, flapCfg, iasValid)` | |
| `flapsMinDeg` | `0` (assumed) | X-Plane doesn't expose airframe flap-min |
| `flapsMaxDeg` | `30` constant (degrees) | X-Plane's airframe flap-max isn't reliably exposed via dataref; constant is fine for v1.  Issue #393 derives properly from `acf_flap_dn[]`. |
| `gOnsetRate` | `0.0f` constant for v1 | The G-history-mode renderer (mode 4) uses `gOnsetRate` only for an annotation overlay; static 0 produces a quiet display.  Plugin-side SavGolDerivative on `g_nrml` is dt-sensitive at variable X-Plane frame rates — defer to a follow-up that does it right. |
| `spinRecoveryCue` | `0` | Reserved per DisplaySerial.h:139 |
| `dataMark` | A monotonic counter the plugin increments on a hotkey press, mod 100. v1: just leave at 0 | |
| `pipPctLift` | Same as `tonesOnPctLift` for v1 (no pip-vs-tones-on separation in plugin). Per issue #392 we'll derive properly later | |

The setpoint percent fields (`tonesOnPctLift` etc.) are *the indexer's
visual UI* — bulldog correctly flagged that these can't be defaulted
to zero. The mapping above derives them all from the plugin's existing
four AOA setpoints + the current AOA on every frame.

### MakeFlapCfg helper

`ComputePercentLift` takes a full `OnSpeedConfig::SuFlaps` instance.
Pulling that struct in transitively pulls the entire config layer
(`OnSpeedConfig.h`, ConfigDefaults, parser, etc.) — too much for the
plugin.  Mitigation: a tiny helper that constructs a stack-local
`SuFlaps` populating only the fields `ComputePercentLift` reads:

```cpp
// In src/m5_indexer/DataRefAdapter.cpp
static onspeed::config::OnSpeedConfig::SuFlaps MakeFlapCfg(
    float alpha0, float alphaStall,
    float ldmax, float fastA, float slowA, float warn)
{
    onspeed::config::OnSpeedConfig::SuFlaps f{};
    f.fAlpha0          = alpha0;
    f.fAlphaStall      = alphaStall;
    f.fLDMAXAOA        = ldmax;
    f.fONSPEEDFASTAOA  = fastA;
    f.fONSPEEDSLOWAOA  = slowA;
    f.fSTALLWARNAOA    = warn;
    return f;
}
```

Built once per dataref-adapter tick from the plugin's existing four
threshold globals (`fLDMAXAOA`, `fONSPEEDFASTAOA`, `fONSPEEDSLOWAOA`,
`fSTALLWARNAOA` — already in `aoa_audio.cpp`).

### alpha_0 / alpha_stall derivation

`ComputePercentLift` takes a `SuFlaps` config that includes
`fAlpha0` (zero-lift body angle) and `fAlphaStall` (stall body angle).
The plugin doesn't expose these as user-config fields. v1 derivation:

- `fAlphaStall := fSTALLWARNAOA × 1.075` (StallWarn is ~93% of stall)
- `fAlpha0 := -2.0` (degrees; conservative GA-airframe approximation
  per CLAUDE.md's body-angle convention discussion)

These produce a percent-lift bar that's *roughly* right for a
generic GA airframe. Issue #392 (X-Plane stall-AOA derivation) will
replace this with `acf_max_aoa_no_flap`-based derivation. v1 ships
with these constants documented as approximations.

This is a deliberate v1 simplification — *not* the historical
hardcoded-zero alpha_0 bug. The plugin is reading wing AOA (X-Plane's
`sim/flightmodel/position/alpha`), so the percent-lift fraction is
referenced to a wing-AOA-equivalent stall, not a body-angle-equivalent
one. The two conventions are linearly related; the constants above
are tuned to give a sensible percent-lift bar for the indexer.

## main.cpp edits (the bulldog correction)

The bulldog correctly identified that `main.cpp` calls
`gdraw.deleteSprite()` at end of every `loop()` and recreates at
8bpp at start. Two ways to handle this:

**Option A (chosen)**: small `#ifdef XPLANE_PLUGIN_BUILD` blocks at
the relevant call sites:

1. `main.cpp:272` — `gdraw.setColorDepth(8)` →
   `gdraw.setColorDepth(XPLANE_PLUGIN_BUILD ? 16 : 8)`. Use 16bpp so
   readback is RGB565 (matches what our `Panel_PluginCanvas` natively
   handles).

2. Same one-liner at the other 2 `setColorDepth(8)` sites outside
   `#if defined(ESP_PLATFORM)` blocks (`main.cpp:460` BtnB cycle
   path, `main.cpp:484` graphics-update path).  The two ESP-only
   sites at lines 293 and 356 are inside firmware-upgrade Wi-Fi
   flows that the X-Plane build doesn't reach — no edit needed.

3. `main.cpp:708` — `gdraw.deleteSprite();` does NOT need editing.
   The data has already flowed through `pushSprite()` into our
   custom panel by the time deleteSprite runs. The custom panel owns
   the persistent buffer; gdraw's destruction doesn't touch it.

4. Possible: `M5.begin(cfg)` at the top of `setup()` may need to be
   commented out under the same ifdef if it overrides our setPanel.
   Verify by reading M5Unified.cpp's begin() body.

This is **3-4 single-token edits** to main.cpp, gated on the X-Plane
build flag. No M5 firmware behavior changes for any other consumer
(M5Stack Basic, M5Stack Core2, huVVer, native-sim).

**Option B (rejected)**: write a wrapper `IndexerLoop()` that
duplicates the body of `loop()`. Larger surface to maintain in sync
with main.cpp; tighter coupling for no real gain.

## Sprite-readback race

The bulldog flagged: between `pushSprite()` and `deleteSprite()`,
`gdraw`'s buffer is valid but the X-Plane draw callback could read it
from a different thread.

With the custom-panel approach, this race **doesn't exist**. The
X-Plane draw callback reads from `Panel_PluginCanvas`'s persistent
buffer, never from `gdraw`. The persistent buffer is updated in
`writeBlock`/`writePixels` (called by gdraw's `pushSprite`). Locking
inside the custom panel's writes (and reads from `CopyToRGBA8888`)
guarantees the X-Plane draw callback always sees a consistent frame —
worst case, one frame stale.

Lock granularity: per-row mutex would be a nightmare. Lock once around
the whole frame: `pushSprite` is one big call from M5GFX's
perspective; the panel's `writeBlock` is called many times during one
push. Cleanest is for the X-Plane draw callback to take the mutex,
copy out, release; the panel's writes don't need a lock as long as
we double-buffer (M5 writes to Buffer A; on `flush()` (we override it
to no-op or to "swap"), Buffer A becomes Buffer B; X-Plane reads
Buffer B always). Implementation note for the implementing agent:
start with single-buffer + mutex; switch to double-buffer only if a
visible flicker shows up.

## OpenGL state

The X-Plane SDK requires plugin draw callbacks to restore GL state
they touched. Concrete in-callback contract:

```cpp
// Use legacy GL push/pop so we restore *all* state we touch in one shot.
// X-Plane's plugin GL context is fixed-function; glPushAttrib is supported.
glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT |
             GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);

// Tell X-Plane which state we're using.  Per SDK docs, this
// configures texture units, lighting, blending; we then bind our own
// texture inside that state.
XPLMSetGraphicsState(
  /*fog=*/        0,
  /*tex=*/        1,
  /*lighting=*/   0,
  /*alpha_test=*/ 0,
  /*alpha_blend=*/ 1,
  /*depth_read=*/ 0,
  /*depth_write=*/0);

glBindTexture(GL_TEXTURE_2D, m_texture);
glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

// Quad spanning the X-Plane window's pixel rect.
glBegin(GL_QUADS);
  glTexCoord2f(0,0); glVertex2i(left,  bottom);
  glTexCoord2f(1,0); glVertex2i(right, bottom);
  glTexCoord2f(1,1); glVertex2i(right, top);
  glTexCoord2f(0,1); glVertex2i(left,  top);
glEnd();

glPopAttrib();   // restores blend, blendfunc, texture binding, color, etc.
```

`glColor4f(1,1,1,1)` is X-Plane's default; restoring it explicitly is
unnecessary but cheap. We don't touch matrix mode (`XPLMSetGraphicsState`
doesn't promise a matrix; X-Plane window draws use window-coordinate
`glVertex2i` calls which work in pixel space against the current
projection — pre-set by X-Plane for window draws).

## Globals + initialization-order audit

Per bulldog demand #4. file-scope globals in main.cpp:

- `M5Canvas gdraw(&M5.Display)` — constructed before main(); requires
  `M5.Display` to exist as an LGFX_Device. M5Unified provides it as a
  default-constructed object until `M5.begin()` configures it. Safe.
- `WebServer server(80)` — Arduino types. Already shimmed in
  `sim/ArduinoShim.h` via stub class for non-ESP builds. Safe (the
  stub is empty).
- `bool fwUpdateMode = false` — POD, no constructor. Safe.
- `int PctAnchors[8]` — POD. Safe.
- M5GFX `M5GFX M5Display` — internal to M5Unified, default-constructed
  with no hardware access. Safe.

`M5.begin()` is called in `setup()` (line 251 area). On non-ESP it
calls our custom `_setup_pinmap` (we provide a stub that does
nothing) and then sets up `M5.Display` to point at our
`Panel_PluginCanvas`. We need to look at how the WASM/native sim
configures the panel today to mirror that path. Per
`sim/SimMain.cpp:74-80`, the pattern is:

```cpp
lgfx::Panel_sdl::setup();   // we replace with our own panel setup
setup();                     // M5 firmware setup
// enter loop
```

We replicate this with `Panel_PluginCanvas::install()` (a static
helper that swaps `M5.Display`'s panel pointer to our instance) called
*before* `setup()` runs.

### Concrete `M5.begin()` replacement

The bulldog asked for the exact 3-line snippet so the implementer
doesn't rediscover it.  In `M5Indexer::Init()`, before calling the
M5 firmware's `setup()`:

```cpp
// Bypass M5.begin() entirely — its board-detect, power, SD, IMU,
// button paths are all irrelevant in the X-Plane plugin context
// and reading panel registers would crash without an SPI bus.
//
// Instead, point M5.Display at our software panel and init it.
static Panel_PluginCanvas s_panel;
M5.Display.setPanel(&s_panel);
M5.Display.init();                 // calls Panel_FrameBufferBase::init
M5.Display.setColorDepth(16);      // RGB565
M5.Display.setRotation(0);         // 320×240 landscape
```

The M5 firmware's `setup()` then proceeds; its eventual `M5.begin(cfg)`
call is a no-op (or overwrites our setPanel — verify by reading
M5Unified.cpp's begin() for the non-board-detect path).  If
`M5.begin()` re-points the panel, **comment out the `M5.begin(cfg)`
line in `main.cpp` under the `XPLANE_PLUGIN_BUILD` ifdef** — adds one
more main.cpp edit, simple to verify.

## NO-DATA gating

Per bulldog demand #14: `serialDataFresh()` returns true if a frame
arrived in the last 300 ms (`SerialRead.cpp:68-73`). The X-Plane
plugin injects bytes synchronously *before* calling `loop()` each
tick, so by the time `loop()` runs, `serialMillis` was just updated.
The freshness check passes on the first tick after `Init()`.

The only edge case: if the plugin's flight-loop callback gates on
something that prevents byte injection (e.g., aircraft not loaded
yet), `loop()` runs without fresh data and the M5 renderer draws the
red NO DATA overlay. v1 acceptable behavior — X-Plane shows NO DATA
until aircraft loads, then shows live indexer.

## Mode cycling

Five buttons in the X-Plane indexer window (or menu items if window
buttons get cluttered):

- "Mode 0: Indexer + nums"
- "Mode 1: Attitude"
- "Mode 2: Narrow AOA"
- "Mode 3: Decel"
- "Mode 4: G history"

Each clicks sets `displayType = N` directly (it's an extern int in
main.cpp; we declare it `extern` in our wrapper). v1 doesn't simulate
the BtnB hardware-button path — direct write is simpler and avoids
the M5Unified button-state machine.

Window state (open/closed) and selected mode persist via the existing
per-aircraft `.prf` from PR #394 — two new fields:
`indexerWindowOpen` (bool) and `indexerMode` (int 0-4).

## XPluginStart sequencing

1. Existing audio init (unchanged, in `init_sound` deferred callback).
2. `M5Indexer::Init()` — also deferred to `init_sound` (relies on
   `XPLMGetSystemPath` being valid, which we've confirmed in the
   POSIX-paths fix).
   1. `SDL_Init(SDL_INIT_TIMER)` — no video subsystem.
   2. `Panel_PluginCanvas::install()` — swaps M5.Display's panel.
   3. Call M5 firmware's `setup()`. This sets up `gdraw`, fonts, etc.
   4. Patch `setColorDepth(8)` → 16 via the `XPLANE_PLUGIN_BUILD`
      gate.
   5. Create XPLM window (initially hidden if `indexerWindowOpen`
      from the .prf is false).
   6. Generate GL texture name; allocate scratch RGBA buffer.
3. Existing flight-loop callback also calls `M5Indexer::Tick()` after
   the audio-decision block.

## Pilot-facing accuracy disclosure

The bulldog correctly noted that the alpha_0/alpha_stall constants
ship as approximations until issue #392 lands.  Render an
"APPROX. CALIBRATE BEFORE FLIGHT" overlay in red text in the corner
of the indexer window so the pilot can't mistake the display for a
calibrated installation.  Removed by the issue #392 PR.

## Per-PR scope

This PR ships:
- The custom panel.
- The M5 firmware run inside the plugin.
- Indexer window with 320×240 GL blit.
- All five modes (driven by direct `displayType` writes from menu).
- Per-aircraft persistence of window-open + mode.

This PR does NOT ship:
- USB serial output to a real M5 (next PR; the plugin already has
  bytes ready, just needs a `cu.usbserial-*` writer).
- Configurable window size (fixed 320×240).
- huVVer-AVI portrait rotation (next PR if needed).
- Issue #392 (X-Plane stall-AOA derivation) — defaults stand.
- Issue #393 (per-flap-detent setpoints) — single derived flapCfg.

## Build complexity / .xpl size impact

Bulldog flagged this. Honest answer:

- Current .xpl: ~292 KB.
- Adding M5GFX + M5Unified + tinyxml2 + GaugeWidgets + M5 firmware
  sources: ~70 .cpp files, all dead-stripped at link time on macOS
  with `-Wl,-dead_strip` (Apple linker dead-strips by default for
  symbols not exported and not reachable from exported symbols).
- Estimated post-PR .xpl size: 1.5–2.5 MB. Within the 30 MB X-Plane
  plugin limit.

`-fvisibility=hidden` is enabled by default on macOS for shared
libraries built with our CMake config. We export only `XPluginStart`
/ `XPluginStop` / etc. via the `PLUGIN_API` macro from the X-Plane
SDK, which already sets visibility = default for those.

SDL2: linked via the system framework on macOS (no shipping needed —
SDL2 framework lives in `/Library/Frameworks/SDL2.framework` on most
dev installs). For redistributable builds we'd need to bundle, but
that's out of scope for v1 (this is a dev/personal-use feature
shipping in source form).

For Linux/Windows CI: defer those builds to a follow-up. v1 macOS-only
acceptable.

## Risks remaining (after the rewrite)

1. **`M5.begin()` on non-ESP path**: still need to verify it doesn't
   try to read panel registers via SPI. Worst case: don't call
   `M5.begin()` at all; manually init `M5.Display` to point at our
   panel and skip the rest. The M5 firmware's `setup()` calls
   `M5.begin(cfg)` early — if we strip that to direct panel init, we
   skip a subset of M5Unified's button-state machine. Buttons aren't
   used in the X-Plane plugin (we drive `displayType` directly), so
   safe.

2. **`M5.update()` per loop iteration**: same concern. If
   `M5.update()` only polls the buttons we're not using, it's a no-op
   on our path. If it touches IMU / mic / SD / NVS, those have shims
   already (per `sim/ArduinoShim.h`). We test by running and watching
   for crashes; if `M5.update()` crashes, we patch with a stub.

3. **`SDL_INIT_TIMER` standalone behavior on macOS**: SDL's timer
   subsystem typically pulls in the event subsystem implicitly. If
   that opens an OS event loop that conflicts with X-Plane's event
   loop, we'll see weird input behavior. Mitigation: SDL has hints
   (`SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR`, etc.) — most
   relevant: we don't init `SDL_INIT_VIDEO`, so the video subsystem
   stays off and no SDL window is created.

4. **Threading**: X-Plane's flight-loop and draw callbacks both run
   on the main thread. The M5 firmware's `loop()` is called from
   the flight-loop callback, single-threaded. SDL's timer subsystem
   spawns a background thread for `SDL_AddTimer`, but `SDL_GetTicks`
   (which is what `lgfx::millis` calls) is lock-free monotonic-counter
   read, not threaded.

5. **`pushSprite()` cost**: 320×240×2 bytes = 153 KB per frame copy.
   At 30 Hz that's 4.6 MB/s. Trivial.

## Estimated implementation time

- Step 1 (CMakeLists + glob): 30 min.
- Step 2 (`Panel_PluginCanvas` + verify M5 firmware compiles in plugin
  context): 90 min.
- Step 3 (XPLM window + GL texture + static-pattern blit verifies the
  GL plumbing): 45 min.
- Step 4 (Wire `setup()`+`loop()` calls; verify renderer initializes
  without crashing): 90 min — the riskiest step, contains the
  M5.begin verification.
- Step 5 (DataRef adapter + BuildDisplayFrame + InjectSerialByte
  pipeline): 60 min.
- Step 6 (Mode cycling via menu): 30 min.
- Step 7 (.prf persistence): 15 min.

Total: ~6 hours of focused implementation. If step 4 hits a wall, the
fallback is option 1's dropped path: include `Panel_sdl` and let it
open a hidden SDL window. SDL has `SDL_HINT_RENDER_DRIVER` and we can
position the window at -10000,-10000 if `SDL_WINDOW_HIDDEN` doesn't
work on Metal. The fallback adds ~1 hour, doesn't change the rest of
the architecture.

## Test strategy

- **Plugin loads without crashing**: load X-Plane with the new .xpl.
  Window stays hidden by default.
- **Window opens, shows static test pattern**: enable indexer window
  via menu before wiring M5 firmware. Verifies GL pipeline.
- **Window shows indexer with synthetic data**: hard-code
  `DisplayBuildInputs` with the same RV-10-full-flaps demo values as
  `SerialRead.cpp:198-220`. Verifies the M5 renderer + custom panel.
- **Window shows indexer driven by datarefs**: full pipeline. Fly the
  RV-10 in X-Plane, watch the indexer respond.
- **Mode cycling works**: menu items switch all five modes.
- **Persistence**: close X-Plane, reopen, indexer window should be in
  the same state.
- **Audio still works**: existing audio path unchanged. Audio test
  suite still passes (138 native tests).

## Out-of-scope / followups

1. USB serial output to a real M5 (the natural next PR).
2. Per-detent setpoints (issue #393).
3. X-Plane stall-AOA derivation (issue #392).
4. Linux/Windows CI for the indexer (audio CI already covers those).
5. huVVer-AVI portrait mode in the X-Plane window.
6. Configurable window size (pixel-doubled or scaled).
