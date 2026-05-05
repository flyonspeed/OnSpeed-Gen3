// Panel_PluginCanvas — a software framebuffer for M5GFX that renders
// into a plugin-owned RGB565 buffer instead of an SDL window.  M5GFX's
// Panel_FrameBufferBase already implements every pixel operation
// (writeBlock, writePixels, drawPixelPreclipped, …) against an internal
// row-pointer table; we only override init/display/setColorDepth and
// expose a thread-safe RGB565 → RGBA8888 readback for the X-Plane GL
// upload path.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

// M5GFX is included inside `lgfx::v1`.  Lowercase `panel/`: the M5GFX
// libdeps tree on disk uses lowercase, so this include is correct on
// case-sensitive filesystems (Linux).  macOS / Windows resolved the
// previous capitalized form via case-insensitive defaults.
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

namespace onspeed_xplane {

class Panel_PluginCanvas : public lgfx::v1::Panel_FrameBufferBase
{
public:
    static constexpr int kWidth  = 320;
    static constexpr int kHeight = 240;
    static constexpr int kBytesPerPixel = 2;       // RGB565 host-endian
    static constexpr std::size_t kFramebufferBytes =
        kWidth * kHeight * kBytesPerPixel;

    Panel_PluginCanvas();
    ~Panel_PluginCanvas() override;

    // Panel_FrameBufferBase virtuals we override.
    bool init(bool use_reset) override;
    lgfx::v1::color_depth_t setColorDepth(lgfx::v1::color_depth_t depth) override;
    void display(uint_fast16_t x, uint_fast16_t y,
                 uint_fast16_t w, uint_fast16_t h) override;

    // Snapshot the framebuffer as packed RGBA8888.  `dst` must point
    // to at least kWidth * kHeight * 4 bytes.
    //
    // The mutex is uncontested in normal X-Plane operation: both
    // M5GFX writes (from Tick → loop()) and this read (from DrawWindow)
    // run on X-Plane's main thread per the SDK's threading model.
    // It's still acquired to (a) make the data-race intent explicit
    // under the C++ memory model and (b) protect against any future
    // code path that adds a background thread reading the framebuffer.
    void CopyToRGBA8888(std::uint32_t* dst);

    // DIAGNOSTIC: direct framebuffer access, bypasses all of M5GFX.
    // Used by the test-pattern path to determine whether the gray-
    // screen problem is on the M5GFX-write side or the GL-read side.
    std::uint8_t* RawFramebuffer() { return m_framebuffer; }

    // True if anything has been drawn since the last CopyToRGBA8888.
    // The X-Plane draw callback skips the GL upload when nothing
    // changed.  Cleared by CopyToRGBA8888.
    bool DirtyAndClear();

private:
    std::uint8_t* m_framebuffer = nullptr;       // owned, kFramebufferBytes
    std::uint8_t** m_lineArray = nullptr;        // owned, kHeight pointers
    std::mutex    m_mutex;                       // guards m_framebuffer + m_dirty
    bool          m_dirty = false;
};

}  // namespace onspeed_xplane
