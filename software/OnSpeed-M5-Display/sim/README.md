# OnSpeed M5 Display — Desktop + Browser Simulator

This directory hosts two things that aren't part of the on-device
firmware but use the same source code:

1. **Native (macOS / Linux) simulator** — runs the real M5Stack Basic
   rendering code against an SDL2 window on your desktop. No physical
   M5 hardware required.
2. **WebAssembly (browser) simulator** — same firmware, compiled to
   WASM + HTML5 canvas via Emscripten. Loads in any modern browser.

Both drive the display from a synthetic data source built into the
firmware (`DUMMY_SERIAL_DATA` — a ramping AOA that sweeps the whole
tone map once per cycle, plus plausible in-flight constants for
everything else). The rendering code in `../src/main.cpp` and
`../src/SerialRead.cpp` is shared with the ESP32 firmware verbatim;
only the I/O layer is swapped.

Replaying real flight logs from the SD card is a follow-up — the
parser path is already factored behind `InjectSerialByte()` in
`SerialRead.cpp`, so adding a CSV-driven source is a straightforward
extension.

---

## Prerequisites

### macOS (Apple Silicon or Intel)
```bash
brew install sdl2             # for the native build
brew install emscripten       # for the WASM build
```

### Linux (Ubuntu / Debian)
```bash
sudo apt install libsdl2-dev  # for the native build
# Emscripten: follow https://emscripten.org/docs/getting_started/downloads.html
```

---

## Native build

```bash
cd software/OnSpeed-M5-Display
pio run -e native
./.pio/build/native/program
```

An SDL window opens showing the M5Stack Basic bezel around a 320×240
display. The firmware renders exactly what it would on a real device,
driven by the synthetic ramp.

Keyboard mapping (inherited from `Panel_sdl`'s default keycode map):

| Key           | Button   | Action                     |
|---------------|----------|----------------------------|
| ←  (Left)     | BtnA     | Brightness down            |
| ↓  (Down)     | BtnB     | Cycle display mode         |
| →  (Right)    | BtnC     | Brightness up              |

Display modes (cycled with BtnB): Energy Display → Attitude →
Indexer → Decel Display → Historic G.

---

## WASM build

Requires the native build to have been run once first (the script
reuses `M5Unified` and `M5GFX` from `.pio/libdeps/native/`):

```bash
cd software/OnSpeed-M5-Display
pio run -e native            # one-time, installs M5 libs
./sim/build_wasm.sh
python3 -m http.server --directory sim/build/wasm 8080
# Open http://localhost:8080/
```

Output artifacts in `sim/build/wasm/`:

| File         | Purpose                                        |
|--------------|------------------------------------------------|
| `index.html` | Shell page with canvas + keyboard instructions |
| `index.js`   | Emscripten runtime glue                        |
| `index.wasm` | Compiled firmware                              |

Gzipped over-the-wire size is approximately 500 KB.

The browser build uses `emscripten_set_main_loop` to drive the SDL
panel tick cooperatively with the JS event loop, so the canvas
updates at ~60 Hz without blocking.

---

## How it works

The M5GFX library ships a first-party SDL panel backend
(`lgfx::Panel_sdl`) that swaps in for the real ILI9341 driver under
preprocessor guards. When `ESP_PLATFORM` is undefined, the platform
abstraction layer picks up `Panel_sdl` and Arduino-like
`millis`/`micros`/GPIO shims (`sim/ArduinoShim.h`). The same
firmware binary compiles for ESP32 and for desktop/WASM.

The simulator's entry point (`sim/SimMain.cpp`) replaces the
Arduino `setup()`/`loop()` boilerplate with:

- **Native:** `Panel_sdl::main(user_func, 16)` which spawns the
  display firmware on an SDL thread and drives the window from the
  main thread.
- **WASM:** `emscripten_set_main_loop` around a single-threaded tick
  that calls `loop()` then `Panel_sdl::loop()` — no pthreads needed,
  yields cleanly to the browser.

`Panel_sdl::addKeyCodeMapping(SDLK_…, GPIO_NUM_…)` binds keyboard
keys to simulated GPIO reads, so the firmware's existing
`M5.BtnA.wasPressed()` code paths work unchanged — no `#ifdef SIM`
branches in render code.
