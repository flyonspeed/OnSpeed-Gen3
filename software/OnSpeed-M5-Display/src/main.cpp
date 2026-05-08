// Ported from OnSpeed Gen2 OnSpeed_M5_display/OnSpeed_M5_display.ino
// M5Stack Core ESP32 secondary display for OnSpeed AOA system.
//
// Provides 5 display modes: Energy Display, Attitude, Indexer, Decel
// Display, and Historic G.
//
// Changes from Gen2:
// - Replaced buggy SavLayFilter with correct SavGolDerivative
// - Fixed deceleration sign convention (no longer needs negation)
// - Split into main.cpp + SerialRead.cpp/h
// - Moved serialSetup() into SerialRead module

// Build: see CLAUDE.md in this directory (`pio run -e m5stack-core-esp32`
// for Basic, `pio run -e m5stack-core2` for Core2). No Arduino-IDE path.

/*
Copyright 2020 V.R. Little

Permission is hereby granted, free of charge, to any person provided a copy of this software and associated documentation files
(the "Software") to use, copy, modify, or merge copies of the Software for non-commercial purposes, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*
 This code is based on the work on Vern Little, see copyright notice above.
 Adapted by/for FlyOnSpeed.org, 2021. lenny@flyonspeed.org
 Ported to Gen3 repository, 2026.
*/

// Runtime-provided version string. Regenerated from git tags each build by
// scripts/generate_buildinfo.py (see BuildInfo::version in buildinfo.cpp).
// Non-release builds look like "4.17.1-dev.19+35a823b" — tagged release
// builds drop the -dev suffix and the +sha metadata.
#include <buildinfo.h>

// lroundf at the GaugeWidgets boundary; reaches us transitively today
// via M5Unified/M5GFX, but this declaration is load-bearing enough to
// not rely on transitive provenance.
#include <cmath>

//#define SERIALDATADEBUG   // show serial packet debug
//#define DUMMY_SERIAL_DATA // dummy serial data for display test

// IAS_IN_MPH gates the IAS units readout (knots default, MPH if
// defined).  Tracked for runtime promotion in issue #419 — once that
// lands, the `#define` goes away and pilots flip units at runtime.
// Until then, uncomment to rebuild with MPH.
//#define IAS_IN_MPH

#include <GaugeWidgets.h>
#if defined(HUVVER)
#include "../sim/HuvverShim.h"
#else
#include <M5Unified.h>
#endif
#include <Free_Fonts.h>

// On the desktop (SDL) and WASM (Emscripten) builds, the ESP32-only headers
// below don't exist; the ArduinoShim provides String/Serial/Preferences and
// the firmware-upgrade path guarded by `ESP_PLATFORM` compiles out entirely.
#if defined(ESP_PLATFORM)
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#else
#include "../sim/ArduinoShim.h"
#endif

#include "SerialRead.h"
#include <gauges/FlapWidgetMath.h>

#if defined(ESP_PLATFORM)
Preferences preferences;

WebServer server(80);

const char* ssid         = "OnSpeedDisplay";
const char* password     = "angleofattack";
uint8_t     aiIP[4]      = {192, 168, 0, 2};
#endif
bool        fwUpdateMode = false;

#define TFT_GREY        0x7BEF
#define TFT_LIGHT_GREY  0xAD55

// Color depth for the off-screen sprite.  Defaults to 8bpp (palette
// indexed) for the firmware builds where flash + RAM matter.  The
// X-Plane plugin overrides to 16bpp via the build flag so its custom
// Panel_FrameBufferBase subclass can read out RGB565 directly.
#ifndef XPLANE_PLUGIN_DEPTH
#define XPLANE_PLUGIN_DEPTH 8
#endif

M5Canvas        gdraw(&M5.Display);
Gauges          myGauges;

// Percent-lift anchors, populated from the wire fields each frame and
// passed to drawAOA / drawSlip / mapPct2Display by index.  Two cues
// are visually distinct (Vac, ld_max.pdf §8 — aerodynamic references
// and operational cues must remain independent):
//   [0] floor               (always 0 — alpha_0 in percent space)
//   [2] kIdxTonesOn         active-detent L/Dmax pct (TonesOnPctLift) —
//                           operational, snapped per detent. Drives
//                           the bottom-chevron lower gate at drawAOA
//                           line ~930.  Same threshold as the audio
//                           low tone.
//   [3] kIdxOnSpeedFast     donut bottom edge (OnSpeedFastPctLift)
//   [4] kIdxOnSpeedSlow     donut top edge / top-chevron lower gate
//                           (OnSpeedSlowPctLift)
//   [6] kIdxPipPctLift      visual L/Dmax pip (PipPctLift) —
//                           aerodynamic reference, lerped clean→
//                           fullflap. Drives the pip dots at
//                           drawAOA line ~1010.
//   [7] kIdxStallWarn       top-chevron flash threshold
//                           (StallWarnPctLift)
// Slots 1 and 5 are reserved for future use; assigning 0 makes the
// renderer treat them as "uncalibrated".
namespace pct_anchors {
    constexpr int kIdxTonesOn      = 2;   // active-detent L/Dmax (chevron + audio gate)
    constexpr int kIdxOnSpeedFast  = 3;
    constexpr int kIdxOnSpeedSlow  = 4;
    constexpr int kIdxPipPctLift   = 6;   // visual L/Dmax pip (lerp clean→fullflap)
    constexpr int kIdxStallWarn    = 7;
}
int PctAnchors[8];

// screen size variables
const uint16_t  WIDTH               = 320; //X
const uint16_t  HEIGHT              = 240; //Y

// VSI bar scaling for Mode 1 (Attitude) and Mode 3 (Energy).  120-px
// bar fills at ±600 fpm — fine-grained for the gentle-cruise band
// pilots typically reference in normal flight.  Saturates beyond,
// which is intentional: the IAS / FPA readouts cover larger excursions.
// Mirror constant in tools/web/lib/core/geometry.js (MODE1_VSI_*).
constexpr int kVsiBarHeightPx     = 120;
constexpr int kVsiBarFullScaleFpm = 600;

// display variables
uint64_t        loopTime            = millis();
uint64_t        currentMillis;
uint64_t        previousMillis      = millis();
uint64_t        flashTime           = millis();
uint64_t        numbersUpdateTime;
uint64_t        gHistoryTime        = millis();
// Initial defaults.  `displayBrightness` and `displayType` are both
// restored from NVS at boot (see setup()); pilot adjustments via
// BtnA/BtnC (brightness) and BtnB (mode cycle) persist on press, so
// the M5 boots back into whatever brightness/page it was on at last
// power-down.  Defaults below apply on a fresh M5 with no saved
// preference: full brightness, Mode 0 (Energy Display).
uint16_t        displayBrightness   = 255;
int16_t         displayType         = 0;

// Canonical names per Vac (VAF threads 228078, 225345). Mirrors
// tools/web/lib/pages/IndexerPage.js MODES. Indexed by displayType.
// Used by the X-Plane plugin's on-canvas MODE button (see
// software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp);
// the M5 hardware itself doesn't render the name today.
// `extern` is required: namespace-scope `const` defaults to internal
// linkage in C++, which would hide the symbol from the X-Plane plugin
// linker.
extern const char* const kModeNames[5] = {
    "Energy",      // 0 — boot default (Energy Display, shortened)
    "Attitude",    // 1
    "Indexer",     // 2 — AOA-only page
    "Decel",       // 3 — Decel Display, shortened
    "Historic G",  // 4
};
boolean         numericDisplay;
boolean         flashFlag;
// Match the OnSpeed #1 frame rate (50 ms cadence from DisplaySerial.cpp)
// so each incoming data frame renders.
const uint16_t  updateRateGraphics  =  50;  //milliseconds
const uint16_t  updateRateNumbers   = 500;  //milliseconds
const uint16_t  flashRate           = 250;  //milliseconds

// Serial data variables (definitions, declared extern in SerialRead.h)
float           PercentLift         = 0.0f;
float           Pitch               = 0.0;
float           Roll                = 0.0;
float           IAS                 = 0.0;
// Default to false so a powered M5 with no serial frames yet shows
// dashed IAS / percentLift rather than a stale "0".  Flips to the
// wire's iasIsValid value once frames start arriving.
bool            IasIsValid          = false;
float           Palt                = 0.0;
float           iVSI                = 0.0;
float           VerticalG           = 1.0;
float           LateralG            = 0.0;
float           FlightPath          = 0.0;
int             FlapPos             = 0;
int             OAT                 = 0;
int16_t         Slip                = 0;

// Per-flap band-edge percents — populated from #1 wire fields.  The
// indexer's chevrons / donut / L/Dmax pip / index bar are all
// rendered against these percent anchors via mapPct2Display.  Default
// values are an uncalibrated "zero everywhere" so the indexer renders
// pinned to the bottom until the first real frame arrives.
int             TonesOnPctLift      = 0;
int             OnSpeedFastPctLift  = 0;
int             OnSpeedSlowPctLift  = 0;
int             StallWarnPctLift    = 0;
int             PipPctLift          = 0;
int             FlapsMinDeg         = 0;
int             FlapsMaxDeg         = 33;

float           gOnsetRate          = 0.0;
int             SpinRecoveryCue     = 0;
int             DataMark            = 0;
float           DecelRate           = 0.0;
float           SmoothedDecelRate   = 0.0;
float           gHistory[300];
int             gHistoryIndex       = 0;
uint64_t        serialMillis        = millis();

// number display variables
float           displayIAS          = 0.0;
float           displayPalt         = 0.0;
float           displayPitch        = 0.0;
float           displayVerticalG    = 0.0;
int             displayPercentLift  = 0;
float           displayDecelRate    = 0.0;

unsigned int    selectedPort        = 0; // selected serial port

// Attitude indicator variables (globals; used only at the AiGraph call site
// to pass initial geometry. `g_` prefix keeps them from shadowing the
// AiGraph parameters of the same semantic name.)
int16_t         g_px0               = 159;
int16_t         g_py0               = 119;
int16_t         g_arcSize           = 115;
int16_t         g_arcWidth          = 15;
int16_t         g_maxDisplay        = 360;
int16_t         g_minDisplay        = 0;
int16_t         g_startAngle        = 0;
int16_t         g_arcAngle          = 360;
int16_t         g_clockWise         = true;
uint8_t         g_gradMarks         = 0;

