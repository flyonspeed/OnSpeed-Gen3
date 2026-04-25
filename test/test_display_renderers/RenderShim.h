// RenderShim.h
//
// Forward declarations for the M5 display renderer functions, compiled
// on native x86 by RenderShim.cpp. The renderer bodies are duplicated
// verbatim from software/OnSpeed-M5-Display/src/main.cpp (revision
// captured at branch huvver-display-integration-spec). This file is
// PR-0 scaffolding; PR 1 will reparameterize the renderers onto a
// DrawApi abstraction and delete the duplicated copy here in favor of
// invoking the production implementation under test.
//
// Tests must NOT modify main.cpp to make host builds work — the whole
// point of PR 0 is to lock down a baseline trace of the code as-it-is.
// Any deliberate layout change lands in a later PR together with an
// updated `UPDATE_GOLDEN=1` hash.

#ifndef RENDER_SHIM_H
#define RENDER_SHIM_H

#include "stubs/Arduino.h"
#include "stubs/M5Unified.h"
#include "stubs/Free_Fonts.h"
#include "MockGauges.h"

// Mirror of the file-scope globals in main.cpp that the renderers read.
// Tests populate these before calling a renderer; resetState() zeroes
// them between tests.
struct RenderShimState
{
    // AOA setpoints and inputs
    float AOA = 0.0f;
    int   PercentLift = 0;
    float Pitch = 0.0f;
    float Roll = 0.0f;
    float IAS = 0.0f;
    float Palt = 0.0f;
    float iVSI = 0.0f;
    float VerticalG = 1.0f;
    float LateralG = 0.0f;
    float FlightPath = 0.0f;
    int   FlapPos = 0;
    int16_t Slip = 0;

    float OnSpeedStallWarnAOA = 20.0f;
    float OnSpeedSlowAOA      = 15.0f;
    float OnSpeedFastAOA      = 10.0f;
    float OnSpeedTonesOnAOA   = 5.0f;

    float gOnsetRate = 0.0f;
    int   SpinRecoveryCue = 0;
    int   DataMark = 0;
    float DecelRate = 0.0f;
    float SmoothedDecelRate = 0.0f;

    // G history ring buffer
    float gHistory[300] = {};
    int   gHistoryIndex = 0;

    // Smoothed "display" copies used by renderers
    float displayIAS = 0.0f;
    float displayPalt = 0.0f;
    float displayPitch = 0.0f;
    float displayVerticalG = 0.0f;
    int   displayPercentLift = 0;
    float displayDecelRate = 0.0f;

    // AOA widget geometry (defaults match main.cpp case 0 layout)
    uint16_t wgtWidth  = 102;
    uint16_t wgtHeight = 192;
    uint16_t wgtX0     = 109;   // (320 - 102) / 2
    uint16_t wgtY0     = 0;
    bool     numericDisplay = true;
    bool     flashFlag = false;

    // Attitude-indicator defaults
    int16_t g_arcSize = 115;
};

extern RenderShimState g_state;

void resetState();

// Populate the file-scope `AOAThresholds` array used by drawAOA / drawSlip
// from the four OnSpeed* setpoint fields on g_state. In production, this
// array is initialized as a side-effect of displayAOA() running first
// each frame; tests that exercise displayDecelGauge / displayGloadHistory
// in isolation must call this explicitly so drawSlip's color-gating
// (`AOA >= AOAThresholds[7]`) sees realistic values rather than zeros.
void seedAOAThresholdsFromState();

// Renderer entry points mirrored from main.cpp. These are the functions
// under test.
void drawAOA(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H,
             float aoa, boolean flashing, float Array[]);
void drawSlip(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H,
              int16_t slipValue, boolean flashing, const float Array[]);
void displayAOA();
void displayDecelGauge();
void displayGloadHistory();
void displaySplashScreen();
void AiGraph(int16_t px0, int16_t py0, int16_t arcSize, int16_t arcWidth,
             int16_t maxDisplay, int16_t minDisplay,
             int16_t startAngle, int16_t arcAngle, bool clockWise,
             uint8_t gradMarks, int16_t pitch, int16_t roll, int16_t yaw,
             float flightPathAngle);
void pitchGraph(int16_t pitch, int16_t roll, int16_t px0, int16_t py0, uint8_t scale);
int mapAOA2Display(float aoa, const float Array[]);

#endif // RENDER_SHIM_H
