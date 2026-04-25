// RenderShim.cpp
//
// Verbatim copies of the M5 display renderer function bodies, wired up
// to a MockM5Canvas so every draw call is recorded for structural
// assertions in the test fixtures. The source-of-truth for each
// function body is software/OnSpeed-M5-Display/src/main.cpp on branch
// huvver-display-integration-spec — if that file changes, the copies
// here drift; PR 1 eliminates the duplication by reparameterizing the
// renderers onto a shared DrawApi.
//
// Deliberately simple: the #define aliasing below lets the function
// bodies be pasted in with no identifier edits, so visual diff-review
// of "is this still the same code?" stays trivial.

#include "RenderShim.h"

#include <cmath>

#include "stubs/buildinfo.h"

RenderShimState g_state;

void resetState()
{
    g_state = RenderShimState{};
    // Prefill gHistory the way setup() does.
    for (int i = 0; i < 300; ++i) g_state.gHistory[i] = 1.00f;
    drawEvents().reset();
}

// Forward decl — definition lives below the displayAOA renderer body so
// the AOAThresholds[] static is in scope. Implementation just mirrors
// the array initialization from displayAOA() lines 241-248.
void seedAOAThresholdsFromState();

// ------- globals used by the copied renderer bodies -------

// The real main.cpp creates `M5Canvas gdraw(&M5.Display);` — our stub
// M5Canvas is MockM5Canvas. The mock ctor accepts any pointer via a
// templated overload; the display object is never accessed.
static M5Canvas gdraw(&M5.Display);
static Gauges   myGauges;

// main.cpp defines these constants for the AOA/attitude pages.
#define TFT_GREY        0x7BEF
#define TFT_LIGHT_GREY  0xAD55
static const uint16_t WIDTH  = 320;
static const uint16_t HEIGHT = 240;

// AOA thresholds — displayAOA() writes into this; we mirror the file-
// scope array from main.cpp rather than putting it on g_state because
// drawAOA() takes the array by pointer (as does drawSlip()).
static float AOAThresholds[8];

// Mirror every state-field reference with a macro so the renderer
// bodies below can be pasted in character-for-character from main.cpp.
#define AOA                  g_state.AOA
#define PercentLift          g_state.PercentLift
#define Pitch                g_state.Pitch
#define Roll                 g_state.Roll
#define IAS                  g_state.IAS
#define Palt                 g_state.Palt
#define iVSI                 g_state.iVSI
#define VerticalG            g_state.VerticalG
#define LateralG             g_state.LateralG
#define FlightPath           g_state.FlightPath
#define FlapPos              g_state.FlapPos
#define Slip                 g_state.Slip
#define OnSpeedStallWarnAOA  g_state.OnSpeedStallWarnAOA
#define OnSpeedSlowAOA       g_state.OnSpeedSlowAOA
#define OnSpeedFastAOA       g_state.OnSpeedFastAOA
#define OnSpeedTonesOnAOA    g_state.OnSpeedTonesOnAOA
#define gOnsetRate           g_state.gOnsetRate
#define SpinRecoveryCue      g_state.SpinRecoveryCue
#define DataMark             g_state.DataMark
#define DecelRate            g_state.DecelRate
#define SmoothedDecelRate    g_state.SmoothedDecelRate
#define gHistory             g_state.gHistory
#define gHistoryIndex        g_state.gHistoryIndex
#define displayIAS           g_state.displayIAS
#define displayPalt          g_state.displayPalt
#define displayPitch         g_state.displayPitch
#define displayVerticalG     g_state.displayVerticalG
#define displayPercentLift   g_state.displayPercentLift
#define displayDecelRate     g_state.displayDecelRate
#define wgtWidth             g_state.wgtWidth
#define wgtHeight            g_state.wgtHeight
#define wgtX0                g_state.wgtX0
#define wgtY0                g_state.wgtY0
#define numericDisplay       g_state.numericDisplay
#define flashFlag            g_state.flashFlag
#define g_arcSize            g_state.g_arcSize

// Helper used by tests that exercise drawSlip indirectly (via
// displayDecelGauge / displayAOA's flow) without calling displayAOA()
// first. Mirrors the array setup at the top of displayAOA() so
// drawSlip's `AOA >= AOAThresholds[7]` color-gating sees realistic
// values rather than the all-zero result of zero-initialization.
void seedAOAThresholdsFromState()
{
    AOAThresholds[0] = 0.0001f;
    AOAThresholds[1] = OnSpeedTonesOnAOA - 0.1f;
    AOAThresholds[2] = OnSpeedTonesOnAOA;
    AOAThresholds[3] = OnSpeedFastAOA;
    AOAThresholds[4] = OnSpeedSlowAOA;
    AOAThresholds[5] = OnSpeedSlowAOA + 0.1f;
    AOAThresholds[6] = OnSpeedStallWarnAOA - 0.1f;
    AOAThresholds[7] = OnSpeedStallWarnAOA;
}

// ============================================================================
// RENDERERS — copied verbatim from main.cpp
// ============================================================================