// AOA widget variables and defaults
uint16_t        wgtWidth;
uint16_t        wgtHeight;
uint16_t        wgtX0;
uint16_t        wgtY0;

// Forward declarations
void drawAOA(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, float aoaPct, boolean flashing, const int Array[]);
void drawSlip (uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, int16_t slipValue, boolean flashing, const int Array[]);
void displayAOA();
void displayDecelGauge();
void displayGloadHistory();
void displaySplashScreen();
void AiGraph (int16_t px0, int16_t py0, int16_t arcSize, int16_t arcWidth, int16_t maxDisplay, int16_t minDisplay,
              int16_t startAngle, int16_t arcAngle, bool clockWise, uint8_t gradMarks,
              float pitch, float roll, int16_t yaw, float flightPathAngle);
void pitchGraph(float pitch, float roll, int16_t px0, int16_t py0, uint8_t scale);
int mapPct2Display(int aoaPct, const int Array[]);
int mapPct2Display(float aoaPctF, const int Array[]);
int map2int(float aoa, float inLow, float inHigh, int outLow, int outHigh);
#if defined(ESP_PLATFORM)
void handleUpgrade();
void handleUpgradeSuccess();
void handleUpgradeFailure();
void handleIndex();
#endif


// -----------------------------------------------
// setup()
// -----------------------------------------------

void setup()
{

    //
    // initialize the M5Stack object and clear display
    //
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.fillScreen(BLACK);
    M5.Display.setBrightness(50);
    // Mute the speaker (annoying hiss) on M5Stack Basic, where GPIO 25 is
    // the internal DAC-to-speaker path. Skip on Core2, where GPIO 25 is an
    // exposed Port-B pin and the speaker is driven via I2S amplifier (not
    // this DAC). See audit finding 039. Skip on huVVer too — HuvverShim's
    // M5_Class::begin() already drove GPIO 25 (and 26) LOW as a regular
    // digital output; calling dacWrite here would reconfigure the pin
    // through the DAC peripheral, leaving it in a mode where future
    // digitalWrite() calls would silently no-op.
#if !defined(ARDUINO_M5STACK_Core2) && !defined(HUVVER)
    dacWrite(25, 0);
#endif

    gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
    gdraw.createSprite(WIDTH, HEIGHT);
    gdraw.fillSprite (TFT_BLACK);

    // prefill gHistory buffer
    for (int i = 0; i < 300; i++)
        gHistory[i] = 1.00;

    displaySplashScreen();

    // duration of splash screen display, check for center button for fw upgrade
    uint64_t waitTime = millis();
    while (millis()-waitTime<3000)
    {
        M5.update();

#if defined(ESP_PLATFORM)
        //Button B held down at bootup
        if (M5.BtnB.isPressed())
        {
            fwUpdateMode=true;
            gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
            gdraw.createSprite(WIDTH, HEIGHT);
            gdraw.fillSprite (TFT_BLACK);
            gdraw.setFont(FSSB12);
            gdraw.setTextColor (TFT_WHITE);
            gdraw.setTextDatum(MC_DATUM);
            gdraw.drawString("Firmware Upgrade Server",160,20);

            gdraw.setFont(FSS12);
            gdraw.setTextDatum(ML_DATUM);
            gdraw.drawString("Wifi SSID: "+String(ssid),20,70);
            gdraw.drawString("Password: "+String(password),20,100);
            gdraw.drawString("Browse to:",20,140);
            gdraw.drawString("http://"+String(aiIP[0])+"."+String(aiIP[1])+"."+String(aiIP[2])+"."+String(aiIP[3])+"/upgrade",20,170);
            gdraw.setTextDatum(ML_DATUM);
            gdraw.drawString(String(BuildInfo::version), 5, 215);

            gdraw.setFont(FSSB12);
            gdraw.setTextColor (TFT_RED);
            gdraw.setTextDatum(MR_DATUM);
            gdraw.drawString("EXIT",280,215);

            gdraw.pushSprite (0, 0);
            gdraw.deleteSprite();

            WiFi.softAP(ssid, password);
            delay(100); // wait to init softAP

            IPAddress Ip(aiIP[0], aiIP[1], aiIP[2], aiIP[3]);
            IPAddress NMask(255, 255, 255, 0);
            WiFi.softAPConfig(Ip, Ip, NMask);
            IPAddress myIP = WiFi.softAPIP();

            Serial.print("AP IP address: ");
            Serial.println(myIP);
            delay(100);

            server.begin();

            // Handle uploading firmware file
            server.on("/upgrade", HTTP_GET, handleUpgrade);
            server.on("/", HTTP_GET, handleIndex);

            server.on("/upload", HTTP_POST,
                []()
                {
                    server.sendHeader("Connection", "close");
                    if (Update.hasError()) handleUpgradeFailure(); else handleUpgradeSuccess();
                    // reset serial port preference on upgrade
                    preferences.begin("OnSpeed", false);
                    preferences.putUInt("SerialPort", 0);
                    preferences.end();
                    delay(5000);
                    ESP.restart();
                },
                []()
                {
                    HTTPUpload& upload = server.upload();
                    if (upload.status == UPLOAD_FILE_START)
                    {
                        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                        { //start with max available size
                        }
                        gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
                        gdraw.createSprite(WIDTH, HEIGHT);
                        gdraw.fillSprite (TFT_BLACK);
                        gdraw.setFont(FSSB12);
                        gdraw.setTextColor (TFT_WHITE);

                        gdraw.setTextDatum(MC_DATUM);
                        gdraw.drawString("Upgrading Firmware",160,90);

                        gdraw.setFont(FSS12);
                        gdraw.drawString("Please wait...",160,150);

                        gdraw.pushSprite (0, 0);
                        gdraw.deleteSprite();
                    }

                    else if (upload.status == UPLOAD_FILE_WRITE)
                    {
                        /* flashing firmware to ESP*/
                        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                        }
                    }

                    else if (upload.status == UPLOAD_FILE_END)
                    {
                        if (Update.end(true))
                        { //true to set the size to the current progress
                        }
                    }
                }
            ); // end server.on()

            // called when the url is not defined here
            server.onNotFound(
                []()
                {
                    server.send(404, "text/plain", "FileNotFound");
                }
            ); // end server.onNotFound()
            break; // break while loop
        } // end if Button B is pressed
#endif // ESP_PLATFORM

    } // end while splash screen

    if (fwUpdateMode)
        return; // do not continue if firmware upgrade mode was selected.

#if defined(ESP_PLATFORM)
    // Console UART for diagnostic prints. Opened in both real-data
    // and DUMMY_SERIAL_DATA builds so debug log lines reach the host
    // regardless of which mode is active.
    Serial.begin(115200);
    delay (100);
#if !defined(DUMMY_SERIAL_DATA)
    // select serial port from preferences or detect it (Serial2 — the
    // OnSpeed `#1` wire input).
    serialSetup();
#endif

    // Restore the pilot's persisted brightness + mode.  The splash
    // ran at the fixed 50/255 dim; loop() applies displayBrightness on
    // every iteration so the first frame after splash gets the right
    // value.  Defaults: full bright, Mode 0 on a fresh M5 with no saved
    // preferences.
    //
    // DisplayType uses getShort/putShort (signed nvs_get_i16) because
    // displayType is int16_t even though values are 0..4.  Brightness
    // uses uint16_t and the unsigned variants.  The pair has to stay
    // matched: NVS treats signed vs unsigned i16 as distinct slots, so
    // writing with putShort and reading with getUShort returns the
    // default, not the value.  Don't "normalize" to a single variant
    // without also clearing the existing key.
    preferences.begin("OnSpeed", true);  // read-only
    displayBrightness = preferences.getUShort("Brightness", 255);
    displayType       = preferences.getShort("DisplayType", 0);
    preferences.end();
    displayBrightness = constrain(displayBrightness, 1, 255);
    displayType       = constrain(displayType, 0, 4);
#endif

} // end setup()


// -----------------------------------------------
// loop()
// -----------------------------------------------

