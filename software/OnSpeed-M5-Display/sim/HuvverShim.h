// HuvverShim.h — minimal compat layer that lets the same M5 display source
// also build for huVVer-AVI hardware.
//
// huVVer-AVI is an avionics-panel-mount instrument: ESP32, 240x320 ST7789
// SPI panel, 4 GPIO buttons, dual RS-232 ports, dual DAC audio. The
// upstream OnSpeed sketch (V.R. Little, FlyOnSpeed.org) drove it via
// TFT_eSPI; we drive it via M5GFX's LovyanGFX backend instead, because
// our M5 source is already tuned against M5GFX's text-baseline and
// font-pointer semantics, and re-exposing that surface through TFT_eSPI
// re-introduces a class of layout-drift bugs the M5 port already fixed.
//
// Scope: when `defined(HUVVER)`, this header is included instead of
// `<M5Unified.h>` in main.cpp and SerialRead.cpp. It defines a global
// `M5` object whose surface (`M5.Display`, `M5.BtnA/B/C`, `M5.config()`,
// `M5.begin()`, `M5.update()`) is duck-typed-compatible with M5Unified.
// `M5.Display` is a custom LGFX_Device subclass (`Huvver_LGFX`) wired to
// huVVer's exact panel/bus pinout. Buttons read GPIOs.
//
// Renderer code, GaugeWidgets, font symbols, datum enums, M5Canvas
// usage — all unchanged between M5 and huVVer builds.

#pragma once

#if !defined(HUVVER)
#error "HuvverShim.h must only be included in HUVVER builds"
#endif

// LovyanGFX is the underlying engine M5GFX builds on. <M5GFX.h> brings
// in the LGFX_Device / LGFX_Sprite / LGFXBase classes plus the
// `M5Canvas` and `M5GFX` typedefs the M5 source uses. We do NOT
// include <M5Unified.h> — its `M5` global would auto-detect a
// non-existent M5 board. <M5GFX.h> by itself doesn't pull in
// per-panel or per-platform headers, so we include those explicitly
// for `lgfx::Panel_ST7789`, `lgfx::Bus_SPI`, `lgfx::Light_PWM`.
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#if defined(ESP_PLATFORM)
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>
#endif

#include <Arduino.h>
#include <cstdint>

namespace onspeed::huvver {

// Pin map for huVVer-AVI (V.R. Little's hardware reference, ESP32
// classic variant). Reproduced from the upstream `my_custom_setup.h`
// at https://github.com/huVVer/OnSpeed_huVVer_display.
//
// V.R. Little reserves an `HUVVER_AVI_S3` define for a future ESP32-S3
// rev with a different pin map; that variant is out of scope for now.
struct PinMap {
    // SPI bus (shared by panel only — huVVer's SD-card slot does not
    // exist on the AVI board).
    static constexpr int kSPI_SCLK = 18;
    static constexpr int kSPI_MOSI = 23;
    static constexpr int kSPI_MISO = 19;

    // ST7789 panel control.
    static constexpr int kPanelCS  = 5;
    static constexpr int kPanelDC  = 2;
    static constexpr int kPanelRST = 0;
    static constexpr int kPanelBL  = 15;  // backlight, PWM-driven, HIGH=on

    // Front-panel pushbuttons (per V.R. Little's upstream sketch).
    static constexpr int kBtnA = 39;  // Menu  (square)
    static constexpr int kBtnB = 36;  // Select (round)
    static constexpr int kBtnC = 34;  // Forward (right triangle)
    static constexpr int kBtnD = 35;  // Back   (left triangle)

    // DAC audio output pins (muted on boot — 8-bit DAC hum is audible
    // through any connected headset otherwise).
    static constexpr int kAudR = 26;
    static constexpr int kAudL = 25;
};

// Custom LGFX driver for huVVer's ST7789 panel. Configured per the pin
// map above; LovyanGFX handles the SPI bus + panel init.
//
// PR 1 instantiates this without exercising it on hardware — first-
// light flash is PR 5 work. The constructor sets up the config_t
// structs but `init()` (which actually talks to the panel) is only
// called from `M5_Class::begin()` below, which `setup()` invokes.
class Huvver_LGFX : public lgfx::LGFX_Device {
private:
    lgfx::Panel_ST7789  panel_;
    lgfx::Bus_SPI       bus_;
    lgfx::Light_PWM     light_;

public:
    Huvver_LGFX() {
        {  // SPI bus
            auto cfg = bus_.config();
            cfg.spi_host    = VSPI_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PinMap::kSPI_SCLK;
            cfg.pin_mosi    = PinMap::kSPI_MOSI;
            cfg.pin_miso    = PinMap::kSPI_MISO;
            cfg.pin_dc      = PinMap::kPanelDC;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {  // panel
            auto cfg = panel_.config();
            cfg.pin_cs           = PinMap::kPanelCS;
            cfg.pin_rst          = PinMap::kPanelRST;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = true;
            cfg.invert           = false;  // huVVer panel: TFT_INVERSION_OFF
            cfg.rgb_order        = false;  // BGR per V.R. Little's setup
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            panel_.config(cfg);
        }
        {  // backlight (PWM on TFT_LED_PIN)
            auto cfg = light_.config();
            cfg.pin_bl      = PinMap::kPanelBL;
            cfg.invert      = false;  // HIGH=on
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            light_.config(cfg);
            panel_.setLight(&light_);
        }
        setPanel(&panel_);
    }
};

// Mimics M5Unified's `Button_Class` API surface, just enough for what
// the M5 source actually uses: isPressed(), wasPressed(), pressedFor().
// Button polling reads the GPIO directly — buttons are wired
// active-low through the front-panel switches per V.R. Little's
// schematic.
class HuvverButton {
private:
    int     pin_;
    bool    pressed_      = false;
    bool    prevPressed_  = false;
    uint32_t lastChange_  = 0;
    uint32_t lastPollMs_  = 0;

public:
    explicit HuvverButton(int pin) : pin_(pin) {}

