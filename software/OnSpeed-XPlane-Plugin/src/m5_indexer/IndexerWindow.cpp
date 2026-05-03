// IndexerWindow.cpp — see IndexerWindow.h.

#include "IndexerWindow.h"
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

// Modern-GL pipeline for the textured quad blit.  Allocated lazily on
// first draw alongside the texture.  Apple Silicon's Metal-bridge
// silently drops immediate-mode GL (glBegin/glEnd/glVertex), so we
// render via a VBO + shader.
GLuint                        s_shaderProgram = 0;
GLuint                        s_vbo           = 0;
GLint                         s_attrPos       = -1;   // location of a_pos
GLint                         s_attrUV        = -1;   // location of a_uv
GLint                         s_uniformVPSize = -1;   // location of u_viewport
GLint                         s_uniformTex    = -1;   // location of u_tex

// glext.h declares these as function-pointer typedefs but doesn't
// instantiate them.  On macOS the OpenGL framework provides them
// directly (extern "C" symbols), so plain function calls work.
// Declared inline in the lgfx common.cpp pattern.

// X-Plane window dimensions.  Native M5 panel is 320×240 — render
// 1:1 by default; could pixel-double in a follow-up.
constexpr int kWindowWidth  = 320;
constexpr int kWindowHeight = 240;

// ------------------------------------------------------------------
// X-Plane window callbacks
// ------------------------------------------------------------------
//
// IMPORTANT: GL operations are only valid inside a draw callback,
// per X-Plane SDK rules — the GL context isn't bound when the menu
// handler runs.  All glGenTextures / glTexImage2D etc. happen here
// on first draw, gated by s_glReady.

// Compile a single shader stage; returns 0 on failure.
GLuint CompileShader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        char buf[640];
        std::snprintf(buf, sizeof(buf),
                      "FlyOnSpeed: shader compile FAILED (%s): %s\n",
                      type == GL_VERTEX_SHADER ? "VS" : "FS", log);
        XPLMDebugString(buf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool BuildShaderProgram()
{
    // GLSL 1.20 — matches X-Plane's compatibility-profile GL 2.1 context.
    static const char* kVS =
        "#version 120\n"
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "uniform vec2 u_viewport;\n"   // (width, height) in pixels
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "    // pixel coords → clip coords [-1, +1].  X-Plane's y axis\n"
        "    // is bottom-up in screen space and our quad uses bottom-\n"
        "    // up too, so no flip needed beyond the rescale.\n"
        "    vec2 ndc = (a_pos / u_viewport) * 2.0 - 1.0;\n"
        "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "    v_uv = a_uv;\n"
        "}\n";
    // DIAGNOSTIC fragment shader: emit solid magenta with no texture
    // sampling.  If this renders a magenta quad, the VBO + shader
    // pipeline is working and the gray problem is purely texture
    // sampling / binding.  If this also produces nothing, the shader
    // pipeline itself is broken on Metal (modern-GL path also dropped).
    // Real shader is below in #else, swap by toggling kDebugSolidShader.
    static const bool kDebugSolidShader =
        std::getenv("FLYONSPEED_INDEXER_SOLID_SHADER") != nullptr;
    static const char* kFSSolid =
        "#version 120\n"
        "void main() {\n"
        "    gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);\n"   // magenta
        "}\n";
    static const char* kFSReal =
        "#version 120\n"
        "uniform sampler2D u_tex;\n"
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(u_tex, v_uv);\n"
        "}\n";
    const char* kFS = kDebugSolidShader ? kFSSolid : kFSReal;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVS);
    if (!vs) return false;
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFS);
    if (!fs) { glDeleteShader(vs); return false; }

    s_shaderProgram = glCreateProgram();
    glAttachShader(s_shaderProgram, vs);
    glAttachShader(s_shaderProgram, fs);
    glLinkProgram(s_shaderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(s_shaderProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512] = {0};
        glGetProgramInfoLog(s_shaderProgram, sizeof(log), nullptr, log);
        char buf[640];
        std::snprintf(buf, sizeof(buf),
                      "FlyOnSpeed: shader link FAILED: %s\n", log);
        XPLMDebugString(buf);
        glDeleteProgram(s_shaderProgram);
        s_shaderProgram = 0;
        return false;
    }

    s_attrPos       = glGetAttribLocation (s_shaderProgram, "a_pos");
    s_attrUV        = glGetAttribLocation (s_shaderProgram, "a_uv");
    s_uniformVPSize = glGetUniformLocation(s_shaderProgram, "u_viewport");
    s_uniformTex    = glGetUniformLocation(s_shaderProgram, "u_tex");
    return true;
}

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

    if (!BuildShaderProgram()) {
        XPLMDebugString("FlyOnSpeed: shader build failed; quad will not render\n");
        return;
    }
    glGenBuffers(1, &s_vbo);
    XPLMDebugString("FlyOnSpeed: indexer GL setup complete\n");
}

