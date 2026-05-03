// IndexerWindow.cpp — see IndexerWindow.h.

#include "IndexerWindow.h"
#include "Panel_PluginCanvas.h"
#include "DataRefAdapter.h"

// X-Plane SDK
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"

// SDL2 — needed because lgfx/v1/platforms/sdl/common.cpp's
// millis()/micros()/delay() are gated #if defined(SDL_h_), and the M5
// firmware calls millis() ~10 times per loop().  We init only the
// subsystems we need (timer; SDL_Init(0) gives us the time helpers
// for free on all platforms but we ask for TIMER explicitly to be safe).
// SDL header path varies: SDL2/SDL.h on Debian/Ubuntu, SDL.h direct
// on Homebrew.  CMake adds the right directory to the include path.
#include <SDL.h>

// OpenGL.  X-Plane plugin GL context is fixed-function.
#if defined(__APPLE__)
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

// M5GFX
#include <M5Unified.h>

// onspeed_core — wire-frame builder.
#include <proto/DisplaySerial.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Externs from the M5 firmware that we drive directly.  These are
// file-scope globals in software/OnSpeed-M5-Display/src/main.cpp.
extern void setup();                  // M5 firmware entry
extern void loop();                   // M5 firmware per-tick
extern int16_t displayType;           // 0..4 mode selector

// From software/OnSpeed-M5-Display/src/SerialRead.cpp — pipes a single
// byte through the display-serial framing accumulator.  The M5 firmware
// already uses this entry point in -DSIM_LIVE WASM builds.
extern void InjectSerialByte(char inChar);

namespace onspeed_xplane::indexer {

namespace {

// ------------------------------------------------------------------
// Engine state
// ------------------------------------------------------------------
bool                          s_initOk     = false;
bool                          s_visible    = false;
Panel_PluginCanvas*           s_panel      = nullptr;
XPLMWindowID                  s_window     = nullptr;
GLuint                        s_texture    = 0;
std::vector<std::uint32_t>    s_rgbaScratch;   // kWidth*kHeight pixels

// X-Plane window dimensions.  Native M5 panel is 320×240 — render
// 1:1 by default; could pixel-double in a follow-up.
constexpr int kWindowWidth  = 320;
constexpr int kWindowHeight = 240;

// ------------------------------------------------------------------
// X-Plane window callbacks
// ------------------------------------------------------------------
void DrawWindow(XPLMWindowID, void*)
{
    if (!s_panel || s_texture == 0) return;

    // Always upload the latest frame.  The dirty flag is a future
    // optimization — for now, upload every draw to guarantee no
    // stale frames after a mode change.
    s_panel->CopyToRGBA8888(s_rgbaScratch.data());

    // Save GL state.  X-Plane's plugin GL context is fixed-function
    // (legacy), so glPushAttrib works.
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT |
                 GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);

    XPLMSetGraphicsState(
        /*fog=*/        0,
        /*tex=*/        1,
        /*lighting=*/   0,
        /*alpha_test=*/ 0,
        /*alpha_blend=*/1,
        /*depth_read=*/ 0,
        /*depth_write=*/0);

    glBindTexture(GL_TEXTURE_2D, s_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0,
                    Panel_PluginCanvas::kWidth,
                    Panel_PluginCanvas::kHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    s_rgbaScratch.data());

    int left, top, right, bottom;
    XPLMGetWindowGeometry(s_window, &left, &top, &right, &bottom);

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(left,  bottom);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(right, bottom);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(right, top);
        glTexCoord2f(0.0f, 0.0f); glVertex2i(left,  top);
    glEnd();

    glPopAttrib();
}

int  HandleClick(XPLMWindowID, int, int, XPLMMouseStatus, void*) { return 1; }
void HandleKey(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}
XPLMCursorStatus HandleCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }
int  HandleWheel(XPLMWindowID, int, int, int, int, void*) { return 1; }

// ------------------------------------------------------------------
// One-time setup helpers
// ------------------------------------------------------------------

bool InstallPanelAndRunSetup()
{
    // SDL provides the time helpers lgfx common.cpp uses.  No video
    // subsystem (no window).  SDL_INIT_TIMER is safe everywhere.
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "FlyOnSpeed: SDL_Init failed: %s\n", SDL_GetError());
        XPLMDebugString(buf);
        return false;
    }

    // Install our software panel BEFORE calling the M5 firmware's
    // setup().  M5.Display.setPanel() rebinds the LGFX_Device's panel
    // pointer; we then call init() to allocate our framebuffer + lines
    // table.
    s_panel = new Panel_PluginCanvas();
    M5.Display.setPanel(s_panel);
    M5.Display.init();
    M5.Display.setColorDepth(16);
    M5.Display.setRotation(0);

    // Run the M5 firmware's setup().  It draws a splash, configures
    // gdraw, sets fonts, etc.  On the non-ESP path it does not call
    // the WiFi stack or NVS (those are #ifdef ESP_PLATFORM).
    setup();

    // Force initial mode.  setup() may set displayType to 0; that's
    // fine but we make it explicit so a future setup() change can't
    // surprise us.
    displayType = 0;

    return true;
}

