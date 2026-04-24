
#include <Arduino.h>

#include "src/Globals.h"
#include <aoa/PercentLift.h>
#include <proto/DisplaySerial.h>

using onspeed::m2ft;
using onspeed::mps2fpm;
using onspeed::aoa::ComputePercentLift;
using onspeed::proto::DisplayBuildInputs;
using onspeed::proto::BuildDisplayFrame;
using onspeed::proto::kDisplayFrameSizeBytes;


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
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kDisplaySerialPeriodMs));
        if (xWasDelayed == pdFALSE)
            {
            // If this task runs late, don't "catch up" by running back-to-back and
            // bursting serial data at the display. Re-align to the current tick
            // period instead.
            xLastWakeTime = xLAST_TICK_TIME(kDisplaySerialPeriodMs);
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

    // In the original G2V3 implementation the panel output port could be
    // selected. In this implementation panel output is a fixed serial
    // port. But to easily allow run time configuration I have made the
    // serial port a pointer to the Stream base object.

    pSerial = pDispSerial;

}

// ----------------------------------------------------------------------------

void DisplaySerial::Write()
    {
    byte            SerialCRC = 0;

//    if (serialOutPort!="NONE" && millis()-serialoutLastUpdate>50) // update every 50ms, 20Hz

    char    serialOutString[200];

    int     iPercentLift;
    float   fDisplayAOA;
    float   fDisplayIAS;
    int     iDisplayVerticalG;

#ifdef SPHERICAL_PROBE
    fDisplayIAS = g_EfisSerial.suEfis.IAS;
#else
    fDisplayIAS = g_Sensors.IAS;
#endif
    // Display-serial gate uses the user-tunable `iMuteAudioUnderIAS`
    // rather than the sensor-level `bIasAlive` flag (see DataServer.cpp
    // for the full rationale): the pilot's configured "below which
    // nothing should show up" choice is the right UX threshold for
    // external displays too.
    const bool bIasValidForOutput = (fDisplayIAS >= g_Config.iMuteAudioUnderIAS);
    const float fIasForOutput = bIasValidForOutput ? fDisplayIAS : 0.0f;
    const float fPAltFt = m2ft(g_AHRS.KalmanAlt);

    if (IsFiniteFloat(g_AHRS.AccelVertFilter.get()))
        iDisplayVerticalG = (int)ceilf(g_AHRS.AccelVertFilter.get() * 10.0f);
    else
        iDisplayVerticalG = 0;


    // PercentLift is computed by the shared core helper so the M5 display
    // and any future native consumer get the identical 0..99 scalar.  See
    // onspeed_core/aoa/PercentLift.h for the range table and the alpha_0
    // floor caveat (issue #199 tracks the JS liveview duplicate).
    iPercentLift = ComputePercentLift(g_Sensors.AOA,
                                      g_Config.aFlaps[g_Flaps.iIndex],
                                      bIasValidForOutput);
    if (bIasValidForOutput)
        fDisplayAOA = g_Sensors.AOA;
    else
        fDisplayAOA = 0;

    // Output the data in the appropriate format

    if (g_Config.enSerialOutFormat == FOSConfig::EnSerialFmtG3X)
        {
        // Clamp to fixed-width protocol fields to prevent buffer overruns and
        // malformed output when values go out of range.
        const int      iPitch10   = SafeScaledInt(g_AHRS.SmoothedPitch, 10.0f, -999,    999);
        const int      iRoll10    = SafeScaledInt(g_AHRS.SmoothedRoll,  10.0f, -9999,  9999);
        const unsigned uIas10     = SafeScaledUInt(fIasForOutput,       10.0f, 0,      9999);
        const int      iPaltFt    = SafeScaledInt(fPAltFt,         1.0f, -99999, 99999);
        const int      iLatG100   = SafeScaledInt(-g_AHRS.AccelLatFilter.get(),  100.0f, -99,      99);
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

        // Send data out the appropriate serial port
        pSerial->print(serialOutString);
        pSerial->printf("%02X",SerialCRC);
        pSerial->println();
        } // end if G3X

    else if (g_Config.enSerialOutFormat == FOSConfig::EnSerialFmtOnSpeed)
        {
        // Build the 80-byte #1 frame via the shared core module so the layout
        // is defined in exactly one place (onspeed_core/proto/DisplaySerial.h).
        //
        // The per-field scale factors, clamps, and sign conventions are
        // documented there. BuildDisplayFrame produces the same bytes that
        // the previous hand-coded snprintf/CRC block did for any given set
        // of inputs.

        const int iOATc = g_Config.bOatSensor ? int(g_Sensors.OatC) : 0;

        DisplayBuildInputs inputs;
        inputs.pitchDeg          = g_AHRS.SmoothedPitch;
        inputs.rollDeg           = g_AHRS.SmoothedRoll;
        inputs.iasKt             = fIasForOutput;
        inputs.paltFt            = fPAltFt;
        inputs.turnRateDps       = g_AHRS.gYaw;
        inputs.lateralG          = -g_AHRS.AccelLatFilter.get();  // negated: positive = leftward
        inputs.verticalGScaled10 = static_cast<float>(iDisplayVerticalG);
        inputs.percentLift       = iPercentLift;
        inputs.aoaDeg            = fDisplayAOA;
        inputs.vsiFpm10          = ClampInt(
                                       (int)floorf(mps2fpm(g_AHRS.KalmanVSI) / 10.0f),
                                       -999, 999);
        inputs.oatC              = iOATc;
        inputs.flightPathDeg     = g_AHRS.FlightPath;
        inputs.flapsDeg          = (int)g_Flaps.iPosition;
        inputs.stallWarnAoaDeg   = g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA;
        inputs.onSpeedSlowAoaDeg = g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA;
        inputs.onSpeedFastAoaDeg = g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA;
        inputs.tonesOnAoaDeg     = g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA;
        inputs.gOnsetRate        = 0.0f;
        inputs.spinRecoveryCue   = 0;
        inputs.dataMark          = (int)((unsigned)g_iDataMark % 100u);

        uint8_t frameBuf[kDisplayFrameSizeBytes];
        const size_t nBytes = BuildDisplayFrame(inputs, frameBuf, sizeof(frameBuf));

        if (nBytes != kDisplayFrameSizeBytes)
            return;

        // Write the complete frame (80 bytes, already includes CRC + CRLF).
        pSerial->write(frameBuf, kDisplayFrameSizeBytes);
        } // end if ONSPEED

    } // end Write()