void loop()
{
#ifndef XPLANE_PLUGIN_BUILD
    // M5.update() polls buttons / IMU / RTC / PMIC via the M5Unified
    // singleton.  In the X-Plane plugin context M5.begin() was never
    // called (we drive M5.Display.setPanel() directly without bringing
    // up the full board), so the singleton sits on Panel_NULL and
    // calling update() pokes uninitialized hardware peripheral state.
    // Today this is harmless (M5Unified no-ops on Panel_NULL); guarded
    // for defense-in-depth against a future M5GFX version where the
    // default device gets a less benign auto-init.
    M5.update();
#endif

#if defined(ESP_PLATFORM)
    if (fwUpdateMode)
    {
        server.handleClient();
        if (M5.BtnC.wasPressed())
        {
            fwUpdateMode = false;
            WiFi.softAPdisconnect (true);
#if !defined(DUMMY_SERIAL_DATA)
            serialSetup(); // firmware update canceled, set up serial port
#endif
        }
        return;
    } // end if fwUpdateMode
#endif // ESP_PLATFORM

    SerialRead(); // get serial data

    //
    // Change display brightness and display format using panel buttons.
    //
    // BtnC doubles brightness, BtnA halves it. constrain() below clamps to
    // the [1, 255] range so no guards are needed on the actions themselves.
    // Persist to NVS on each adjust so the pilot's chosen brightness
    // survives power-cycles — same "OnSpeed" namespace the SerialPort
    // detection uses (SerialRead.cpp).  M5Unified's NVS handles wear-
    // leveling; button-press cadence is far below the rewrite limits.
    const uint16_t prevBrightness = displayBrightness;
    if (M5.BtnC.wasPressed()) displayBrightness *= 2;  // brightness up
    if (M5.BtnA.wasPressed()) displayBrightness /= 2;  // brightness down

    displayBrightness = constrain(displayBrightness, 1, 255);

    M5.Display.setBrightness(displayBrightness);

#if defined(ESP_PLATFORM)
    if (displayBrightness != prevBrightness)
    {
        preferences.begin("OnSpeed", false);
        preferences.putUShort("Brightness", displayBrightness);
        preferences.end();
    }
#else
    (void)prevBrightness;
#endif

    if (M5.BtnB.wasPressed())
    {
        gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
        gdraw.createSprite(WIDTH, HEIGHT);
        gdraw.fillSprite (TFT_BLACK);
        displayType ++;
        if (displayType > 4) displayType = 0; // type of display

#if defined(ESP_PLATFORM)
        // Persist the new mode the same way brightness persists on
        // BtnA/BtnC: write on every press.  M5Unified's NVS handles
        // wear-leveling; pilots cycle modes a handful of times per
        // flight, far below rewrite limits.
        preferences.begin("OnSpeed", false);
        preferences.putShort("DisplayType", displayType);
        preferences.end();
#endif
    }

    // update G history buffer (finding 037: only when data is fresh —
    // otherwise the 60 s Mode 4 trace backfills with the frozen last-known
    // VerticalG during an outage, misleading the pilot into thinking no G
    // excursions occurred in that window).
    if (serialDataFresh() && millis()-gHistoryTime>200)
    {
        gHistory[gHistoryIndex]=VerticalG;
        if (gHistoryIndex<299)  gHistoryIndex++;
        else                    gHistoryIndex = 0;
        gHistoryTime=millis();
    }

    // Update graphics
    if (millis() > (loopTime + updateRateGraphics))
    {
        loopTime = millis();

        gdraw.setColorDepth(XPLANE_PLUGIN_DEPTH);
        gdraw.createSprite(WIDTH, HEIGHT);
        gdraw.fillSprite (TFT_BLACK);
        // Reset text anchoring for this frame. M5GFX's print()/setCursor()
        // honors the current datum; the old M5Stack/TFT_eSPI fork always
        // anchored setCursor-style output at the top-left regardless.
        // Re-seat to TL_DATUM here so setCursor(x,y)+print(...) sequences
        // in this render behave as the Gen2 layout code assumed. Any block
        // that needs a different datum sets it locally around the
        // drawString() call and falls back to TL_DATUM for later code.
        gdraw.setTextDatum(textdatum_t::baseline_left);

        // update numbers at a slower rate so they are readable
        if (millis()-numbersUpdateTime>updateRateNumbers)
        {
#ifdef IAS_IN_MPH
            displayIAS         = IAS * 1.15078;
#else
            displayIAS         = IAS;
#endif
            displayPalt        = Palt;
            displayPitch       = Pitch;
            displayVerticalG   = VerticalG;
            // Snapshot for the readout digits.  Truncate (NOT round) and
            // clamp to [0, 99] so:
            //   (a) the digit stays in lockstep with the chevron color
            //       comparisons, which use the raw float aoaPct against
            //       integer band anchors via implicit promotion (e.g.
            //       `aoaPct >= Array[2]` flips at exactly aoaPct == 33,
            //       not at aoaPct == 32.5).  Rounding here would advance
            //       the digit to "33" at PercentLift = 32.5 while the
            //       chevron stays dark until 33.0 — a per-frame
            //       indicator-vs-color disagreement at the threshold.
            //   (b) a saturated 99.9 (the float ceiling) renders as
            //       "99", not "100" — the saturation convention is
            //       "never reads 100", same as it's always been.  Old
            //       code `PercentLift = f.percentLift / 10` (int /
            //       int) was also truncation, so this matches v4.23
            //       behavior exactly.
            {
                int pctInt = static_cast<int>(PercentLift);
                if (pctInt < 0)  pctInt = 0;
                if (pctInt > 99) pctInt = 99;
                displayPercentLift = pctInt;
            }
            displayDecelRate   = SmoothedDecelRate;
            numbersUpdateTime  = millis();
        } // end if update numbers

        /*
        Main AOA alarm detection & update AOA display
        */
        switch (displayType)
        {
            case 0: // Energy Display — indexer + IAS/G/PALT/flap/slip/percent
            {
                wgtWidth = 102;
                wgtHeight = 192;
                wgtX0 = (WIDTH - wgtWidth) / 2; wgtY0 = 0;
                numericDisplay = true;
                displayAOA();
                break;
            }

            case 1: // Attitude — synthetic horizon + flight-path marker
            {
                // display Attitude Indicator
                AiGraph (g_px0, g_py0, g_arcSize, g_arcWidth, g_maxDisplay, g_minDisplay, g_startAngle, g_arcAngle, g_clockWise,
                g_gradMarks, Pitch, Roll, 360, FlightPath);

                // All four corners use setCursor+print with baseline_left
                // (frame default) for consistent y semantics. Right-side
                // alignment is done by computing x = RIGHT_X - textWidth().
                // RIGHT_X = 303 pulls in from the panel edge to clear the
                // VSI tick ladder at x=313.
                constexpr int RIGHT_X = 307;

                // ----- Labels (sit close under/over their numbers) -----
                // Tuned against the baseline_left datum convention. The
                // previous layout placed labels ~7 px higher because
                // pitchGraph() left MC_DATUM active (centered text).
                // After the datum-restore fix, +7 keeps the visual
                // position stable.
                constexpr int TOP_LABEL_Y = 62;   // just below number baseline=30
                constexpr int BOT_LABEL_Y = 230;  // just below number baseline=198
                gdraw.setFont(FSS12);

                gdraw.setCursor(5, TOP_LABEL_Y);
                gdraw.setTextColor (TFT_GREY);
                gdraw.print("IAS");

                gdraw.setCursor(5, BOT_LABEL_Y);
                gdraw.setTextColor (TFT_LIGHT_GREY);
                gdraw.print("G");

                gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth("PALT"), TOP_LABEL_Y);
                gdraw.setTextColor (TFT_GREY);
                gdraw.print("PALT");

                gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth("AOA"), BOT_LABEL_Y);
                gdraw.setTextColor (TFT_LIGHT_GREY);
                gdraw.print("AOA");

                // ----- Pitch readout (center, over horizon line) -----
                gdraw.fillRoundRect(55,129,56,21,3,TFT_DARKGREY);
                gdraw.setTextColor (TFT_WHITE);
                char PitchStr[8];
                // Worst-case "-99.9\0" is 6 bytes; "%1.1f" has no max width
                // so any 2+-digit or negative pitch overflowed the old [4].
                static_assert(sizeof(PitchStr) >= 6,
                              "PitchStr must hold at least \"-99.9\\0\" (6 bytes)");
                snprintf(PitchStr, sizeof(PitchStr), "%1.1f", displayPitch);
                gdraw.setTextDatum(textdatum_t::middle_right);
                gdraw.drawString(PitchStr,100,138);
                gdraw.setTextDatum(textdatum_t::baseline_left); // restore
                // draw degree symbol
                gdraw.drawCircle (106, 132, 0.50f * 5, TFT_WHITE);

                // ----- Numbers -----
                gdraw.setFont(FSSB18);
                gdraw.setTextColor (TFT_BLACK);

                // IAS dashes when the producer's bIasAlive is false (the
                // wire's iasKt sentinel was 9999); see proto/DisplaySerial.h
                // and issue #358.  Dashes right-align to where a 3-digit
                // IAS reading would end so the placeholder sits in the
                // same visual column as live digits.
                if (IasIsValid)
                    {
                    gdraw.setCursor(5, 30);
                    gdraw.print(int(displayIAS));
                    }
                else
                    {
                    const int iasRightX = 5 + (int)gdraw.textWidth("000");
                    gdraw.setCursor(iasRightX - (int)gdraw.textWidth("--"), 30);
                    gdraw.print("--");
                    }

                char PressAltStr[8];
                snprintf(PressAltStr, sizeof(PressAltStr), "%5.0f", displayPalt);
                gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth(PressAltStr), 30);
                gdraw.print(PressAltStr);

                gdraw.setTextColor (TFT_WHITE);
                gdraw.setCursor(5, 198);
                gdraw.printf("%1.1f", displayVerticalG);

                // Percent-lift dashes whenever IAS is invalid — without
                // air data, percent-of-stall is not meaningful (same
                // gate the producer applies).
                char PctStr[4];
                if (IasIsValid)
                    snprintf(PctStr, sizeof(PctStr), "%02d", displayPercentLift);
                else
                    snprintf(PctStr, sizeof(PctStr), "--");
                gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth(PctStr), 198);
                gdraw.print(PctStr);

                // Update ball display on attitude page
                // Increase sensitivity of slip indicator
                drawSlip(80, 204, 160, 20, Slip, false, PctAnchors);

                // iVSI
                // draw iVSI line
                if (iVSI!=0.0)
                {
                    int vsiHeight=abs(int(iVSI*kVsiBarHeightPx/kVsiBarFullScaleFpm));
                    vsiHeight=constrain(vsiHeight,0,kVsiBarHeightPx);
                    int vsiTop;
                    if (iVSI > 0) vsiTop = 119 - vsiHeight;
                    else          vsiTop = 119;
                    gdraw.fillRect(313,vsiTop,7,vsiHeight,TFT_WHITE);
                }

                // vsi ladder, every 20 pixels
                for (int i=19;i<220;i=i+20)
                {
                    gdraw.drawLine (313, i, 319, i, TFT_BLACK);
                }

                //zero line
                gdraw.drawLine (306, 118, 312, 118, TFT_BLACK);
                gdraw.drawLine (306, 119, 312, 119, TFT_BLACK);
                gdraw.drawLine (306, 120, 312, 120, TFT_BLACK);

                break;
            }

            case 2: // Indexer — AOA-only page (chevrons + slip, no corners)
            {
                wgtWidth  = 102;
                wgtHeight = 192;
                wgtX0 = (WIDTH - wgtWidth) / 2;
                wgtY0 = 0;
                numericDisplay = false;
                displayAOA();
                break;
            }

            case 3: // Decel Display — IAS-derivative gauge in kt/s
            {
                displayDecelGauge();
                break;
            }

            case 4: // Historic G — 60s vertical-G strip chart
            {
                displayGloadHistory();
                break;
            }

            default: break;
        } // end switch on display type

        // Look for serial link failure (finding 037: use the shared
        // serialDataFresh() predicate rather than duplicating the threshold).
        // Draw red lines across display
        if (!serialDataFresh())
        {
            gdraw.fillSprite (TFT_BLACK);
            gdraw.drawLine (0, 0, 319, 239, TFT_RED); // center

            gdraw.drawLine (0, 1, 318, 239, TFT_RED); // left
            gdraw.drawLine (0, 2, 317, 239, TFT_RED);
            gdraw.drawLine (0, 3, 316, 239, TFT_RED);
            gdraw.drawLine (0, 4, 315, 239, TFT_RED);

            gdraw.drawLine (1, 0, 319, 238, TFT_RED); // right
            gdraw.drawLine (2, 0, 319, 237, TFT_RED);
            gdraw.drawLine (3, 0, 319, 236, TFT_RED);
            gdraw.drawLine (4, 0, 319, 235, TFT_RED);

            gdraw.drawLine (0, 239, 319, 0, TFT_RED); // center

            gdraw.drawLine (0, 238, 318, 0, TFT_RED); // left
            gdraw.drawLine (0, 237, 317, 0, TFT_RED);
            gdraw.drawLine (0, 236, 316, 0, TFT_RED);
            gdraw.drawLine (0, 235, 315, 0, TFT_RED);

            gdraw.drawLine (1, 239, 319, 1, TFT_RED); // right
            gdraw.drawLine (2, 239, 319, 2, TFT_RED);
            gdraw.drawLine (3, 239, 319, 3, TFT_RED);
            gdraw.drawLine (4, 239, 319, 4, TFT_RED);

            gdraw.setFont(FSSB18);
            gdraw.setTextColor (TFT_WHITE);
            gdraw.setTextDatum(MC_DATUM);
            gdraw.fillRect(100,100,120,40,TFT_BLACK);
            gdraw.drawString("NO DATA",160,120);

            gdraw.pushSprite (0, 0);
            gdraw.deleteSprite();
            return;
        } // end if serial data timeout

    } // end if time to update graphics

    if (millis()-flashTime>=flashRate)
        {
        flashFlag = !flashFlag;
        flashTime=millis();
        }

    gdraw.pushSprite (0, 0);
    gdraw.deleteSprite();
} // end loop()