bool CreateXPlaneWindow()
{
    // Generate the GL texture once, sized for the native M5 panel
    // (320×240 RGBA8).  Allocates GPU memory but doesn't upload data
    // until the first draw.
    glGenTextures(1, &s_texture);
    glBindTexture(GL_TEXTURE_2D, s_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 Panel_PluginCanvas::kWidth, Panel_PluginCanvas::kHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    s_rgbaScratch.assign(
        static_cast<std::size_t>(Panel_PluginCanvas::kWidth) *
        Panel_PluginCanvas::kHeight, 0u);

    XPLMCreateWindow_t params = {};
    params.structSize             = sizeof(params);
    params.left                   = 100;
    params.top                    = 600;
    params.right                  = 100 + kWindowWidth;
    params.bottom                 = 600 - kWindowHeight;
    params.visible                = 0;       // start hidden; menu toggles
    params.drawWindowFunc         = DrawWindow;
    params.handleMouseClickFunc   = HandleClick;
    params.handleRightClickFunc   = HandleClick;
    params.handleKeyFunc          = HandleKey;
    params.handleCursorFunc       = HandleCursor;
    params.handleMouseWheelFunc   = HandleWheel;
    params.refcon                 = nullptr;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    params.layer                  = xplm_WindowLayerFloatingWindows;
    s_window = XPLMCreateWindowEx(&params);
    if (!s_window) return false;

    XPLMSetWindowTitle(s_window, "OnSpeed Indexer");
    return true;
}

}  // namespace

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

bool Init()
{
    if (s_initOk) return true;

    if (!InstallPanelAndRunSetup()) return false;
    if (!CreateXPlaneWindow())      return false;

    s_initOk = true;
    XPLMDebugString("FlyOnSpeed: M5 indexer initialized\n");
    return true;
}

void Tick()
{
    if (!s_initOk) return;

    // Build display-serial frame from current datarefs.
    onspeed::proto::DisplayBuildInputs in =
        onspeed_xplane::indexer::BuildInputsFromDatarefs();

    std::uint8_t frameBytes[onspeed::proto::kDisplayFrameSizeBytes] = {0};
    const std::size_t emitted = onspeed::proto::BuildDisplayFrame(
        in, frameBytes, sizeof(frameBytes));
    if (emitted == 0) {
        // Build failure (clamping bug / bad inputs).  Skip this tick.
        return;
    }

    // Pipe through the M5 firmware's parser one byte at a time.
    // Updates the M5-side global state (Pitch, Roll, IAS, PercentLift, …).
    for (std::size_t i = 0; i < emitted; ++i) {
        InjectSerialByte(static_cast<char>(frameBytes[i]));
    }

    // Tick the M5 renderer.  Draws into gdraw → pushSprite() → our
    // panel's writePixels → m_framebuffer.
    loop();
}

bool IsVisible()
{
    return s_visible && s_window && XPLMGetWindowIsVisible(s_window);
}

void Show()
{
    if (!s_initOk || !s_window) return;
    XPLMSetWindowIsVisible(s_window, 1);
    s_visible = true;
}

void Hide()
{
    if (!s_window) return;
    XPLMSetWindowIsVisible(s_window, 0);
    s_visible = false;
}

void SetMode(int mode)
{
    if (mode < 0) mode = 0;
    if (mode > 4) mode = 4;
    displayType = static_cast<int16_t>(mode);
}

int GetMode()
{
    return static_cast<int>(displayType);
}

void Shutdown()
{
    if (s_window) {
        XPLMDestroyWindow(s_window);
        s_window = nullptr;
    }
    if (s_texture) {
        glDeleteTextures(1, &s_texture);
        s_texture = 0;
    }
    s_rgbaScratch.clear();
    s_rgbaScratch.shrink_to_fit();

    if (s_panel) {
        // Don't delete s_panel — M5.Display still holds the pointer.
        // Letting the OS reclaim on plugin unload is safe; X-Plane
        // shutdown frees the whole address space.  Avoids ordering
        // issues with M5.Display destruction.
        s_panel = nullptr;
    }

    SDL_Quit();
    s_initOk = false;
}

}  // namespace onspeed_xplane::indexer
