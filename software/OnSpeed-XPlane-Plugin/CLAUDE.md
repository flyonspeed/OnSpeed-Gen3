# OnSpeed X-Plane Plugin — Developer Notes

This file captures the hard-won lessons from building the plugin so
future agents (and humans) don't relive the debugging push.  See
also the repo-root `CLAUDE.md` for project-wide rules (worktrees,
git, doc style, body-angle convention).

## What ships in this directory

The plugin renders OnSpeed audio cues + the M5 indexer inside X-Plane
12.  Three independent surfaces:

1. **Audio engine** — same `Envelope`, `AudioMixer`, `AudioOrchestrator`,
   `Panning` code as the firmware via `onspeed_core::audio`.  Pumps
   OpenAL streaming buffers from a render thread.
2. **Embedded M5 indexer** — a floating X-Plane window hosts the M5
   firmware's renderer.  We link the actual `software/OnSpeed-M5-Display/`
   source into the plugin via `build_src_filter` glob magic and run
   the M5's `loop()` on the X-Plane flight loop.
3. **USB-serial output** — same display-serial wire frames also go
   out a configurable USB port, driving a physical M5Stack.

Per-aircraft settings live in
`Output/preferences/AOA-Tone-FlyOnSpeed-<acf>.prf`.

## Apple Silicon GL: the rules that aren't in the SDK docs

