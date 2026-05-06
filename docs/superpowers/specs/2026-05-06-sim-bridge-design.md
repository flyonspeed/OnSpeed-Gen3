# OnSpeed Sim Bridge — Design

**Status:** Approved, pre-implementation.
**Author:** sam@frogrocketai.com
**Date:** 2026-05-06.
**Tracking issue:** linked from PR description once branch is pushed.

## Problem

Today, OnSpeed integrates with X-Plane 12 via `software/OnSpeed-XPlane-Plugin/`,
which embeds the M5 indexer firmware in a floating X-Plane window and plays
audio cues from `onspeed_core::audio` driven by X-Plane datarefs.

We have no equivalent for **Microsoft Flight Simulator (2020/2024), Lockheed
Martin Prepar3D, FSX, or Redbird trainers**. These three families of sims all
expose flight state through a single API — Microsoft's **SimConnect** — that
P3D inherits from FSX and that Redbird uses internally. So "MSFS + P3D + FSX +
Redbird" collapses to a single integration target.

## Goal

Build **OnSpeed Sim Bridge**, a small Windows executable that:

1. Connects to a running SimConnect server (MSFS / P3D / FSX / Redbird) and
   subscribes to AOA, IAS, lateral G, on-ground, and pause state at 20 Hz.
2. Drives the same `onspeed_core::audio` engine the X-Plane plugin and Gen3
   firmware already use, playing OnSpeed audio cues through the host PC's
   default audio output.
3. Hosts the M5 indexer firmware in an SDL2 window, identical to the
   `OnSpeed-M5-Display` native simulator and the X-Plane plugin's embedded
   indexer.
4. Optionally streams `#1` display-serial frames over USB to a tethered
   physical M5Stack / huVVer-AVI panel-mount instrument.

The product runs as a single double-clickable `.exe` with no installer, no
admin rights, no driver, no service.

## Non-goals (v1)

- CSV log replay (defer to v2; `tools/m5-replay/replay.py` already covers
  log-driven validation against a physical M5).
