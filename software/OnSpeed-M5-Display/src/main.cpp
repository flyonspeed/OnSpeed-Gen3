// Ported from OnSpeed Gen2 OnSpeed_M5_display/OnSpeed_M5_display.ino
// M5Stack Core ESP32 secondary display for OnSpeed AOA system.
//
// Provides 5 display modes: AOA chevron, attitude indicator, narrow AOA,
// deceleration gauge, and G-load history.
//
// Changes from Gen2:
// - Replaced buggy SavLayFilter with correct SavGolDerivative
// - Fixed deceleration sign convention (no longer needs negation)
// - Split into main.cpp + SerialRead.cpp/h
// - Moved serialSetup() into SerialRead module

// compiles with ESP32 board library v2.0, does not boot if compiled with previous versions.
// set board type to M5Stack-Core-ESP32

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

//#define SERIALDATADEBUG   // show serial packet debug
//#define DUMMY_SERIAL_DATA // dummy serial data for display test
//#define IAS_IN_MPH        // uncomment this line for IAS in MPH, otherwise it will display in Kts;

//#define REPEATER_MODE       // Used to turn on settings for video recorder repeater
//#define VAC_MODE            // Used to turn on Vac specific features

// REPEATER_MODE/VAC_MODE prefix character (R/V) for the variant, prepended
// to BuildInfo::version at runtime by the renderers below.
#if defined(REPEATER_MODE)
#define VARIANT_PREFIX "R"
#define IAS_IN_MPH
#define DATAMARK_DISPLAY
#elif defined(VAC_MODE)
#define VARIANT_PREFIX "V"
#define IAS_IN_MPH
#define DATAMARK_DISPLAY
#else
#define VARIANT_PREFIX ""
#endif

#include <GaugeWidgets.h>
#include <M5Unified.h>
#include <Free_Fonts.h>

// includes for web firmware update
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>

#include "SerialRead.h"

Preferences preferences;

WebServer server(80);

const char* ssid         = "OnSpeedDisplay";
const char* password     = "angleofattack";
bool        fwUpdateMode = false;
uint8_t     aiIP[4]      = {192, 168, 0, 2};

#define TFT_GREY        0x7BEF
#define TFT_LIGHT_GREY  0xAD55
#define TFT_LIGHT_BLUE  0x421F  // 01000 010000 11111 0100001000011111

M5Canvas        gdraw(&M5.Display);
Gauges          myGauges;

float AOAThresholds[8];

// screen size variables
const uint16_t  WIDTH               = 320; //X
const uint16_t  HEIGHT              = 240; //Y

// display variables
uint64_t        loopTime            = millis();
uint64_t        currentMillis;
uint64_t        previousMillis      = millis();
uint64_t        flashTime           = millis();
uint64_t        numbersUpdateTime;
uint64_t        gHistoryTime        = millis();
#ifndef REPEATER_MODE
uint16_t        displayBrightness   = 255;
int16_t         displayType         = 0;
#else
uint16_t        displayBrightness   = 4;
int16_t         displayType         = 0;
#endif
boolean         numericDisplay;
boolean         flashFlag;
// Match the OnSpeed #1 frame rate (50 ms cadence from DisplaySerial.cpp)
// so each incoming data frame renders.
const uint16_t  updateRateGraphics  =  50;  //milliseconds
const uint16_t  updateRateNumbers   = 500;  //milliseconds
const uint16_t  flashRate           = 250;  //milliseconds

// Serial data variables (definitions, declared extern in SerialRead.h)
String          serialBufferString;
float           AOA                 = 0.0;
float           SmoothedAOA         = 0.0;
int             PercentLift;
float           Pitch               = 0.0;
float           Roll                = 0.0;
float           IAS                 = 0.0;
float           Palt                = 0.0;
float           iVSI                = 0.0;
float           VerticalG           = 1.0;
float           LateralG            = 0.0;
float           SmoothedLateralG;
float           FlightPath          = 0.0;
int             FlapPos             = 0;
int             OAT                 = 0;
int16_t         Slip                = 0;