Plugin GL on M-series Metal-bridge has unwritten rules.  Violating
any of them produces silent no-ops — the GL call returns no error
but nothing reaches the screen.  After ~6 hours of debugging, the
working pattern is the one from imgui4xp
(<https://github.com/sparker256/imgui4xp/blob/master/src/ImgWindow/ImgWindow.cpp>):

- **Draw INSIDE the per-window callback.**  NOT a global
  `XPLMRegisterDrawCallback` phase.  X-Plane's matrices are set up
  to map window coords → clip space ONLY during the per-window
  callback.
- **Do NOT call `glOrtho` or `glLoadIdentity`.**  Just `glPushMatrix`
  the projection (untouched) and feed your vertex pixel coords
  directly.  Calling glOrtho overwrites the working setup with a
  broken one — you get nothing on screen.
- **Use GL 1.x client-side vertex arrays.**  `glDrawElements` with
  `glVertexPointer` / `glTexCoordPointer` / `glColorPointer`.
  Immediate-mode (`glBegin` / `glEnd`) and modern shader VBOs
  (`glDrawArrays` + `glCreateShader`) both silently no-op on the
  Metal bridge from a per-window callback.
- **`XPLMSetGraphicsState(0, 1, 0, 1, 1, 0, 0)`** — alpha test on,
  blend on, depth/fog/lighting off, 1 texture unit.
- **`glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT)`**
  + `glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS)` around the whole
  draw, with matching pops.
- **Use `XPLMGenerateTextureNumbers` + `XPLMBindTexture2d`**, NOT
  `glGenTextures` / `glBindTexture`.  X-Plane reserves IDs and
  caches binding state across plugins; raw GL texture calls confuse
  the bridge.

The canonical implementation is `RenderTexturedQuadVA` in
`src/m5_indexer/IndexerWindow.cpp`.  Don't deviate without testing.

## Threading model

Per X-Plane SDK: ALL plugin callbacks run on the main thread.  Flight
loop callbacks, draw callbacks, menu handlers — same thread, never
concurrent.

Background threads we own:

- **Audio render thread** (`AudioRenderThread` in `aoa_audio.cpp`).
  Pumps OpenAL queued buffers.  Holds `g_EngineMutex` while reading
  `g_Engine` state.  Never calls into M5GFX or X-Plane SDK.

The `Panel_PluginCanvas::m_mutex` exists for forward-compatibility
(if a future code path adds background readers) and to make the
data-race intent explicit under the C++ memory model.  In normal
operation it's uncontested.

Reference:
<https://developer.x-plane.com/2019/02/the-plugin-sdk-is-not-even-remotely-thread-safe/>.

## Window decoration

`xplm_WindowDecorationRoundRectangle` paints a solid bg fill behind
plugin draws.  Fine if your texture is fully opaque (alpha=255
throughout), which our 320×240 indexer is.  The chrome bg fill is
hidden under the texture, and we get titlebar / drag handle / close
button for free.

`xplm_WindowDecorationSelfDecorated` (no chrome) was the wrong call
during bring-up — losing the titlebar means losing the drag handle.

## M5 firmware integration via CMake

The plugin links the entire M5 firmware (`software/OnSpeed-M5-Display/src/main.cpp`
and dependencies + M5GFX + M5Unified) via CMake `build_src_filter`
glob magic.  Rules:

- The build is gated on `option(ENABLE_M5_INDEXER ...)` (defaults
  ON).  Off: the plugin is audio-only.
- `XPLANE_PLUGIN_BUILD=1` and `XPLANE_PLUGIN_DEPTH=16` are compile
  defines.  The M5 firmware uses these to gate ESP-only paths and
  to set `setColorDepth(XPLANE_PLUGIN_DEPTH)` in place of the
  hardware default of 8.
- `M5G_*`, `M5U_*`, `M5_FW_SOURCES` are GLOB_RECURSE'd.  Watch out:
  if M5 firmware adds new files under platform-specific paths
  (`/platforms/sdl/`, etc.), update the exclusion list.
- All vendored TUs are built with `-w` (warnings off).  The firmware
  passes our own `-Werror` posture but M5GFX and M5Unified do not.

When the M5 firmware adds globals, they get linked into the plugin's
address space.  Re-run a regression check after touching the M5 firmware.

## M5GFX framebuffer endianness

M5GFX writes RGB565 pixels **big-endian** in the framebuffer (high
byte first, to match SPI display wire order).  Host-side
`reinterpret_cast<uint16_t*>` reads them little-endian.  **Byte-swap
before unpacking** in `Panel_PluginCanvas::CopyToRGBA8888` or you
get the R/B-swap-via-bit-shuffling problem (saturated red looks
like magenta).

## M5Unified singleton trap

`M5.Display.init()` is a no-op when the M5Unified singleton is
already pointed at `Panel_NULL` (which it is at static-init time
unless you call `M5.begin()`).  If you rely on `M5.Display.init()`
to drive YOUR panel's `init()` indirectly, it never runs and your
framebuffer stays unallocated.

**Always call `s_panel->init(false)` directly** before
`M5.Display.setPanel(s_panel)`.  See `InstallPanelAndRunSetup` in
`IndexerWindow.cpp`.

The earlier "audio thread crashes when Show is clicked" bug was
heap corruption cascading from this uninitialized panel — the
panel's `std::mutex` member sat in heap that downstream allocations
clobbered.

## HWCDC vs HardwareSerial

The M5 firmware in this plugin's universe runs on three hardware
families:

- **M5Stack Basic / Core2**: original ESP32 + external CP2104 or
  CH9102F USB-to-UART bridge chip.  `Serial` is `HardwareSerial`
  (UART0).  No native USB-CDC.
- **M5Stack CoreS3 (hypothetical)**: ESP32-S3 + native USB-CDC.
  `Serial` is `HWCDC`.  Has `setTxTimeoutMs(0)`.
- **CP2104/CH9102F enumerate as `cu.usbserial-*` / `cu.SLAB_*` /
  `cu.wchusbserial*`** on macOS, NOT `cu.usbmodem*` (the latter is
  reserved for native USB-CDC).

`Serial.setTxTimeoutMs(0)` only exists on `HWCDC`.  Compile-gate any
call to it on `CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3`.

## USB-serial output design

The plugin can write display-serial frames to a USB port at 115200
8N1.  Pilot picks the port from `Plugins → Fly On Speed → Serial output`.

POSIX rules in `serial_port.cpp`:

- Open with `O_WRONLY | O_NOCTTY | O_NONBLOCK`.  KEEP `O_NONBLOCK` —
  blocking writes on a stalled USB-CDC device hang the flight-loop
  thread, which X-Plane kills the plugin for.
- `tcflush(fd, TCOFLUSH)` after `tcsetattr` to drop stale bytes from
  prior sessions.  Otherwise first-frame `write()` can EAGAIN.
- `write()` returns false on partial-write OR EAGAIN.  Caller should
  count consecutive failures (3 = ~150 ms) before closing — single
  EAGAIN is transient buffer pressure, not a hard error.
- Auto-reopen every 2 s while `s_serialOutPath` is set but port is
  closed.  Handles unplug-replug without pilot intervention.

The M5 firmware reads from `Serial` (USB-CDC or USB-bridged UART) when
`selectedPort=4`.  Detection logic in `software/OnSpeed-M5-Display/src/SerialRead.cpp`:

- Boot probe: `checkSerial()` only runs the 2-s USB probe if
  `Serial.available()` is true at start.  Otherwise an in-airplane
  M5 with no laptop connected pays 2 s of pointless boot delay.
- Late-binding: if any byte arrives on `Serial` post-detection,
  switch to port=4 only on a full `#1` two-byte signature, not just
  a single `#`.  A docked laptop emitting random USB-CDC noise can
  contain `#` (0x23) — a single-byte trigger would brick the M5
  in flight.
- **`selectedPort=4` MUST NOT persist to NVS.**  Sim-only mode;
  next boot must re-detect.  The `serialSetup()` save guard is
  `if (selectedPort!=0 && selectedPort!=4)`.

## Per-aircraft settings file

Path:
`Output/preferences/AOA-Tone-FlyOnSpeed-<acf>.prf`
where `<acf>` is the aircraft's `.acf` filename (no path, no
extension).  See `sanitizeAcfBasename` and `buildSettingsPath` in
`aoa_audio.cpp`.

