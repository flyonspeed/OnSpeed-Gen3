// SimMain.cpp — desktop (SDL) + WASM (Emscripten) entry point.
//
// M5GFX ships a first-party SDL panel backend that emulates the
// M5Stack Basic display end-to-end (320×240 panel, optional bezel
// frame, keycode-to-GPIO mapping so `M5.BtnA.wasPressed()` still
// works against keyboard keys). This file wires that backend to the
// M5 display's existing `setup()` / `loop()`. Build with
// -DDUMMY_SERIAL_DATA and the display firmware drives itself off a
// synthetic data source — a ramping AOA that sweeps the whole tone
// map once per cycle — so the sim produces visible output with no
// external data.
//
// Keyboard mapping inherited from lgfx::Panel_sdl::setup():
//   Left   -> GPIO 39 = M5.BtnA (brightness down)
//   Down   -> GPIO 38 = M5.BtnB (cycles display mode)
//   Right  -> GPIO 37 = M5.BtnC (brightness up)
//   Up     -> GPIO 36 = M5.BtnPWR (unused by the OnSpeed display)

#include <M5GFX.h>

#if defined(SDL_h_)

#include <cstdio>
#include <cstdlib>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include "ArduinoShim.h"

void setup(void);
void loop(void);

#if defined(__EMSCRIPTEN__)

// Browser main loop: single-threaded. Panel_sdl's native `main()` entry
// spawns an SDL thread for the user function while the main thread
// drives SDL events and rendering. Emscripten without pthreads has no
// threads; blocking the JS thread in a while-loop would freeze the
// canvas. Instead we drive setup/per-frame work from
// emscripten_set_main_loop, which yields to the browser between ticks.
static bool g_sim_ready = false;

static void emsc_tick(void)
{
    if (!g_sim_ready)
        {
        setup();
        g_sim_ready = true;
        return;
        }

    loop();
    // Pump the SDL→canvas pipeline so pushSprite() output shows up.
    lgfx::Panel_sdl::loop();
}

int main(int /*argc*/, char** /*argv*/)
{
    if (0 != lgfx::Panel_sdl::setup())
        {
        std::fprintf(stderr, "[sim] Panel_sdl::setup() failed\n");
        return 1;
        }
    // fps=0 lets the browser pick (requestAnimationFrame ~60 Hz);
    // simulate_infinite_loop=1 keeps `main` from returning, which
    // would otherwise tear down the runtime.
    emscripten_set_main_loop(emsc_tick, 0, 1);
    return 0;
}

#else

static int user_func(bool* running)
{
    setup();
    do
        {
        loop();
        }
    while (*running);
    return 0;
}

int main(int /*argc*/, char** /*argv*/)
{
    // 16 ms ≈ 60 Hz, a reasonable ceiling for a 20 Hz data stream.
    return lgfx::Panel_sdl::main(user_func, 16);
}

#endif

#else
// SDL not linked in — this TU is a no-op and the project fails to
// link, which is the right behavior: the native env always brings SDL2.
int main(int, char**)
{
    std::fprintf(stderr, "SDL2 support not compiled in. Check build_flags.\n");
    return 1;
}
#endif
