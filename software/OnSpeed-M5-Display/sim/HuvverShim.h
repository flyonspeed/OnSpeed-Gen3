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

// Inline variables (`inline onspeed::huvver::M5_Class M5;` below) need
// C++17 to avoid ODR violations across the multiple TUs that include
// this header transitively. Fail the build loudly if a future change
// drops -std=gnu++17 from the env.
static_assert(__cplusplus >= 201703L,
              "HuvverShim.h requires C++17 for inline variables (M5 global)");

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
            // The AVI board has no SD card slot or other SPI peripherals,
            // so LovyanGFX can hold the bus exclusively for the panel.
            // If a future hardware revision adds any SPI peripheral,
            // this MUST be changed to `true` and the panel transactions
            // must be wrapped in startWrite()/endWrite() — otherwise
            // the bus contends and the SPI traffic deadlocks.
            cfg.bus_shared       = false;
            panel_.config(cfg);
        }
        {  // backlight (PWM on TFT_LED_PIN)
            auto cfg = light_.config();
            cfg.pin_bl      = PinMap::kPanelBL;
            cfg.invert      = false;  // HIGH=on
            cfg.freq        = 44100;
            // LEDC channel 7. The ESP32 Arduino framework reserves
            // channels 0-3 for analogWrite(); channels 4-6 are likely
            // needed for audio tone generation in PR 5+ (the upstream
            // huVVer sketch's mute path uses ledcSetup with channel 4
            // for backlight, but our LovyanGFX-managed light reuses 7
            // to leave 4-6 free).
            cfg.pwm_channel = 7;
            light_.config(cfg);
            panel_.setLight(&light_);
        }
        setPanel(&panel_);
    }
};

// Mimics M5Unified's `Button_Class` API surface, just enough for what
// the M5 source actually uses: isPressed(), wasPressed(), pressedFor().
// Buttons are wired active-low through the front-panel switches per
// V.R. Little's schematic.
//
// Debounce: the front-panel tactile switches bounce for ~5–20 ms on
// each press/release. Polling the raw GPIO and edge-detecting against
// the previous poll's value lets a single physical press register as
// 2–6 separate `wasPressed()` events — chaotic for mode cycling and
// brightness ramping. The implementation below only commits a
// candidate level to the stable state once it has held for
// kDebounceMs. 50 ms is the canonical avionics-button debounce — past
// the 99th percentile of bounce duration on consumer-grade tactile
// switches, well below human perceptual threshold for press latency.
class HuvverButton {
private:
    static constexpr uint32_t kDebounceMs = 50;

    int      pin_;
    bool     stable_         = false;  // committed (debounced) state
    bool     prevStable_     = false;  // stable_ from previous poll, for wasPressed()
    bool     candidate_      = false;  // current raw reading awaiting confirmation
    uint32_t candidateSince_ = 0;      // millis() when candidate_ first read
    uint32_t stableSince_    = 0;      // millis() when stable_ last transitioned, for pressedFor()

public:
    explicit HuvverButton(int pin) : pin_(pin) {}

    // Sample GPIO and advance the debounce state machine. Each call
    // advances `prevStable_` so `wasPressed()` returns true on exactly
    // one call after a press edge — regardless of how many loop()
    // iterations happen per millisecond. (At 240 MHz the M5 loop runs
    // many iterations per ms; a once-per-ms throttle on this state
    // machine would let `wasPressed()` keep returning true across
    // those iterations, registering one physical press as 5–10 events.)
    void poll() {
        const uint32_t now = millis();

        prevStable_ = stable_;

        // Active-low: GPIO LOW means button pressed.
        const bool reading = (digitalRead(pin_) == LOW);

        if (reading != candidate_) {
            // Reading just changed — start a new debounce window.
            candidate_      = reading;
            candidateSince_ = now;
        } else if (reading != stable_ && (now - candidateSince_) >= kDebounceMs) {
            // Candidate has held for the debounce window — commit.
            stable_      = reading;
            stableSince_ = now;
        }
    }

    bool isPressed()  const { return stable_; }
    bool wasPressed() const { return stable_ && !prevStable_; }
    bool pressedFor(uint32_t ms) const {
        return stable_ && (millis() - stableSince_) >= ms;
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
        // LovyanGFX rotation 1 = 90° clockwise from native portrait,
        // producing 320x240 landscape from a 240x320 panel — matching
        // the M5 source's WIDTH=320 HEIGHT=240 assumption. This is the
        // same rotation V.R. Little's upstream sketch uses on the same
        // panel hardware (OnSpeed_huVVer_display.ino:236 calls
        // tft.setRotation(1)).
        Display.setRotation(1);
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