Format: plain `key = value\n` lines.  Best-effort parser via
`sscanf` — unknown keys, malformed lines, missing files all silently
ignored.  Defensive trim trailing whitespace on string values
(hand-edited .prfs).

Keys persisted:
- `fLDMAXAOA`, `fONSPEEDFASTAOA`, `fONSPEEDSLOWAOA`, `fSTALLWARNAOA`
- `iMuteAudioUnderIAS`, `iMasterVolumePct`
- `iAoaMedianWindow`, `iAoaMeanWindow`
- `audioEnabled` (0/1)
- `bMuteStallHorn` (0/1) — when 1, plugin clears
  `sim/aircraft/view/acf_has_stallwarn` each flight loop so the sim's
  built-in stall horn doesn't talk over OnSpeed's audio.  Original
  `.acf`-defined value is captured on first apply per-aircraft and
  restored on toggle-off, so an aircraft that legitimately has no horn
  doesn't end up with one after a mute/unmute cycle.
- `serialPortPath` (string, may be empty)

The audio control window (Plugins → Fly On Speed → Show) edits all
of these except `serialPortPath` (which is set via the Serial output
submenu).

## Build + install

```bash
# Build
cd software/OnSpeed-XPlane-Plugin
cmake -B build -S . -DENABLE_M5_INDEXER=1
cmake --build build -j

# Install (macOS)
cp build/mac_x64/AOA-Tone-FlyOnSpeed.xpl \
   "/Users/sritchie/X-Plane 12/Resources/plugins/AOA-Tone-FlyOnSpeed/mac_x64/"

# Run X-Plane from a terminal (so env vars propagate)
"/Users/sritchie/X-Plane 12/X-Plane.app/Contents/MacOS/X-Plane"
```

For native unit tests, see `software/Libraries/onspeed_core/`.  The
plugin's tones come from the same `onspeed_core::audio` code that
`software/Libraries/onspeed_core/test/` exercises.

## Diagnostic env vars

Read once at first use, cached as `static const bool`.  Zero overhead
in production:

- `FLYONSPEED_INDEXER_FORCE_RED=1` — force the upload buffer red,
  bypass M5GFX content.  Sanity-check the GL pipeline.
- `FLYONSPEED_INDEXER_SKIP_INJECT=1` — skip InjectSerialByte loop.
- `FLYONSPEED_INDEXER_SKIP_LOOP=1` — skip the M5 firmware's loop().
- `FLYONSPEED_INDEXER_LOG_FIRST_FRAME=1` — hex-dump the first emitted
  serial frame to log.

The bring-up debug shaders (`FLYONSPEED_INDEXER_SOLID_SHADER` etc.)
were removed when the dead VBO+shader path was deleted.

## Cross-platform status

- **macOS arm64**: actively developed, verified working.
- **macOS x86_64**: should build but hasn't been verified since the
  Apple Silicon-specific GL workarounds landed.
- **Linux**: CMake is set up for it, no test pass yet.  Tracked in
  issue #396.
- **Windows**: same as Linux.  CreateFile/DCB serial path exists in
  `serial_port.cpp` but never run on a real Windows host.

## Surfaces NOT to touch without thinking

- `RenderTexturedQuadVA` setup sequence — every line matters on
  Apple Silicon.  Adding `glOrtho`, removing `glPushAttrib`, switching
  to `glDrawArrays` — any of these will silently produce a gray screen.
- `setTxTimeoutMs(0)` compile gate — must stay on the
  `CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3` test or
  the M5 firmware fails to build for ESP32 / Core2.
- M5 firmware late-binding logic — must require the full `#1`
  two-byte signature; relaxing to a single `#` reintroduces the
  in-flight bricking risk.
- NVS persistence guard — `selectedPort=4` must never persist.

## Useful issues / PRs

- PR #381 — onspeed_core audio extraction (foundation).
- PR #394 — spec-conformant audio rewrite for the plugin.
- PR #395 — embedded M5 indexer (the big one).
- PR #397 — USB-serial-to-physical-M5 + post-review hardening (this
  branch).
- #392 — derive AOA setpoints from X-Plane datarefs (per-aircraft
  auto-cal, future).
- #393 — per-flap-detent setpoints (future).
- #396 — verify Win/Linux indexer build.