- Auto-update notifications.
- Authenticode code signing.
- Auto-derived calibration setpoints from sim datarefs (v2; cf. plugin
  issue #392).
- Per-flap-detent setpoints in the prefs UI (v2; cf. plugin issue #393).
- A Mac-native production build. Mac is supported as a development host
  (mock + CSV sources only); production audio + SimConnect ship Windows-only.

## Architecture

### High-level shape

The bridge is a single Windows executable with four internal subsystems
wired by a 20 Hz tick loop:

```
   ┌──────────────────────────────────────────────────────────────────┐
   │                     OnSpeed-Sim-Bridge.exe                       │
   │                                                                  │
   │  ┌──────────────┐   ┌──────────────────┐   ┌────────────────┐    │
   │  │ Data source  │──▶│ onspeed_core     │──▶│ Audio output   │────┼─▶ Speakers
   │  │ (pluggable)  │   │ ::audio engine   │   │ (WASAPI)       │    │
   │  └──────┬───────┘   └──────────────────┘   └────────────────┘    │
   │         │                    │                                   │
   │         │                    ▼                                   │
   │         │           ┌──────────────────┐                         │
   │         │           │ proto::Build     │                         │
   │         │           │ DisplayFrame()   │                         │
   │         │           └────────┬─────────┘                         │
   │         │                    │                                   │
   │         │           ┌────────┴────────────────┐                  │
   │         │           ▼                         ▼                  │
   │         │  ┌──────────────────┐    ┌──────────────────┐          │
   │         │  │ Indexer window   │    │ USB-serial out   │──────────┼─▶ Tethered M5
   │         │  │ (SDL + Panel_sdl)│    │ (optional)       │          │
   │         │  └──────────────────┘    └──────────────────┘          │
   │         │                                                        │
   │         ▼                                                        │
   │  Sources: SimConnect | Mock                                      │
   └──────────────────────────────────────────────────────────────────┘
```

### Pluggable data sources

Three sources, one interface (`ISimDataSource::BuildInputs(DisplayBuildInputs&)`):

- **`SimConnectSource`** — production. Connects to SimConnect, subscribes to
  the variable list below at 20 Hz. Windows-only (`#ifdef _WIN32`).
- **`MockSource`** — built-in synthetic AOA ramp. The same 30-second
  0→99→0 sweep against fixed RV-10 full-flaps calibration constants currently
  living behind the M5 firmware's `DUMMY_SERIAL_DATA` build flag. Used for:
  development on Mac (where SimConnect can't run), demos with no sim attached,
  and smoke tests in CI. Promoted into `onspeed_core::demo::SyntheticRamp` as
  Phase 0 of the implementation so all three hosts can use it (M5 firmware,
  X-Plane plugin, and bridge).
- **`CsvReplaySource`** — *deferred to v2.* Would replay OnSpeed SD-card CSV
  logs, mirroring `tools/onspeed_py/log_replay.py`. v1 ships with mock +
  SimConnect only.

All sources produce a `onspeed::proto::DisplayBuildInputs` per tick, the same
struct the X-Plane plugin's `DataRefAdapter::BuildInputsFromDatarefs()` produces
and that the firmware's `software/sketch_common/src/io/DisplaySerial.cpp`
populates from globals. This is the canonical input type already in
`onspeed_core`.

### One canonical wire encoder

`onspeed::proto::BuildDisplayFrame()` (in
`software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp`) is already
host-portable and pure. It takes `DisplayBuildInputs`, returns 77 bytes of
`#1` v4.23 wire format. The bridge calls it directly. **No new wire-encoder
code in the bridge.** The X-Plane plugin already does the same — see
`OnSpeed-XPlane-Plugin/src/m5_indexer/DataRefAdapter.cpp:115` and
`IndexerWindow.cpp:607`.

### Fan-out from the encoded frame

Each tick: source produces `DisplayBuildInputs` → `BuildDisplayFrame` produces
77 bytes → fan out to:

- **Audio engine** — fed from `DisplayBuildInputs` directly (not from the
  encoded frame), same path as the X-Plane plugin. Body angle, percent lift,
  and IAS-alive are read from the input struct on a mutex-guarded shared state.
- **Indexer window** — frame bytes are written byte-by-byte into the M5
  firmware's `InjectSerialByte()`, which parses the `#1` protocol and updates
  its render globals. M5 firmware then runs its `loop()` once, drawing into
  the Panel_sdl framebuffer that backs the SDL2 window.
- **USB-serial out (optional)** — frame bytes written to the user-selected
  COM port at 115200 8N1, driving a tethered physical M5.

### Threading model

```
Main thread (SDL event loop, 60 Hz):
  ├─ Pump SDL events (keyboard for Panel_sdl buttons, settings UI)
  ├─ Render Panel_sdl backbuffer to window
  └─ Service settings UI

Tick thread (20 Hz, hard-paced via SDL2 timer):
  ├─ source->BuildInputs(out)
  ├─ proto::BuildDisplayFrame(out, frameBuf)
  ├─ engine->UpdateInputs(out)        (mutex-guarded write)
  ├─ for each byte in frameBuf: M5fw::InjectSerialByte(byte)
  ├─ M5fw::loop()                     (renders to Panel_sdl framebuffer)
  └─ if serialOutPath: serial_port::Write(frameBuf, 77)

Audio thread (WASAPI render callback, ~10 ms slices):
  └─ engine->RenderSamples(out_buffer)   (mutex-guarded read)
```

This matches the X-Plane plugin's threading model, with three differences:

1. The bridge owns the SDL window, so we don't need the plugin's
   Apple-Silicon-GL contortions (`RenderTexturedQuadVA`, etc.). Panel_sdl
   draws directly to a window the bridge owns.
2. The plugin runs the M5 `loop()` on X-Plane's flight loop (variable rate);
   the bridge runs it on its own 20 Hz tick. M5 firmware tolerates either.
3. The plugin's `g_EngineMutex` pattern transfers verbatim to the bridge.

### Failure modes

| Failure                       | Behavior                                                        |
|-------------------------------|-----------------------------------------------------------------|
| SimConnect not running        | Indexer shows "Waiting for sim...". Audio muted. Retry every 2 s. |
| Sim paused                    | `paused` snapshot field true. Engine emits silence with envelope tail. Indexer shows last frame. |
| Sim alt-tabbed (no dispatch)  | No SimConnect dispatch in >200 ms → treated as paused.          |
| USB-serial unplugged          | 3 consecutive write failures → close fd. Auto-reopen every 2 s. |
| Audio device disconnected     | `AUDCLNT_E_DEVICE_INVALIDATED` caught, audio thread reinits on default device. |
| Aircraft change in sim        | Detected by `TITLE` change. Save current prefs, load new prefs. |

## SimConnect data mapping

Variable subscription, all at 20 Hz via `SIMCONNECT_PERIOD_SECOND` with
`dwInterval` set for 50 ms cadence:

| `DisplayBuildInputs` field    | X-Plane dataref (reference)                            | SimConnect variable                       |
|-------------------------------|--------------------------------------------------------|-------------------------------------------|
| `bodyAngleDeg`                | `sim/flightmodel/position/alpha`                       | `INCIDENCE ALPHA` (radians → deg)         |
| `ias` (kt)                    | `sim/flightmodel/position/indicated_airspeed`          | `AIRSPEED INDICATED` (knots)              |
| `lateralG` (body-frame, +R)   | `sim/flightmodel/forces/g_side`                        | `ACCELERATION BODY X` (ft/s² → G, signed) |
| `paused`                      | `sim/time/paused`                                      | `SIM PAUSED` (bool)                       |
| `onGround`                    | `sim/flightmodel/failures/onground_any`                | `SIM ON GROUND` (bool)                    |
| `verticalG`                   | `sim/flightmodel/forces/g_nrml`                        | `ACCELERATION BODY Y` (ft/s² → G)         |
| `pitchDeg`                    | `sim/flightmodel/position/theta`                       | `PLANE PITCH DEGREES` (radians → deg)     |
| `rollDeg`                     | `sim/flightmodel/position/phi`                         | `PLANE BANK DEGREES` (radians → deg)      |
| `palt` (ft)                   | `sim/flightmodel/misc/h_ind`                           | `INDICATED ALTITUDE` (ft)                 |
| `vsiKnots` (kt)               | `sim/flightmodel/position/vh_ind_fpm`                  | `VERTICAL SPEED` (ft/s → kt)              |
| `oat` (°F)                    | `sim/cockpit2/temperature/outside_air_temp_degc`       | `AMBIENT TEMPERATURE` (°C → °F)           |
| `flapPos` (deg)               | `sim/cockpit2/controls/flap_handle_deploy_ratio`...    | `FLAPS HANDLE INDEX` × per-aircraft map   |

**Sign convention for lateral G.** v4.23 wire format defined lateral G as
body-frame, positive right (PR #386, commit `3e5bc5a`). Both X-Plane's
`sim/flightmodel/forces/g_side` and SimConnect's `ACCELERATION BODY X` use
right-positive. We convert ft/s² to G (`/32.174`) and pass through. Unit test
on captured snapshot data catches sign errors.

**Aircraft identification.** SimConnect exposes `TITLE` (free-form, e.g.
"Cessna 172 Skyhawk") and `ATC MODEL` (ICAO, e.g. "C172"). Bridge prefers
`TITLE`, falls back to `ATC MODEL`, sanitizes via the same alphanumeric +
`_-` rule the X-Plane plugin uses (`sanitizeAcfBasename`).

## Code organization

New top-level project peer to existing X-Plane and M5 work:

```
software/
├── OnSpeed-Gen3-ESP32/        (firmware, unchanged)
├── OnSpeed-M5-Display/        (lightly extended in Phase 0)
├── OnSpeed-XPlane-Plugin/     (unchanged)
├── OnSpeed-Sim-Bridge/        ← NEW
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── CLAUDE.md
│   ├── src/
│   │   ├── main.cpp
│   │   ├── audio_wasapi.cpp/.h        (Windows)
│   │   ├── audio_coreaudio.cpp/.h     (Mac dev host only)
│   │   ├── indexer_window.cpp/.h
│   │   ├── serial_out.cpp/.h          (lifted from XPlane plugin)
│   │   ├── prefs.cpp/.h               (lifted from XPlane plugin)
│   │   ├── settings_window.cpp/.h
│   │   └── sources/
│   │       ├── ISimDataSource.h
│   │       ├── MockSource.cpp/.h
│   │       └── SimConnectSource.cpp/.h    (#ifdef _WIN32)
│   ├── tests/
│   └── SDK/SimConnect/        (vendored P3D/MSFS SDK)
└── Libraries/
    └── onspeed_core/          (unchanged — bridge links against it)
```

### What's reused without modification

- **`onspeed_core::audio`** — entire audio engine. Bridge wires its render
  output to WASAPI (Win) or CoreAudio (Mac dev). Zero changes.
- **`onspeed_core::aoa::PercentLift`** — body-angle → fraction math. Already
  the canonical implementation across firmware, plugin, M5, and JS LiveView.
- **`onspeed::proto::BuildDisplayFrame`** — 77-byte wire encoder.
- **`Panel_sdl`** (M5GFX) — SDL2 panel backend the M5 native sim already uses.
- **OnSpeed M5 firmware source** (`OnSpeed-M5-Display/src/main.cpp` +
  `SerialRead.cpp`) — globbed in via the same CMake `build_src_filter` magic
  the X-Plane plugin uses. The bridge becomes a fourth host for the M5
  firmware, alongside ESP32 hardware (Basic/Core2/huVVer), the X-Plane
  plugin, and the standalone SDL native sim.
- **`OnSpeed-XPlane-Plugin/src/serial_port.cpp`** — Windows COM-port writer.
  Was written but not previously exercised; bridge becomes its first real
  consumer.
- **Per-aircraft prefs** (`AOA-Tone-FlyOnSpeed-<acf>.prf` format) — same key=value
  format as the plugin. Aircraft identifier comes from SimConnect `TITLE`
  instead of X-Plane `.acf` filename.

### What's genuinely new

- `SimConnectSource.cpp` — opens SimConnect, subscribes, populates
  `DisplayBuildInputs`. Modeled on `DataRefAdapter.cpp`. ~150 LOC.
- `audio_wasapi.cpp` — IAudioClient/IAudioRenderClient render loop. Pattern
  mirrors plugin's `aoa_audio.cpp` OpenAL render thread.
- `audio_coreaudio.cpp` — Mac dev-host parity for the audio surface.
- `main.cpp` — tick loop, source dispatch, M5 firmware embedding glue. ~200 LOC.
- `settings_window.cpp` — SDL2 dialog, sliders + checkbox + serial-port picker.

### Build targets

| CMake target                   | Platform        | Sources active                                        |
|--------------------------------|-----------------|-------------------------------------------------------|
| `onspeed-sim-bridge` (default) | Windows x64     | All paths, SimConnect on, WASAPI                      |
| `onspeed-sim-bridge-mock`      | macOS arm64/x64 | Mock only, CoreAudio, no SimConnect                   |
| `onspeed-sim-bridge-tests`     | Mac + Win       | Unit tests, no audio/window                           |

The Mac mock build is the development loop: every iteration before SimConnect
integration runs there. Cross-compile to Windows from Mac via MinGW-w64 once
we want to test against P3D in Parallels — same toolchain the X-Plane plugin's
`win_x64` build uses.

### Strict warning posture

`-Wall -Wextra -Werror -Wshadow -Wformat=2 -Wunreachable-code -Wnull-dereference`
on bridge sources, matching the rest of the repo. SDK/M5GFX/M5Unified vendored
headers compiled with `-w` (warnings off). No new `-Wno-error=` exceptions
without fixing the underlying issue first.

## Deploy story

### Shape 1: Home sim, bridge on the same PC as MSFS / P3D / FSX

```
   ┌─────────────────────────────────────────────────────┐
   │                Windows PC                           │
   │  ┌──────────────┐         ┌──────────────────────┐  │
   │  │ MSFS / P3D   │◀──TCP──▶│ OnSpeed-Sim-Bridge   │  │
   │  │ (SimConnect  │  500    │ - WASAPI tones       │──┼──▶ PC speakers / BT headset
   │  │  server)     │         │ - SDL indexer window │  │
   │  └──────────────┘         │ - (opt) USB-serial   │──┼──▶ Tethered M5 (USB cable)
   │                           └──────────────────────┘  │
   └─────────────────────────────────────────────────────┘
```

Pilot downloads the zip, double-clicks the exe. SimConnect connects on
loopback automatically. Audio plays through the system default output.

### Shape 2: Networked dev box. MSFS on a beefy PC, bridge on a lighter machine.

Same exe, `--simconnect-host=192.168.1.50`. Requires `SimConnect.xml` on the
sim PC to enable remote connections (one config file edit, documented).

### Shape 3: Redbird / commercial trainer with a sealed sim PC

```
   ┌──────────────────────────┐         ┌─────────────────────────┐
   │ Redbird FMX / MX2        │         │ Instructor laptop       │
   │ (sealed cabinet, P3D     │  ◀LAN▶  │ ┌─────────────────────┐ │
   │  underneath, can't       │ TCP/500 │ │ OnSpeed-Sim-Bridge  │ │
   │  install software        │         │ │ - WASAPI tones      │─┼─▶ Cabinet speakers
   │  on the sim PC)          │         │ │ - Indexer window    │ │
   │                          │         │ │ - (opt) USB-M5      │─┼─▶ Tethered M5
   └──────────────────────────┘         │ └─────────────────────┘ │
                                        └─────────────────────────┘
```

Bridge runs on the instructor laptop. Tones play from the laptop's audio out.
Optional physical M5 plugged into the laptop. Sim PC untouched. Requires the
Redbird's `SimConnect.xml` to allow remote connections — set per-Redbird by
the operator. We document the procedure; we do not bypass it.

### What the user downloads

```
OnSpeed-Sim-Bridge-v1.0-win-x64.zip
├── OnSpeed-Sim-Bridge.exe          (single binary, ~5 MB + ~30 MB M5 firmware)
├── SimConnect.dll                  (vendored from P3D SDK, license-permitting)
├── SDL2.dll
├── README.txt
└── prefs/                          (empty; populated as user uses aircraft)
```

No installer, no admin rights, no service, no driver. Unzip and run.

### Documentation

`docs/site/docs/sim/` — peer to existing `docs/site/docs/xplane/`. Pages:
`index.md`, `install.md`, `msfs.md`, `p3d.md`, `fsx.md`, `redbird.md`,
`indexer.md`, `m5-tethered.md`, `settings.md`, `troubleshooting.md`. Most
of `indexer.md`, `m5-tethered.md`, and `troubleshooting.md` is copied from
the X-Plane plugin's docs and edited.

### Distribution

GitHub Releases attached to a tag. CI cross-compiles via MinGW from Linux
runners (cheaper than Windows runners), uploads zip on tag push. Same
mechanism as the X-Plane plugin's `.xpl` artifacts.

## Implementation phases

Each phase ends in a runnable, demoable artifact.

### Phase 0 — Lift synthetic ramp into core

Move `DUMMY_SERIAL_DATA` ramp out of `OnSpeed-M5-Display/src/SerialRead.cpp`
into `onspeed_core::demo::SyntheticRamp`. Returns `DisplayBuildInputs`
parameterized by elapsed-ms. M5 firmware's `DUMMY_SERIAL_DATA` path becomes
a one-liner. X-Plane plugin gains an optional "demo mode" toggle. Bridge
will use it as `MockSource`.

**Ships:** Plumbing PR. Three hosts gain a shared, testable demo source.

### Phase 1 — Bridge skeleton on Mac, mock-only, indexer window

Create `software/OnSpeed-Sim-Bridge/` with CMake build for macOS arm64.
Tick loop, mock source, `BuildDisplayFrame`, M5 firmware embedded via
`build_src_filter`, Panel_sdl indexer window. No audio. No SimConnect.

**Ships:** Working OnSpeed indexer on Mac, no sim, no audio. Sweeping ramp,
all 5 modes cycle on keyboard.

### Phase 2 — Audio output (WASAPI on Windows + CoreAudio on Mac)

Wire `onspeed_core::audio::Engine` to platform audio. Tick updates engine
inputs every 50 ms; audio thread renders samples on demand. Cross-platform
via `IAudioOutput` interface.

**Ships:** Mac mock build plays full OnSpeed tone progression as the ramp
sweeps. End-to-end audio works before SimConnect lands.

### Phase 3 — Cross-compile to Windows from Mac

MinGW-w64 toolchain file. Build SimConnect-less version
(`-DENABLE_SIMCONNECT=OFF`) that runs on Windows with mock source only.
Lift `serial_port.cpp` from X-Plane plugin. Verify USB-serial out to a
tethered M5 works.

**Ships:** Bridge running on Windows, mock-driven, tones playing, indexer
window, optional tethered-M5 path proven.

### Phase 4 — SimConnect source

Implement `SimConnectSource` modeled on `DataRefAdapter`. Vendor SimConnect
SDK headers + `SimConnect.lib`. Subscribe to variable list, populate
`DisplayBuildInputs`, handle pause and connection-lost.

**Ships:** Cessna in P3D, OnSpeed tones, indexer on screen. The product.

### Phase 5 — Settings UI + per-aircraft prefs

Settings window (volume, mute-IAS, audio enable, serial-port picker).
Per-aircraft `.prf` save/load on aircraft change. Calibration setpoint editor.
Default profiles for MSFS Cessna 172 and P3D default aircraft.

**Ships:** A pilot can install, dial in tones for their favorite sim
aircraft, and not redo it next session.

### Phase 6 — Polish, packaging, docs

CI: cross-compile Win-x64 zip on tag push, attach to release.
`docs/site/docs/sim/`. Bridge version in window title. Rotating debug log.
Top-level crash handler. README + CLAUDE.md for the project.

**Ships:** Tagged release on GitHub. Pilots can download.

### Pre-Phase-0: SimConnect spike

Before Phase 0, build a throwaway 100-line WIN32 console exe that opens
SimConnect, subscribes to alpha + IAS, prints values at 20 Hz to stdout.
Cross-compile from Mac via MinGW. Run in Parallels + P3D Developer License.

**Why:** Highest-leverage de-risking move in the plan. If SimConnect doesn't
work in Parallels for some reason (firewall, SDK version, license activation),
discovering it against a 100-line throwaway is a rounding error; discovering
it after Phase 4 is half a week of work down the drain. Spike is throwaway
but the artifact validates the SimConnect SDK path, P3D Developer license
activation, and Parallels TCP routing.

## Test strategy

- **Unit tests** (Mac CI): wire-encoder round-trips, SyntheticRamp golden
  values at known elapsed-ms, mock source ramp values, snapshot-to-engine
  plumbing, `.prf` round-trip, lateral-G sign convention.
- **Bench replay**: `tools/m5-replay/replay.py` and the bridge's MockSource
  share the SyntheticRamp module. Use existing `~/Dropbox/N720AK/OnSpeed Cals/`
  flights to A/B the bridge audio against firmware audio on the same data.
- **Integration test (manual, Windows + P3D)**: per-release checklist —
  slow flight, stall progression, aborted approach, configuration changes,
  aircraft swap. Mirrors the X-Plane plugin's manual test cycle.
- **Regression harness**: extend `tools/regression/host_main.cpp` with a
  SimConnect-snapshot-driven scenario so the bridge's pipeline is part of
  the goldens that catch core regressions.

## Risks

| Risk                                              | Mitigation                                                        |
|---------------------------------------------------|-------------------------------------------------------------------|
| SimConnect dispatch patterns are unintuitive      | Pre-Phase-0 spike validates surface before real code is written.  |
| WASAPI vs CoreAudio parity drift                  | Phase 2 includes a WAV-dump regression test on a fixed snapshot.  |
| MSFS sandboxes external clients differently from WASM-internal | Tested in Phase 4 against MSFS specifically, not just P3D.        |
| P3D SimConnect SDK license terms on `SimConnect.dll` redistribution | Verify per Lockheed Martin EULA before Phase 6 packaging.         |
| Redbird `SimConnect.xml` may not allow remote     | Documented as a per-Redbird operator config. Out of scope to bypass. |
| MSFS run elevated breaks external SimConnect      | Documented in `docs/site/docs/sim/msfs.md` install notes.         |

## Open questions

- None blocking. P3D Developer License purchase is the last external dependency.

## Decision log

- **Single binary owns audio + indexer + serial-out** (vs. split across
  processes). All three consumers key off the same per-tick state; IPC would
  add latency and debugging surface. Confirmed with user 2026-05-06.
- **Bridge as new top-level project** (vs. extending X-Plane plugin). Distinct
  host environment — SDL window, no GL hassle, different audio API, different
  sim API. Code reuse is at the `onspeed_core` library level, not the plugin
  level.
- **Defer CSV replay to v2.** The bridge's value is sim → tones, not log → tones.
  `tools/m5-replay/replay.py` already handles the latter.
- **Mac is a development host, not a production target.** SimConnect is
  Windows-only. CoreAudio support exists for the mock dev loop only.
- **`SyntheticRamp` extracted to `onspeed_core::demo`** so all three hosts
  (M5 firmware, X-Plane plugin, bridge) share one demo source. Side benefit:
  X-Plane plugin gains a runtime "demo mode" that doesn't require a recompile.
- **No `wire_encoder.cpp` in the bridge.** `onspeed::proto::BuildDisplayFrame`
  is already extracted into `onspeed_core` and the bridge calls it directly.
  (Earlier draft of this design wrongly proposed extracting it as a "PR 0";
  the extraction had already happened.)
