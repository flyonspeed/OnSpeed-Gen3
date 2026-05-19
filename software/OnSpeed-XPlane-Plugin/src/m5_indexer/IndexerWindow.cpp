// IndexerWindow.cpp — see IndexerWindow.h.

#include "IndexerWindow.h"
#include "Indexer3DPlacement.h"
#include "Panel_PluginCanvas.h"
#include "DataRefAdapter.h"
#include "../serial_port.h"

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

// OpenGL.  X-Plane on macOS Apple Silicon runs OpenGL 2.1 over a
// Metal bridge.  Immediate-mode draw (glBegin/glEnd/glVertex/glColor)
// is silently dropped on Metal — confirmed empirically: an
// XPLMDrawTranslucentDarkBox shows up in the same draw callback where
// glBegin/glEnd produces nothing.  Use VBO + shader instead.
#if defined(__APPLE__)
    #include <OpenGL/gl.h>
    #include <OpenGL/glext.h>
#else
    #include <GL/gl.h>
    #include <GL/glext.h>
#endif

// M5GFX
#include <M5Unified.h>

// onspeed_core — wire-frame builder.
#include <proto/DisplaySerial.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>

// Bring-up diagnostic toggle.  Set to non-zero to force the upload
// buffer red, regardless of M5 framebuffer contents — quick way to
// confirm the GL pipeline is reaching the screen during development.
static bool EnvFlag(const char* name)
{
    const char* v = std::getenv(name);
    return v && v[0] && v[0] != '0';
}

// Externs from the M5 firmware that we drive directly.  These are
// file-scope globals in software/OnSpeed-M5-Display/src/main.cpp.
extern void setup();                  // M5 firmware entry — NOT called from
                                      // the X-Plane plugin (blocks main thread
                                      // for 3s on splash screen, plus M5.begin
                                      // overrides our custom panel)
extern void loop();                   // M5 firmware per-tick
extern int16_t displayType;           // 0..4 mode selector
extern const char* const kModeNames[5]; // canonical page names (mirrors
                                        // tools/web/lib/pages/IndexerPage.js;
                                        // defined in M5 main.cpp)

// Globals from M5 firmware that the renderer expects to be initialized
// before the first loop() call.  We replicate the minimal subset of
// setup() that's safe in the X-Plane plugin context.
extern M5Canvas gdraw;
extern float gHistory[300];
extern int   gHistoryIndex;

// From software/OnSpeed-M5-Display/src/SerialRead.cpp — pipes a single
// byte through the display-serial framing accumulator.  The M5 firmware
// already uses this entry point in -DSIM_LIVE WASM builds.
extern void InjectSerialByte(char inChar);

// `sim/time/paused` dataref handle, looked up in XPluginStart.  Lives
// in aoa_audio.cpp alongside the audio-path datarefs.  Used by Tick to
// skip wire-frame emit during pause so the M5's gHistory buffer
// (advanced on `serialDataFresh()`) doesn't keep accumulating samples
// while the sim is frozen.
#include "XPLMDataAccess.h"
extern XPLMDataRef pausedDataRef;

