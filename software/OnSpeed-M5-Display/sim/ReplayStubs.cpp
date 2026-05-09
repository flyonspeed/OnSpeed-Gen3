// ReplayStubs.cpp — link-time satisfiers for the M5 firmware → WASM
// "replay" target.
//
// M5GFX.cpp's desktop-build path always references `lgfx::v1::Panel_sdl`
// (autodetect path → `auto p = new Panel_sdl();`) and the SDL platform
// is the only one that defines `lgfx::v1::gpio_hi/lo/pinMode`. The
// replay target excludes the `platforms/sdl/*` source files from the
// link (so SDL's runtime is never pulled in) but Emscripten still puts
// the SDL2 headers on the include path, so M5GFX.cpp sees the full
// `Panel_sdl` class definition and emits unresolved references to its
// constructor, destructor, vtable, and a handful of methods called via
// `p->`.
//
// All call sites are dead at runtime: `M5.begin()` is gated under
// `#ifdef REPLAY_TARGET` in `src/main.cpp::setup()`, so autodetect is
// never reached for the replay build. But the linker still needs the
// symbols to satisfy unresolved references — without them, link fails.
//
// This TU provides minimum-viable stubs by including the real
// Panel_sdl header (so the Panel_sdl class definition matches M5GFX.cpp's
// view exactly — no ODR violation) and supplying empty bodies for the
// methods the linker complains about. Adding bodies the linker doesn't
// ask for is bloat, so the list is exactly what the link errors named.

#ifndef REPLAY_TARGET
#error "ReplayStubs.cpp must only be compiled for the replay target"
#endif

// SDL2 headers are on the include path under Emscripten without
// `-sUSE_SDL=2` (the port flag controls library linking, not header
// availability). M5GFX's `lgfx/v1/platforms/sdl/common.hpp` triggers
// on `__has_include(<SDL2/SDL.h>)` and defines `SDL_h_` itself via
// `<SDL.h>`'s own header guard, which then unhides the full
// `Panel_sdl` class declaration in `Panel_sdl.hpp`.
#include "lgfx/v1/platforms/sdl/Panel_sdl.hpp"

namespace lgfx { inline namespace v1 {

    // Constructor / destructor: M5GFX.cpp's `new Panel_sdl()` and the
    // owning `unique_ptr<Panel_Device>::reset()` cleanup both reference
    // these. Empty bodies suffice because the autodetect path is dead.
    Panel_sdl::Panel_sdl() {}
    Panel_sdl::~Panel_sdl() {}

    // M5GFX.cpp's autodetect path calls these directly via `p->`.
    void Panel_sdl::setWindowTitle(const char* /*title*/) {}
    void Panel_sdl::setScaling(uint_fast8_t /*x*/, uint_fast8_t /*y*/) {}
    void Panel_sdl::setFrameImage(const void* /*img*/, int /*w*/, int /*h*/,
                                  int /*ix*/, int /*iy*/) {}

    // Virtual overrides declared in the header — bodies are required to
    // anchor the vtable. The compiler emits the vtable for Panel_sdl
    // when it sees the constructor body (the vtable is keyed off the
    // first non-inline virtual definition), so the linker pulls in
    // every slot's referenced symbol. Empty bodies suffice because the
    // autodetect path is dead.
    bool Panel_sdl::init(bool) { return false; }
    color_depth_t Panel_sdl::setColorDepth(color_depth_t depth) { return depth; }
    void Panel_sdl::display(uint_fast16_t, uint_fast16_t,
                            uint_fast16_t, uint_fast16_t) {}
    void Panel_sdl::drawPixelPreclipped(uint_fast16_t, uint_fast16_t,
                                        uint32_t) {}
    void Panel_sdl::writeFillRectPreclipped(uint_fast16_t, uint_fast16_t,
                                            uint_fast16_t, uint_fast16_t,
                                            uint32_t) {}
    void Panel_sdl::writeBlock(uint32_t, uint32_t) {}
    void Panel_sdl::writeImage(uint_fast16_t, uint_fast16_t,
                               uint_fast16_t, uint_fast16_t,
                               pixelcopy_t*, bool) {}
    void Panel_sdl::writeImageARGB(uint_fast16_t, uint_fast16_t,
                                   uint_fast16_t, uint_fast16_t,
                                   pixelcopy_t*) {}
    void Panel_sdl::writePixels(pixelcopy_t*, uint32_t, bool) {}
    uint_fast8_t Panel_sdl::getTouchRaw(touch_point_t*, uint_fast8_t) { return 0; }

    // Class-static `_keymod` is consulted by `setShortcutKeymod`, which
    // is defined inline in the header. The static itself needs storage.
    SDL_Keymod Panel_sdl::_keymod = KMOD_NONE;

    // Platform-shim symbols used by Panel_Device.cpp's ctor wiring.
    // Not strictly Panel_sdl methods, but `platforms/sdl/common.cpp` is
    // the file that would have provided them on a normal SDL build, and
    // we excluded that file at the build-script level. The wiring runs
    // only if a panel is bound, which it never is in the replay build.
    void gpio_hi(uint32_t)               {}
    void gpio_lo(uint32_t)               {}
    bool gpio_in(uint32_t)               { return false; }
    void pinMode(int, pin_mode_t)        {}

} } // namespace lgfx::v1
