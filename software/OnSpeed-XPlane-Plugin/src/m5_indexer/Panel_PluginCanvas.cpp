// Panel_PluginCanvas — see Panel_PluginCanvas.h for the design.
//
// The framebuffer layout matches lgfx's expectation of one big
// allocation indexed by a per-row pointer table.  At 16bpp each row
// is `width * 2` bytes; row N starts at framebuffer + N * width * 2.

#include "Panel_PluginCanvas.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace onspeed_xplane {

Panel_PluginCanvas::Panel_PluginCanvas()
{
    // Configure the panel before init().  These show up to lgfx via
    // the protected _cfg / _write_depth state in Panel_FrameBufferBase.
    auto cfg          = config();
    cfg.panel_width   = kWidth;
    cfg.panel_height  = kHeight;
    cfg.memory_width  = kWidth;
    cfg.memory_height = kHeight;
    config(cfg);

    // M5GFX uses _auto_display = true to decide whether to call
    // display() after each pixel op.  The Panel_sdl example does the
    // same; without it the renderer never invokes our display()
    // override and our dirty flag stays false.
    _auto_display = true;
}

Panel_PluginCanvas::~Panel_PluginCanvas()
{
    if (m_framebuffer) {
        std::free(m_framebuffer);
        m_framebuffer = nullptr;
    }
    if (m_lineArray) {
        std::free(m_lineArray);
        m_lineArray = nullptr;
    }
    // The Panel_FrameBufferBase destructor doesn't free _lines_buffer
    // automatically (it's protected, treated as caller-owned).  We
    // already freed our backing store above; null the inherited
    // pointer so any later access fails fast rather than reading
    // through a dangling pointer.
    _lines_buffer = nullptr;
}

bool Panel_PluginCanvas::init(bool use_reset)
{
    // Allocate the framebuffer once; allocate row-pointer table
    // pointing into it; hand the row-pointer table to the base class.
    if (m_framebuffer == nullptr) {
        m_framebuffer = static_cast<std::uint8_t*>(
            std::calloc(kFramebufferBytes, 1));
        if (m_framebuffer == nullptr) return false;
    }
    if (m_lineArray == nullptr) {
        m_lineArray = static_cast<std::uint8_t**>(
            std::calloc(kHeight, sizeof(std::uint8_t*)));
        if (m_lineArray == nullptr) {
            std::free(m_framebuffer);
            m_framebuffer = nullptr;
            return false;
        }
    }
    const std::size_t rowBytes = kWidth * kBytesPerPixel;
    for (int y = 0; y < kHeight; ++y) {
        m_lineArray[y] = m_framebuffer + y * rowBytes;
    }
    _lines_buffer = m_lineArray;

    return Panel_FrameBufferBase::init(use_reset);
}

lgfx::v1::color_depth_t
Panel_PluginCanvas::setColorDepth(lgfx::v1::color_depth_t depth)
{
    // We're a 16bpp panel.  M5GFX may try to set us to 8bpp during
    // M5.Display.init() defaults; clamp to RGB565 so the linesBuffer
    // layout above stays right.  Same trick Panel_sdl::setColorDepth
    // uses (clamps everything ≥16 to rgb565 for two-byte depths).
    (void)depth;
    _write_depth = lgfx::v1::color_depth_t::rgb565_2Byte;
    _read_depth  = lgfx::v1::color_depth_t::rgb565_2Byte;
    return _write_depth;
}

void Panel_PluginCanvas::display(uint_fast16_t /*x*/, uint_fast16_t /*y*/,
                                 uint_fast16_t /*w*/, uint_fast16_t /*h*/)
{
    // M5GFX calls this whenever a region has been written to the
    // framebuffer.  We don't need to do anything per-region — the
    // framebuffer IS our "display" — we just need to record that
    // something changed so the next X-Plane draw uploads it.
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirty = true;
}

bool Panel_PluginCanvas::DirtyAndClear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool was = m_dirty;
    m_dirty = false;
    return was;
}

void Panel_PluginCanvas::CopyToRGBA8888(std::uint32_t* dst)
{
    // RGB565 (host-endian uint16 per pixel) → RGBA8888.
    // Bit layout: R = bits 11..15, G = bits 5..10, B = bits 0..4.
    // Expand 5/6-bit channels to 8-bit by replicating high bits into
    // the low (`x = (x << 3) | (x >> 2)` for 5-bit, `(x << 2) | (x >> 4)`
    // for 6-bit) — standard RGB565 → RGB888 expansion.
    //
    // The lock guards against concurrent writeBlock on the M5 firmware's
    // loop() (X-Plane flight-loop thread) overlapping with our read.
    // Worst case the read sees a half-frame visual glitch; without the
    // lock, multi-byte writeBlock strides + our concurrent reads are UB
    // under the C++ memory model.
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_framebuffer) return;
    const std::uint16_t* src =
        reinterpret_cast<const std::uint16_t*>(m_framebuffer);
    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;

    for (std::size_t i = 0; i < pixels; ++i) {
        // M5GFX stores RGB565 pixels in big-endian byte order in the
        // framebuffer (high byte first), to match what an SPI display
        // would expect.  Host CPU reads uint16 little-endian, so byte-
        // swap before unpacking.  Symptom of skipping the swap: red
        // and blue channels look correct but values that should be
        // saturated red render as magenta and vice-versa, because the
        // 5-bit-red and 5-bit-blue fields land in each other's slots.
        const std::uint16_t raw = src[i];
        const std::uint16_t p = static_cast<std::uint16_t>(
            (raw >> 8) | (raw << 8));
        const std::uint8_t r5 = (p >> 11) & 0x1F;
        const std::uint8_t g6 = (p >>  5) & 0x3F;
        const std::uint8_t b5 = (p      ) & 0x1F;
        const std::uint8_t r8 = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
        const std::uint8_t g8 = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
        const std::uint8_t b8 = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
        // RGBA8888 little-endian word: A=ff, B=b8, G=g8, R=r8.
        // Stored in memory as R, G, B, A which is what GL_RGBA expects.
        dst[i] = (std::uint32_t(0xFF) << 24)
               | (std::uint32_t(b8)   << 16)
               | (std::uint32_t(g8)   <<  8)
               | (std::uint32_t(r8)   <<  0);
    }
}

}  // namespace onspeed_xplane