    // Read current GPIO state. Idempotent within one millis() tick.
    void poll() {
        const uint32_t now = millis();
        if (now == lastPollMs_) return;
        lastPollMs_ = now;

        prevPressed_ = pressed_;
        // Active-low: GPIO LOW means button pressed.
        const bool nowPressed = (digitalRead(pin_) == LOW);
        if (nowPressed != pressed_) {
            pressed_    = nowPressed;
            lastChange_ = now;
        }
    }

    bool isPressed()  const { return pressed_; }
    bool wasPressed() const { return pressed_ && !prevPressed_; }
    bool pressedFor(uint32_t ms) const {
        return pressed_ && (millis() - lastChange_) >= ms;
    }
};

// Mimics M5Unified's `M5_t::config_t` for `M5.config()` / `M5.begin(cfg)`.
// Empty for now — huVVer doesn't use any of the M5-specific config
// fields (external speaker, RTC, IMU); the M5 source treats the
// returned struct as opaque and just passes it back to begin().
struct config_t {};

// Mimics the M5Unified `M5_t` global. Renderer code calls this object
// via the global `M5` defined below.
class M5_Class {
public:
    Huvver_LGFX  Display;
    HuvverButton BtnA{PinMap::kBtnA};
    HuvverButton BtnB{PinMap::kBtnB};
    HuvverButton BtnC{PinMap::kBtnC};
    // BtnD (the 4th huVVer button) is unused by the M5 source today
    // — the upstream sketch maps it to "Back" but the M5 nav model
    // only exposes 3 buttons. Reserving here for future use.
    HuvverButton BtnD{PinMap::kBtnD};

    static config_t config() { return {}; }

    void begin(const config_t& /*cfg*/) {
        // Configure button GPIOs as inputs with internal pull-up
        // (huVVer's hardware has no external pull-ups on these lines).
        pinMode(PinMap::kBtnA, INPUT_PULLUP);
        pinMode(PinMap::kBtnB, INPUT_PULLUP);
        pinMode(PinMap::kBtnC, INPUT_PULLUP);
        pinMode(PinMap::kBtnD, INPUT_PULLUP);

        // Mute both DAC audio outputs on boot. The 8-bit DAC produces
        // an audible hum at idle; setting both pins LOW silences it.
        // M5Stack Basic does the same on its single DAC pin (25); for
        // huVVer we mute both 25 and 26.
        pinMode(PinMap::kAudR, OUTPUT);
        pinMode(PinMap::kAudL, OUTPUT);
        digitalWrite(PinMap::kAudR, LOW);
        digitalWrite(PinMap::kAudL, LOW);

        // Initialize the LGFX panel via LovyanGFX's standard init path.
        Display.init();
        Display.setRotation(1);  // 240x320 portrait → 320x240 landscape,
                                 // matching the M5 source's WIDTH/HEIGHT.
    }

    // Polls all buttons. Renderer's main loop calls this every frame.
    void update() {
        BtnA.poll();
        BtnB.poll();
        BtnC.poll();
        BtnD.poll();
    }
};

}  // namespace onspeed::huvver

// Global `M5` — mimics M5Unified's name-resolution. The M5 source uses
// `M5.foo()` and `M5.Display.bar()` unmodified.
//
// `M5Canvas` and `M5GFX` are NOT aliased here — <M5GFX.h> already
// imports `m5gfx::M5Canvas` at file scope (see end of M5GFX.h).
// `M5Canvas gdraw(&M5.Display)` works because M5Canvas's constructor
// accepts any `LovyanGFX*`, and our `Huvver_LGFX` inherits from
// `lgfx::LGFX_Device` which inherits from LovyanGFX.
inline onspeed::huvver::M5_Class M5;
