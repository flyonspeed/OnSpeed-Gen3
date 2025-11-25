
#include <Arduino.h>
//#include "SoftwareSerial.h"
//#include "EspSoftwareSerial.h"

#include "Globals.h"
#include "DisplaySerial.h"

//SoftwareSerial      DispSerial(DISPLAY_SER_RX, DISPLAY_SER_TX);

const int   serialDisplaySmoothingLat  = 50;    // smoothing serial display data (LateralG)  10hz data.
const int   serialDisplaySmoothingVert = 20;    // smoothing serial display data (VertG)  10hz data.


// ----------------------------------------------------------------------------

// FreeRTOS task for writing display data

void WriteDisplayDataTask(void * pvParams)
    {
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime = xTaskGetTickCount();

    while (true)
        {
        // No delay happening is a design error so flag it if it happens
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, 100);  // 100 msec
        if (xWasDelayed == pdFALSE)
            g_Log.println(MsgLog::EnDisplay, MsgLog::EnWarning, "WriteDisplayDataTask Late");

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
    if (fPAltSmoothed == 0.0) 
        fPAltSmoothed = M2FT(g_AHRS.KalmanAlt);
    else 
        fPAltSmoothed = M2FT(g_AHRS.KalmanAlt) * smoothingAlphaVert/10+ (1-smoothingAlphaVert/10)*fPAltSmoothed; // increased smoothing needed

    fVerticalGSmoothed = g_AHRS.AccelVertCorr * smoothingAlphaVert+ (1-smoothingAlphaVert)*fVerticalGSmoothed;
    iDisplayVerticalG = ceil(fVerticalGSmoothed * 10.0);

    fLateralGSmoothed = g_AHRS.AccelLatCorr * smoothingAlphaLat+ (1-smoothingAlphaLat)*fLateralGSmoothed;

    // don't output precentLift at low speeds.
    if (fDisplayIAS >= g_Config.iMuteAudioUnderIAS)
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
        sprintf(serialOutString,"=1100000000%+04i%+05i___%04u%+06i____%+03i%+03i%02u__________",
            int(g_AHRS.SmoothedPitch*10), int(g_AHRS.SmoothedRoll*10),
            unsigned(fDisplayIAS*10), int(fPAltSmoothed),
            int(-fLateralGSmoothed*100),iDisplayVerticalG, unsigned(iPercentLift));

        SerialCRC = 0x00;
        for (int i = 0; i<= 54; i++) 
            SerialCRC += serialOutString[i];
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

        sprintf(serialOutString,"#1%+04i%+05i%04u%+06i%+05i%+03i%+03i%02u%+04i%+04i%+03i%+04i%+03i%+04i%+04i%+04i%+04i%+04i%+02i%02u",
            int(g_AHRS.SmoothedPitch*10), int(g_AHRS.SmoothedRoll*10),
            unsigned(fDisplayIAS*10), int(fPAltSmoothed), int(g_AHRS.gYaw*10),
            int(-fLateralGSmoothed*100), iDisplayVerticalG, unsigned(iPercentLift),
            int(fDisplayAOA*10), int(floor(MPS2FPM(g_AHRS.KalmanVSI)/10)), iOATc,
            int (g_AHRS.FlightPath*10), int(g_Flaps.iPosition),
            int(g_Config.aFlaps[g_Flaps.iIndex].fSTALLWARNAOA*10), int(g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDSLOWAOA*10), 
            int(g_Config.aFlaps[g_Flaps.iIndex].fONSPEEDFASTAOA*10), int(g_Config.aFlaps[g_Flaps.iIndex].fLDMAXAOA*10),
            int(gOnsetRate*100), int(spinRecoveryCue), unsigned(g_iDataMark));

        SerialCRC = 0x00;
        for (int i = 0; i <= 75; i++) 
            SerialCRC += serialOutString[i];
        } // end if ONSPEED

    // Send data out the appropriate serial port
    pSerial->print(serialOutString);
    pSerial->printf("%02X",SerialCRC);
    pSerial->println();
    } // end Write()