float           OnSpeedStallWarnAOA = 20;
float           OnSpeedSlowAOA      = 15;
float           OnSpeedFastAOA      = 10;
float           OnSpeedTonesOnAOA   = 5;

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
void drawAOA(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, float aoa, boolean flashing, float Array[]);
void drawSlip (uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, int16_t slipValue, boolean flashing, const float Array[]);
void displayAOA();
void displayDecelGauge();
void displayGloadHistory();
void displaySplashScreen();
void AiGraph (int16_t px0, int16_t py0, int16_t arcSize, int16_t arcWidth, int16_t maxDisplay, int16_t minDisplay,
              int16_t startAngle, int16_t arcAngle, bool clockWise, uint8_t gradMarks,
              int16_t pitch, int16_t roll, int16_t yaw, float flightPathAngle);
void pitchGraph(int16_t pitch, int16_t roll, int16_t px0, int16_t py0, uint8_t scale);
int mapAOA2Display(float aoa, const float Array[]);
int map2int(float aoa, float inLow, float inHigh, int outLow, int outHigh);
void handleUpgrade();
void handleUpgradeSuccess();
void handleUpgradeFailure();
void handleIndex();


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
    // mute the speaker (annoying hiss)
    dacWrite(25, 0);

    gdraw.setColorDepth(8);
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

        //Button B held down at bootup
        if (M5.BtnB.isPressed())
        {
            fwUpdateMode=true;
            gdraw.setColorDepth(8);
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
            gdraw.drawString(String(VARIANT_PREFIX) + BuildInfo::version, 5, 215);

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
                        gdraw.setColorDepth(8);
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

    } // end while splash screen

    if (fwUpdateMode)
        return; // do not continue if firmware upgrade mode was selected.

#if !defined(DUMMY_SERIAL_DATA)
    //select serial port from preferences or detect it
    serialSetup();
    Serial.begin(115200); // console serial
    delay (100);
#endif

} // end setup()


// -----------------------------------------------
// loop()
// -----------------------------------------------

