
#include <Arduino.h>
#include "SoftwareSerial.h"
//#include "EspSoftwareSerial.h"

#include "Globals.h"
#include "DisplaySerial.h"

//SoftwareSerial      DispSerial(DISPLAY_SER_RX, DISPLAY_SER_TX);

const int   serialDisplaySmoothingLat  = 50;    // smoothing serial display data (LateralG)  10hz data.
const int   serialDisplaySmoothingVert = 20;    // smoothing serial display data (VertG)  10hz data.

static inline bool IsFiniteFloat(float v)
{
    return !isnan(v) && !isinf(v);
}

static inline int ClampInt(int value, int minValue, int maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static inline unsigned ClampUInt(unsigned value, unsigned minValue, unsigned maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static inline int SafeScaledInt(float value, float scale, int minValue, int maxValue)
{
    if (!IsFiniteFloat(value))
        return 0;

    // Preserve original behavior (truncate toward zero) but clamp to the
    // fixed-width protocol field size to prevent message length changes.
    long scaled = (long)(value * scale);
    if (scaled < minValue) scaled = minValue;
    if (scaled > maxValue) scaled = maxValue;
    return (int)scaled;
}

static inline unsigned SafeScaledUInt(float value, float scale, unsigned minValue, unsigned maxValue)
{
    if (!IsFiniteFloat(value))
        return minValue;

    long scaled = (long)(value * scale);
    if (scaled < (long)minValue) scaled = (long)minValue;
    if (scaled > (long)maxValue) scaled = (long)maxValue;
    return (unsigned)scaled;
}

static inline unsigned WrapUInt(unsigned value, unsigned modulo)
{
    return (modulo == 0) ? value : (value % modulo);
}


// ----------------------------------------------------------------------------

// FreeRTOS task for writing display data

void WriteDisplayDataTask(void * pvParams)
    {
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime = xTaskGetTickCount();
    static unsigned long uLastLateLogMs = 0;

    while (true)
        {
        // No delay happening is a design error so flag it if it happens
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));  // 100 msec
        if (xWasDelayed == pdFALSE)
            {
            // If this task runs late, don't "catch up" by running back-to-back and
            // bursting serial data at the display. Re-align to the current tick
            // period instead.
            xLastWakeTime = xLAST_TICK_TIME(100);
            unsigned long uNow = millis();
            if ((uNow - uLastLateLogMs) > 1000)
                {
                g_Log.println(MsgLog::EnDisplay, MsgLog::EnWarning, "WriteDisplayDataTask Late");
                uLastLateLogMs = uNow;
                }
            }

        g_DisplaySerial.Write();
        }

    } // end WriteSerialDataTask()


// ============================================================================

DisplaySerial::DisplaySerial()
{

}

// ----------------------------------------------------------------------------

void DisplaySerial::Init(Stream * pDispSerial)
{
    fPAltSmoothed      = 0.0;
    fVerticalGSmoothed = 1.0;  // start at 1G;
    fLateralGSmoothed  = 0.0;

    // In the original G2V3 implementation the panel output port could be
    // selected. In this implementation panel output is a fixed serial
    // port. But to easily allow run time configuration I have made the
    // serial port a pointer to the Stream base object.

    pSerial = pDispSerial;

}

// ----------------------------------------------------------------------------