// map2int — helper used by mapAOA2Display
static int map2int(float aoa, float inLow, float inHigh, int outLow, int outHigh)
{
    int Result = int(std::round(float(aoa - inLow) * (outHigh - outLow) / float(inHigh - inLow) + outLow));
    return Result;
}

int mapAOA2Display(float aoa, const float Array[])
{
    if      (aoa <= Array[0])                    return 192;
    else if (aoa >  Array[0] && aoa <= Array[2]) return map2int(aoa, Array[0], Array[2], 192, 148);
    else if (aoa >  Array[2] && aoa <= Array[3]) return map2int(aoa, Array[2], Array[3], 148, 115);
    else if (aoa >  Array[3] && aoa <= Array[4]) return map2int(aoa, Array[3], Array[4], 115,  78);
    else if (aoa >  Array[4] && aoa <= Array[7]) return map2int(aoa, Array[4], Array[7],  78,   1);
    else                                         return 1;
}

void drawAOA(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, float aoa, boolean flashing, float Array[])
{
    float    Theta;
    float    cosTheta;
    float    sinTheta;
    uint16_t Colour;

    X0 = X0 + W / 2;
    Y0 = Y0 + H / 2;

    gdraw.drawRoundRect(X0 - W / 2,     Y0 - H / 2,     W,     H,     5, TFT_DARKGREY);
    gdraw.drawRoundRect(X0 - W / 2 + 1, Y0 - H / 2 + 1, W - 2, H - 2, 5, TFT_DARKGREY);

    int16_t Px0 = -W / 12, Py0 = -H / 4;
    int16_t Px1 = +W / 12, Py1 =  H / 4;

    float chevMid = Array[4] + (Array[7] - Array[4]) / 2.0f;
    if      (aoa > Array[4] && aoa <= chevMid)  Colour = TFT_YELLOW;
    else if (aoa > chevMid  && aoa <= Array[7]) Colour = TFT_RED;
    else if (aoa > Array[7] && !flashing      ) Colour = TFT_RED;
    else                                        Colour = TFT_DARKGREY;

    Theta = PI / 8;
    cosTheta = std::cos(Theta);
    sinTheta = std::sin(Theta);

    int16_t XA0 = int16_t((Px0 * cosTheta - Py0 * sinTheta) + X0 + W / 4);
    int16_t YA0 = int16_t((Px0 * sinTheta + Py0 * cosTheta) + Y0 - H / 4);
    int16_t XA1 = int16_t((Px1 * cosTheta - Py0 * sinTheta) + X0 + W / 4);
    int16_t YA1 = int16_t((Px1 * sinTheta + Py0 * cosTheta) + Y0 - H / 4);
    int16_t XA2 = int16_t((Px1 * cosTheta - Py1 * sinTheta) + X0 + W / 4);
    int16_t YA2 = int16_t((Px1 * sinTheta + Py1 * cosTheta) + Y0 - H / 4);
    int16_t XA3 = int16_t((Px0 * cosTheta - Py1 * sinTheta) + X0 + W / 4);
    int16_t YA3 = int16_t((Px0 * sinTheta + Py1 * cosTheta) + Y0 - H / 4);

    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    Theta = -PI / 8;
    cosTheta = std::cos(Theta);
    sinTheta = std::sin(Theta);
    XA0 = int16_t((Px0 * cosTheta - Py0 * sinTheta) + X0 - W / 4);
    YA0 = int16_t((Px0 * sinTheta + Py0 * cosTheta) + Y0 - H / 4);
    XA1 = int16_t((Px1 * cosTheta - Py0 * sinTheta) + X0 - W / 4);
    YA1 = int16_t((Px1 * sinTheta + Py0 * cosTheta) + Y0 - H / 4);
    XA2 = int16_t((Px1 * cosTheta - Py1 * sinTheta) + X0 - W / 4);
    YA2 = int16_t((Px1 * sinTheta + Py1 * cosTheta) + Y0 - H / 4);
    XA3 = int16_t((Px0 * cosTheta - Py1 * sinTheta) + X0 - W / 4);
    YA3 = int16_t((Px0 * sinTheta + Py1 * cosTheta) + Y0 - H / 4);
    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    // Bottom chevrons
    if (aoa >= Array[1] && aoa < Array[4]) Colour = TFT_GREEN;
    else                                    Colour = TFT_DARKGREY;
    Theta = PI / 8;
    cosTheta = std::cos(Theta); sinTheta = std::sin(Theta);
    XA0 = int16_t((Px0 * cosTheta - Py0 * sinTheta) + X0 - W / 4);
    YA0 = int16_t((Px0 * sinTheta + Py0 * cosTheta) + Y0 + H / 4);
    XA1 = int16_t((Px1 * cosTheta - Py0 * sinTheta) + X0 - W / 4);
    YA1 = int16_t((Px1 * sinTheta + Py0 * cosTheta) + Y0 + H / 4);
    XA2 = int16_t((Px1 * cosTheta - Py1 * sinTheta) + X0 - W / 4);
    YA2 = int16_t((Px1 * sinTheta + Py1 * cosTheta) + Y0 + H / 4);
    XA3 = int16_t((Px0 * cosTheta - Py1 * sinTheta) + X0 - W / 4);
    YA3 = int16_t((Px0 * sinTheta + Py1 * cosTheta) + Y0 + H / 4);
    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    Theta = -PI / 8;
    cosTheta = std::cos(Theta); sinTheta = std::sin(Theta);
    XA0 = int16_t((Px0 * cosTheta - Py0 * sinTheta) + X0 + W / 4);
    YA0 = int16_t((Px0 * sinTheta + Py0 * cosTheta) + Y0 + H / 4);
    XA1 = int16_t((Px1 * cosTheta - Py0 * sinTheta) + X0 + W / 4);
    YA1 = int16_t((Px1 * sinTheta + Py0 * cosTheta) + Y0 + H / 4);
    XA2 = int16_t((Px1 * cosTheta - Py1 * sinTheta) + X0 + W / 4);
    YA2 = int16_t((Px1 * sinTheta + Py1 * cosTheta) + Y0 + H / 4);
    XA3 = int16_t((Px0 * cosTheta - Py1 * sinTheta) + X0 + W / 4);
    YA3 = int16_t((Px0 * sinTheta + Py1 * cosTheta) + Y0 + H / 4);
    gdraw.fillTriangle(XA0, YA0, XA1, YA1, XA3, YA3, Colour);
    gdraw.fillTriangle(XA1, YA1, XA2, YA2, XA3, YA3, Colour);

    uint16_t bullsEye = H * (65 - 55 - 2) / 200;
    gdraw.fillCircle(X0, Y0, bullsEye + H / 12, TFT_BLACK);

    float    OnspeedRange = Array[4] - Array[3];
    int16_t  ArcRadius    = bullsEye + H / 16;
    uint16_t LineWidth    = 8;

    if (aoa >= Array[3] && aoa <= (Array[4] - OnspeedRange * 0.25f)) Colour = TFT_GREEN;
    else                                                             Colour = TFT_DARKGREY;
    myGauges.drawArc(X0, Y0, ArcRadius, 0.0f, PI, Colour, LineWidth);

    if (aoa >= (Array[3] + OnspeedRange * 0.25f) && aoa <= Array[4]) Colour = TFT_GREEN;
    else                                                             Colour = TFT_DARKGREY;
    myGauges.drawArc(X0, Y0, ArcRadius, PI, PI, Colour, LineWidth);

    gdraw.fillRect(X0 - W / 3, Y0 - H / 48, 2 * W / 3, H / 24, TFT_BLACK);

    if (aoa >= (Array[3] + OnspeedRange * 0.25f) && aoa <= (Array[4] - OnspeedRange * 0.25f)) Colour = TFT_GREEN;
    else                                                                                      Colour = TFT_DARKGREY;
    gdraw.fillCircle(X0, Y0, bullsEye + 2, Colour);

    int indexY = mapAOA2Display(aoa, Array);
    gdraw.fillRect(X0 - W / 2, indexY, W, H / 24, TFT_WHITE);
    gdraw.drawRect(X0 - W / 2, indexY, W, H / 24, TFT_BLACK);

    gdraw.fillCircle(X0 - W / 2,     (HEIGHT - 39 * HEIGHT / 100), H / 24, TFT_BLACK);
    gdraw.fillCircle(X0 + W / 2 - 1, (HEIGHT - 39 * HEIGHT / 100), H / 24, TFT_BLACK);
    gdraw.fillCircle(X0 - W / 2,     (HEIGHT - 39 * HEIGHT / 100), H / 32, TFT_WHITE);
    gdraw.fillCircle(X0 + W / 2 - 1, (HEIGHT - 39 * HEIGHT / 100), H / 32, TFT_WHITE);
}