void loop()
{
    M5.update();

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

    SerialRead(); // get serial data

    //
    // Change display brightness and display format using panel buttons.
    //
    if (M5.BtnC.wasPressed() && (displayBrightness > 0))
    {
        displayBrightness *= 2; // brightness up
    }

    if (M5.BtnA.wasPressed() && (displayBrightness <= 255))
    {
        displayBrightness /= 2; // brightness down
    }

    displayBrightness=constrain(displayBrightness,1,255);

    M5.Display.setBrightness(displayBrightness);

    if (M5.BtnB.wasPressed())
    {
        gdraw.setColorDepth(8);
        gdraw.createSprite(WIDTH, HEIGHT);
        gdraw.fillSprite (TFT_BLACK);
        displayType ++;
        if (displayType > 4) displayType = 0; // type of display
    }

    // update G history buffer
    if (millis()-gHistoryTime>200)
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

        gdraw.setColorDepth(8);
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
            displayPercentLift = PercentLift;
            displayDecelRate   = SmoothedDecelRate;
            numbersUpdateTime  = millis();
        } // end if update numbers

        /*
        Main AOA alarm detection & update AOA display
        */
        switch (displayType)
        {
            case 0: // 0 default indicator with numeric display
            {
                wgtWidth = 102;
                wgtHeight = 192;
                wgtX0 = (WIDTH - wgtWidth) / 2; wgtY0 = 0;
                numericDisplay = true;
                displayAOA();
                break;
            }

            case 1:
            {
                // display Attitude Indicator
                AiGraph (g_px0, g_py0, g_arcSize, g_arcWidth, g_maxDisplay, g_minDisplay, g_startAngle, g_arcAngle, g_clockWise,
                g_gradMarks, int(Pitch), int(Roll), 360, FlightPath);

                // All four corners use setCursor+print with baseline_left
                // (frame default) for consistent y semantics. Right-side
                // alignment is done by computing x = RIGHT_X - textWidth().
                // RIGHT_X = 303 pulls in from the panel edge to clear the
                // VSI tick ladder at x=313.
                constexpr int RIGHT_X = 307;

                // ----- Labels (sit close under/over their numbers) -----
                constexpr int TOP_LABEL_Y = 55;   // just below number baseline=30
                constexpr int BOT_LABEL_Y = 223;  // just below number baseline=198
                gdraw.setFont(FSS12);

                gdraw.setCursor(5, TOP_LABEL_Y);
                gdraw.setTextColor (TFT_GREY);
                gdraw.print("IAS");

                gdraw.setCursor(5, BOT_LABEL_Y);
                gdraw.setTextColor (TFT_LIGHT_GREY);
                gdraw.print("G");

                gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth("ALT"), TOP_LABEL_Y);
                gdraw.setTextColor (TFT_GREY);
                gdraw.print("ALT");

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

                gdraw.setCursor(5, 30);
                gdraw.print(int(displayIAS));

                char PressAltStr[8];
                snprintf(PressAltStr, sizeof(PressAltStr), "%5.0f", displayPalt);
                gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth(PressAltStr), 30);
                gdraw.print(PressAltStr);

                gdraw.setTextColor (TFT_WHITE);
                gdraw.setCursor(5, 198);
                gdraw.printf("%1.1f", displayVerticalG);

                char PctStr[4];
                snprintf(PctStr, sizeof(PctStr), "%02d", displayPercentLift);
                gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth(PctStr), 198);
                gdraw.print(PctStr);

                // Update ball display on attitude page
                // Increase sensitivity of slip indicator
                drawSlip(80, 204, 160, 20, Slip, false, AOAThresholds);

                // iVSI
                // draw iVSI line
                if (iVSI!=0.0)
                {
                    int vsiHeight=abs(int(iVSI*120/600));
                    vsiHeight=constrain(vsiHeight,0,120);
                    int vsiTop;
                    if (iVSI > 0) vsiTop = 119 - vsiHeight;
                    else          vsiTop = 119;
                    gdraw.fillRect(313,vsiTop,7,vsiHeight,TFT_ORANGE);
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

            case 2:  // 2 narrow AOA and slip indicator
            {
                wgtWidth  = 102;
                wgtHeight = 192;
                wgtX0 = (WIDTH - wgtWidth) / 2;
                wgtY0 = 0;
                numericDisplay = false;
                displayAOA();
                break;
            }

            case 3: // decel gauge
            {
                displayDecelGauge();
                break;
            }

            case 4:
            {
                displayGloadHistory();
                break;
            }

            default: break;
        } // end switch on display type

        // Look for serial link failure
        // Draw red lines across display
        if (millis()-serialMillis>300)
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
    // Build Setpoint array
    // --------------------
    AOAThresholds[0] = 0.0001;
    AOAThresholds[1] = OnSpeedTonesOnAOA - 0.1;
    AOAThresholds[2] = OnSpeedTonesOnAOA;
    AOAThresholds[3] = OnSpeedFastAOA;
    AOAThresholds[4] = OnSpeedSlowAOA;
    AOAThresholds[5] = OnSpeedSlowAOA + 0.1;
    AOAThresholds[6] = OnSpeedStallWarnAOA - 0.1;
    AOAThresholds[7] = OnSpeedStallWarnAOA;

    drawAOA(wgtX0, wgtY0, wgtWidth, wgtHeight, SmoothedAOA, flashFlag, AOAThresholds);

    // Draw the percent lift display
    // -----------------------------
    #define PERCENT_X_POS   140
    #define PERCENT_Y_POS    27     // Top of chevron

    gdraw.setFont(FSSB18);

    // Percent lift number (above chevron). Outlined by drawing black
    // copies at ±3 offset then white on top. Uses setCursor+print with
    // the frame default datum (baseline_left) — same convention as the
    // rest of this page.
    char PctLiftStr[4];
    snprintf(PctLiftStr, sizeof(PctLiftStr), "%02d", displayPercentLift);
    const int pctX = (displayPercentLift < 100) ? PERCENT_X_POS : PERCENT_X_POS - 7;
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
        gdraw.setFont(FSSB18);
        gdraw.setTextColor (TFT_WHITE);
        gdraw.setCursor(7, NUM_Y);
        gdraw.print(int(displayIAS));

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
        int triangleTopX    = int(cX+sin(FlapPos*PI/180)*Radius);
        int triangleTopY    = int(cY-cos(FlapPos*PI/180)*Radius);
        int triangleBottomX = int(cX-sin(FlapPos*PI/180)*Radius);
        int triangleBottomY = int(cY+cos(FlapPos*PI/180)*Radius);
        int triangleRightX  = int(cX+cos(FlapPos*PI/180)*(Radius+33));
        int triangleRightY  = int(cY+sin(FlapPos*PI/180)*(Radius+33));
        gdraw.fillTriangle(triangleTopX, triangleTopY, triangleBottomX, triangleBottomY, triangleRightX, triangleRightY, TFT_GREY);
        gdraw.drawPixel(triangleRightX, triangleRightY, TFT_BLACK); // blunt the flap tip 1 pixel

        // show flap stops
        gdraw.drawPixel(72, 204, TFT_WHITE);
        gdraw.drawPixel(71, 212, TFT_WHITE);
        gdraw.drawPixel(69, 220, TFT_WHITE);
        gdraw.drawPixel(65, 228, TFT_WHITE);
        gdraw.drawPixel(60, 235, TFT_WHITE);

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
    drawSlip(80, 204, 160, 34, Slip, flashFlag, AOAThresholds);

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

    // vsi ladder, every 20 pixels
    for (int i=15;i<226;i=i+15)
    {
        gdraw.drawLine (313, i, 319, i, TFT_GREY);
    }

    //zero line
    gdraw.drawLine (306, 118, 312, 118, TFT_GREY);
    gdraw.drawLine (306, 119, 312, 119, TFT_GREY);
    gdraw.drawLine (306, 120, 312, 120, TFT_GREY);

