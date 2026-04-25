// HuvverShim.h — typedef-and-stub compat layer to compile main.cpp /
// SerialRead.cpp / GaugeWidgets.cpp against TFT_eSPI on the huVVer-AVI
// (ESP32 + ST7789) target without modifying production source.
//
// Strategy: make `M5Canvas` resolve to a thin derived class of TFT_eSprite
// that adds the M5GFX-flavored APIs the firmware actually calls
// (`setFont(const GFXfont*)` overload, `setColorDepth(int)`,
// `setBrightness(uint8_t)` on the parent `Display` object). Provide a
// global `M5_t M5` with `Display`, `BtnA/B/C`, `update/begin/config`
// stubs. The Free_Fonts.h in lib/GaugeWidgets/ uses the M5GFX
// `fonts::FreeSans12pt7b` namespace; we declare a `fonts` namespace
// here that re-exports TFT_eSPI's globally-named `FreeSans12pt7b`
// symbols so the existing FSS*/FM* macros resolve.
//
// Scope: research prototype. Stubs compile and link; runtime correctness
// (buttons, brightness, board init) is intentionally out of scope.

#pragma once

#if !defined(HUVVER)
#error "HuvverShim.h must only be included in HUVVER builds"
#endif

#include <Arduino.h>
#include <TFT_eSPI.h>

// M5GFX exposes color shorthand (BLACK, WHITE, ...) at the global level;
// TFT_eSPI namespaces them as TFT_BLACK etc. Add the M5-flavored aliases
// for source compat — only the ones actually used by main.cpp.
#ifndef BLACK
#define BLACK TFT_BLACK
#endif

// ---------------------------------------------------------------------------
// Free font namespace shim. Free_Fonts.h expands FSS12 -> &fonts::FreeSans12pt7b.
// TFT_eSPI declares those font tables in the global namespace as
// `FreeSans12pt7b` etc. Re-export each into `fonts::` via reference aliases.
// ---------------------------------------------------------------------------

// fonts:: namespace aliases. M5GFX exposes free fonts as `fonts::FreeSans12pt7b`;
// TFT_eSPI exposes them at global scope as `FreeSans12pt7b`. We declare
// extern references in `fonts::` here; the matching definitions in
// HuvverShim.cpp bind each one to its TFT_eSPI counterpart so the linker
// resolves the namespaced address-of expressions used by FSS*/FM* macros.
namespace fonts
{
    extern const GFXfont& FreeMono9pt7b;
    extern const GFXfont& FreeMono12pt7b;
    extern const GFXfont& FreeMono18pt7b;
    extern const GFXfont& FreeMono24pt7b;
    extern const GFXfont& FreeMonoBold9pt7b;
    extern const GFXfont& FreeMonoBold12pt7b;
    extern const GFXfont& FreeMonoBold18pt7b;
    extern const GFXfont& FreeMonoBold24pt7b;
    extern const GFXfont& FreeSans9pt7b;
    extern const GFXfont& FreeSans12pt7b;
    extern const GFXfont& FreeSans18pt7b;
    extern const GFXfont& FreeSans24pt7b;
    extern const GFXfont& FreeSansBold9pt7b;
    extern const GFXfont& FreeSansBold12pt7b;
    extern const GFXfont& FreeSansBold18pt7b;
    extern const GFXfont& FreeSansBold24pt7b;
}

// ---------------------------------------------------------------------------
// textdatum_t namespace shim — main.cpp uses textdatum_t::baseline_left etc.
// Map to TFT_eSPI's integer-typed datum macros (BL_DATUM, MC_DATUM, ...).
// ---------------------------------------------------------------------------

namespace textdatum
{
    constexpr uint8_t top_left      = TL_DATUM;
    constexpr uint8_t top_center    = TC_DATUM;
    constexpr uint8_t top_right     = TR_DATUM;
    constexpr uint8_t middle_left   = ML_DATUM;
    constexpr uint8_t middle_center = MC_DATUM;
    constexpr uint8_t middle_right  = MR_DATUM;
    constexpr uint8_t bottom_left   = BL_DATUM;
    constexpr uint8_t bottom_center = BC_DATUM;
    constexpr uint8_t bottom_right  = BR_DATUM;
    // M5GFX's "baseline" datums map to TFT_eSPI's bottom_* (close enough
    // for prototype purposes).
    constexpr uint8_t baseline_left = BL_DATUM;
}
namespace textdatum_t = textdatum;  // C++17 namespace alias

// ---------------------------------------------------------------------------
// HuvverDisplay — TFT_eSPI plus M5GFX-style setBrightness() so call sites
// like M5.Display.setBrightness(50) compile. The PWM is wired to TFT_BL
// (defined in HuvverPinSetup.h).
// ---------------------------------------------------------------------------

class HuvverDisplay : public TFT_eSPI
{
public:
    HuvverDisplay() : TFT_eSPI() {}

    void setBrightness(uint8_t /*level*/)
    {
        // Stub: real impl would PWM TFT_BL. Acceptable for compile-feasibility.
    }
};

extern HuvverDisplay g_huvverDisplay;

// ---------------------------------------------------------------------------
// M5Canvas — TFT_eSprite plus the M5GFX surface our firmware calls.
//   - setFont(const GFXfont*)  -> setFreeFont
//   - setColorDepth(int)       -> already in TFT_eSprite
// Construction: M5Canvas gdraw(&M5.Display) needs the parent ctor to take
// a TFT_eSPI*. HuvverDisplay derives from TFT_eSPI, so the upcast is
// implicit and the typedef-style usage in main.cpp Just Works.
// ---------------------------------------------------------------------------

class M5Canvas : public TFT_eSprite
{
public:
    explicit M5Canvas(TFT_eSPI* parent) : TFT_eSprite(parent) {}

    using TFT_eSprite::setTextFont;  // keep numeric overload visible
    using TFT_eSprite::setFreeFont;

    // Forward setFont(const GFXfont*) -> setFreeFont for source compat with
    // M5GFX, which exposes setFont as the unified entry point.
    void setFont(const GFXfont* f) { setFreeFont(f); }
};

// ---------------------------------------------------------------------------
// Button stub — minimal surface used by main.cpp (wasPressed, isPressed).
// Always returns false in the prototype; real impl would debounce a GPIO
// (pins BUTTON_A/B/C in HuvverPinSetup.h).
// ---------------------------------------------------------------------------

class HuvverButton
{
public:
    bool wasPressed() { return false; }
    bool isPressed()  { return false; }
};

// ---------------------------------------------------------------------------
// M5_t — global facade. config()/begin() take an opaque struct; we
// accept anything via a templated forwarder so callers keep their
// existing `auto cfg = M5.config(); M5.begin(cfg);` shape.
// ---------------------------------------------------------------------------

struct M5Config { };

class M5_t
{
public:
    HuvverDisplay& Display;
    HuvverDisplay& Lcd;     // M5Stack legacy alias; keep parity for any callers
    HuvverButton   BtnA;
    HuvverButton   BtnB;
    HuvverButton   BtnC;

    M5_t() : Display(g_huvverDisplay), Lcd(g_huvverDisplay) {}

    M5Config config() { return {}; }
    void     begin(const M5Config& /*cfg*/) {}
    void     begin()                        {}
    void     update() {}
};

extern M5_t M5;