void drawSlip(uint16_t X0, uint16_t Y0, uint16_t W, uint16_t H, int16_t slipValue, boolean flashing, const float Array[])
{
    uint16_t CenterX = X0 + W / 2;
    uint16_t CenterY = Y0 + H / 2;

    uint16_t Colour = TFT_GREEN;
    if ( flashing && (std::abs(slipValue) >= 30) && AOA >= Array[7]) Colour = TFT_BLACK;
    if (!flashing && (std::abs(slipValue) >= 30) && AOA >= Array[7]) Colour = TFT_RED;

    gdraw.fillCircle(CenterX + slipValue * (W - H - 1) / 99 / 2, CenterY, H / 2 - 1, Colour);

    gdraw.fillRect(CenterX - H / 2 - 9, Y0, 10, H, TFT_BLACK);
    gdraw.fillRect(CenterX - H / 2 - 7, Y0, 6,  H, TFT_WHITE);
    gdraw.fillRect(CenterX + H / 2,     Y0, 10, H, TFT_BLACK);
    gdraw.fillRect(CenterX + H / 2 + 2, Y0, 6,  H, TFT_WHITE);
}

void displayAOA()
{
    AOAThresholds[0] = 0.0001f;
    AOAThresholds[1] = OnSpeedTonesOnAOA - 0.1f;
    AOAThresholds[2] = OnSpeedTonesOnAOA;
    AOAThresholds[3] = OnSpeedFastAOA;
    AOAThresholds[4] = OnSpeedSlowAOA;
    AOAThresholds[5] = OnSpeedSlowAOA + 0.1f;
    AOAThresholds[6] = OnSpeedStallWarnAOA - 0.1f;
    AOAThresholds[7] = OnSpeedStallWarnAOA;

    drawAOA(wgtX0, wgtY0, wgtWidth, wgtHeight, AOA, flashFlag, AOAThresholds);

    #define PERCENT_X_POS   140
    #define PERCENT_Y_POS    27

    gdraw.setFont(FSSB18);

    char PctLiftStr[4];
    std::snprintf(PctLiftStr, sizeof(PctLiftStr), "%02d", displayPercentLift);
    const int pctX = (displayPercentLift < 100) ? PERCENT_X_POS : PERCENT_X_POS - 7;
    gdraw.setTextColor(TFT_BLACK);
    for (int xoffset = -3; xoffset <= 3; xoffset += 3)
        for (int yoffset = -3; yoffset <= 3; yoffset += 3)
        {
            gdraw.setCursor(pctX + xoffset, PERCENT_Y_POS + yoffset);
            gdraw.print(PctLiftStr);
        }
    gdraw.setTextColor(TFT_WHITE);
    gdraw.setCursor(pctX, PERCENT_Y_POS);
    gdraw.print(PctLiftStr);

    if (numericDisplay)
    {
        constexpr int RIGHT_X = 303;
        constexpr int LABEL_Y = 90;
        constexpr int NUM_Y   = 130;

        gdraw.setFont(FSS18);
        gdraw.setTextColor(TFT_GREEN);
        gdraw.setCursor(5, LABEL_Y);
        gdraw.print("IAS");
        gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth("G"), LABEL_Y);
        gdraw.print("G");

        gdraw.setFont(FSSB18);
        gdraw.setTextColor(TFT_WHITE);
        gdraw.setCursor(7, NUM_Y);
        gdraw.print(int(displayIAS));

        char GStr[6];
        std::snprintf(GStr, sizeof(GStr), "%+1.1f", displayVerticalG);
        gdraw.setCursor(RIGHT_X - (int)gdraw.textWidth(GStr), NUM_Y);
        gdraw.print(GStr);

        gdraw.fillCircle(23, 204, 16, TFT_GREY);
        int cX              =  23;
        int cY              = 204;
        int Radius          =  16;
        int triangleTopX    = int(cX + std::sin(FlapPos * PI / 180) * Radius);
        int triangleTopY    = int(cY - std::cos(FlapPos * PI / 180) * Radius);
        int triangleBottomX = int(cX - std::sin(FlapPos * PI / 180) * Radius);
        int triangleBottomY = int(cY + std::cos(FlapPos * PI / 180) * Radius);
        int triangleRightX  = int(cX + std::cos(FlapPos * PI / 180) * (Radius + 33));
        int triangleRightY  = int(cY + std::sin(FlapPos * PI / 180) * (Radius + 33));
        gdraw.fillTriangle(triangleTopX, triangleTopY, triangleBottomX, triangleBottomY, triangleRightX, triangleRightY, TFT_GREY);
        gdraw.drawPixel(triangleRightX, triangleRightY, TFT_BLACK);

        gdraw.drawPixel(72, 204, TFT_WHITE);
        gdraw.drawPixel(71, 212, TFT_WHITE);
        gdraw.drawPixel(69, 220, TFT_WHITE);
        gdraw.drawPixel(65, 228, TFT_WHITE);
        gdraw.drawPixel(60, 235, TFT_WHITE);

        gdraw.setFont(FSS12);
        gdraw.setTextColor(TFT_WHITE);
        gdraw.setTextDatum(textdatum_t::middle_center);
        char FlapsChar[8];
        std::snprintf(FlapsChar, sizeof(FlapsChar), "%i", FlapPos);
        gdraw.drawString(FlapsChar, cX, cY);
        gdraw.setTextDatum(textdatum_t::baseline_left);
    }

    drawSlip(80, 204, 160, 34, Slip, flashFlag, AOAThresholds);

    if (gOnsetRate != 0.0f)
    {
        int gOnsetHeight = std::abs(int(gOnsetRate * 120 / 2));
        gOnsetHeight = constrain(gOnsetHeight, 0, 120);
        int gOnsetTop;
        if (gOnsetRate > 0) gOnsetTop = 119 - gOnsetHeight;
        else                gOnsetTop = 119;
        gdraw.fillRect(313, gOnsetTop, 7, gOnsetHeight, TFT_YELLOW);
    }

    for (int i = 15; i < 226; i += 15)
    {
        gdraw.drawLine(313, i, 319, i, TFT_GREY);
    }

    gdraw.drawLine(306, 118, 312, 118, TFT_GREY);
    gdraw.drawLine(306, 119, 312, 119, TFT_GREY);
    gdraw.drawLine(306, 120, 312, 120, TFT_GREY);
}