namespace onspeed_xplane::indexer {

namespace {

// ------------------------------------------------------------------
// Engine state
// ------------------------------------------------------------------
bool                          s_initOk     = false;
bool                          s_visible    = false;
Panel_PluginCanvas*           s_panel      = nullptr;
XPLMWindowID                  s_window     = nullptr;
// Texture ID is allocated via XPLMGenerateTextureNumbers (NOT glGenTextures)
// and bound via XPLMBindTexture2d (NOT glBindTexture).  X-Plane reserves
// texture IDs for its own use, and on Apple Silicon's Metal-backed GL
// bridge raw glGenTextures/glBindTexture corrupt the driver's bridging
// tables in ways that take the whole process down on the next render.
// Per XPLMGraphics.h: "Use this routine instead of glGenTextures" /
// "Use this routine instead of glBindTexture(GL_TEXTURE_2D, ...)".
int                           s_textureId  = 0;
std::vector<std::uint32_t>    s_rgbaScratch;   // kWidth*kHeight pixels

// Optional USB-serial output to a physical M5Stack.  Empty path =
// closed.  Tick writes the same display-serial bytes it sends to the
// embedded indexer to this port too — pilot connects an M5 Core2 via
// USB-C and gets the same render on real hardware.
serial::SerialPort            s_serialOut;
std::string                   s_serialOutPath;
std::uint64_t                 s_serialErrCount = 0;

// Datarefs for mounted-mode projection.  Looked up lazily on first
// Tick where placementMode==kPlacementMounted3D, then cached.
XPLMDataRef s_drAcLocalX        = nullptr;
XPLMDataRef s_drAcLocalY        = nullptr;
XPLMDataRef s_drAcLocalZ        = nullptr;
XPLMDataRef s_drAcPitch         = nullptr;
XPLMDataRef s_drAcRoll          = nullptr;
XPLMDataRef s_drAcHeading       = nullptr;
XPLMDataRef s_drCamX            = nullptr;
XPLMDataRef s_drCamY            = nullptr;
XPLMDataRef s_drCamZ            = nullptr;
XPLMDataRef s_drCamPitch        = nullptr;
XPLMDataRef s_drCamRoll         = nullptr;
XPLMDataRef s_drCamHeading      = nullptr;
XPLMDataRef s_drFovDeg          = nullptr;
XPLMDataRef s_drViewType        = nullptr;
bool        s_mount3DRefsInit   = false;

// X-Plane window dimensions.  Native M5 panel is 320×240 — render
// 1:1 by default; could pixel-double in a follow-up.
constexpr int kWindowWidth  = 320;
constexpr int kWindowHeight = 240;

// Floor on resize.  X-Plane allows arbitrary user drag; below this
// the window is too small for the M5 framebuffer to show anything
// useful and we skip persistence to avoid getting stuck in a
// degenerate state.
constexpr int kMinWidth  = 160;
constexpr int kMinHeight = 120;

// Sanity bounds on persisted coords.  Any sane multi-monitor desktop
// fits within ±50000 px/boxels.  Anything bigger is junk (transient
// pop-out coordinate confusion seen on 2026-05-05, hand-edited file,
// etc.) and should be rejected on save.
constexpr int kSaneAbs    = 50000;
constexpr int kMinVisible = 80;     // px/boxels of window kept on-screen

// 4:3 aspect ratio of the M5 panel (320:240).  DrawWindow letterboxes
// the textured quad inside whatever window shape the user picks, so
// the indexer texture stays unstretched at any window size — non-4:3
// resizes get black bars on the long axis.
constexpr float kAspect = static_cast<float>(kWindowWidth) /
                          static_cast<float>(kWindowHeight);

// M5-style mode button row at the bottom of the window.  The real
// M5Stack has three hardware buttons (dim / select / bright) below
// the screen; only Select maps to a useful action in the X-Plane
// plugin (cycle display modes), so the button strip carries one
// labeled with the active page name centered in the strip.  Provides
// a visible click target — the existing whole-body click cycle still
// works, but the button gives a discoverable cue the way the real
// M5's hardware-button row does.
constexpr int kButtonStripHeight = 36;     // strip y-extent in boxels
constexpr int kButtonWidth       = 80;     // pill width
constexpr int kButtonHeight      = 24;     // pill height

// Compute the MODE button rect for a given window geometry.  Centered
// horizontally in the bottom strip.  Returns false if the window is
// too small to host both a usable indexer texture above and the
// button strip below (skip drawing + click-handling, use the full
// window for the texture).
bool ComputeButtonRect(int winLeft, int winTop, int winRight, int winBottom,
                       int* outL, int* outT, int* outR, int* outB)
{
    const int winW = winRight - winLeft;
    const int winH = winTop   - winBottom;
    if (winW < kButtonWidth + 8 || winH < kButtonStripHeight + kMinHeight)
        return false;
    const int stripCenterY = winBottom + kButtonStripHeight / 2;
    const int centerX      = (winLeft + winRight) / 2;
    *outL = centerX - kButtonWidth  / 2;
    *outR = centerX + kButtonWidth  / 2;
    *outT = stripCenterY + kButtonHeight / 2;
    *outB = stripCenterY - kButtonHeight / 2;
    return true;
}

// Persisted indexer state.  Default values are the same as the
// pre-sticky-persistence behavior so a fresh install (no .prf yet)
// puts the window where the old code did.
PersistedState s_persisted;

// Dirty flag, flipped by MarkDirtyIfChanged when the live window
// geometry diverges from s_persisted.  Polled by aoa_audio.cpp's
// periodic save callback (1 Hz), which flushes SaveSettings if set.
// Replaces the old per-frame fopen/write in DrawWindow that was
// hammering the .prf on every drag tick.
bool s_dirty = false;

// Drag state for mounted-mode click+drag placement.  In mounted
// mode the indexer has no chrome, so left-button mouse events on
// the window are interpreted as either a short tap (cycle mode)
// or a drag (move the body-frame anchor).  Aircraft + camera
// state are captured at MouseDown so the inverse-projection math
// stays consistent for the duration of the drag even if the
// pilot's view jitters slightly.
struct DragState {
    bool  active        = false;     // currently dragging?
    int   downX         = 0;         // mouse pos at MouseDown
    int   downY         = 0;
    int   offsetX       = 0;         // (downX - projectedCenterX)
    int   offsetY       = 0;
    float depthMeters   = 0.0f;      // camera-frame depth at click time
    onspeed_xplane::indexer::AircraftState ac{};   // captured at click time
    onspeed_xplane::indexer::CameraState   cam{};
    onspeed_xplane::indexer::ScreenDim     sd{};
};
DragState s_drag;

constexpr int kDragThreshold = 5;    // pixels of motion to enter drag

// ------------------------------------------------------------------
// Mounted-mode helpers
// ------------------------------------------------------------------

void EnsureMount3DRefs()
{
    if (s_mount3DRefsInit) return;
    s_drAcLocalX   = XPLMFindDataRef("sim/flightmodel/position/local_x");
    s_drAcLocalY   = XPLMFindDataRef("sim/flightmodel/position/local_y");
    s_drAcLocalZ   = XPLMFindDataRef("sim/flightmodel/position/local_z");
    s_drAcPitch    = XPLMFindDataRef("sim/flightmodel/position/theta");
    s_drAcRoll     = XPLMFindDataRef("sim/flightmodel/position/phi");
    s_drAcHeading  = XPLMFindDataRef("sim/flightmodel/position/psi");
    s_drCamX       = XPLMFindDataRef("sim/graphics/view/pilots_head_x");
    s_drCamY       = XPLMFindDataRef("sim/graphics/view/pilots_head_y");
    s_drCamZ       = XPLMFindDataRef("sim/graphics/view/pilots_head_z");
    s_drCamPitch   = XPLMFindDataRef("sim/graphics/view/pilots_head_the");
    s_drCamRoll    = XPLMFindDataRef("sim/graphics/view/pilots_head_phi");
    s_drCamHeading = XPLMFindDataRef("sim/graphics/view/pilots_head_psi");
    s_drFovDeg     = XPLMFindDataRef("sim/graphics/view/field_of_view_deg");
    s_drViewType   = XPLMFindDataRef("sim/graphics/view/view_type");
    s_mount3DRefsInit = true;
}

bool IsInCockpitView()
{
    if (!s_drViewType) return false;
    const int v = XPLMGetDatai(s_drViewType);
    // 1023 = forward_with_panel (2D cockpit); 1026 = forward 3D cockpit.
    return v == 1023 || v == 1026;
}

// Per-tick projection update for mounted mode.  Sets the existing
// X-Plane window's geometry to the projected screen rect and toggles
// visibility based on view type and whether the anchor is in front
// of the camera.  No-op if the window doesn't exist yet.
void UpdateMounted3DGeometry()
{
    if (!s_window) return;
    EnsureMount3DRefs();

    if (!IsInCockpitView()) {
        XPLMSetWindowIsVisible(s_window, 0);
        return;
    }

    onspeed_xplane::indexer::AircraftState ac{};
    ac.xWorld     = s_drAcLocalX   ? XPLMGetDataf(s_drAcLocalX)   : 0.0f;
    ac.yWorld     = s_drAcLocalY   ? XPLMGetDataf(s_drAcLocalY)   : 0.0f;
    ac.zWorld     = s_drAcLocalZ   ? XPLMGetDataf(s_drAcLocalZ)   : 0.0f;
    ac.pitchDeg   = s_drAcPitch    ? XPLMGetDataf(s_drAcPitch)    : 0.0f;
    ac.rollDeg    = s_drAcRoll     ? XPLMGetDataf(s_drAcRoll)     : 0.0f;
    ac.headingDeg = s_drAcHeading  ? XPLMGetDataf(s_drAcHeading)  : 0.0f;

    onspeed_xplane::indexer::CameraState cam{};
    cam.xWorld     = s_drCamX       ? XPLMGetDataf(s_drCamX)       : 0.0f;
    cam.yWorld     = s_drCamY       ? XPLMGetDataf(s_drCamY)       : 0.0f;
    cam.zWorld     = s_drCamZ       ? XPLMGetDataf(s_drCamZ)       : 0.0f;
    cam.pitchDeg   = s_drCamPitch   ? XPLMGetDataf(s_drCamPitch)   : 0.0f;
    cam.rollDeg    = s_drCamRoll    ? XPLMGetDataf(s_drCamRoll)    : 0.0f;
    cam.headingDeg = s_drCamHeading ? XPLMGetDataf(s_drCamHeading) : 0.0f;
    cam.fovDeg     = s_drFovDeg     ? XPLMGetDataf(s_drFovDeg)     : 70.0f;

    onspeed_xplane::indexer::Anchor3D anchor{
        s_persisted.mount3D_X,
        s_persisted.mount3D_Y,
        s_persisted.mount3D_Z};

    onspeed_xplane::indexer::ScreenDim sd{};
    XPLMGetScreenSize(&sd.wPx, &sd.hPx);

    const onspeed_xplane::indexer::ProjectedQuad pq =
        onspeed_xplane::indexer::ProjectAnchor(anchor, ac, cam, sd);
    if (!pq.visible) {
        XPLMSetWindowIsVisible(s_window, 0);
        return;
    }

    const int halfW = s_persisted.floatWidth  / 2;
    const int halfH = s_persisted.floatHeight / 2;
    const int cx    = static_cast<int>(pq.centerX);
    const int cy    = static_cast<int>(pq.centerY);

    XPLMSetWindowGeometry(s_window,
                          cx - halfW,         // left
                          cy + halfH,         // top   (Y-up in X-Plane)
                          cx + halfW,         // right
                          cy - halfH);        // bottom
    XPLMSetWindowIsVisible(s_window, 1);
}

// ------------------------------------------------------------------
// X-Plane window callbacks
// ------------------------------------------------------------------
//
// IMPORTANT: GL operations are only valid inside a draw callback,
// per X-Plane SDK rules — the GL context isn't bound when the menu
// handler runs.  All glGenTextures / glTexImage2D etc. happen here
// on first draw, gated by s_glReady.

void EnsureGLReady()
{
    if (s_textureId != 0) return;
    XPLMDebugString("FlyOnSpeed: indexer first draw — GL setup\n");
    XPLMGenerateTextureNumbers(&s_textureId, 1);
    XPLMBindTexture2d(s_textureId, /*unit=*/0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 Panel_PluginCanvas::kWidth, Panel_PluginCanvas::kHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    XPLMDebugString("FlyOnSpeed: indexer GL setup complete\n");
}

// Per-window draw callback.  Upload current panel framebuffer to the
// X-Plane GL texture, then render a textured quad covering the window.
// This is the same draw model imgui4xp uses (open-source ImGui-on-XPlane
// plugin) — drawing happens INSIDE this callback, not in a registered
// global draw phase.  X-Plane's matrices are set up to map window
// coordinates directly to clip space here, which is why our quad
// vertices in window coords work without any glOrtho.
void RenderTexturedQuadVA(int left, int top, int right, int bottom, int texId);

void DrawWindow(XPLMWindowID, void*)
{
    if (!s_initOk || !s_panel) return;

    EnsureGLReady();
    if (s_textureId == 0) return;

    s_panel->CopyToRGBA8888(s_rgbaScratch.data());

    // Cached: avoids a getenv() call on every draw (60+ Hz).  Same
    // pattern as kSkipInject / kSkipLoop in Tick.
    static const bool kForceRed = EnvFlag("FLYONSPEED_INDEXER_FORCE_RED");
    if (kForceRed) {
        const std::uint32_t red = 0xFF0000FFu;
        std::fill(s_rgbaScratch.begin(), s_rgbaScratch.end(), red);
    }

    // Upload framebuffer to texture.
    XPLMSetGraphicsState(0, 1, 0, 1, 1, 0, 0);
    XPLMBindTexture2d(s_textureId, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    Panel_PluginCanvas::kWidth,
                    Panel_PluginCanvas::kHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    s_rgbaScratch.data());

    int left, top, right, bottom;
    XPLMGetWindowGeometry(s_window, &left, &top, &right, &bottom);

    // Letterbox the M5 framebuffer at the panel's native 4:3 inside
    // the X-Plane window, ABOVE the bottom MODE-button strip.  User
    // can drag the window to any shape; the textured quad fits
    // centered at 4:3 with the chrome bg showing through as letterbox
    // bars.  Strip is dropped (texture uses full window) when the
    // window is too short to host both — see ComputeButtonRect.
    const int winW = right - left;
    const int winH = top   - bottom;

    int btnL = 0, btnT = 0, btnR = 0, btnB = 0;
    const bool buttonVisible = ComputeButtonRect(left, top, right, bottom,
                                                 &btnL, &btnT, &btnR, &btnB);
    const int textureBottom = buttonVisible ? bottom + kButtonStripHeight
                                            : bottom;
    const int textureH      = top - textureBottom;

    int quadW = winW;
    int quadH = static_cast<int>(static_cast<float>(quadW) / kAspect);
    if (quadH > textureH) {
        quadH = textureH;
        quadW = static_cast<int>(static_cast<float>(quadH) * kAspect);
    }
    const int offX  = (winW - quadW) / 2;
    const int offY  = (textureH - quadH) / 2;
    const int quadL = left + offX;
    const int quadR = quadL + quadW;
    const int quadT = top  - offY;
    const int quadB = quadT - quadH;
    RenderTexturedQuadVA(quadL, quadT, quadR, quadB, s_textureId);

    // M5-style MODE button at the bottom-center of the window.  The
    // dark translucent box matches the M5's plastic button face; the
    // label shows the current page name (Energy / Attitude / Indexer
    // / Decel / Historic G) so the pilot can see it change at a
    // glance.  Drawn AFTER the texture so it overlays
    // any letterbox area underneath.  Uses XPLMDrawTranslucentDarkBox
    // and XPLMDrawString which handle their own GL state safely on
    // Apple Silicon (raw GL primitives don't).
    if (buttonVisible) {
        XPLMDrawTranslucentDarkBox(btnL, btnT, btnR, btnB);
        const int idx = std::min(4, std::max(0, static_cast<int>(displayType)));
        const char* label = kModeNames[idx];
        const int labelLen = static_cast<int>(std::strlen(label));
        float white[3] = { 1.0f, 1.0f, 1.0f };

        // XPLMDrawString anchors the BASELINE of the first glyph at
        // (textX, textY) — not the lower-left of the bounding box.
        // The baseline sits roughly 1/4 of the font height above the
        // glyph cell bottom (descender region), so center vertically
        // by placing the baseline at:
        //
        //     btnB + (kButtonHeight - fontH) / 2 + descender
        //
        // XPLMGetFontDimensions reports total cell height; the SDK
        // doesn't separately expose the baseline offset, but a 1/4
        // bias approximates standard typography for sans-serif UI
        // fonts.
        int fontH = 10;
        XPLMGetFontDimensions(xplmFont_Proportional, nullptr, &fontH, nullptr);
        const int descender = std::max(2, fontH / 4);
        const int textY = btnB + (kButtonHeight - fontH) / 2 + descender;

        const int textW = static_cast<int>(std::round(
            XPLMMeasureString(xplmFont_Proportional, label, labelLen)));
        const int textX = (btnL + btnR) / 2 - textW / 2;

        XPLMDrawString(white, textX, textY, label, nullptr,
                       xplmFont_Proportional);
    }

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        s_loggedOnce = true;
        char eb[160];
        std::snprintf(eb, sizeof(eb),
                      "FlyOnSpeed: indexer first draw OK — geom L=%d T=%d R=%d B=%d tex=%d\n",
                      left, top, right, bottom, s_textureId);
        XPLMDebugString(eb);
    }
}

// Mouse handler.  In floating / pop-out mode, a MouseDown inside the
// MODE-button rect cycles the display mode (same behavior as before
// mounted mode existed).  In mounted mode the window has no chrome,
// so the handler runs a drag state machine: short tap cycles the
// mode; drag past kDragThreshold inverse-projects the new screen
// position back to a body-frame anchor and persists it.
int HandleClick(XPLMWindowID, int x, int y,
                XPLMMouseStatus status, void* /*refcon*/)
{
    if (!s_window) return 1;

    // Only intercept drag for mounted mode.  Floating/pop-out keep
    // legacy behavior: any MouseDown cycles modes if inside the
    // MODE button rect.
    const bool mounted = (s_persisted.placementMode ==
        onspeed_xplane::indexer::kPlacementMounted3D);

    if (!mounted) {
        if (status != xplm_MouseDown) return 1;
        int wL, wT, wR, wB;
        XPLMGetWindowGeometry(s_window, &wL, &wT, &wR, &wB);
        int bL, bT, bR, bB;
        if (!ComputeButtonRect(wL, wT, wR, wB, &bL, &bT, &bR, &bB))
            return 1;
        if (x >= bL && x <= bR && y >= bB && y <= bT) {
            const int next = (static_cast<int>(displayType) + 1) % 5;
            displayType = static_cast<int16_t>(next);
        }
        return 1;
    }

    // Mounted-mode click handling.
    if (status == xplm_MouseDown) {
        s_drag.active = false;
        s_drag.downX = x;
        s_drag.downY = y;

        // Capture aircraft + camera state at click time; reused for
        // drag inverse-projection so the math is consistent across
        // the drag duration even if the pilot's view jitters slightly.
        EnsureMount3DRefs();
        s_drag.ac.xWorld     = s_drAcLocalX   ? XPLMGetDataf(s_drAcLocalX)   : 0;
        s_drag.ac.yWorld     = s_drAcLocalY   ? XPLMGetDataf(s_drAcLocalY)   : 0;
        s_drag.ac.zWorld     = s_drAcLocalZ   ? XPLMGetDataf(s_drAcLocalZ)   : 0;
        s_drag.ac.pitchDeg   = s_drAcPitch    ? XPLMGetDataf(s_drAcPitch)    : 0;
        s_drag.ac.rollDeg    = s_drAcRoll     ? XPLMGetDataf(s_drAcRoll)     : 0;
        s_drag.ac.headingDeg = s_drAcHeading  ? XPLMGetDataf(s_drAcHeading)  : 0;
        s_drag.cam.xWorld     = s_drCamX       ? XPLMGetDataf(s_drCamX)       : 0;
        s_drag.cam.yWorld     = s_drCamY       ? XPLMGetDataf(s_drCamY)       : 0;
        s_drag.cam.zWorld     = s_drCamZ       ? XPLMGetDataf(s_drCamZ)       : 0;
        s_drag.cam.pitchDeg   = s_drCamPitch   ? XPLMGetDataf(s_drCamPitch)   : 0;
        s_drag.cam.rollDeg    = s_drCamRoll    ? XPLMGetDataf(s_drCamRoll)    : 0;
        s_drag.cam.headingDeg = s_drCamHeading ? XPLMGetDataf(s_drCamHeading) : 0;
        s_drag.cam.fovDeg     = s_drFovDeg     ? XPLMGetDataf(s_drFovDeg)     : 70.0f;
        XPLMGetScreenSize(&s_drag.sd.wPx, &s_drag.sd.hPx);

        // Project the current anchor to find depth + screen center.
        const onspeed_xplane::indexer::Anchor3D a{s_persisted.mount3D_X,
                                                  s_persisted.mount3D_Y,
                                                  s_persisted.mount3D_Z};
        const onspeed_xplane::indexer::ProjectedQuad pq =
            onspeed_xplane::indexer::ProjectAnchor(a, s_drag.ac, s_drag.cam,
                                                   s_drag.sd);
        s_drag.depthMeters = pq.depthMeters;
        s_drag.offsetX = x - static_cast<int>(pq.centerX);
        s_drag.offsetY = y - static_cast<int>(pq.centerY);
        return 1;
    }

    if (status == xplm_MouseDrag) {
        if (!s_drag.active) {
            if (std::abs(x - s_drag.downX) + std::abs(y - s_drag.downY) <
                    kDragThreshold) {
                return 1;
            }
            s_drag.active = true;
        }
        const float newCx = static_cast<float>(x - s_drag.offsetX);
        const float newCy = static_cast<float>(y - s_drag.offsetY);
        const onspeed_xplane::indexer::Anchor3D newAnchor =
            onspeed_xplane::indexer::InverseProject(
                newCx, newCy, s_drag.depthMeters,
                s_drag.ac, s_drag.cam, s_drag.sd);
        s_persisted.mount3D_X = newAnchor.xMeters;
        s_persisted.mount3D_Y = newAnchor.yMeters;
        s_persisted.mount3D_Z = newAnchor.zMeters;
        s_dirty = true;
        return 1;
    }

    if (status == xplm_MouseUp) {
        if (!s_drag.active) {
            // Tap, not drag.  Same MODE-cycle behavior as floating mode.
            int wL, wT, wR, wB;
            XPLMGetWindowGeometry(s_window, &wL, &wT, &wR, &wB);
            int bL, bT, bR, bB;
            if (ComputeButtonRect(wL, wT, wR, wB, &bL, &bT, &bR, &bB)
                && x >= bL && x <= bR && y >= bB && y <= bT)
            {
                const int next = (static_cast<int>(displayType) + 1) % 5;
                displayType = static_cast<int16_t>(next);
            }
        }
        s_drag.active = false;
        return 1;
    }

    return 1;
}
void HandleKey(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}
XPLMCursorStatus HandleCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }
int  HandleWheel(XPLMWindowID, int, int, int, int, void*) { return 1; }

// Render a textured quad using GL 1.x client-side vertex arrays.
// Modeled directly on imgui4xp's RenderImGui (open-source X-Plane plugin
// that runs Dear ImGui on Apple Silicon successfully):
//
//   https://github.com/sparker256/imgui4xp/blob/master/src/ImgWindow/ImgWindow.cpp
//
// Critical pattern: X-Plane's MODELVIEW + PROJECTION matrices are already
// set up to map "boxels" (X-Plane window coordinates) directly to clip
// space when our per-window draw callback runs.  We DO NOT call glOrtho
// or glLoadIdentity.  We just push/pop PROJECTION (untouched) and feed
// our vertices in window coords directly.  Don't deviate without
// testing — immediate-mode glBegin/glEnd and modern shader VBO paths
// both silently no-op on the Metal-backed GL bridge from a plugin's
// per-window draw callback.
void RenderTexturedQuadVA(int left, int top, int right, int bottom, int texId)
{
    // 1TU + alpha + alpha-test, no depth, no fog.  ImGui's exact mask.
    XPLMSetGraphicsState(0, 1, 0, 1, 1, 0, 0);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
    glDisable(GL_CULL_FACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    // Do NOT glLoadIdentity / glOrtho — X-Plane's matrices already map
    // window-coords → clip-space correctly.  imgui4xp adds a flip+
    // translate here because ImGui's local origin is (0,0) at top-left
    // of the window with Y-down.  Our vertices are already in absolute
    // X-Plane window coords (Y-up, top > bottom numerically), so no
    // additional transform is needed.

    XPLMBindTexture2d(texId, 0);

    struct Vert { GLfloat x,y,u,v; GLubyte r,g,b,a; };
    Vert verts[4] = {
        { (GLfloat)left,  (GLfloat)bottom, 0.0f, 1.0f, 255,255,255,255 },
        { (GLfloat)right, (GLfloat)bottom, 1.0f, 1.0f, 255,255,255,255 },
        { (GLfloat)right, (GLfloat)top,    1.0f, 0.0f, 255,255,255,255 },
        { (GLfloat)left,  (GLfloat)top,    0.0f, 0.0f, 255,255,255,255 },
    };
    static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

    glVertexPointer  (2, GL_FLOAT,         sizeof(Vert),
                      (const GLvoid*)((const char*)verts + offsetof(Vert,x)));
    glTexCoordPointer(2, GL_FLOAT,         sizeof(Vert),
                      (const GLvoid*)((const char*)verts + offsetof(Vert,u)));
    glColorPointer   (4, GL_UNSIGNED_BYTE, sizeof(Vert),
                      (const GLvoid*)((const char*)verts + offsetof(Vert,r)));

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPopAttrib();
    glPopClientAttrib();
}

// ------------------------------------------------------------------
// One-time setup helpers
// ------------------------------------------------------------------

bool InstallPanelAndRunSetup()
{
    // SDL provides millis()/micros()/delay() that lgfx's common.cpp
    // depends on under #if defined(SDL_h_).  No video subsystem.
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "FlyOnSpeed: SDL_Init failed: %s\n", SDL_GetError());
        XPLMDebugString(buf);
        return false;
    }