#if defined(DATAMARK_DISPLAY)
    // Draw Data Mark value
    // --------------------
    gdraw.setFont(FM12);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setCursor(10, 15);
    gdraw.printf ("%02d", DataMark);
#endif
} // end displayAOA()


// -----------------------------------------------

//
// Draw AOA indicator
//
void drawAOA(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, float aoa, boolean flashing, float Array[])
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
    float   chevMid = Array[4] + (Array[7] - Array[4]) / 2.0;
    if      (aoa > Array[4] && aoa <= chevMid ) Colour = TFT_YELLOW;
    else if (aoa > chevMid  && aoa <= Array[7]) Colour = TFT_RED;
    else if (aoa > Array[7] && !flashing      ) Colour = TFT_RED;
    else                                        Colour = TFT_DARKGREY;

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
     Bottom chevron
    */
    if (aoa >= Array [1] && aoa < Array [4]) Colour = TFT_LIGHT_BLUE;
    else                                     Colour = TFT_DARKGREY;

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

    float       OnspeedRange = Array [4] - Array [3];
    int16_t     ArcRadius    = bullsEye + H / 16;
    uint16_t    LineWidth    = 8;

    // Bottom arc
    if (aoa >= Array [3] && aoa <= (Array [4] - OnspeedRange * 0.25)) Colour = TFT_GREEN;
    else                                                              Colour = TFT_DARKGREY;
    myGauges.drawArc(X0, Y0, ArcRadius, 0.0, PI, Colour, LineWidth);

    // Top arc
    if (aoa >= (Array [3] + OnspeedRange * 0.25) && aoa <= Array [4]) Colour = TFT_GREEN;
    else                                                              Colour = TFT_DARKGREY;
    myGauges.drawArc(X0, Y0, ArcRadius,  PI, PI, Colour, LineWidth);

    // Black segments between arcs
    gdraw.fillRect (X0 - W / 3, Y0 - H / 48, 2 * W / 3, H / 24, TFT_BLACK);

    // Center dot
    if (aoa >= (Array [3] + OnspeedRange * 0.25) && aoa <= (Array [4] - OnspeedRange * 0.25)) Colour = TFT_GREEN;
    else                                                                                      Colour = TFT_DARKGREY;
    gdraw.fillCircle (X0, Y0, bullsEye + 2, Colour);

    /*
    Index pointer
    */
    int indexY = mapAOA2Display(aoa, Array);
    gdraw.fillRect (X0 - W / 2, indexY, W, H / 24, TFT_WHITE);
    gdraw.drawRect (X0 - W / 2, indexY, W, H / 24, TFT_BLACK);

    /*
     Draw marker dots for maximum climb rate
    */
    gdraw.fillCircle (X0 - W / 2,     (HEIGHT - 39 * HEIGHT / 100), H / 24, TFT_BLACK);
    gdraw.fillCircle (X0 + W / 2 - 1, (HEIGHT - 39 * HEIGHT / 100), H / 24, TFT_BLACK);
    gdraw.fillCircle (X0 - W / 2,     (HEIGHT - 39 * HEIGHT / 100), H / 32, TFT_WHITE);
    gdraw.fillCircle (X0 + W / 2 - 1, (HEIGHT - 39 * HEIGHT / 100), H / 32, TFT_WHITE);
} // end drawAOA()


