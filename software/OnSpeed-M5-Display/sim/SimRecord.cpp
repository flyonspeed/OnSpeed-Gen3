// SimRecord.cpp — headless recording entry point for the M5 sim.
//
// Replaces SimMain.cpp when -DSIM_RECORD is defined.  Reads OnSpeed `#1`
// wire frames from stdin (74 bytes each), feeds them through the same
// SerialRead.cpp parser the device uses, runs one loop() per frame to
// render, and dumps the panel pixels into a single raw RGB24 file (one
// frame after another, no headers).
//
// Pipe input on stdin; output goes to a file so SDL's various writes to
// stdout (initialization banner, OpenGL extension info, etc.) don't
// contaminate the pixel stream.
//
//   sim_record /path/to/frames.rgb < frames.bin
//   ffmpeg -f rawvideo -pixel_format rgb24 -video_size 320x240 \
//          -framerate 50 -i frames.rgb out.mp4
//
// Why this exists:
//   - The default SimMain.cpp opens a window and runs interactively, paced
//     by Panel_sdl's 60 Hz event loop. Recording video deterministically
//     needs single-stepped frames, no human-in-the-loop, no real-time pacing.
//   - SerialRead.cpp's `InjectSerialByte` is the same entry point both
//     paths use, so the renderer code in main.cpp is unchanged.
//
// Usage:
//   pio run -e native_record
//   ./.pio/build/native_record/program out/frames.rgb < frames.bin
//
// First positional argument is the output path; if omitted, falls back
// to ./frames.rgb. Logs go to stderr.

#include "ArduinoShim.h"
#include <M5Unified.h>

#if defined(SDL_h_)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

// From the firmware:
void setup(void);
void loop(void);
extern void InjectSerialByte(char inChar);

// Display dimensions are fixed for M5Stack Basic / native sim.
static constexpr int kPanelW = 320;
static constexpr int kPanelH = 240;
static constexpr int kFrameSize = 76;       // OnSpeed #1 wire frame size (v4.22+)

static const char* g_out_path = "frames.rgb";

static int run_record(bool* /*running*/)
{
    FILE* out = std::fopen(g_out_path, "wb");
    if (!out) {
        std::fprintf(stderr, "[sim_record] failed to open %s for write\n", g_out_path);
        std::_Exit(2);
    }

    setup();

    static uint8_t  frame_buf[kFrameSize];
    static uint8_t  rgb_buf[kPanelW * kPanelH * 3];
    static uint16_t rgb565_buf[kPanelW * kPanelH];

    int frames_in  = 0;
    int frames_out = 0;

    for (;;) {
        size_t n = std::fread(frame_buf, 1, kFrameSize, stdin);
        if (n == 0) break;             // EOF
        if (n < kFrameSize) {
            std::fprintf(stderr, "[sim_record] short read at frame %d (got %zu bytes)\n",
                         frames_in, n);
            break;
        }
        ++frames_in;

        for (int i = 0; i < kFrameSize; ++i) {
            InjectSerialByte(static_cast<char>(frame_buf[i]));
        }

        // Pace the loop at the protocol's 50 Hz so millis()-gated update
        // intervals inside main.cpp's loop() (graphics refresh,
        // numbers refresh, G-history sample) fire on schedule. Without
        // this, all 500 frames would run within tens of milliseconds and
        // those gates would block the render path from updating.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // One render iteration with the freshly-injected frame data.
        // pushSprite() inside loop() writes into the panel's framebuffer
        // synchronously; we don't need to drain Panel_sdl::loop() here
        // (and on macOS we can't — Cocoa requires the SDL pump on the
        // main thread, and run_record() is the user thread spawned by
        // Panel_sdl::main()).
        loop();

        // Read panel pixels as RGB565 then expand to RGB24.  Panel_sdl
        // delivers bytes in the byte-swapped 565 layout (high byte first),
        // so expand from the byte pair manually instead of trusting the
        // platform's uint16_t endianness.
        M5.Display.readRect(0, 0, kPanelW, kPanelH, rgb565_buf);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(rgb565_buf);
        for (int i = 0; i < kPanelW * kPanelH; ++i) {
            // swap565: [hi byte, lo byte] = [RRRRRGGG, GGGBBBBB]
            const uint8_t hi = p[2*i + 0];
            const uint8_t lo = p[2*i + 1];
            const uint8_t r5 = (hi >> 3) & 0x1F;
            const uint8_t g6 = ((hi & 0x07) << 3) | ((lo >> 5) & 0x07);
            const uint8_t b5 = lo & 0x1F;
            rgb_buf[3*i + 0] = (r5 << 3) | (r5 >> 2);
            rgb_buf[3*i + 1] = (g6 << 2) | (g6 >> 4);
            rgb_buf[3*i + 2] = (b5 << 3) | (b5 >> 2);
        }
        std::fwrite(rgb_buf, 1, sizeof(rgb_buf), out);
        ++frames_out;
    }

    std::fflush(out);
    std::fclose(out);
    std::fprintf(stderr, "[sim_record] frames in=%d out=%d → %s\n",
                 frames_in, frames_out, g_out_path);

    // Tear down the whole process — we're inside a worker thread spawned
    // by Panel_sdl::main(). Returning from this thread leaves the main
    // thread spinning on SDL events forever; explicit _exit() short-
    // circuits that and lets ffmpeg / our orchestrator continue.
    std::fflush(stderr);
    std::_Exit(0);
}

int main(int argc, char** argv)
{
    if (argc > 1) g_out_path = argv[1];

    // Reuse Panel_sdl::main, which spawns an SDL thread for our user
    // function and drives the window from the main thread.  The window
    // briefly appears during recording — harmless; readRect captures
    // from the panel surface, not from the screen.
    return lgfx::Panel_sdl::main(run_record, /*delay_ms=*/0);
}

#else
int main(int, char**)
{
    std::fprintf(stderr, "SDL2 support not compiled in. Check build_flags.\n");
    return 1;
}
#endif
