// M5Unified.h (host stub)
//
// Stub the M5GFX / M5Unified surface so renderer code compiled on host
// routes every draw call into MockM5Canvas. This is the linchpin of PR 0:
// the renderer code is compiled verbatim against a MockM5Canvas that
// records events. No production code is modified.

#ifndef RENDERTEST_M5UNIFIED_H
#define RENDERTEST_M5UNIFIED_H

#include "Arduino.h"
#include "../MockDrawApi.h"

// RGB565 color constants used by the renderer code. Values copied from
// M5GFX's lgfx_fonts.h / tft_color.h — same on both M5GFX and TFT_eSPI.
#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xC618
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFDA0
#define TFT_PINK        0xFE19
#define TFT_GOLD        0xFEA0
#define TFT_SKYBLUE     0x867D
#define TFT_VIOLET      0x915C
#define TFT_BROWN       0x9A60

// Also the non-prefixed aliases main.cpp uses (BLACK in the splash).
#ifndef BLACK
#define BLACK TFT_BLACK
#endif

// Legacy datum constants (MC_DATUM etc.) — renderer code uses both these
// and textdatum_t::baseline_left style. Map to MockTextDatum integer
// values (matching M5GFX's integer encoding).
#define TL_DATUM    MOCK_top_left
#define TC_DATUM    MOCK_top_center
#define TR_DATUM    MOCK_top_right
#define ML_DATUM    MOCK_middle_left
#define MC_DATUM    MOCK_middle_center
#define MR_DATUM    MOCK_middle_right
#define BL_DATUM    MOCK_bottom_left
#define BC_DATUM    MOCK_bottom_center
#define BR_DATUM    MOCK_bottom_right
#define L_BASELINE  MOCK_baseline_left
#define C_BASELINE  MOCK_baseline_center
#define R_BASELINE  MOCK_baseline_right

// namespace wrapper so `textdatum_t::baseline_left` expressions parse.
// The renderer code uses textdatum_t::baseline_left / ::middle_right
// etc. (M5GFX's enum style).
namespace textdatum_t_ns {
    constexpr int top_left        = MOCK_top_left;
    constexpr int top_center      = MOCK_top_center;
    constexpr int top_right       = MOCK_top_right;
    constexpr int middle_left     = MOCK_middle_left;
    constexpr int middle_center   = MOCK_middle_center;
    constexpr int middle_right    = MOCK_middle_right;
    constexpr int bottom_left     = MOCK_bottom_left;
    constexpr int bottom_center   = MOCK_bottom_center;
    constexpr int bottom_right    = MOCK_bottom_right;
    constexpr int baseline_left   = MOCK_baseline_left;
    constexpr int baseline_center = MOCK_baseline_center;
    constexpr int baseline_right  = MOCK_baseline_right;
}
namespace textdatum_t = textdatum_t_ns;

// `M5Canvas` is the name the renderer code references. We alias it to
// our mock so the renderer compiles unmodified. The M5 ctor takes a
// pointer to an `M5.Display` LCD; our mock ctor takes nothing. A
// preprocessor shim in RenderShim.cpp rewrites the ctor call. See there.
using M5Canvas = MockM5Canvas;

// M5.Display sentinel — the real M5Canvas ctor takes &M5.Display. Our
// mock ctor ignores it; we just need the symbol to exist.
struct MockDisplay {
    void setBrightness(int) {}
    void fillScreen(uint32_t) {}
};
struct MockM5 {
    MockDisplay Display;
    void update() {}
    int  config() { return 0; }
    void begin(int) {}
};
static inline MockM5 M5; // NOLINT

#endif // RENDERTEST_M5UNIFIED_H
