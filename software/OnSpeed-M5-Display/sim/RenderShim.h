// RenderShim.h — replay-target glue for the M5 firmware → WASM build.
//
// Brief: when REPLAY_TARGET is defined, the M5 firmware is being compiled
// to WebAssembly for use as a state-engine inside the JS replay tool.
// In that build:
//
//   1. SDL is NOT linked. The existing native / docs / live wasm targets
//      compile against `M5GFX/src/lgfx/v1/platforms/sdl/` (Panel_sdl +
//      common.cpp), which provides `lgfx::v1::millis()/micros()/delay()`
//      from the host wall clock. Replay needs JS-controlled virtual time
//      instead, so the SDL platform sources are excluded from the
//      compile and replacements are linked from `ReplayMain.cpp`.
//
//   2. M5GFX + M5Unified ARE linked. M5Canvas (LGFX_Sprite) renders into
//      an in-memory pixel buffer without touching any panel — every draw
//      call (`gdraw.fillSprite`, `gdraw.drawString`, `gdraw.fillCircle`)
//      writes to the sprite's malloc'd buffer. `gdraw.pushSprite()` is
//      the only path that would touch hardware; on a default-constructed
//      `M5GFX` (no panel attached) it's a software no-op against the
//      `Panel_NULL` default. Cheap, correct, no per-call-site stubbing
//      needed.
//
//   3. `M5.begin()` / `M5.update()` and the button checks (`M5.BtnB.
//      wasPressed()` etc.) — gated under `#ifdef REPLAY_TARGET` in the
//      sketch. JS drives `displayType` directly through
//      `replay_set_displayType()`, so the BtnB-cycling path is bypassed.
//
// Why not stub draw calls per-method via macro substitution? The plan
// budgeted 500 LOC for that. This approach is smaller (~50 LOC of
// guards across the sketch), keeps every render path identical to what
// flashes to the panel (no behavior drift between native firmware and
// replay sim), and the WASM bundle stays well under budget because
// M5GFX's font / sprite code paths the replay actually hits are small.
//
// The exported `g_replay_millis_us` is the single shared state between
// `ReplayMain.cpp` (reads it in the `lgfx::v1::millis/micros` overrides)
// and `replay_set_time(uint64_t)` (writes it from JS). uint64 to keep
// long replay sessions well clear of the 32-bit ms wraparound (~49.7
// days), even though firmware code reads only the low 32 bits via the
// Arduino `millis()` shim.

#pragma once

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#error "RenderShim.h is desktop-only and must not be included on ESP targets"
#endif

#ifndef REPLAY_TARGET
#error "RenderShim.h must only be included when REPLAY_TARGET is defined"
#endif

#include <cstdint>

// Virtual clock, in microseconds. Set from JS before each replay_loop()
// via `replay_set_time(milliseconds)` (which multiplies by 1000).
// Defined in ReplayMain.cpp.
extern uint64_t g_replay_millis_us;