void AiGraph(int16_t px0, int16_t py0, int16_t arcSize, int16_t arcWidth, int16_t maxDisplay, int16_t minDisplay,
             int16_t /*startAngle*/, int16_t arcAngle, bool clockWise, uint8_t gradMarks,
             int16_t pitch, int16_t roll, int16_t /*yaw*/, float flightPathAngle)
{
    {
        gdraw.fillSprite(TFT_CYAN);

        float xRotate = (2.0f * (float)WIDTH) * std::cos(roll * DEG_TO_RAD);
        float yRotate = (2.0f * (float)WIDTH) * std::sin(roll * DEG_TO_RAD);

        float pxc = px0 + pitch * HEIGHT / 80 * std::sin(roll * DEG_TO_RAD);
        float pyc = py0 + pitch * HEIGHT / 80 * std::cos(roll * DEG_TO_RAD);

        float px1 = pxc - xRotate;
        float py1 = pyc + yRotate;

        float px2 = pxc + xRotate;
        float py2 = pyc - yRotate;

        float px3 = px1 + 3 * HEIGHT * std::cos(roll * DEG_TO_RAD - HALF_PI);
        float py3 = py1 - 3 * HEIGHT * std::sin(-roll * DEG_TO_RAD - HALF_PI);

        float px4 = px2 - 3 * HEIGHT * std::cos(roll * DEG_TO_RAD + HALF_PI);
        float py4 = py2 + 3 * HEIGHT * std::sin(roll * DEG_TO_RAD + HALF_PI);

        gdraw.fillTriangle(px1, py1, px2, py2, px3, py3, 0x8281);
        gdraw.fillTriangle(px3, py3, px4, py4, px2, py2, 0x8281);
        gdraw.drawLine(px1, py1, px2, py2, TFT_BLACK);
        gdraw.drawLine(px3, py3, px4, py4, TFT_BLUE);
        gdraw.fillCircle(pxc, pyc, 3, TFT_BLACK);
    }

    pitchGraph(pitch, roll, px0, py0, 10);

    arcSize  = 115;
    arcWidth =  15;

    myGauges.clearRanges();
    myGauges.clearPointers();
    myGauges.setPointer(1,   0, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer(2, 180, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer(3, 210, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer(4, 240, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer(5, 270, ARROW_OUT, TFT_YELLOW, '\0');
    myGauges.setPointer(6, 300, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer(7, 330, BAR_LONG,  TFT_WHITE,  '\0');
    myGauges.setPointer(8, 0, 0, 0, '\0');

    myGauges.arcGraph(px0, py0, arcSize, arcWidth, maxDisplay, minDisplay,
                      -roll, arcAngle, clockWise, gradMarks);

    arcSize = 115;
    arcWidth = 15;

    myGauges.clearRanges();
    myGauges.clearPointers();
    myGauges.setPointer(1, 250, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer(2, 260, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer(3, 280, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer(4, 290, BAR_SHORT, TFT_WHITE, '\0');
    myGauges.setPointer(5, 225, ROUND_DOT, TFT_WHITE, '\0');
    myGauges.setPointer(6, 315, ROUND_DOT, TFT_WHITE, '\0');
    myGauges.setPointer(7, 0, 0, 0, '\0');
    myGauges.setPointer(8, 0, 0, 0, '\0');

    myGauges.arcGraph(px0, py0, arcSize, arcWidth, maxDisplay, minDisplay,
                      -roll, arcAngle, clockWise, gradMarks);

    arcSize = 100;
    arcWidth = 15;

    uint16_t px1 = px0 - arcSize;
    uint16_t py1 = py0;
    uint16_t px2 = px0 - arcSize / 4;
    uint16_t py2 = py0;
    uint16_t px3 = px0 + arcSize / 4;
    uint16_t py3 = py0;
    uint16_t px4 = px0 + arcSize;
    uint16_t py4 = py0;
    uint16_t px5 = px0;
    uint16_t py5 = py0 + arcSize / 4;

    gdraw.fillCircle(px0, py0, 2 * HEIGHT / 80, TFT_YELLOW);
    gdraw.drawCircle(px0, py0, 2 * HEIGHT / 80, TFT_BLACK);

    gdraw.drawFastHLine(px1, py1, 3 * arcSize / 4, TFT_YELLOW);
    gdraw.drawLine(px2, py2, px5, py5, TFT_YELLOW);
    gdraw.drawLine(px5, py5, px3, py3, TFT_YELLOW);
    gdraw.drawFastHLine(px3, py3, 3 * arcSize / 4, TFT_YELLOW);

    gdraw.drawFastHLine(px1, py1 - 1, 3 * arcSize / 4, TFT_YELLOW);
    gdraw.drawLine(px2, py2 - 1, px5, py5 - 1, TFT_YELLOW);
    gdraw.drawLine(px5, py5 - 1, px3, py3 - 1, TFT_YELLOW);
    gdraw.drawFastHLine(px3, py3 - 1, 3 * arcSize / 4, TFT_YELLOW);

    gdraw.drawFastHLine(px1, py1 - 2, 3 * arcSize / 4, TFT_YELLOW);
    gdraw.drawLine(px2, py2 - 2, px5, py5 - 2, TFT_YELLOW);
    gdraw.drawLine(px5, py5 - 2, px3, py3 - 2, TFT_YELLOW);
    gdraw.drawFastHLine(px3, py3 - 2, 3 * arcSize / 4, TFT_YELLOW);

    gdraw.drawFastHLine(px1, py1 - 3, 3 * arcSize / 4, TFT_BLACK);
    gdraw.drawLine(px2, py2 - 3, px5, py5 - 3, TFT_BLACK);
    gdraw.drawLine(px5, py5 - 3, px3, py3 - 3, TFT_BLACK);
    gdraw.drawFastHLine(px3, py3 - 3, 3 * arcSize / 4, TFT_BLACK);

    gdraw.drawFastHLine(px1, py1 + 1, 3 * arcSize / 4, TFT_YELLOW);
    gdraw.drawLine(px2, py2 + 1, px5, py5 + 1, TFT_YELLOW);
    gdraw.drawLine(px5, py5 + 1, px3, py3 + 1, TFT_YELLOW);
    gdraw.drawFastHLine(px3, py3 + 1, 3 * arcSize / 4, TFT_YELLOW);

    gdraw.drawFastHLine(px1, py1 + 2, 3 * arcSize / 4, TFT_YELLOW);
    gdraw.drawLine(px2, py2 + 2, px5, py5 + 2, TFT_YELLOW);
    gdraw.drawLine(px5, py5 + 2, px3, py3 + 2, TFT_YELLOW);
    gdraw.drawFastHLine(px3, py3 + 2, 3 * arcSize / 4, TFT_YELLOW);

    gdraw.drawFastHLine(px1, py1 + 3, 3 * arcSize / 4, TFT_BLACK);
    gdraw.drawLine(px2, py2 + 3, px5, py5 + 3, TFT_BLACK);
    gdraw.drawLine(px5, py5 + 3, px3, py3 + 3, TFT_BLACK);
    gdraw.drawFastHLine(px3, py3 + 3, 3 * arcSize / 4, TFT_BLACK);

    gdraw.drawFastVLine(px1, py1 - 3, 6, TFT_BLACK);
    gdraw.drawFastVLine(px4, py4 - 3, 6, TFT_BLACK);

    // Top pointer
    px1 = px0;
    py1 = py0 - arcSize + arcWidth / 2;
    px2 = px0 - arcWidth / 2;
    py2 = py0 - arcSize + 2 * arcWidth;
    px3 = px0 + arcWidth / 2;
    py3 = py0 - arcSize + 2 * arcWidth;

    gdraw.fillTriangle(px1, py1, px2, py2, px3, py3, TFT_YELLOW);
    gdraw.drawLine(px1, py1, px2, py2, TFT_BLACK);
    gdraw.drawLine(px2, py2, px3, py3, TFT_BLACK);
    gdraw.drawLine(px3, py3, px1, py1, TFT_BLACK);

    // Flight-path marker
    int fpY = 120 - int((flightPathAngle - Pitch) * 120 / 40);
    fpY = constrain(fpY, 0, 239);
    int fpX = 159;

    gdraw.drawCircle(fpX, fpY, 12, TFT_MAGENTA);
    gdraw.drawCircle(fpX, fpY, 13, TFT_MAGENTA);
    gdraw.drawCircle(fpX, fpY, 14, TFT_MAGENTA);

    gdraw.drawLine(fpX - 33, fpY - 1, fpX - 14, fpY - 1, TFT_MAGENTA);
    gdraw.drawLine(fpX - 33, fpY,     fpX - 14, fpY,     TFT_MAGENTA);
    gdraw.drawLine(fpX - 33, fpY + 1, fpX - 14, fpY + 1, TFT_MAGENTA);

    gdraw.drawLine(fpX + 33, fpY - 1, fpX + 14, fpY - 1, TFT_MAGENTA);
    gdraw.drawLine(fpX + 33, fpY,     fpX + 14, fpY,     TFT_MAGENTA);
    gdraw.drawLine(fpX + 33, fpY + 1, fpX + 14, fpY + 1, TFT_MAGENTA);

    gdraw.drawLine(fpX - 1, fpY - 14, fpX - 1, fpY - 33, TFT_MAGENTA);
    gdraw.drawLine(fpX,     fpY - 14, fpX,     fpY - 33, TFT_MAGENTA);
    gdraw.drawLine(fpX + 1, fpY - 14, fpX + 1, fpY - 33, TFT_MAGENTA);
}

void pitchGraph(int16_t pitch, int16_t roll, int16_t px0, int16_t py0, uint8_t scale)
{
    float px1, px2, px3, px4;
    float py1, py2, py3, py4;
    float xRotate;
    float yRotate;

    float pxc = px0 + pitch * HEIGHT / 80 * std::sin(roll * DEG_TO_RAD);
    float pyc = py0 + pitch * HEIGHT / 80 * std::cos(roll * DEG_TO_RAD);

    gdraw.setTextDatum(MC_DATUM);

    xRotate = (0.10f * g_arcSize) * std::cos(roll * DEG_TO_RAD);
    yRotate = (0.10f * g_arcSize) * std::sin(roll * DEG_TO_RAD);

    px1 = pxc - xRotate * 1.0f;
    py1 = pyc + yRotate * 1.0f;
    px2 = pxc + xRotate * 1.0f;
    py2 = pyc - yRotate * 1.0f;

    for (int16_t i = -85; i <= 85; i += scale)
    {
        px3 = px1 - (i * HEIGHT / 80) * std::cos(roll * DEG_TO_RAD - HALF_PI);
        py3 = py1 + (i * HEIGHT / 80) * std::sin(-roll * DEG_TO_RAD - HALF_PI);
        px4 = px2 + (i * HEIGHT / 80) * std::cos(roll * DEG_TO_RAD + HALF_PI);
        py4 = py2 - (i * HEIGHT / 80) * std::sin(roll * DEG_TO_RAD + HALF_PI);
        gdraw.drawLine(px3, py3, px4, py4, TFT_BLACK);
    }

    px1 = pxc - xRotate * 2.0f;
    py1 = pyc + yRotate * 2.0f;
    px2 = pxc + xRotate * 2.0f;
    py2 = pyc - yRotate * 2.0f;

    for (int16_t i = -90; i <= 90; i += scale)
    {
        px3 = px1 - (i * HEIGHT / 80) * std::cos(roll * DEG_TO_RAD - HALF_PI);
        py3 = py1 + (i * HEIGHT / 80) * std::sin(-roll * DEG_TO_RAD - HALF_PI);
        px4 = px2 + (i * HEIGHT / 80) * std::cos(roll * DEG_TO_RAD + HALF_PI);
        py4 = py2 - (i * HEIGHT / 80) * std::sin(roll * DEG_TO_RAD + HALF_PI);

        gdraw.setCursor(px4, py4);
        gdraw.drawLine(px3, py3, px4, py4, TFT_BLACK);

        px4 += xRotate * 0.75f;
        py4 -= yRotate * 0.75f;
        myGauges.printNum(String(i) + "o", px4, py4, 8, 12, roll, TFT_BLACK, ML_DATUM);
    }
    gdraw.setTextDatum(textdatum_t::baseline_left);
}

void displayDecelGauge()
{
    gdraw.fillRoundRect(109, 1, 102, 210, 5, TFT_RED);
    gdraw.fillRect(109, 87, 102, 36, TFT_GREEN);
    gdraw.drawRoundRect(109, 1, 102, 210, 5, TFT_LIGHT_GREY);

    int decelIndex = int(35.143f * SmoothedDecelRate + 141.48f - 3.5f);
    decelIndex = constrain(decelIndex, 2, 205);

    gdraw.fillRect(109, decelIndex, 102, 7, TFT_WHITE);
    gdraw.drawRect(109, decelIndex, 102, 7, TFT_BLACK);

    gdraw.setFont(FSS9);
    gdraw.setTextColor(TFT_WHITE);
    gdraw.setTextDatum(textdatum_t::middle_right);
    gdraw.drawString("-1", 95, 106);
    gdraw.drawString("-2", 95,  72);
    gdraw.drawString("-3", 95,  36);
    gdraw.drawString("0",  95, 141);
    gdraw.drawString("1",  95, 177);
    gdraw.setTextDatum(textdatum_t::baseline_left);

    gdraw.drawLine(99, 106, 107, 106, TFT_LIGHT_GREY);
    gdraw.drawLine(99,  72, 107,  72, TFT_LIGHT_GREY);
    gdraw.drawLine(99,  36, 107,  36, TFT_LIGHT_GREY);
    gdraw.drawLine(99, 141, 107, 141, TFT_LIGHT_GREY);
    gdraw.drawLine(99, 177, 107, 177, TFT_LIGHT_GREY);

    if (iVSI != 0.0f)
    {
        int vsiHeight = std::abs(int(iVSI * 120 / 600));
        vsiHeight = constrain(vsiHeight, 0, 120);
        int vsiTop;
        if (iVSI > 0) vsiTop = 119 - vsiHeight;
        else          vsiTop = 119;
        gdraw.fillRect(313, vsiTop, 7, vsiHeight, TFT_ORANGE);
    }

    for (int i = 19; i < 220; i += 20)
    {
        gdraw.drawLine(313, i, 319, i, TFT_LIGHT_GREY);
    }

    gdraw.drawLine(306, 118, 312, 118, TFT_LIGHT_GREY);
    gdraw.drawLine(306, 119, 312, 119, TFT_LIGHT_GREY);
    gdraw.drawLine(306, 120, 312, 120, TFT_LIGHT_GREY);

    drawSlip(80, 215, 160, 20, Slip, false, AOAThresholds);

    constexpr int DEC_RIGHT_X     = 303;
    constexpr int DEC_IAS_LABEL_Y = 90;
    constexpr int DEC_KTS_LABEL_Y = 90;
    constexpr int DEC_NUM_Y       = 130;

    gdraw.setFont(FSS18);
    gdraw.setTextColor(TFT_GREEN);
    gdraw.setCursor(5, DEC_IAS_LABEL_Y);
    gdraw.print("IAS");
    gdraw.setCursor(DEC_RIGHT_X - (int)gdraw.textWidth("Kt/s"), DEC_KTS_LABEL_Y);
    gdraw.print("Kt/s");

    gdraw.setFont(FSSB18);
    gdraw.setTextColor(TFT_WHITE);
    gdraw.setCursor(7, DEC_NUM_Y);
    gdraw.print(int(displayIAS));

    char DecelStr[6];
    std::snprintf(DecelStr, sizeof(DecelStr), "%+1.1f", displayDecelRate);
    gdraw.setCursor(DEC_RIGHT_X - (int)gdraw.textWidth(DecelStr), DEC_NUM_Y);
    gdraw.print(DecelStr);
}

void displayGloadHistory()
{
    gdraw.drawLine(19, 133, 319, 133, TFT_WHITE);
    gdraw.drawLine(19,   0,  19, 239, TFT_WHITE);

    gdraw.drawLine(19,  27, 319,  27, TFT_GREY);
    gdraw.drawLine(19,  53, 319,  53, TFT_GREY);
    gdraw.drawLine(19,  80, 319,  80, TFT_GREY);
    gdraw.drawLine(19, 106, 319, 106, TFT_GREY);

    gdraw.drawLine(19, 160, 319, 160, TFT_GREY);
    gdraw.drawLine(19, 186, 319, 186, TFT_GREY);
    gdraw.drawLine(19, 213, 319, 213, TFT_GREY);

    gdraw.setFont(FSS12);
    gdraw.setTextColor(TFT_WHITE);
    gdraw.setTextDatum(textdatum_t::middle_right);
    gdraw.drawString("5",  18,  27);
    gdraw.drawString("4",  18,  53);
    gdraw.drawString("3",  18,  80);
    gdraw.drawString("2",  18, 106);
    gdraw.drawString("1",  18, 133);
    gdraw.drawString("0",  18, 160);
    gdraw.drawString("-1", 18, 186);
    gdraw.drawString("-2", 18, 213);

    gdraw.setFont(FSS12);
    gdraw.setTextDatum(textdatum_t::top_center);
    gdraw.drawString("G-LOAD [1 min]", 160, 2);
    gdraw.setTextDatum(textdatum_t::baseline_left);

    int      gDisplayIndex = gHistoryIndex;
    uint16_t gColor;
    for (int i = 319; i > 19; i--)
    {
        int gHeight = 160 - int(gHistory[gDisplayIndex] * 26.67f);
        gHeight = constrain(gHeight, 0, 239);

        if      (gHistory[gDisplayIndex] >= 1)                                 gColor = TFT_GREEN;
        else if (gHistory[gDisplayIndex] <  1 && gHistory[gDisplayIndex] >= 0) gColor = TFT_YELLOW;
        else                                                                   gColor = TFT_RED;

        gdraw.fillCircle(i, gHeight, 2, gColor);

        if (gDisplayIndex < 299) gDisplayIndex++;
        else                     gDisplayIndex = 0;
    }
}

void displaySplashScreen()
{
    gdraw.setFont(FSSB24);
    gdraw.setTextColor(TFT_WHITE);
    gdraw.setTextDatum(MC_DATUM);
    gdraw.drawString("Fly OnSpeed", 160, 60);

    gdraw.setFont(FSS9);
    gdraw.drawString(String("Version: ") + BuildInfo::version, 160, 120);
    gdraw.drawString("To upgrade press Center button", 160, 220);
    gdraw.pushSprite(0, 0);
    gdraw.deleteSprite();
}

// Bound the macro-aliasing block to this TU. Without these #undefs, any
// later include in this translation unit that references a member named
// AOA / Pitch / etc. would silently get rewritten to g_state.<that
// member> — a class of bug -Wshadow cannot catch. Today no such include
// reaches here, but the renderer-body block above is the only intended
// scope for these aliases.
#undef AOA
#undef PercentLift
#undef Pitch
#undef Roll
#undef IAS
#undef Palt
#undef iVSI
#undef VerticalG
#undef LateralG
#undef FlightPath
#undef FlapPos
#undef Slip
#undef OnSpeedStallWarnAOA
#undef OnSpeedSlowAOA
#undef OnSpeedFastAOA
#undef OnSpeedTonesOnAOA
#undef gOnsetRate
#undef SpinRecoveryCue
#undef DataMark
#undef DecelRate
#undef SmoothedDecelRate
#undef gHistory
#undef gHistoryIndex
#undef displayIAS
#undef displayPalt
#undef displayPitch
#undef displayVerticalG
#undef displayPercentLift
#undef displayDecelRate
#undef wgtWidth
#undef wgtHeight
#undef wgtX0
#undef wgtY0
#undef numericDisplay
#undef flashFlag
#undef g_arcSize