    s_panel = new Panel_PluginCanvas();

    // Init the panel directly rather than through M5.Display.init().
    // M5.Display.init() drives a hardware panel through
    // Panel_Device::init() (bus init, reset pulse) — neither makes
    // sense for our software framebuffer.  M5.Display itself is a
    // singleton that may already be init'd against Panel_NULL, in
    // which case its init() is a no-op and our framebuffer never
    // gets allocated.  Calling s_panel->init(false) ourselves
    // guarantees framebuffer + line-pointer table are ready.
    if (!s_panel->init(false)) {
        XPLMDebugString("FlyOnSpeed: s_panel->init FAILED\n");
        return false;
    }
    M5.Display.setPanel(s_panel);
    M5.Display.setColorDepth(16);
    M5.Display.setRotation(0);
    M5.Display.fillScreen(0x0000);

    // Replicate the minimal subset of the M5 firmware's setup() that's
    // safe in the X-Plane plugin context.  The full setup() blocks 3 s
    // on a splash-screen loop, calls M5.begin() (overrides our panel),
    // and pokes ESP-only peripherals (dacWrite, button polling).  All
    // we need for the renderer to produce a frame is gdraw sprite +
    // gHistory ring buffer + displayType seed.
    gdraw.setColorDepth(16);
    gdraw.createSprite(Panel_PluginCanvas::kWidth,
                       Panel_PluginCanvas::kHeight);
    gdraw.fillSprite(0x0000);
    for (int i = 0; i < 300; ++i) gHistory[i] = 1.0f;
    gHistoryIndex = 0;
    displayType = 0;
    return true;
}