// -----------------------------------------------

// Update AOA display

void displayAOA()
{
    // Build percent-lift anchor array.  All values come from the wire
    // and represent the per-flap setpoints expressed as percent-lift
    // (the honest single-linear formula in onspeed_core/aoa/PercentLift).
    // Slot 0 is the floor (always 0 in percent space).  Slot 2 is the
    // L/Dmax pip position — slides per flap because TonesOnPctLift
    // varies per flap.  Slots 3, 4, 7 are the band edges used by
    // mapPct2Display and the chevron-color logic in drawAOA.
    PctAnchors[0] = 0;
    PctAnchors[1] = 0;                                       // unused
    PctAnchors[pct_anchors::kIdxTonesOn]     = TonesOnPctLift;     // operational — chevron + audio gate
    PctAnchors[pct_anchors::kIdxOnSpeedFast] = OnSpeedFastPctLift;
    PctAnchors[pct_anchors::kIdxOnSpeedSlow] = OnSpeedSlowPctLift;
    PctAnchors[5] = 0;                                       // unused
    PctAnchors[pct_anchors::kIdxPipPctLift]  = PipPctLift;        // visual aerodynamic pip
    PctAnchors[pct_anchors::kIdxStallWarn]   = StallWarnPctLift;

    drawAOA(wgtX0, wgtY0, wgtWidth, wgtHeight, PercentLift, flashFlag, PctAnchors);

    // Draw the percent lift display
    // -----------------------------
    #define PERCENT_X_POS   140
    #define PERCENT_Y_POS    27     // Top of chevron

    gdraw.setFont(FSSB18);

    // Percent lift number (above chevron). Outlined by drawing black
    // copies at ±3 offset then white on top. Uses setCursor+print with
    // the frame default datum (baseline_left) — same convention as the
    // rest of this page.  Dashes when the producer's bIasAlive is
    // false (wire's iasKt sentinel was 9999): without air data,
    // percent-of-stall is not meaningful.  See issue #358.
    char PctLiftStr[4];
    if (IasIsValid)
        snprintf(PctLiftStr, sizeof(PctLiftStr), "%02d", displayPercentLift);
    else
        snprintf(PctLiftStr, sizeof(PctLiftStr), "--");
    // Dashes right-align to where a 2-digit reading would end so the
    // placeholder sits in the same visual column as live "%02d" digits.
    // 3-digit live values shift -7 to keep the right edge in place.
    int pctX;
    if (!IasIsValid)
        pctX = (PERCENT_X_POS + (int)gdraw.textWidth("00")) - (int)gdraw.textWidth(PctLiftStr);
    else
        pctX = (displayPercentLift < 100) ? PERCENT_X_POS : PERCENT_X_POS - 7;
    gdraw.setTextColor (TFT_BLACK);
    for (int xoffset = -3; xoffset <=3; xoffset += 3)
        for (int yoffset = -3; yoffset <=3; yoffset += 3)
        {
            gdraw.setCursor(pctX + xoffset, PERCENT_Y_POS + yoffset);
            gdraw.print(PctLiftStr);
        }
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setCursor(pctX, PERCENT_Y_POS);
    gdraw.print(PctLiftStr);

    if (numericDisplay)
    {
        // Shared layout constants for this page.
        constexpr int RIGHT_X = 303;   // right-edge anchor, clears VSI ticks
        constexpr int LABEL_Y = 90;
        constexpr int NUM_Y   = 130;

        // ----- Labels (IAS left, G right) -----
        gdraw.setFont(FSS18);
        gdraw.setTextColor (TFT_GREEN);
        gdraw.setCursor(5, LABEL_Y);
        gdraw.print("IAS");
        gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth("G"), LABEL_Y);
        gdraw.print("G");

        // ----- Numbers (IAS left, G right, same y) -----
        // IAS dashes right-align to where a 3-digit reading would end
        // so the placeholder sits in the same visual column as live
        // digits — the G readout on the right is the visual reference.
        gdraw.setFont(FSSB18);
        gdraw.setTextColor (TFT_WHITE);
        if (IasIsValid)
            {
            gdraw.setCursor(7, NUM_Y);
            gdraw.print(int(displayIAS));
            }
        else
            {
            const int iasRightX = 7 + (int)gdraw.textWidth("000");
            gdraw.setCursor(iasRightX - (int)gdraw.textWidth("--"), NUM_Y);
            gdraw.print("--");
            }

        char GStr[6];
        snprintf(GStr, sizeof(GStr), "%+1.1f", displayVerticalG);
        gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth(GStr), NUM_Y);
        gdraw.print(GStr);

        // Update flaps display
        // --------------------
        gdraw.fillCircle (23, 204, 16, TFT_GREY);
        //top, bottom, right
        int cX              =  23;
        int cY              = 204;
        int Radius          =  16;
        // Map FlapPos into a fraction of configured travel
        // [FlapsMinDeg..FlapsMaxDeg] -> [0..1] via the pure helper, then
        // sweep the triangle through a fixed visual arc (0..kFlapArcDeg).
        // Endpoint-locked regardless of the absolute degree values, so
        // reflex flaps (negative min) and >30 deg deployments render
        // correctly.  See test/test_flap_widget_math/ for the contract.
        constexpr float kFlapArcDeg = 40.0f;                      // visual arc the triangle sweeps
        constexpr float kFlapArcRad = kFlapArcDeg * float(PI) / 180.0f;
        const float frac     = onspeed::gauges::FlapWidgetFrac(FlapPos, FlapsMinDeg, FlapsMaxDeg);
        const float angleRad = frac * kFlapArcRad;
        int triangleTopX    = int(cX+sin(angleRad)*Radius);
        int triangleTopY    = int(cY-cos(angleRad)*Radius);
        int triangleBottomX = int(cX-sin(angleRad)*Radius);
        int triangleBottomY = int(cY+cos(angleRad)*Radius);
        int triangleRightX  = int(cX+cos(angleRad)*(Radius+33));
        int triangleRightY  = int(cY+sin(angleRad)*(Radius+33));
        gdraw.fillTriangle(triangleTopX, triangleTopY, triangleBottomX, triangleBottomY, triangleRightX, triangleRightY, TFT_GREY);
        gdraw.drawPixel(triangleRightX, triangleRightY, TFT_BLACK); // blunt the flap tip 1 pixel

        // Stop marks at the arc endpoints (FlapsMinDeg, FlapsMaxDeg).
        // Per-detent stops would need a wire-format change; out of scope.
        const int stopRadius = Radius + 33;
        gdraw.drawPixel(int(cX + cos(0.0f)       * stopRadius),
                        int(cY + sin(0.0f)       * stopRadius), TFT_WHITE);
        gdraw.drawPixel(int(cX + cos(kFlapArcRad) * stopRadius),
                        int(cY + sin(kFlapArcRad) * stopRadius), TFT_WHITE);

        // show numeric flap angle
        gdraw.setFont(FSS12);
        gdraw.setTextColor (TFT_WHITE);
        gdraw.setTextDatum(textdatum_t::middle_center);
        char FlapsChar[8];
        // Wire-format flapsDeg is signed %+03d (range -99..+99, worst
        // case "-99\0" = 4 bytes); defensive headroom covers a future
        // 3-digit value like "-999\0" (5 bytes). The old [2] buffer
        // overflowed every frame for any two-digit flap setting.
        static_assert(sizeof(FlapsChar) >= 5,
                      "FlapsChar must hold at least \"-999\\0\" (5 bytes)");
        snprintf(FlapsChar, sizeof(FlapsChar), "%i", FlapPos);
        gdraw.drawString(FlapsChar,cX,cY);
        gdraw.setTextDatum(textdatum_t::baseline_left); // restore
    } // end if numeric display

    // Update ball display
    // -------------------
    drawSlip(80, 204, 160, 34, Slip, flashFlag, PctAnchors);

    // Update gOnset rates
    // -------------------
    // draw gOnset line
    if (gOnsetRate != 0.0)
    {
        int gOnsetHeight=abs(int(gOnsetRate*120/2));
        gOnsetHeight=constrain(gOnsetHeight,0,120);
        int gOnsetTop;
        if (gOnsetRate > 0) gOnsetTop = 119 - gOnsetHeight;
        else                gOnsetTop = 119;
        gdraw.fillRect(313,gOnsetTop,7,gOnsetHeight,TFT_YELLOW);
    }

    // gOnset ladder, every 15 pixels.  Start offset chosen so the
    // center tick lands on y=119, the visual center of the 3-px zero
    // pip below.  Without this offset (start=15), the ladder's center
    // tick was at y=120 and the pip read as one pixel high relative to
    // the ladder.  Modes 1/3 use a 20-px spacing whose center tick is
    // already at y=119, so they don't need this adjustment.
    for (int i=14;i<226;i=i+15)
    {
        gdraw.drawLine (313, i, 319, i, TFT_GREY);
    }

    //zero pip
    gdraw.drawLine (306, 118, 312, 118, TFT_GREY);
    gdraw.drawLine (306, 119, 312, 119, TFT_GREY);
    gdraw.drawLine (306, 120, 312, 120, TFT_GREY);

    // Draw the data-mark counter at top-left.  Increments on a 1-second
    // long-press of the OnSpeed switch (Switch.cpp); the value comes
    // off the wire (offset 69, mod 100) so the M5 readout matches what
    // the SD log records.  Drawn on both Mode 0 (Energy Display) and
    // Mode 2 (Indexer) so the counter advance stays visible regardless
    // of which page the pilot picked — confirms the long-press registered.
    // 16×14 px at top-left, well clear of the indexer column (x=109+)
    // and the IAS readout (y=90).
    gdraw.setFont(FM12);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setCursor(10, 15);
    gdraw.printf ("%02d", DataMark);
} // end displayAOA()


