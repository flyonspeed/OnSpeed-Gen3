// ReplayStubs.cpp — link-time satisfiers for the M5 firmware → WASM
// "replay" target.
//
// M5GFX.cpp's desktop-build path always references `lgfx::v1::Panel_sdl`
// (autodetect path → `auto p = new Panel_sdl();`) and the SDL platform
// is the only one that defines `lgfx::v1::gpio_hi/lo/pinMode`. The
// replay target deliberately doesn't link SDL2, so those references go
// unresolved — even though every call site is dead at run time
// (`M5.begin()` is never called for replay; the autodetect branch is
// unreachable).
//
// This TU provides minimum-viable stubs to satisfy the linker:
//   * `Panel_sdl::Panel_sdl()`, `setWindowTitle`, `setScaling` — empty.
//     Constructor runs only via `new Panel_sdl()` inside autodetect,
//     which is never reached for replay.
//   * `gpio_hi`, `gpio_lo`, `pinMode` — no-ops. Panel_Device.cpp's ctor
//     wiring runs only if a panel is bound, which it never is here.
//
// All stubs keep the same mangled name (no extern "C") because
// wasm-ld matches Itanium ABI mangled names. Class-member stubs need
// to live inside `lgfx::v1::Panel_sdl`, which means we re-declare a
// minimal version of the class here. The minimal declaration mirrors
// the public surface that M5GFX.cpp invokes — nothing more — and is
// guarded with `#ifdef REPLAY_TARGET` so it cannot accidentally
// participate in any other build configuration.

#ifndef REPLAY_TARGET
#error "ReplayStubs.cpp must only be compiled for the replay target"
#endif

#include <cstdint>

namespace lgfx { inline namespace v1 {

    // pin_mode_t is declared in lgfx/v1/platforms/common.hpp; minimal
    // local re-declaration so the function signature matches without
    // pulling that header (which on the desktop path drags in SDL).
    enum pin_mode_t : uint8_t;

    // Panel_sdl forward + stub. Real declaration in
    // platforms/sdl/Panel_sdl.hpp gates the body on `SDL_h_`; without
    // SDL we get an incomplete-class link error otherwise. Since every
    // site that calls these is provably dead in the replay build (see
    // file header), the bodies are empty.
    struct Panel_sdl {
        Panel_sdl();
        void setWindowTitle(const char*);
        void setScaling(uint8_t, uint8_t);
    };

    Panel_sdl::Panel_sdl() {}
    void Panel_sdl::setWindowTitle(const char*) {}
    void Panel_sdl::setScaling(uint8_t, uint8_t) {}

    void gpio_hi(uint32_t)               {}
    void gpio_lo(uint32_t)               {}
    bool gpio_in(uint32_t)               { return false; }
    void pinMode(int, pin_mode_t)        {}

} } // namespace lgfx::v1