bool CreateXPlaneWindow(XPLMWindowDecoration decoration)
{
    // GL texture allocation is deferred to the first DrawWindow call —
    // see EnsureGLReady().  X-Plane's GL context is only bound during
    // draw callbacks, so glGenTextures here would silently fail or
    // crash on Metal hosts.

    s_rgbaScratch.assign(
        static_cast<std::size_t>(Panel_PluginCanvas::kWidth) *
        Panel_PluginCanvas::kHeight, 0u);

    XPLMCreateWindow_t params = {};
    params.structSize             = sizeof(params);
    // Initial geometry comes from the persisted floating coords
    // (defaults match the pre-sticky-persistence values).  When the
    // .prf has saved state, the periodic-save callback in
    // aoa_audio.cpp will then call ApplyPersistedState() on the next
    // tick after AIRPORT_LOADED to restore pop-out mode + visibility.
    params.left                   = s_persisted.floatLeft;
    params.top                    = s_persisted.floatTop;
    params.right                  = s_persisted.floatLeft + s_persisted.floatWidth;
    params.bottom                 = s_persisted.floatTop  - s_persisted.floatHeight;
    params.visible                = 0;       // start hidden; menu toggles
    params.drawWindowFunc         = DrawWindow;
    params.handleMouseClickFunc   = HandleClick;
    params.handleRightClickFunc   = HandleClick;
    params.handleKeyFunc          = HandleKey;
    params.handleCursorFunc       = HandleCursor;
    params.handleMouseWheelFunc   = HandleWheel;
    params.refcon                 = nullptr;
    // RoundRectangle gives us X-Plane's standard window chrome
    // (titlebar with drag handle, close button, resizing).  The chrome
    // also paints a solid bg fill behind our content — but since our
    // texture is fully opaque (alpha=255 throughout) and exactly
    // covers the content area, the bg fill is invisible underneath.
    params.decorateAsFloatingWindow = decoration;
    params.layer                  = xplm_WindowLayerFloatingWindows;
    s_window = XPLMCreateWindowEx(&params);
    if (!s_window) return false;

    XPLMSetWindowTitle(s_window, "OnSpeed Indexer");
    return true;
}