// -----------------------------------------------

//
// Draw AOA indicator.  All thresholds are in percent-lift units: aoaPct is
// the current AOA's percent (0..99) and Array[] holds the per-flap percent
// anchors populated by displayAOA().
//
void drawAOA(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, float aoaPct, boolean flashing, const int Array[])
{
    float       Theta;
    float       cosTheta;
    float       sinTheta;
    uint16_t    Colour;

    X0 = X0 + W / 2;
    Y0 = Y0 + H / 2; // Adjust datum to center of widget

    gdraw.drawRoundRect (X0 - W / 2 ,     Y0 - H / 2 ,     W ,     H,     5, TFT_DARKGREY); // Gauge bounding box
    gdraw.drawRoundRect (X0 - W / 2 + 1 , Y0 - H / 2 + 1 , W - 2 , H - 2, 5, TFT_DARKGREY); // Gauge bounding box

    int16_t Px0 = -W / 12, Py0 = -H / 4;
    int16_t Px1 = +W / 12, Py1 =  H / 4 ;

    /*
     Top chevron
    */

    // Chevron changes color midway between "slow" (4) and "stall warning" (7)
    int chevMid = Array[4] + (Array[7] - Array[4]) / 2;
    if      (aoaPct > Array[4] && aoaPct <= chevMid ) Colour = TFT_YELLOW;
    else if (aoaPct > chevMid  && aoaPct <= Array[7]) Colour = TFT_RED;
    else if (aoaPct > Array[7] && !flashing         ) Colour = TFT_RED;
    else                                              Colour = TFT_DARKGREY;

    Theta    = PI / 8;
    cosTheta = cos(Theta);
    sinTheta = sin(Theta);

    int16_t XA0 = (Px0 * cosTheta - Py0 * sinTheta) + X0 + W / 4;
    int16_t YA0 = (Px0 * sinTheta + Py0 * cosTheta) + Y0 - H / 4;

    int16_t XA1 = (Px1 * cosTheta - Py0 * sinTheta) + X0 + W / 4;
    int16_t YA1 = (Px1 * sinTheta + Py0 * cosTheta) + Y0 - H / 4;

    int16_t XA2 = (Px1 * cosTheta - Py1 * sinTheta) + X0 + W / 4;
    int16_t YA2 = (Px1 * sinTheta + Py1 * cosTheta) + Y0 - H / 4;

    int16_t XA3 = (Px0 * cosTheta - Py1 * sinTheta) + X0 + W / 4;
    int16_t YA3 = (Px0 * sinTheta + Py1 * cosTheta) + Y0 - H / 4;

    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    Theta    = -PI / 8;
    cosTheta = cos(Theta);
    sinTheta = sin(Theta);

    XA0 = (Px0 * cosTheta - Py0 * sinTheta) + X0 - W / 4;
    YA0 = (Px0 * sinTheta + Py0 * cosTheta) + Y0 - H / 4;

    XA1 = (Px1 * cosTheta - Py0 * sinTheta) + X0 - W / 4;
    YA1 = (Px1 * sinTheta + Py0 * cosTheta) + Y0 - H / 4;

    XA2 = (Px1 * cosTheta - Py1 * sinTheta) + X0 - W / 4;
    YA2 = (Px1 * sinTheta + Py1 * cosTheta) + Y0 - H / 4;

    XA3 = (Px0 * cosTheta - Py1 * sinTheta) + X0 - W / 4;
    YA3 = (Px0 * sinTheta + Py1 * cosTheta) + Y0 - H / 4;

    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    /*
     Bottom chevron — green when the audio low tone is playing, dark
     otherwise.  Gates on Array[kIdxTonesOn] (the active-detent
     L/Dmax percent, snapped) and Array[kIdxOnSpeedSlow] (the donut's
     upper edge).  Same threshold the audio path uses — chevron and
     low tone fire together on every flap setting.  This is the
     "operational cue" half of Vac's pip-vs-tone independence rule
     (see onspeed_core/aoa/DisplayPctAnchors.h).
    */
    if (aoaPct >= Array[pct_anchors::kIdxTonesOn] &&
        aoaPct <  Array[pct_anchors::kIdxOnSpeedSlow]) Colour = TFT_GREEN;
    else                                                Colour = TFT_DARKGREY;

    Theta    = PI / 8;
    cosTheta = cos(Theta);
    sinTheta = sin(Theta);

    XA0 = (Px0 * cosTheta - Py0 * sinTheta) + X0 - W / 4;
    YA0 = (Px0 * sinTheta + Py0 * cosTheta) + Y0 + H / 4;

    XA1 = (Px1 * cosTheta - Py0 * sinTheta) + X0 - W / 4;
    YA1 = (Px1 * sinTheta + Py0 * cosTheta) + Y0 + H / 4;

    XA2 = (Px1 * cosTheta - Py1 * sinTheta) + X0 - W / 4;
    YA2 = (Px1 * sinTheta + Py1 * cosTheta) + Y0 + H / 4;

    XA3 = (Px0 * cosTheta - Py1 * sinTheta) + X0 - W / 4;
    YA3 = (Px0 * sinTheta + Py1 * cosTheta) + Y0 + H / 4;

    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    Theta    = -PI / 8;
    cosTheta = cos(Theta);
    sinTheta = sin(Theta);

    XA0 = (Px0 * cosTheta - Py0 * sinTheta) + X0 + W / 4;
    YA0 = (Px0 * sinTheta + Py0 * cosTheta) + Y0 + H / 4;

    XA1 = (Px1 * cosTheta - Py0 * sinTheta) + X0 + W / 4;
    YA1 = (Px1 * sinTheta + Py0 * cosTheta) + Y0 + H / 4;

    XA2 = (Px1 * cosTheta - Py1 * sinTheta) + X0 + W / 4;
    YA2 = (Px1 * sinTheta + Py1 * cosTheta) + Y0 + H / 4;

    XA3 = (Px0 * cosTheta - Py1 * sinTheta) + X0 + W / 4;
    YA3 = (Px0 * sinTheta + Py1 * cosTheta) + Y0 + H / 4;

    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    /*
     Draw black surround for inner circles
    */
    uint16_t bullsEye = H * (65 - 55 - 2) / 200;
    gdraw.fillCircle (X0, Y0, bullsEye + H / 12, TFT_BLACK);

    float       OnspeedRange = (float)(Array [4] - Array [3]);
    int16_t     ArcRadius    = bullsEye + H / 16;
    uint16_t    LineWidth    = 8;

    // Bottom arc
    if ((float)aoaPct >= (float)Array [3] && (float)aoaPct <= ((float)Array [4] - OnspeedRange * 0.25f)) Colour = TFT_GREEN;
    else                                                                                                 Colour = TFT_DARKGREY;
    myGauges.drawArc(X0, Y0, ArcRadius, 0.0, PI, Colour, LineWidth);

    // Top arc
    if ((float)aoaPct >= ((float)Array [3] + OnspeedRange * 0.25f) && (float)aoaPct <= (float)Array [4]) Colour = TFT_GREEN;
    else                                                                                                 Colour = TFT_DARKGREY;
    myGauges.drawArc(X0, Y0, ArcRadius,  PI, PI, Colour, LineWidth);

    // Black segments between arcs
    gdraw.fillRect (X0 - W / 3, Y0 - H / 48, 2 * W / 3, H / 24, TFT_BLACK);

    // Center dot
    if ((float)aoaPct >= ((float)Array [3] + OnspeedRange * 0.25f) && (float)aoaPct <= ((float)Array [4] - OnspeedRange * 0.25f)) Colour = TFT_GREEN;
    else                                                                                                                          Colour = TFT_DARKGREY;
    gdraw.fillCircle (X0, Y0, bullsEye + 2, Colour);

    /*
    Index pointer.  Position uses the same float-resolution aoaPct that
    drives every chevron and arc color comparison above; the float
    overload of mapPct2Display gives the bar sub-pixel temporal
    smoothness off the 20 Hz wire.  Comparisons against integer band
    anchors (Array[i]) compare via implicit float-int promotion —
    exact for integer-valued floats in the [0, 99] range we work in.
    */
    int indexY = mapPct2Display(aoaPct, Array);
    gdraw.fillRect (X0 - W / 2, indexY, W, H / 24, TFT_WHITE);
    gdraw.drawRect (X0 - W / 2, indexY, W, H / 24, TFT_BLACK);

    /*
     Draw marker dots at the visual L/Dmax pip (Array[kIdxPipPctLift]).
     The pip is a smooth aerodynamic reference: it lerps from the
     cleanest detent's L/Dmax percent up to the most-deployed detent's
     bottom-half-of-donut target ((3*fast + slow) / 4) as the lever
     sweeps.  This is intentionally decoupled from the chevron's audio
     gate (Array[kIdxTonesOn]) — see onspeed_core/aoa/DisplayPctAnchors.h
     for the design rule.
    */
    int ldmaxY = mapPct2Display(Array[pct_anchors::kIdxPipPctLift], Array);
    gdraw.fillCircle (X0 - W / 2,     ldmaxY, H / 24, TFT_BLACK);
    gdraw.fillCircle (X0 + W / 2 - 1, ldmaxY, H / 24, TFT_BLACK);
    gdraw.fillCircle (X0 - W / 2,     ldmaxY, H / 32, TFT_WHITE);
    gdraw.fillCircle (X0 + W / 2 - 1, ldmaxY, H / 32, TFT_WHITE);
} // end drawAOA()