// -----------------------------------------------
/*
   Draw slip indicator
*/
void drawSlip (uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H,  int16_t slipValue, boolean flashing,  const float Array[])
{
    uint16_t CenterX = X0 + W / 2;
    uint16_t CenterY = Y0 + H / 2;

    /*
     Add ball graphic
    */

    uint16_t Colour = TFT_GREEN;
    if ( flashing && (abs(slipValue) >= 30) && AOA >= Array[7]) Colour = TFT_BLACK;
    if (!flashing && (abs(slipValue) >= 30) && AOA >= Array[7]) Colour = TFT_RED;

    gdraw.fillCircle (CenterX + slipValue * (W - H - 1) / 99 / 2, CenterY, H / 2 - 1, Colour);

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
              int16_t pitch, int16_t roll, int16_t yaw, float flightPathAngle)
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
                       -roll, arcAngle, clockWise, gradMarks);

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
                       -roll, arcAngle, clockWise, gradMarks);


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

    // 120 -screen center
    int fpY = 120-(flightPathAngle-Pitch)*120/40; // 40 degrees of pitch per half screen height
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

void pitchGraph(int16_t pitch, int16_t roll, int16_t px0, int16_t py0, uint8_t scale)
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
        myGauges.printNum (String (i)+"o", px4, py4, 8, 12, roll, TFT_BLACK, ML_DATUM);
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
        int vsiHeight=abs(int(iVSI*120/600));
        vsiHeight=constrain(vsiHeight,0,120);
        int vsiTop;
        if (iVSI > 0) vsiTop = 119 - vsiHeight;
        else          vsiTop = 119;
        gdraw.fillRect(313,vsiTop,7,vsiHeight,TFT_ORANGE);
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
    drawSlip(80, 215, 160, 20, Slip, false, AOAThresholds);

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
    gdraw.setFont(FSSB18);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setCursor(7, DEC_NUM_Y);
    gdraw.print(int(displayIAS));

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

// Convert AOA value to M5 display vertical coordinate

int mapAOA2Display(float aoa, const float Array[])
{
    if      (aoa <= Array[0])                    return 192;                                    // display bottom
    else if (aoa >  Array[0] && aoa <= Array[2]) return map2int(aoa,Array[0],Array[2],192,148); // display bottom to L/Dmax
    else if (aoa >  Array[2] && aoa <= Array[3]) return map2int(aoa,Array[2],Array[3],148,115); // L/Dmax to onspeed fast
    else if (aoa >  Array[3] && aoa <= Array[4]) return map2int(aoa,Array[3],Array[4],115, 78); // onspeed fast to onspeed slow
    else if (aoa >  Array[4] && aoa <= Array[7]) return map2int(aoa,Array[4],Array[7], 78,  1); // onspeed slow to stall warning
    else                                         return 1;                                      // display top
}


// -----------------------------------------------

// Interpolate display coordinate between two AOA limits

int map2int(float aoa, float inLow, float inHigh, int outLow, int outHigh)
{
    int Result;
    Result = round((float)(aoa - inLow) * (outHigh - outLow) / (float)(inHigh - inLow) + outLow);
    return Result;
}


// -----------------------------------------------
// Upgrade web server routines
// -----------------------------------------------

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
        + VARIANT_PREFIX
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


// -----------------------------------------------

void displaySplashScreen()
{
    // display splash screen and firmware upgrade option
    gdraw.setFont(FSSB24);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.setTextDatum(MC_DATUM);
    gdraw.drawString("Fly OnSpeed",160,60);

    gdraw.setFont(FSS9);
    gdraw.drawString(String("Version: ") + VARIANT_PREFIX + BuildInfo::version, 160, 120);
    gdraw.drawString("To upgrade press Center button",160,220);
    gdraw.pushSprite (0, 0);
    gdraw.deleteSprite();
    M5.update();
}