// Destroy + recreate the window with a different decoration.  X-Plane
// SDK provides no in-place decoration setter, so switching between
// mounted-mode (self-decorated, no chrome) and floating / pop-out
// (round-rectangle, drag handle + close box) requires this dance.
bool RecreateWindowWithDecorationInternal(XPLMWindowDecoration decoration)
{
    if (s_window) {
        XPLMDestroyWindow(s_window);
        s_window = nullptr;
    }
    return CreateXPlaneWindow(decoration);
}

}  // namespace

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

// Lazy initialization: Init() is a no-op.  All the M5GFX/M5Unified/
// renderer setup is deferred to the first Show() so the indexer code
// path can't crash X-Plane unless the user explicitly opens the
// window.  This isolates the indexer from the audio plugin —
// audio works regardless of indexer state.
bool Init()
{
    XPLMDebugString("FlyOnSpeed: M5 indexer Init() (lazy; setup deferred to Show)\n");
    return true;
}

static bool LazyInitOnFirstShow()
{
    if (s_initOk) return true;
    XPLMDebugString("FlyOnSpeed: M5 indexer Show() lazy init starting\n");
    if (!InstallPanelAndRunSetup()) {
        XPLMDebugString("FlyOnSpeed: M5 indexer InstallPanelAndRunSetup failed\n");
        return false;
    }
    XPLMDebugString("FlyOnSpeed: M5 indexer panel installed, creating window\n");
    if (!CreateXPlaneWindow(xplm_WindowDecorationRoundRectangle)) {
        XPLMDebugString("FlyOnSpeed: M5 indexer CreateXPlaneWindow failed\n");
        return false;
    }
    s_initOk = true;
    XPLMDebugString("FlyOnSpeed: M5 indexer fully initialized\n");
    return true;
}