void DisplaySerial::Write()
    {
    byte            SerialCRC;

//    if (serialOutPort!="NONE" && millis()-serialoutLastUpdate>100) // update every 100ms, 10Hz

    char    serialOutString[200];

    int     iPercentLift;
    float   fDisplayAOA;
    float   fDisplayIAS;
    float   smoothingAlphaLat  = 2.0 / (serialDisplaySmoothingLat  + 1);
    float   smoothingAlphaVert = 2.0 / (serialDisplaySmoothingVert + 1);
    int     iDisplayVerticalG;

#ifdef SPHERICAL_PROBE
    fDisplayIAS = g_EfisSerial.suEfis.IAS;
#else
    fDisplayIAS = g_Sensors.IAS;
#endif
    const bool bIasValidForOutput = (fDisplayIAS >= g_Config.iMuteAudioUnderIAS);
    const float fIasForOutput = bIasValidForOutput ? fDisplayIAS : 0.0f;
    if (fPAltSmoothed == 0.0)
        fPAltSmoothed = M2FT(g_AHRS.KalmanAlt);
    else
        fPAltSmoothed = M2FT(g_AHRS.KalmanAlt) * smoothingAlphaVert/10+ (1-smoothingAlphaVert/10)*fPAltSmoothed; // increased smoothing needed

    fVerticalGSmoothed = g_AHRS.AccelVertCorr * smoothingAlphaVert+ (1-smoothingAlphaVert)*fVerticalGSmoothed;
    if (IsFiniteFloat(fVerticalGSmoothed))
        iDisplayVerticalG = (int)ceilf(fVerticalGSmoothed * 10.0f);
    else
        iDisplayVerticalG = 0;

    fLateralGSmoothed = g_AHRS.AccelLatCorr * smoothingAlphaLat+ (1-smoothingAlphaLat)*fLateralGSmoothed;

    // don't output precentLift at low speeds.
    if (bIasValidForOutput)
        {
        fDisplayAOA = g_Sensors.AOA;
        // Scale percent lift
        if       (g_Sensors.AOA <  g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA)          // LDmaxAOA
            iPercentLift = map(g_Sensors.AOA, 0, g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA,         0, 50);
        else if ((g_Sensors.AOA >= g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA)       && // LDmaxAOA
                 (g_Sensors.AOA <= g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA))   // onSpeedAOAfast
            iPercentLift = map(g_Sensors.AOA, g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA, g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA,  50, 55);
        else if ((g_Sensors.AOA >  g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA) &&
                 (g_Sensors.AOA <= g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA))   // onSpeedAOAslow
            iPercentLift = map(g_Sensors.AOA, g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA,  g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA,  55, 66);
        else if ((g_Sensors.AOA >  g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA) &&
                 (g_Sensors.AOA <= g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA))     // stallWarningAOA
            iPercentLift = map(g_Sensors.AOA, g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA,  g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA, 66, 90);
        else
            iPercentLift = map(g_Sensors.AOA, g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA, g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA*100/90,90,100);
        iPercentLift = constrain(iPercentLift,0,99);
        }
    else
        {
        iPercentLift = 0;
        fDisplayAOA  = 0;
        }

    // Output the data in the appropriate format

    if (g_Config.sSerialOutFormat == "G3X")
        {
        // Clamp to fixed-width protocol fields to prevent buffer overruns and
        // malformed output when values go out of range.
        const int      iPitch10   = SafeScaledInt(g_AHRS.SmoothedPitch, 10.0f, -999,    999);
        const int      iRoll10    = SafeScaledInt(g_AHRS.SmoothedRoll,  10.0f, -9999,  9999);
        const unsigned uIas10     = SafeScaledUInt(fIasForOutput,       10.0f, 0,      9999);
        const int      iPaltFt    = SafeScaledInt(fPAltSmoothed,         1.0f, -99999, 99999);
        const int      iLatG100   = SafeScaledInt(-fLateralGSmoothed,  100.0f, -99,      99);
        const int      iVertG10   = ClampInt(iDisplayVerticalG,                -99,      99);
        const unsigned uPctLift   = ClampUInt((unsigned)iPercentLift,           0,       99);

        const int iChars = snprintf(
            serialOutString,
            sizeof(serialOutString),
            "=1100000000%+04i%+05i___%04u%+06i____%+03i%+03i%02u__________",
            iPitch10,
            iRoll10,
            uIas10,
            iPaltFt,
            iLatG100,
            iVertG10,
            uPctLift);

        // Expected fixed-length payload is 55 bytes.
        if (iChars != 55)
            return;

        SerialCRC = 0x00;
        for (int i = 0; i < 55; i++)
            SerialCRC += (byte)serialOutString[i];
        } // end if G3X

    else if (g_Config.sSerialOutFormat == "ONSPEED")
        {
        //  0 - #                         Escape character  '#'
        //  1 - 1                         Sentence ID '1'
        //  2 - %+04i  Pitch              Pitch, 4 bytes, 0.1 degree, positive = up
        //  6 - %+05i  Roll               Roll, 5 bytes, 0.1 degree, positive = right
        // 11 - %04u   IAS                IAS, 4 bytes, 0.1 kts
        // 15 - %+06i  PALT               PALT, 6 bytes, 1 ft
        // 21 - %+05i  Rate of Turn       Rate of Turn,  5 bytes, .1 deg/sec, positive = right
        // 26 - %+03i  LateralG           Lateral G, 3 bytes , 0.01g, positive = leftward
        // 29 - %+03i  VerticalG          VerticalG, 3 bytes, 0.1g, positive = upward
        // 32 - %02i   Percent Lift       Percent Lift, 2 bytes, 00-99%
        // 34 - %+04i  AOA Degrees        AOA Degrees, 4 bytes, 0.1 degree
        // 38 - %+04i  iVSI               iVSI, 4 bytes, 10 fpm, positive up [-999;999]
        // 42 - %+03i  OAT                OAT, 3 bytes, 1 deg C
        // 45 - %+04i  FlightPath         FlightPath angle, 4 bytes, 0.1 degree
        // 49 - %+03i  Flaps              Flaps Pos, 3 bytes, 1 degree, with +/-sign
        // 52 - %+04i  StallWarn          StallWarn AOA, 4 bytes, 0.1 degrees
        // 56 - %+04i  OnSpeedSlow        OnSpeedSlow AOA, 4 bytes, 0.1 degrees
        // 60 - %+04i  OnSpeedFast        OnSpeedFast AOA, 4 bytes, 0.1 degrees
        // 64 - %+04i  Tones On           Tones On AOA, 4 bytes, 0.1 degrees
        // 68 - %+04i  G onset rate       G onset rate, 4 bytes, 0.01 G/sec
        // 72 - %+02i  Spin Recovery Cue  Spin Recovery Cue, 2 bytes, -1/0/+1
        // 74 - %02u   DataMark           DataMark, 2 bytes
        // 76 -                           Checksum, 2 bytes, ASCII HEX, sum of all previous bytes
        // 78 -                           CR/LF,2 bytes, 0x0D 0x0A

// Actual...
// #1+2147483647+21474836470000+2147483647+0001+02-0900+000+000-127+000+40+145+124+086+047+000+000A5

        int gOnsetRate      = 0;
        int spinRecoveryCue = 0;
#ifdef OAT_AVAILABLE
        int iOATc           = int(g_Sensors.OatC);
#else
        int iOATc           = 0;
#endif

        const int      iPitch10    = SafeScaledInt(g_AHRS.SmoothedPitch, 10.0f, -999,    999);
        const int      iRoll10     = SafeScaledInt(g_AHRS.SmoothedRoll,  10.0f, -9999,  9999);
        const unsigned uIas10      = SafeScaledUInt(fIasForOutput,       10.0f, 0,      9999);
        const int      iPaltFt     = SafeScaledInt(fPAltSmoothed,         1.0f, -99999, 99999);
        const int      iYaw10      = SafeScaledInt(g_AHRS.gYaw,          10.0f, -9999,  9999);
        const int      iLatG100    = SafeScaledInt(-fLateralGSmoothed,  100.0f, -99,      99);
        const int      iVertG10    = ClampInt(iDisplayVerticalG,                -99,      99);
        const unsigned uPctLift    = ClampUInt((unsigned)iPercentLift,           0,       99);
        const int      iAoa10      = SafeScaledInt(fDisplayAOA,          10.0f, -999,    999);
        const int      iVsi10Fpm   = ClampInt((int)floor(MPS2FPM(g_AHRS.KalmanVSI) / 10.0f), -999, 999);
        const int      iOatC       = ClampInt(iOATc,                              -99,      99);
        const int      iFpa10      = SafeScaledInt(g_AHRS.FlightPath,    10.0f, -999,    999);
        const int      iFlapsDeg   = ClampInt((int)g_Flaps.iPosition,             -99,      99);
        const int      iStall10    = SafeScaledInt(g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA,  10.0f, -999, 999);
        const int      iSlow10     = SafeScaledInt(g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA, 10.0f, -999, 999);
        const int      iFast10     = SafeScaledInt(g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA, 10.0f, -999, 999);
        const int      iLdMax10    = SafeScaledInt(g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA,       10.0f, -999, 999);
        const int      iOnset100   = ClampInt((int)(gOnsetRate * 100),            -999,    999);
        const int      iSpinCue    = ClampInt((int)spinRecoveryCue,                 -9,      9);
        const unsigned uDataMark2  = WrapUInt((unsigned)g_iDataMark,               100);

        const int iChars = snprintf(
            serialOutString,
            sizeof(serialOutString),
            "#1%+04i%+05i%04u%+06i%+05i%+03i%+03i%02u%+04i%+04i%+03i%+04i%+03i%+04i%+04i%+04i%+04i%+04i%+02i%02u",
            iPitch10,
            iRoll10,
            uIas10,
            iPaltFt,
            iYaw10,
            iLatG100,
            iVertG10,
            uPctLift,
            iAoa10,
            iVsi10Fpm,
            iOatC,
            iFpa10,
            iFlapsDeg,
            iStall10,
            iSlow10,
            iFast10,
            iLdMax10,
            iOnset100,
            iSpinCue,
            uDataMark2);

        // Expected fixed-length payload is 76 bytes.
        if (iChars != 76)
            return;

        SerialCRC = 0x00;
        for (int i = 0; i < 76; i++)
            SerialCRC += (byte)serialOutString[i];
        } // end if ONSPEED

    // Send data out the appropriate serial port
    pSerial->print(serialOutString);
    pSerial->printf("%02X",SerialCRC);
    pSerial->println();
    } // end Write()