// Render the texture onto the given screen rect using a VBO + shader.
// Coordinates are X-Plane window coords (y-up, top > bottom).
void BlitTexturedQuad(int left, int top, int right, int bottom)
{
    if (s_shaderProgram == 0 || s_vbo == 0) return;

    // Bottom-left and top-right of the quad, plus matching UVs.  The
    // texture's row 0 is the top of the framebuffer, so flip v.
    const GLfloat verts[] = {
        // x,    y,         u,    v
        (GLfloat)left,  (GLfloat)bottom, 0.0f, 1.0f,
        (GLfloat)right, (GLfloat)bottom, 1.0f, 1.0f,
        (GLfloat)right, (GLfloat)top,    1.0f, 0.0f,
        (GLfloat)left,  (GLfloat)bottom, 0.0f, 1.0f,
        (GLfloat)right, (GLfloat)top,    1.0f, 0.0f,
        (GLfloat)left,  (GLfloat)top,    0.0f, 0.0f,
    };

    int viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);

    glUseProgram(s_shaderProgram);
    glUniform2f(s_uniformVPSize,
                static_cast<GLfloat>(viewport[2]),
                static_cast<GLfloat>(viewport[3]));
    glUniform1i(s_uniformTex, 0);    // texture unit 0

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);

    glEnableVertexAttribArray(s_attrPos);
    glVertexAttribPointer(s_attrPos, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(s_attrUV);
    glVertexAttribPointer(s_attrUV, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat),
                          reinterpret_cast<void*>(2 * sizeof(GLfloat)));

    glDrawArrays(GL_TRIANGLES, 0, 6);

    {
        // Once-only: log any GL error from the modern-GL path so we
        // can tell whether the draw is actually accepted by the driver.
        static bool s_loggedBlitErr = false;
        if (!s_loggedBlitErr) {
            s_loggedBlitErr = true;
            GLenum err = glGetError();
            char eb[160];
            std::snprintf(eb, sizeof(eb),
                          "FlyOnSpeed: DIAG blit glGetError=0x%04x prog=%u vbo=%u "
                          "attrPos=%d attrUV=%d uVP=%d uTex=%d\n",
                          (unsigned)err, (unsigned)s_shaderProgram,
                          (unsigned)s_vbo, s_attrPos, s_attrUV,
                          s_uniformVPSize, s_uniformTex);
            XPLMDebugString(eb);
        }
    }

    glDisableVertexAttribArray(s_attrPos);
    glDisableVertexAttribArray(s_attrUV);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
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

    if (EnvFlag("FLYONSPEED_INDEXER_FORCE_RED")) {
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
    RenderTexturedQuadVA(left, top, right, bottom, s_textureId);

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

// Left-click on the indexer body cycles through display modes.  The
// X-Plane RoundRectangle chrome reserves the titlebar for drag/move,
// so clicks reach this handler only when the user clicked inside the
// content area we paint into.  Fires on MouseDown so the mode change
// is responsive — without waiting for MouseUp.
int HandleClick(XPLMWindowID, int /*x*/, int /*y*/,
                XPLMMouseStatus status, void* /*refcon*/)
{
    if (status == xplm_MouseDown) {
        const int next = (static_cast<int>(displayType) + 1) % 5;
        displayType = static_cast<int16_t>(next);
    }
    return 1;
}
void HandleKey(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}
XPLMCursorStatus HandleCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }
int  HandleWheel(XPLMWindowID, int, int, int, int, void*) { return 1; }

// Render a textured quad using GL 1.x client-side vertex arrays.
// Setup mirrors Dear ImGui's GL2 backend (`imgui_impl_opengl2.cpp`,
// `ImGui_ImplOpenGL2_SetupRenderState`) line-for-line — the same
// path PilotEdge uses to render textured UI on Apple Silicon's
// Metal-backed GL bridge.  Don't deviate without testing — the
// state setup matters more than it looks.
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
// our vertices in window coords directly.  That's why imgui4xp works
// where our prior glOrtho approach silently produced nothing.
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

// Global phase draw callback registered at xplm_Phase_Window (after).
// All actual texture upload + render happens here; the per-window draw
// callback is a no-op.  See the comment on RenderTexturedQuadVA for why
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

bool CreateXPlaneWindow()
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
    // RoundRectangle gives us X-Plane's standard window chrome
    // (titlebar with drag handle, close button, resizing).  The chrome
    // also paints a solid bg fill behind our content — but since our
    // texture is fully opaque (alpha=255 throughout) and exactly
    // covers the content area, the bg fill is invisible underneath.
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
    if (!CreateXPlaneWindow()) {
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
    // verify the wire content is sane (correct '#1' header, 76 bytes,
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

}  // namespace onspeed_xplane::indexer