// First N Tick() calls log every step; after that, throttle to
// every 60th call (about once a second) so the log stays readable.
static int s_tickCount = 0;
static bool s_loopEverReturned = false;

void Tick()
{
    // Run if any of:
    //   - the embedded indexer window is visible (renderer needs to tick)
    //   - the USB-serial port is open (write next frame)
    //   - a USB-serial port is configured but currently closed (the
    //     auto-retry block below must run to re-establish the connection
    //     after a transient unplug)
    if (!s_initOk) return;
    if (!s_visible && !s_serialOut.IsOpen() && s_serialOutPath.empty()) return;

    // Throttle to 20 Hz to match the OnSpeed display-serial cadence.
    // X-Plane's flight loop fires at frame rate (60–80 Hz typical),
    // which would feed the M5 firmware's per-frame parser at 4× the
    // wire rate it expects, tripping its dt-out-of-band warning loop.
    static const std::uint32_t kTickPeriodMs = 50;   // 20 Hz
    static std::uint32_t s_lastTickMs = 0;
    const std::uint32_t now = static_cast<std::uint32_t>(SDL_GetTicks());
    if (s_lastTickMs != 0 && (now - s_lastTickMs) < kTickPeriodMs) return;
    s_lastTickMs = now;

    // Skip wire-frame emit while the sim is paused.  X-Plane's flight
    // loop continues to fire during pause, so without this guard the
    // M5 firmware's gHistory buffer (advanced on serialDataFresh())
    // keeps logging the same paused vG sample at 5 Hz — the G-load
    // page on Mode 4 visibly drifts even though the sim is frozen.
    // Letting the parser go stale also keeps the gOnset pre-smoother
    // and GOnsetFilter from accumulating a long string of identical
    // samples that produce a phantom rate when the sim resumes.
    if (pausedDataRef && XPLMGetDatai(pausedDataRef) != 0) return;

    const bool verbose = (s_tickCount < 5) || (s_tickCount % 60 == 0);
    ++s_tickCount;

    if (verbose) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "FlyOnSpeed: Tick %d entry (loopEverReturned=%d)\n",
                      s_tickCount, s_loopEverReturned ? 1 : 0);
        XPLMDebugString(buf);
    }

    // Build display-serial frame from current datarefs.
    if (verbose) XPLMDebugString("FlyOnSpeed: Tick A: BuildInputsFromDatarefs\n");
    onspeed::proto::DisplayBuildInputs in =
        onspeed_xplane::indexer::BuildInputsFromDatarefs();

    if (verbose) XPLMDebugString("FlyOnSpeed: Tick B: BuildDisplayFrame\n");
    std::uint8_t frameBytes[onspeed::proto::kDisplayFrameSizeBytes] = {0};
    const std::size_t emitted = onspeed::proto::BuildDisplayFrame(
        in, frameBytes, sizeof(frameBytes));
    if (emitted == 0) {
        if (verbose) XPLMDebugString("FlyOnSpeed: Tick: BuildDisplayFrame returned 0\n");
        return;
    }

    if (verbose) {
        char buf[64];
        std::snprintf(buf, sizeof(buf),
                      "FlyOnSpeed: Tick C: emitted %zu bytes\n", emitted);
        XPLMDebugString(buf);
    }

    // Optional: dump the first emitted frame in hex+ascii so we can
    // verify the wire content is sane (correct '#1' header, 77 bytes,
    // CRLF terminator).  Lets us rule out "we're feeding the parser
    // garbage that overflows somewhere downstream".
    static bool s_dumpedFirstFrame = false;
    static const bool kLogFirstFrame =
        EnvFlag("FLYONSPEED_INDEXER_LOG_FIRST_FRAME");
    if (kLogFirstFrame && !s_dumpedFirstFrame) {
        s_dumpedFirstFrame = true;
        XPLMDebugString("FlyOnSpeed: --- first wire frame ---\n");
        for (std::size_t i = 0; i < emitted; i += 16) {
            char hex[80] = {0};
            char asc[24] = {0};
            int  hOff = 0, aOff = 0;
            for (std::size_t j = 0; j < 16 && (i + j) < emitted; ++j) {
                hOff += std::snprintf(hex + hOff, sizeof(hex) - hOff,
                                      "%02x ", frameBytes[i + j]);
                const char c = static_cast<char>(frameBytes[i + j]);
                asc[aOff++] = (c >= 0x20 && c < 0x7f) ? c : '.';
            }
            char line[160];
            std::snprintf(line, sizeof(line),
                          "FlyOnSpeed:   %04zu  %-48s |%s|\n",
                          i, hex, asc);
            XPLMDebugString(line);
        }
        XPLMDebugString("FlyOnSpeed: --- end frame ---\n");
    }

    // Diagnostic: skip the M5 firmware's per-byte parser.  No writes to
    // M5 globals (Pitch, Roll, IAS, ...), no SavGol filter compute, no
    // SerialProcess side-effects.  If audio survives Show with this on
    // (and SKIP_DRAW also on), the bug is on the parser/inject side.
    static const bool kSkipInject = EnvFlag("FLYONSPEED_INDEXER_SKIP_INJECT");
    if (kSkipInject) {
        if (verbose) XPLMDebugString("FlyOnSpeed: Tick D: InjectSerialByte SKIPPED (env)\n");
    } else {
        // Pipe through the M5 firmware's parser one byte at a time.
        for (std::size_t i = 0; i < emitted; ++i) {
            InjectSerialByte(static_cast<char>(frameBytes[i]));
        }
        if (verbose) XPLMDebugString("FlyOnSpeed: Tick D: bytes injected\n");
    }

    // Mirror the same wire frame to the USB-serial port if open, so a
    // physically-connected M5 sees the same data the embedded indexer
    // sees.  Errors close the port — but s_serialOutPath stays set so
    // we can auto-reopen below (handles transient unplug-replug).
    //
    // A non-blocking Write() can return false for two distinct reasons:
    // (a) transient kernel-buffer full (EAGAIN/EWOULDBLOCK) — the right
    //     response is to drop this frame and try the next one.
    // (b) hard error (device unplugged, EIO) — the right response is to
    //     close the port and let the retry loop re-establish.
    // Without distinguishing these, every transient hiccup throws the
    // M5 into a 2-second blackout.  Use consecutive-failure counting:
    // single failure drops the frame, three in a row (150 ms) closes
    // the port.  A truly disconnected device fails every frame so this
    // converges to "close" within 150 ms; transient buffer pressure
    // recovers on the next frame without loss of connection.
    constexpr std::uint64_t kCloseAfterFails = 3;
    if (s_serialOut.IsOpen()) {
        if (!s_serialOut.Write(frameBytes, emitted)) {
            ++s_serialErrCount;
            if (s_serialErrCount >= kCloseAfterFails) {
                XPLMDebugString(("FlyOnSpeed: serial write failed "
                                + std::to_string(kCloseAfterFails)
                                + " frames on " + s_serialOutPath
                                + ", will retry\n").c_str());
                s_serialOut.Close();
                // Don't clear s_serialOutPath — the periodic retry
                // below uses it to auto-reopen when the device comes
                // back.
            }
        } else {
            s_serialErrCount = 0;
        }
    } else if (!s_serialOutPath.empty()) {
        // Port wanted but closed (either initial setup hit a transient
        // failure, or a write error closed it).  Retry every 2 seconds
        // — fast enough that a USB replug recovers within a couple
        // frames, slow enough not to spam the OS open() call when the
        // device is genuinely gone.
        static std::uint32_t s_lastReopenAttempt = 0;
        const std::uint32_t now = static_cast<std::uint32_t>(SDL_GetTicks());
        if (now - s_lastReopenAttempt > 2000) {
            s_lastReopenAttempt = now;
            if (s_serialOut.Open(s_serialOutPath)) {
                XPLMDebugString(("FlyOnSpeed: serial reopen OK on "
                                + s_serialOutPath + "\n").c_str());
                s_serialErrCount = 0;
            }
            // Open failure: silent, will retry in 2s.
        }
    }

    // Run the M5 renderer only when the embedded window is visible —
    // serial-only mode skips the framebuffer paint to save CPU.  The
    // bring-up SKIP_LOOP env var still applies for diagnostics.
    static const bool kSkipLoop = EnvFlag("FLYONSPEED_INDEXER_SKIP_LOOP");
    if (kSkipLoop) {
        if (verbose) XPLMDebugString("FlyOnSpeed: Tick E: loop() SKIPPED (env)\n");
    } else if (s_visible) {
        if (verbose) XPLMDebugString("FlyOnSpeed: Tick E: loop()\n");
        loop();
        if (verbose) XPLMDebugString("FlyOnSpeed: Tick F: loop() returned\n");
    }
    s_loopEverReturned = true;

    if (s_persisted.placementMode ==
            onspeed_xplane::indexer::kPlacementMounted3D)
    {
        UpdateMounted3DGeometry();
    }
}