// -----------------------------------------------
/*
   Draw slip indicator
*/
void drawSlip (uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H,  int16_t slipValue, boolean flashing,  const int Array[])
{
    uint16_t CenterX = X0 + W / 2;
    uint16_t CenterY = Y0 + H / 2;

    /*
     Add ball graphic
    */

    uint16_t Colour = TFT_GREEN;
    if ( flashing && (abs(slipValue) >= 30) && PercentLift >= Array[7]) Colour = TFT_BLACK;
    if (!flashing && (abs(slipValue) >= 30) && PercentLift >= Array[7]) Colour = TFT_RED;

    gdraw.fillCircle (CenterX + (int32_t)roundf(slipValue * (W - H - 1) / (99.0f * 2)), CenterY, H / 2 - 1, Colour);

    /*
    Draw slip indicator tick marks in foreground
    */
    gdraw.fillRect (CenterX - H / 2 - 9, Y0, 10, H, TFT_BLACK);
    gdraw.fillRect (CenterX - H / 2 - 7, Y0, 6,  H, TFT_WHITE);
    gdraw.fillRect (CenterX + H / 2,     Y0, 10, H, TFT_BLACK);
    gdraw.fillRect (CenterX + H / 2 + 2, Y0, 6,  H, TFT_WHITE);
}


// -----------------------------------------------