bool IsVisible()
{
    return s_visible && s_window && XPLMGetWindowIsVisible(s_window);
}

void Show()
{
    if (!LazyInitOnFirstShow()) return;     // bring up M5 + window on demand
    if (!s_window) return;
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

// ------------------------------------------------------------------
// Persisted state machinery
// ------------------------------------------------------------------

namespace {

void ClampFloatingGeom(PersistedState* st)
{
    if (st->floatWidth  < kMinWidth)  st->floatWidth  = kMinWidth;
    if (st->floatHeight < kMinHeight) st->floatHeight = kMinHeight;

    // Reject pathological absolute values entirely (defaults stand).
    if (std::abs(st->floatLeft) >= kSaneAbs ||
        std::abs(st->floatTop)  >= kSaneAbs ||
        st->floatWidth  >= kSaneAbs ||
        st->floatHeight >= kSaneAbs)
    {
        st->floatLeft   = 100;
        st->floatTop    = 600;
        st->floatWidth  = kWindowWidth;
        st->floatHeight = kWindowHeight;
    }

    // Clamp against live X-Plane desktop bounds so kMinVisible boxels
    // stay on-screen.  Multi-monitor disconnect since the .prf was
    // last written can put a previously-visible window off-screen
    // otherwise.
    int sLeft = 0, sTop = 0, sRight = 0, sBottom = 0;
    XPLMGetScreenBoundsGlobal(&sLeft, &sTop, &sRight, &sBottom);
    if (sRight > sLeft && sTop > sBottom) {
        const int maxLeft = sRight  - kMinVisible;
        const int minLeft = sLeft   - (st->floatWidth  - kMinVisible);
        const int maxTop  = sTop;
        const int minTop  = sBottom + kMinVisible;
        if (st->floatLeft > maxLeft) st->floatLeft = maxLeft;
        if (st->floatLeft < minLeft) st->floatLeft = minLeft;
        if (st->floatTop  > maxTop)  st->floatTop  = maxTop;
        if (st->floatTop  < minTop)  st->floatTop  = minTop;
    }
}

void ClampPopOutGeom(PersistedState* st)
{
    if (st->popWidth  < kMinWidth)  st->popWidth  = kMinWidth;
    if (st->popHeight < kMinHeight) st->popHeight = kMinHeight;

    // We don't know which OS monitor the window will land on so we
    // can't tightly bounds-check left/top — the OS itself will clamp
    // visible-area on XPLMSetWindowGeometryOS.  Reject only the
    // pathological-magnitude cases.
    if (std::abs(st->popLeft) >= kSaneAbs ||
        std::abs(st->popTop)  >= kSaneAbs ||
        st->popWidth  >= kSaneAbs ||
        st->popHeight >= kSaneAbs)
    {
        st->popLeft   = 100;
        st->popTop    = 100;
        st->popWidth  = kWindowWidth;
        st->popHeight = kWindowHeight;
    }
}

}  // namespace

void ApplyPersistedState(const PersistedState& in)
{
    PersistedState st = in;
    ClampFloatingGeom(&st);
    ClampPopOutGeom(&st);
    s_persisted = st;

    const XPLMWindowDecoration desiredDecoration =
        (st.placementMode ==
            onspeed_xplane::indexer::kPlacementMounted3D)
        ? xplm_WindowDecorationSelfDecorated
        : xplm_WindowDecorationRoundRectangle;

    // If window already exists with wrong decoration, recreate.  X-Plane
    // SDK provides no in-place decoration setter.  This static-local
    // tracking is safe because ApplyPersistedState is documented to
    // run only from the flight-loop callback context.
    if (s_window) {
        static XPLMWindowDecoration s_currentDecoration =
            xplm_WindowDecorationRoundRectangle;
        if (s_currentDecoration != desiredDecoration) {
            if (RecreateWindowWithDecorationInternal(desiredDecoration)) {
                s_currentDecoration = desiredDecoration;
            } else {
                XPLMDebugString("FlyOnSpeed: window recreate failed; "
                                "indexer hidden until next Show()\n");
            }
        }
    }

    // Logging is cheap and load-bearing for debugging the boot path —
    // keep it.  XPLMDebugString is safe from any non-render context.
    char dbuf[256];
    std::snprintf(dbuf, sizeof(dbuf),
        "FlyOnSpeed: ApplyPersistedState window=%p visible=%d popped=%d "
        "float=%d,%d,%dx%d\n",
        static_cast<const void*>(s_window),
        st.visible ? 1 : 0,
        st.isPoppedOut ? 1 : 0,
        st.floatLeft, st.floatTop, st.floatWidth, st.floatHeight);
    XPLMDebugString(dbuf);

    // If the user has never opened the indexer this session, the
    // window doesn't exist yet.  CreateXPlaneWindow will read
    // s_persisted on first Show; pop-out mode + visibility get
    // applied here on the next ApplyPersistedState() after that.
    // For visible=true on cold boot we DO want to make the window
    // appear, but only from a flight-loop callback context — Show()
    // lazy-inits SDL/M5GFX and the M5Unified singleton can't tolerate
    // that from arbitrary SDK threads.  This function is documented
    // to only be called from the periodic flight-loop callback, so
    // calling Show() here is safe.
    if (st.visible && !s_window) {
        Show();
    }

    if (!s_window) {
        // Show() failed to create the window (or visible=false).
        // Nothing more to do; the flag is held for next call.
        return;
    }

    // Window exists.  Apply positioning mode + geometry per the
    // active placement mode.
    using onspeed_xplane::indexer::kPlacementPopOut;
    using onspeed_xplane::indexer::kPlacementFloating;
    using onspeed_xplane::indexer::kPlacementMounted3D;

    if (st.placementMode == kPlacementPopOut) {
        XPLMSetWindowPositioningMode(s_window, xplm_WindowPopOut, -1);
        XPLMSetWindowGeometryOS(s_window,
                                st.popLeft,
                                st.popTop,
                                st.popLeft + st.popWidth,
                                st.popTop  - st.popHeight);
    } else if (st.placementMode == kPlacementFloating) {
        XPLMSetWindowPositioningMode(s_window, xplm_WindowPositionFree, -1);
        XPLMSetWindowGeometry(s_window,
                              st.floatLeft,
                              st.floatTop,
                              st.floatLeft + st.floatWidth,
                              st.floatTop  - st.floatHeight);
    } else {
        // Mounted3D: per-Tick projection sets geometry.  Just clear
        // any pop-out state so it lives in the floating coord space.
        XPLMSetWindowPositioningMode(s_window, xplm_WindowPositionFree, -1);
    }

    XPLMSetWindowIsVisible(s_window, st.visible ? 1 : 0);
    s_visible = st.visible;
    s_dirty   = false;     // geometry now matches s_persisted
}

void GetCurrentState(PersistedState* out)
{
    if (!out) return;
    *out = s_persisted;
    out->visible = IsVisible();
    out->mode    = static_cast<int>(displayType);

    if (!s_window) return;

    const bool poppedOut = (XPLMWindowIsPoppedOut(s_window) != 0);
    out->isPoppedOut = poppedOut;
    // Also derive placementMode for forward-compat. Mounted3D is not
    // detectable from XPLM window state (its geometry is overwritten
    // every Tick), so the only reliable signal is s_persisted itself.
    if (s_persisted.placementMode ==
            onspeed_xplane::indexer::kPlacementMounted3D) {
        out->placementMode = onspeed_xplane::indexer::kPlacementMounted3D;
    } else {
        out->placementMode = poppedOut
            ? onspeed_xplane::indexer::kPlacementPopOut
            : onspeed_xplane::indexer::kPlacementFloating;
    }
    if (poppedOut) {
        int l, t, r, b;
        XPLMGetWindowGeometryOS(s_window, &l, &t, &r, &b);
        // Only commit if the read looks sane.  X-Plane occasionally
        // returns transient nonsense during pop-out / un-pop / monitor
        // change; the sane-bounds gate prevents that landing in the .prf.
        if (std::abs(l) < kSaneAbs && std::abs(t) < kSaneAbs &&
            (r - l) > 0 && (t - b) > 0 &&
            (r - l) < kSaneAbs && (t - b) < kSaneAbs)
        {
            out->popLeft   = l;
            out->popTop    = t;
            out->popWidth  = r - l;
            out->popHeight = t - b;
        }
    } else {
        int l, t, r, b;
        XPLMGetWindowGeometry(s_window, &l, &t, &r, &b);
        if (std::abs(l) < kSaneAbs && std::abs(t) < kSaneAbs &&
            (r - l) > 0 && (t - b) > 0 &&
            (r - l) < kSaneAbs && (t - b) < kSaneAbs)
        {
            out->floatLeft   = l;
            out->floatTop    = t;
            out->floatWidth  = r - l;
            out->floatHeight = t - b;
        }
    }
}

void MarkDirtyIfChanged()
{
    if (!s_window) return;
    PersistedState live;
    GetCurrentState(&live);

    bool changed =
        live.visible     != s_persisted.visible    ||
        live.mode        != s_persisted.mode       ||
        live.isPoppedOut != s_persisted.isPoppedOut;
    if (live.isPoppedOut) {
        changed = changed ||
            live.popLeft   != s_persisted.popLeft   ||
            live.popTop    != s_persisted.popTop    ||
            live.popWidth  != s_persisted.popWidth  ||
            live.popHeight != s_persisted.popHeight;
    } else {
        changed = changed ||
            live.floatLeft   != s_persisted.floatLeft   ||
            live.floatTop    != s_persisted.floatTop    ||
            live.floatWidth  != s_persisted.floatWidth  ||
            live.floatHeight != s_persisted.floatHeight;
    }

    if (changed) {
        s_persisted = live;
        s_dirty     = true;
    }
}

bool IsDirty()  { return s_dirty; }
void ClearDirty() { s_dirty = false; }

void Shutdown()
{
    if (s_window) {
        XPLMDestroyWindow(s_window);
        s_window = nullptr;
    }
    if (s_textureId) {
        // No matching XPLMReleaseTextureNumbers exists in the SDK — the
        // texture is leaked at plugin unload, which is harmless: X-Plane
        // is shutting down too, and even on Reload Plugins the leak is
        // bounded to one int per indexer-show across the session.
        s_textureId = 0;
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

    s_serialOut.Close();
    s_serialOutPath.clear();

    SDL_Quit();
    s_initOk = false;
}

bool OpenSerialOut(const std::string& portPath)
{
    if (portPath.empty()) {
        s_serialOut.Close();
        s_serialOutPath.clear();
        return true;
    }

    // Lazy-init the indexer pipeline so Tick will run even if the
    // embedded window is hidden.  This pulls up SDL + the panel +
    // sets up gdraw, but doesn't show the X-Plane window.
    if (!s_initOk) {
        if (!LazyInitOnFirstShow()) {
            XPLMDebugString("FlyOnSpeed: OpenSerialOut: indexer init failed\n");
            return false;
        }
    }

    // Set s_serialOutPath BEFORE attempting Open, and leave it set
    // even on initial failure.  This way the Tick auto-retry loop
    // picks the port up the moment it appears (e.g., user told us
    // about an M5 they hadn't plugged in yet).  Caller still gets
    // false so it knows the device isn't responding right now.
    s_serialOutPath  = portPath;
    s_serialErrCount = 0;

    if (!s_serialOut.Open(portPath)) {
        XPLMDebugString(("FlyOnSpeed: serial open FAILED on " + portPath
                        + " — will retry until device appears\n").c_str());
        return false;
    }
    XPLMDebugString(("FlyOnSpeed: serial open OK on " + portPath
                    + "\n").c_str());
    return true;
}

void CloseSerialOut()
{
    s_serialOut.Close();
    s_serialOutPath.clear();
}

bool IsSerialOutOpen()
{
    return s_serialOut.IsOpen();
}

const std::string& SerialOutPath()
{
    return s_serialOutPath;
}

void RecreateWindowWithDecoration(int decoration)
{
    (void)RecreateWindowWithDecorationInternal(
        static_cast<XPLMWindowDecoration>(decoration));
}

}  // namespace onspeed_xplane::indexer