void AiGraph (int16_t px0, int16_t py0, int16_t arcSize, int16_t arcWidth, int16_t maxDisplay, int16_t minDisplay,
              int16_t startAngle, int16_t arcAngle, bool clockWise, uint8_t gradMarks,
              float pitch, float roll, int16_t yaw, float flightPathAngle)
{

/*
    Draw Horizon first
*/
    {

    gdraw.fillSprite (TFT_CYAN);

    /*
    Establish a wide horizontal baseline segment
    */
    float xRotate = (2.0f * (float)WIDTH) * cos (roll * DEG_TO_RAD);
    float yRotate = (2.0f * (float)WIDTH) * sin (roll * DEG_TO_RAD);

    /*
    Adjust it for roll and pitch
    */
    float pxc = px0 + pitch * HEIGHT/80 * sin (roll * DEG_TO_RAD );
    float pyc = py0 + pitch * HEIGHT/80 * cos (roll * DEG_TO_RAD );

    float px1 = pxc - xRotate;
    float py1 = pyc + yRotate;

    float px2 = pxc + xRotate;
    float py2 = pyc - yRotate;

    /*
    Compute offset parallel line segment to establish a wide bar
    */
    float px3 = px1 + 3 * HEIGHT * cos(roll * DEG_TO_RAD - HALF_PI);
    float py3 = py1 - 3 * HEIGHT * sin(-roll * DEG_TO_RAD - HALF_PI);

    float px4 = px2 - 3 * HEIGHT * cos(roll * DEG_TO_RAD + HALF_PI);
    float py4 = py2 + 3 * HEIGHT * sin(roll * DEG_TO_RAD + HALF_PI);

    /*
    Fill the bar.  Will be automatically clipped outside of screen bounds
    */

    gdraw.fillTriangle (px1, py1, px2, py2, px3, py3, 0x8281); // brown color for ground
    gdraw.fillTriangle (px3, py3, px4, py4, px2, py2, 0x8281);
    gdraw.drawLine (px1, py1, px2, py2, TFT_BLACK);
    gdraw.drawLine (px3, py3, px4, py4, TFT_BLUE);
    gdraw.fillCircle (pxc, pyc, 3, TFT_BLACK);

}
    /*
    Draw pitch graphic in background
    */
    pitchGraph(pitch, roll, px0, py0, 10);

    /*
    Draw Horizon Indicator
    */
    arcSize  = 115;
    arcWidth =  15;

    myGauges.clearRanges();

    myGauges.clearPointers();
    myGauges.setPointer (1,   0, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer (2, 180, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer (3, 210, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer (4, 240, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer (5, 270, ARROW_OUT, TFT_YELLOW, '\0');
    myGauges.setPointer (6, 300, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer (7, 330, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer (8, 0, 0, 0, '\0');

    myGauges.arcGraph (px0, py0, arcSize, arcWidth , maxDisplay, minDisplay,
                       static_cast<int16_t>(lroundf(-roll)), arcAngle, clockWise, gradMarks);

    /*
    Draw additional small markers
    */

    arcSize = 115;
    arcWidth = 15;

    myGauges.clearRanges();

    myGauges.clearPointers();
    myGauges.setPointer (1, 250, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer (2, 260, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer (3, 280, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer (4, 290, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer (5, 225, ROUND_DOT, TFT_WHITE, '\0');
    myGauges.setPointer (6, 315, ROUND_DOT, TFT_WHITE, '\0');
    myGauges.setPointer (7, 0, 0,  0, '\0');
    myGauges.setPointer (8, 0, 0,  0, '\0');

    myGauges.arcGraph (px0, py0, arcSize, arcWidth , maxDisplay, minDisplay,
                       static_cast<int16_t>(lroundf(-roll)), arcAngle, clockWise, gradMarks);


  /*
    Draw Airplane
  */

    arcSize = 100;
    arcWidth = 15;

    uint16_t px1 = px0 - arcSize;
    uint16_t py1 = py0;
    uint16_t px2 = px0 - arcSize / 4 ;
    uint16_t py2 = py0;
    uint16_t px3 = px0 + arcSize / 4 ;
    uint16_t py3 = py0;
    uint16_t px4 = px0 + arcSize;
    uint16_t py4 = py0;
    uint16_t px5 = px0;
    uint16_t py5 = py0 + arcSize / 4;

    gdraw.fillCircle (px0, py0, 2 * HEIGHT/80, TFT_YELLOW);   // 2 degree radius circle
    gdraw.drawCircle (px0, py0, 2 * HEIGHT/80, TFT_BLACK);

    gdraw.drawFastHLine (px1, py1, 3*arcSize/4, TFT_YELLOW);
    gdraw.drawLine (px2, py2, px5, py5, TFT_YELLOW);
    gdraw.drawLine (px5, py5, px3, py3, TFT_YELLOW);
    gdraw.drawFastHLine (px3, py3, 3*arcSize/4, TFT_YELLOW);

    gdraw.drawFastHLine (px1, py1-1, 3*arcSize/4, TFT_YELLOW);
    gdraw.drawLine (px2, py2-1, px5, py5-1, TFT_YELLOW);
    gdraw.drawLine (px5, py5-1, px3, py3-1, TFT_YELLOW);
    gdraw.drawFastHLine (px3, py3-1, 3*arcSize/4, TFT_YELLOW);

    gdraw.drawFastHLine (px1, py1-2, 3*arcSize/4, TFT_YELLOW);
    gdraw.drawLine (px2, py2-2, px5, py5-2, TFT_YELLOW);
    gdraw.drawLine (px5, py5-2, px3, py3-2, TFT_YELLOW);
    gdraw.drawFastHLine (px3, py3-2, 3*arcSize/4, TFT_YELLOW);

    gdraw.drawFastHLine (px1, py1-3, 3*arcSize/4, TFT_BLACK);
    gdraw.drawLine (px2, py2-3, px5, py5-3, TFT_BLACK);
    gdraw.drawLine (px5, py5-3, px3, py3-3, TFT_BLACK);
    gdraw.drawFastHLine (px3, py3-3, 3*arcSize/4, TFT_BLACK);

    gdraw.drawFastHLine (px1, py1+1, 3*arcSize/4, TFT_YELLOW);
    gdraw.drawLine (px2, py2+1, px5, py5+1, TFT_YELLOW);
    gdraw.drawLine (px5, py5+1, px3, py3+1, TFT_YELLOW);
    gdraw.drawFastHLine (px3, py3+1, 3*arcSize/4, TFT_YELLOW);

    gdraw.drawFastHLine (px1, py1+2, 3*arcSize/4, TFT_YELLOW);
    gdraw.drawLine (px2, py2+2, px5, py5+2, TFT_YELLOW);
    gdraw.drawLine (px5, py5+2, px3, py3+2, TFT_YELLOW);
    gdraw.drawFastHLine (px3, py3+2, 3*arcSize/4, TFT_YELLOW);

    gdraw.drawFastHLine (px1, py1+3, 3*arcSize/4, TFT_BLACK);
    gdraw.drawLine (px2, py2+3, px5, py5+3, TFT_BLACK);
    gdraw.drawLine (px5, py5+3, px3, py3+3, TFT_BLACK);
    gdraw.drawFastHLine (px3, py3+3, 3*arcSize/4, TFT_BLACK);

    gdraw.drawFastVLine (px1, py1-3, 6, TFT_BLACK);
    gdraw.drawFastVLine (px4, py4-3, 6, TFT_BLACK);

    /*
    Draw top pointer
    */

    px1 = px0;
    py1 = py0 - arcSize + arcWidth / 2;
    px2 = px0 - arcWidth / 2;
    py2 = py0 - arcSize + 2 * arcWidth;
    px3 = px0 + arcWidth / 2;
    py3 = py0 - arcSize + 2 * arcWidth;

    gdraw.fillTriangle (px1, py1, px2, py2, px3, py3, TFT_YELLOW);

    gdraw.drawLine (px1, py1, px2, py2, TFT_BLACK);
    gdraw.drawLine (px2, py2, px3, py3, TFT_BLACK);
    gdraw.drawLine (px3, py3, px1, py1, TFT_BLACK);

    /*
    Draw FlightPath marker
    */

    // 120 -screen center. Reads the parameter rather than the global Pitch
    // so a future caller passing a smoothed/synthetic pitch (X-Plane plugin,
    // unit test) gets the FPV consistent with the horizon it just drew.
    int fpY = 120-(flightPathAngle-pitch)*120/40; // 40 degrees of pitch per half screen height
    fpY = constrain(fpY,0,239);
    int fpX = 159; // screen center

    // circle
    gdraw.drawCircle (fpX, fpY, 12, TFT_MAGENTA);
    gdraw.drawCircle (fpX, fpY, 13, TFT_MAGENTA);
    gdraw.drawCircle (fpX, fpY, 14, TFT_MAGENTA);

    // left line
    gdraw.drawLine (fpX-33, fpY-1, fpX-14, fpY-1, TFT_MAGENTA);
    gdraw.drawLine (fpX-33, fpY,   fpX-14, fpY,   TFT_MAGENTA);
    gdraw.drawLine (fpX-33, fpY+1, fpX-14, fpY+1, TFT_MAGENTA);

    // right line
    gdraw.drawLine (fpX+33, fpY-1, fpX+14, fpY-1, TFT_MAGENTA);
    gdraw.drawLine (fpX+33, fpY,   fpX+14, fpY,   TFT_MAGENTA);
    gdraw.drawLine (fpX+33, fpY+1, fpX+14, fpY+1, TFT_MAGENTA);

    // top line
    gdraw.drawLine (fpX-1, fpY-14, fpX-1, fpY-33, TFT_MAGENTA);
    gdraw.drawLine (fpX,   fpY-14, fpX,   fpY-33, TFT_MAGENTA);
    gdraw.drawLine (fpX+1, fpY-14, fpX+1, fpY-33, TFT_MAGENTA);

}


// -----------------------------------------------

void pitchGraph(float pitch, float roll, int16_t px0, int16_t py0, uint8_t scale)
{

    float px1, px2, px3, px4;
    float py1, py2, py3, py4;
    float xRotate;
    float yRotate;

    float pxc = px0 + pitch * HEIGHT/80 * sin (roll * DEG_TO_RAD );
    float pyc = py0 + pitch * HEIGHT/80 * cos (roll * DEG_TO_RAD );

  /*
    Compute pitch scale
  */

    gdraw.setTextDatum(MC_DATUM);

    xRotate = (0.10f * g_arcSize) * cos (roll * DEG_TO_RAD); // establish the width.
    yRotate = (0.10f * g_arcSize) * sin (roll * DEG_TO_RAD);

    px1 = pxc - xRotate*1.0f;
    py1 = pyc + yRotate*1.0f;

    px2 = pxc + xRotate*1.0f;
    py2 = pyc - yRotate*1.0f;

    for (int16_t i = -85; i <= 85; i+= scale)
    {
        // Marks every 5 degrees
        px3 = px1 - (i * HEIGHT/80) * cos( roll * DEG_TO_RAD - HALF_PI);
        py3 = py1 + (i * HEIGHT/80) * sin(-roll * DEG_TO_RAD - HALF_PI);

        px4 = px2 + (i * HEIGHT/80) * cos( roll * DEG_TO_RAD + HALF_PI);
        py4 = py2 - (i * HEIGHT/80) * sin( roll * DEG_TO_RAD + HALF_PI);

        gdraw.drawLine (px3, py3, px4, py4, TFT_BLACK);
    }

    px1 = pxc - xRotate*2.0f;
    py1 = pyc + yRotate*2.0f;

    px2 = pxc + xRotate*2.0f;
    py2 = pyc - yRotate*2.0f;

    for (int16_t i = -90; i <= 90; i+= scale)
    {
        // Marks every 5 degrees
        px3 = px1 - (i * HEIGHT/80) * cos( roll * DEG_TO_RAD - HALF_PI);
        py3 = py1 + (i * HEIGHT/80) * sin(-roll * DEG_TO_RAD - HALF_PI);

        px4 = px2 + (i * HEIGHT/80) * cos( roll * DEG_TO_RAD + HALF_PI);
        py4 = py2 - (i * HEIGHT/80) * sin( roll * DEG_TO_RAD + HALF_PI);

        gdraw.setCursor(px4, py4);
        gdraw.drawLine (px3, py3, px4, py4, TFT_BLACK);

        px4 += xRotate*0.75f;
        py4 -= yRotate*0.75f;
        myGauges.printNum (String (i)+"o", px4, py4, 8, 12, static_cast<int16_t>(lroundf(roll)), TFT_BLACK, ML_DATUM);
    }
    gdraw.setTextDatum(textdatum_t::baseline_left); // restore project datum convention
} // end pitchGraph


// -----------------------------------------------

void displayDecelGauge()
{
    // draw gauge background
    gdraw.fillRoundRect(109,1,102,210,5,TFT_RED);
    gdraw.fillRect(109,87,102,36,TFT_GREEN);
    gdraw.drawRoundRect(109,1,102,210,5,TFT_LIGHT_GREY);

    int decelIndex=int(35.143*SmoothedDecelRate+141.48-3.5); // 3.5 is half the indexer pointer height
    decelIndex=constrain(decelIndex,2,205);

    // draw index pointer
    gdraw.fillRect (109, decelIndex, 102, 7, TFT_WHITE);
    gdraw.drawRect (109, decelIndex, 102, 7, TFT_BLACK);

    // gauge numbers
    gdraw.setFont(FSS9);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setTextDatum(textdatum_t::middle_right);
    gdraw.drawString("-1",95,106);
    gdraw.drawString("-2",95, 72);
    gdraw.drawString("-3",95, 36);
    gdraw.drawString("0", 95,141);
    gdraw.drawString("1", 95,177);
    gdraw.setTextDatum(textdatum_t::baseline_left); // restore frame default

    // pips
    gdraw.drawLine (99, 106, 107, 106, TFT_LIGHT_GREY);
    gdraw.drawLine (99,  72, 107,  72, TFT_LIGHT_GREY);
    gdraw.drawLine (99,  36, 107,  36, TFT_LIGHT_GREY);
    gdraw.drawLine (99, 141, 107, 141, TFT_LIGHT_GREY);
    gdraw.drawLine (99, 177, 107, 177, TFT_LIGHT_GREY);

    // iVSI
    // draw iVSI line
    if (iVSI!=0.0)
    {
        int vsiHeight=abs(int(iVSI*kVsiBarHeightPx/kVsiBarFullScaleFpm));
        vsiHeight=constrain(vsiHeight,0,kVsiBarHeightPx);
        int vsiTop;
        if (iVSI > 0) vsiTop = 119 - vsiHeight;
        else          vsiTop = 119;
        gdraw.fillRect(313,vsiTop,7,vsiHeight,TFT_WHITE);
    }

    // vsi ladder, every 20 pixels
    for (int i=19;i<220;i=i+20)
    {
        gdraw.drawLine (313, i, 319, i, TFT_LIGHT_GREY);
    }

    //zero line
    gdraw.drawLine (306, 118, 312, 118, TFT_LIGHT_GREY);
    gdraw.drawLine (306, 119, 312, 119, TFT_LIGHT_GREY);
    gdraw.drawLine (306, 120, 312, 120, TFT_LIGHT_GREY);

    // Update ball display
    drawSlip(80, 215, 160, 20, Slip, false, PctAnchors);

    // Shared layout constants for this page.
    constexpr int DEC_RIGHT_X     = 303;   // right-edge anchor, clears VSI ticks
    constexpr int DEC_IAS_LABEL_Y = 90;
    constexpr int DEC_KTS_LABEL_Y = 90;   // same row as IAS label
    constexpr int DEC_NUM_Y       = 130;  // both numbers share this y

    // ----- Labels -----
    gdraw.setFont(FSS18);
    gdraw.setTextColor (TFT_GREEN);
    gdraw.setCursor(5, DEC_IAS_LABEL_Y);
    gdraw.print("IAS");
    gdraw.setCursor(DEC_RIGHT_X - (int)gdraw.textWidth("Kt/s"), DEC_KTS_LABEL_Y);
    gdraw.print("Kt/s");

    // ----- Numbers -----
    // IAS dashes right-align to where a 3-digit reading would end so
    // the placeholder shares the live-digit column.
    gdraw.setFont(FSSB18);
    gdraw.setTextColor (TFT_WHITE);
    if (IasIsValid)
        {
        gdraw.setCursor(7, DEC_NUM_Y);
        gdraw.print(int(displayIAS));
        }
    else
        {
        const int iasRightX = 7 + (int)gdraw.textWidth("000");
        gdraw.setCursor(iasRightX - (int)gdraw.textWidth("--"), DEC_NUM_Y);
        gdraw.print("--");
        }

    char DecelStr[6];
    snprintf(DecelStr, sizeof(DecelStr), "%+1.1f", displayDecelRate);
    gdraw.setCursor(DEC_RIGHT_X - (int)gdraw.textWidth(DecelStr), DEC_NUM_Y);
    gdraw.print(DecelStr);
}


// -----------------------------------------------

void displayGloadHistory()
{

    // 1G line
    gdraw.drawLine(19,133,319,133,TFT_WHITE);

    //vertical line
    gdraw.drawLine(19,  0, 19,239,TFT_WHITE);

    gdraw.drawLine(19, 27,319, 27,TFT_GREY);
    gdraw.drawLine(19, 53,319, 53,TFT_GREY);
    gdraw.drawLine(19, 80,319, 80,TFT_GREY);
    gdraw.drawLine(19,106,319,106,TFT_GREY);

    gdraw.drawLine(19,160,319,160,TFT_GREY);
    gdraw.drawLine(19,186,319,186,TFT_GREY);
    gdraw.drawLine(19,213,319,213,TFT_GREY);

    // pips (middle-right anchors vertically-center the number at the given y)
    gdraw.setFont(FSS12);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setTextDatum(textdatum_t::middle_right);
    // Move pips in a few pixels so the leftmost digit is fully visible (M5GFX
    // renders a little farther right than the old TFT_eSPI fork did).
    gdraw.drawString("5", 18, 27);
    gdraw.drawString("4", 18, 53);
    gdraw.drawString("3", 18, 80);
    gdraw.drawString("2", 18,106);
    gdraw.drawString("1", 18,133);
    gdraw.drawString("0", 18,160);
    gdraw.drawString("-1",18,186);
    gdraw.drawString("-2",18,213);

    // Top-center anchor keeps the header inside the visible area at y=2.
    gdraw.setFont(FSS12);
    gdraw.setTextDatum(textdatum_t::top_center);
    gdraw.drawString("G-LOAD [1 min]",160,2);
    gdraw.setTextDatum(textdatum_t::baseline_left); // restore frame default

    // draw gHistory
    int         gDisplayIndex = gHistoryIndex;
    uint16_t    gColor;
    for (int i = 319; i > 19; i--)
    {
        int gHeight=160-int(gHistory[gDisplayIndex]*26.67);
        gHeight=constrain(gHeight,0,239);

        if      (gHistory[gDisplayIndex] >= 1)                                 gColor=TFT_GREEN;
        else if (gHistory[gDisplayIndex] <  1 && gHistory[gDisplayIndex] >= 0) gColor=TFT_YELLOW;
        else                                                                   gColor=TFT_RED;

        gdraw.fillCircle (i, gHeight, 2, gColor);

        if (gDisplayIndex<299) gDisplayIndex++;
        else                   gDisplayIndex = 0;
    }
} // end displayGloadHistory()


// -----------------------------------------------

// Convert AOA percent-lift to M5 display vertical coordinate.
//
// Two ramps with the OnSpeed donut anchored at fixed screen-y in the
// middle.  The lower ramp slope changes per flap because the band-edge
// percents (Array[3] = OnSpeedFastPctLift, Array[4] = OnSpeedSlowPctLift)
// change per flap.  Array[0] is always 0 (the alpha_0 floor in percent
// space).  The upper ramp tops out at percent_lift = 99 — the lift-
// envelope ceiling — independent of the active detent's stall-warn
// percent.  Stall-warn still drives the chevron flash-red color logic
// in drawAOA(); this mapping only governs the y-coordinate.
//
//   aoaPct ≤ 0                            → y = 192 (bottom)
//   0 < aoaPct ≤ OnSpeedFastPctLift       → linear, y = 192 → 115
//   OnSpeedFastPctLift < aoaPct ≤ OnSpeedSlowPctLift → linear, y = 115 → 78  (donut band, fixed screen-y)
//   OnSpeedSlowPctLift < aoaPct ≤ 99      → linear, y = 78 → 1
//   aoaPct > 99                           → y = 1 (top)
//
// L/Dmax pip is drawn at mapPct2Display(Array[kIdxPipPctLift], Array).
// The pip percent slides smoothly clean→fullflap; at clean it sits in
// the lower ramp at L/Dmax, at full flap it slides up into the donut
// band.  The piecewise-linear mapping handles both regions
// transparently — no special case needed.
int mapPct2Display(int aoaPct, const int Array[])
{
    if      (aoaPct <= Array[0])                       return 192;                                                  // display bottom
    else if (aoaPct >  Array[0] && aoaPct <= Array[3]) return map2int((float)aoaPct,(float)Array[0],(float)Array[3],192,115); // floor → OnSpeedFast
    else if (aoaPct >  Array[3] && aoaPct <= Array[4]) return map2int((float)aoaPct,(float)Array[3],(float)Array[4],115, 78); // donut band
    else if (aoaPct >  Array[4] && aoaPct <= 99)       return map2int((float)aoaPct,(float)Array[4],99.0f,            78,  1); // OnSpeedSlow → 99% (lift ceiling)
    else                                               return 1;                                                    // display top
}

// Float overload — used by drawAOA's index-pointer site so the bar y
// position resolves to sub-pixel input even though map2int still rounds
// to whole pixels.  Mirrors the integer version's piecewise ramp.
//
// Upper ramp ceiling is 99.0 (matching the integer overload), even
// though `PercentLift` itself can range up to 99.9 (the float clamp
// from PercentLift.cpp).  Floats in (99.0, 99.9] saturate at y=1 just
// like the integer 99 does — that's the documented "lift-envelope
// ceiling" contract: the bar is pinned at top whenever the pilot is
// at or above 99%.  The 0.9% range in (99.0, 99.9] is intentionally
// not interpolated; the wire can carry sub-tenth values up there
// (pilot deep into stall) but the bar visibly saturating at "off the
// chart" is the right cue.
int mapPct2Display(float aoaPctF, const int Array[])
{
    const float a0 = (float)Array[0];
    const float a3 = (float)Array[3];
    const float a4 = (float)Array[4];

    if      (aoaPctF <= a0)                       return 192;                              // display bottom
    else if (aoaPctF >  a0 && aoaPctF <= a3)      return map2int(aoaPctF, a0, a3, 192, 115); // floor → OnSpeedFast
    else if (aoaPctF >  a3 && aoaPctF <= a4)      return map2int(aoaPctF, a3, a4, 115,  78); // donut band
    else if (aoaPctF >  a4 && aoaPctF <= 99.0f)   return map2int(aoaPctF, a4, 99.0f, 78,  1); // OnSpeedSlow → 99% (lift ceiling — saturates at top above 99)
    else                                          return 1;                                  // display top
}


// -----------------------------------------------

// Interpolate display coordinate between two AOA limits

int map2int(float aoa, float inLow, float inHigh, int outLow, int outHigh)
{
    // Degenerate band (e.g. uncalibrated anchors with Array[3] == Array[4],
    // or Array[4] == 99): collapse to outLow rather than dividing by zero
    // and feeding INT_MIN/INT_MAX to the renderer. Mirrors pct2y.js.
    if (inHigh == inLow) return outLow;
    int Result;
    Result = round((float)(aoa - inLow) * (outHigh - outLow) / (float)(inHigh - inLow) + outLow);
    return Result;
}


// -----------------------------------------------
// Upgrade web server routines
// -----------------------------------------------

#if defined(ESP_PLATFORM)
String HtmlStyle =
    "<style  type='text/css' media='screen'>\n"
    "body {\n"
    "    display: inline-block;\n"
    "    padding: 1.0em;\n"
    "    border: 3px;\n"
    "    border-style: solid;\n"
    "    border-radius: 15px;\n"
    "    }\n"
    "</style>\n";

// Constructed at request time so BuildInfo::version is resolved at runtime.
static String buildHtmlTitle()
{
    return String(
        "<center>\n"
        "    <h2><u>FlyONSPEED M5 Display</u><br>Upgrade Server</h2>\n"
        "    <h3>Current Ver ")
        + BuildInfo::version
        + "</h3>\n"
        "</center>\n";
}

// -----------------------------------------------

void handleUpgrade()
{
    String page = "";

    page += "<html>\n";

    page += "<head>\n";
    page += HtmlStyle;
    page += "</head>\n";

    page += "<body>\n";
    page += buildHtmlTitle();
    page +=
        "<div>\n"
        "<p>Upgrade display firmware via binary (.bin) file upload</p>\n"
        "<p>Note: Please make sure you are uploading the M5 .bin file not the PicoKit .bin!</p>\n"
        "<form method='POST' action='/upload' enctype='multipart/form-data' id='upload_form'>\n"
        "<input type='file' name='update'>\n"
        "<p/>\n"
        "<input class='redbutton' type='submit' value='Upload'>\n"
        "</form>\n"
        "</div>\n";
    page += "</body>\n";

    page += "</html>";

    server.send(200, "text/html", page);
}

// -----------------------------------------------

void handleUpgradeSuccess()
{
    String page = "";

    page += "<html>\n";

    page += "<head>\n";
    page += HtmlStyle;
    page += "</head>\n";

    page += "<body>\n";
    page += buildHtmlTitle();
    page +=
        "<span style=\"color:black\">\n"
        "Firmware upgrade complete.<br>\n"
        "Wait a few seconds until the new software version reboots.\n"
        "</span>\n";

    page +=
        "<script>\n"
        "setInterval(function () \n"
        "    { \n"
        "    document.getElementById('rebootprogress').value+=0.1; \n"
        "    document.getElementById('rebootprogress').innerHTML=document.getElementById('rebootprogress').value+'%'\n"
        "    }, 10);\n"
        "setTimeout(function () \n"
        "    { \n"
        "    window.location.href = \"/\";\n"
        "    }, 10000);\n"
        "</script>\n";

    page +=
        "<div align=\"center\">\n"
        "<progress id=\"rebootprogress\" max=\"100\" value=\"0\"> 0% </progress>\n"
        "</div>\n";

    page += "</body></html>\n";
    server.send(200, "text/html", page);
}

// -----------------------------------------------

void handleUpgradeFailure()
{
    String page = "";

    page += "<html>\n";

    page += "<head>\n";
    page += HtmlStyle;
    page += "</head>\n";

    page += "<body>\n";
    page += buildHtmlTitle();

    page +=
        "<span style=\"color:red\">\n"
        "Firmware upgrade failed. Power cycle the display and try again.\n"
        "</span>\n";

    page += "</body></html>\n";
    server.send(200, "text/html", page);
}


// -----------------------------------------------

void handleIndex()
{
    String page = "";
    page += "<html>\n";

    page += "<head>\n";
    page += HtmlStyle;
    page += "</head>\n";

    page += "<body>\n";
    page += buildHtmlTitle();
    page += "<a href=\"/upgrade\">Upgrade now</a>\n";
    page += "</body></html>\n";

    server.send(200, "text/html", page);
}
#endif // ESP_PLATFORM


// -----------------------------------------------

void displaySplashScreen()
{
    // display splash screen and firmware upgrade option
    gdraw.setFont(FSSB24);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setTextDatum(MC_DATUM);
    gdraw.drawString("Fly OnSpeed",160,60);

    gdraw.setFont(FSS9);
    // Explicit .c_str(): the native-build drawString overload accepts a
    // bare C-string only. The ESP path had an implicit String->const char*
    // conversion through M5GFX's Arduino-flavored overloads.
    const String versionLine = String("Version: ") + BuildInfo::version;
    gdraw.drawString(versionLine.c_str(), 160, 120);
    gdraw.drawString("To upgrade press Center button",160,220);
    gdraw.pushSprite (0, 0);
    gdraw.deleteSprite();
    M5.update();
}
