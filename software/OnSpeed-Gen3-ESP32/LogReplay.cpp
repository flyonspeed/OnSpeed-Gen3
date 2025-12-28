
//#include <Arduino.h>
//#include "SdFat.h" // https://github.com/greiman/SdFat
//#include <fstream>

//#include "../OnSpeedG2V4/Globals.h"
//#define xLAST_TICK_TIME(period_msec)     ((xTaskGetTickCount() / pdMS_TO_TICKS(period_msec)) * pdMS_TO_TICKS(period_msec))

#include "FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/message_buffer.h"
//#include "freertos/ringbuf.h"

#include "csv_parser.hpp"

#include "Globals.h"
#include "Config.h"
#include "LogReplay.h"
#include "SensorIO.h"

FsFile                      hReplayFile;
char                        szInLine[1000];
CSV_Parser                  CsvParser;
CSV_FIELDS                  CsvHeaders;
KEY_VAL_FIELDS              CsvData;

bool OpenReplayLog(String sLogFile);
bool ReadLogLine();
void RemoveSpaces(char * szLine);

//-----------------------------------------------------------------------------
// REPLAYLOGFILE data source routines
//-----------------------------------------------------------------------------

// FreeRTOS task for reading log data

void LogReplayTask(void *pvParams)
{
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime;
//    unsigned long   uStartMicros = micros();
//    unsigned long   uCurrMicros;
//    static unsigned uLoops = 0;
    bool            bReadStatus = false;

#if 1
    // Get the passed parameters
    SuParamsReplay    * psuParamsReplay = (SuParamsReplay *)pvParams;

    // Open the log file and read the headers
    bReadStatus = OpenReplayLog(psuParamsReplay->sReplayLogFile);
    if (!bReadStatus)
        g_Log.println(MsgLog::EnReplay, MsgLog::EnError, "Unable to read and replay file.");

    xLastWakeTime = xLAST_TICK_TIME(20);

    while (bReadStatus == true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));

        // If this task wasn't delayed before it ran again it means it
        // it ran long for some reason (like the CPU is overloaded) or
        // or was stopped for a time (like during sensor cal). Regardless,
        // make sure the xLastWakeTime parameter is set to an integer
        // multiple of the delay time to maintain time alignment of
        // the data.
        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(20);
            g_Log.println(MsgLog::EnReplay, MsgLog::EnWarning, "LogReplayTask Late");
        }

        // Read the next log line
        bReadStatus = ReadLogLine();

    } // end while read() is OK

    g_Log.println("Finished replaying file.");

    g_AudioPlay.UpdateTones(); // to turn off tone at the end;

    if (hReplayFile.isOpen())
        {
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            {
            hReplayFile.close();
            xSemaphoreGive(xWriteMutex);
            }
        else
            {
            // Best effort close; SD may be busy.
            hReplayFile.close();
            }
        }
#endif

} // end SensorReadTask


// ----------------------------------------------------------------------------

bool OpenReplayLog(String sLogFile)
    {
    bool    bStatus = false;
    int     iCharsRead = 0;

    if (!xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
        {
        g_Log.printf(MsgLog::EnReplay, MsgLog::EnError, "OpenReplayLog: SD busy (xWriteMutex)\n");
        return false;
        }

    // Open the log CSV file
    if (!g_SdFileSys.exists(sLogFile.c_str()))
        {
        xSemaphoreGive(xWriteMutex);
        g_Log.printf(MsgLog::EnReplay, MsgLog::EnError, "Could not find %s on the SD card\n", sLogFile.c_str());
        return false;
        }

    hReplayFile = g_SdFileSys.open(sLogFile.c_str(), O_READ);
    if (!hReplayFile.isOpen())
        {
        xSemaphoreGive(xWriteMutex);
        g_Log.printf(MsgLog::EnReplay, MsgLog::EnError, "Could not open %s on the SD card.\n", sLogFile.c_str());
        return false;
        }

//        digitalWrite(PIN_LED1, HIGH);

    // Read the CSV header line
    iCharsRead = hReplayFile.fgets(szInLine, sizeof(szInLine));
    xSemaphoreGive(xWriteMutex);
    if (iCharsRead <= 0)
        goto fail;

    RemoveSpaces(szInLine);

    bStatus = CsvParser.parse_line(szInLine, CsvHeaders);
    if (bStatus == false)
        goto fail;

    // Make sure required headers are present
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "PfwdSmoothed") == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "P45Smoothed")  == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "flapsPos")     == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "Palt")         == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "IAS")          == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "DataMark")     == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "VSI")          == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "VerticalG")    == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "LateralG")     == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "ForwardG")     == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "RollRate")     == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "PitchRate")    == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "YawRate")      == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "Pitch")        == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "Roll")         == CsvHeaders.end()) goto fail;
    if (std::find(CsvHeaders.begin(), CsvHeaders.end(), "FlightPath")   == CsvHeaders.end()) goto fail;

    g_Log.printf("Replaying data from log file: %s\n", sLogFile.c_str());
    return true;

fail:
    if (hReplayFile.isOpen())
        {
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            {
            hReplayFile.close();
            xSemaphoreGive(xWriteMutex);
            }
        else
            {
            hReplayFile.close();
            }
        }

    return false;
    }


// ----------------------------------------------------------------------------

bool ReadLogLine()
    {
    bool    bStatus;
    int     iCharsRead;
    long    lTimestamp;

    // Read until a good line is read or we run out of lines. Malformed
    // lines result in no data filled in to the CsvData array.
    iCharsRead = 0;
    while (true)
        {
        if (!xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            return false;

        iCharsRead = hReplayFile.fgets(szInLine, sizeof(szInLine));
        xSemaphoreGive(xWriteMutex);
        if (iCharsRead <= 0)
            return false;

        RemoveSpaces(szInLine);

        // Read until a good line is parsed
        bStatus = CsvParser.parse_line(szInLine, CsvHeaders, CsvData);
        if (bStatus)
            {
            break;
            }
        } // end reading lines looking for a good one

    // We got a good line so convert some values
    try { lTimestamp             =  std::stol(CsvData["timeStamp"].c_str());    } catch (const std::invalid_argument&) { lTimestamp              = 0; }
    try { g_Sensors.PfwdSmoothed =  std::stof(CsvData["PfwdSmoothed"].c_str()); } catch (const std::invalid_argument&) { g_Sensors.PfwdSmoothed  = 0; }
    try { g_Sensors.P45Smoothed  =  std::stof(CsvData["P45Smoothed"].c_str());  } catch (const std::invalid_argument&) { g_Sensors.P45Smoothed   = 0; }
    try { g_Flaps.iPosition      =  std::stoi(CsvData["flapsPos"].c_str());     } catch (const std::invalid_argument&) { g_Flaps.iPosition       = 0; }

    g_fCoeffP = PCOEFF(g_Sensors.PfwdSmoothed, g_Sensors.P45Smoothed);

    // Find the flap index from flap degrees
    for (int iFlapsIdx = 0; iFlapsIdx < g_Config.aFlaps.size(); iFlapsIdx++)
        {
        if (g_Flaps.iPosition == g_Config.aFlaps[iFlapsIdx].iDegrees)
            {
            g_Flaps.iIndex = iFlapsIdx;
            break;
            }
        }

    try { g_Sensors.Palt         =  std::stof(CsvData["Palt"]);                 } catch (const std::invalid_argument&) { g_Sensors.Palt       = 0; }
    try { g_Sensors.IAS          =  std::stof(CsvData["IAS"]);                  } catch (const std::invalid_argument&) { g_Sensors.IAS        = 0; }
    try { g_iDataMark            =  std::stoi(CsvData["DataMark"]);             } catch (const std::invalid_argument&) { g_iDataMark          = 0; }
    try { g_AHRS.KalmanVSI       =  std::stof(CsvData["VSI"]) / 196.85;         } catch (const std::invalid_argument&) { g_AHRS.KalmanVSI     = 0; }
    try { g_pIMU->Ax             =  std::stof(CsvData["ForwardG"]);             } catch (const std::invalid_argument&) { g_pIMU->Ax = 0; }   // forward G
    try { g_pIMU->Ay             =  std::stof(CsvData["LateralG"]);             } catch (const std::invalid_argument&) { g_pIMU->Ay = 0; }   // lateralG
    try { g_pIMU->Az             =  std::stof(CsvData["VerticalG"]);            } catch (const std::invalid_argument&) { g_pIMU->Az = 0; }   // vertical G
    try { g_pIMU->Gx             =  std::stof(CsvData["RollRate"]);             } catch (const std::invalid_argument&) { g_pIMU->Gx = 0; }   // roll
    try { g_pIMU->Gy             = -std::stof(CsvData["PitchRate"]);            } catch (const std::invalid_argument&) { g_pIMU->Gy = 0; }   // pitch (reversed in log file)
    try { g_pIMU->Gz             =  std::stof(CsvData["YawRate"]);              } catch (const std::invalid_argument&) { g_pIMU->Gz = 0; }   // yaw
    try { g_AHRS.SmoothedPitch   =  std::stof(CsvData["Pitch"]);                } catch (const std::invalid_argument&) { g_AHRS.SmoothedPitch = 0; }
    try { g_AHRS.SmoothedRoll    =  std::stof(CsvData["Roll"]);                 } catch (const std::invalid_argument&) { g_AHRS.SmoothedRoll  = 0; }
    try { g_AHRS.FlightPath      =  std::stof(CsvData["FlightPath"]);           } catch (const std::invalid_argument&) { g_AHRS.FlightPath    = 0; }

    // AOA is recalculated, which I think is kind of stinky. I'd rather display the AOA
    // that was calculated during the recording.
//  SetAOApoints(g_Flaps.iIndex);
    const SuCalibrationCurve& curve = g_Config.aFlaps[g_Flaps.iIndex].AoaCurve;
    AOACalculatorResult result = g_Sensors.AoaCalc.calculate(g_Sensors.PfwdSmoothed, g_Sensors.P45Smoothed, curve);
    g_Sensors.AOA = result.aoa;
    g_fCoeffP = result.coeffP;

    // VN attitude and VSI
    // vnPitch      = std::stof(CsvData["vnPitch"]);
    // vnRoll       = std::stof(CsvData["vnRoll"]);
    // vnVelNedDown = std::stof(CsvData["vnVelNedDown"]);

    // Update TAS
    // Ballpark TAS 2% per thousand feet pressure altitude, in m/sec
    // smoothedTAS = TASAvg.getFastAverage();

    g_AHRS.AccelLatCorr   = g_pIMU->Ay;
    g_AHRS.AccelVertCorr  = g_pIMU->Az;

    g_AudioPlay.UpdateTones();

    //Serial.printf("Time:%ld", lTimestamp);
    //Serial.printf(", Pfwd:%.1f", g_Sensors.PfwdSmoothed);
    //Serial.printf(", P45:%.1f",  g_Sensors.P45Smoothed);
    //Serial.printf(", IAS:%.1f",  g_Sensors.IAS);
    //Serial.printf(", AOA:%.2f",  g_Sensors.AOA);
    //Serial.printf(", Pitch:%.2f", g_AHRS.SmoothedPitch);
//    Serial.print(", efisPitch: ");
//    Serial.print(efisPitch);
//    Serial.printf(", Palt:%.0f", g_Sensors.Palt);
    // Print tone type & pps
//    Serial.print(",tonemode: ");
//      if      (toneMode==TONE_OFF  ) Serial.print("TONE_OFF,");
//      else if (toneMode==PULSE_TONE) Serial.print("PULSE_TONE,");
//      else if (toneMode==SOLID_TONE) Serial.print("SOLID_TONE,");
//      else
//          {
//          Serial.print("tonetype: ");
//          if (!highTone) Serial.print("LOW_TONE,");
//          else Serial.print("HIGH_TONE,");
//          Serial.print("tonepulse: ");
//          Serial.print(pps);
//          Serial.print(",");
//          }
    //Serial.printf(", Flaps:%d %d", g_Flaps.iPosition, g_Flaps.iIndex);
    //Serial.println();

    return true;
}

// ----------------------------------------------------------------------------

void RemoveSpaces(char * szLine)
    {
    // Remove any embedded blank spaces
    int     iStrLen = strlen(szLine);
    for (int iStrIdx = 0; iStrIdx < iStrLen; iStrIdx++)
        {
        if (szLine[iStrIdx] == ' ')
            {
            for (int iMoveIdx = iStrIdx; iMoveIdx <= iStrLen; iMoveIdx++)
                szLine[iMoveIdx] = szLine[iMoveIdx+1];
            iStrLen--;
            }
        }
    }

//-----------------------------------------------------------------------------
// TESTPOT data source routines
//-----------------------------------------------------------------------------

void  ReadTestPot();


// FreeRTOS task for reading test pot

void TestPotTask(void *pvParams)
{
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime;

    // Get the passed parameters
//    SuParamsReplay    * psuParamsReplay = (SuParamsReplay *)pvParams;

    xLastWakeTime = xLAST_TICK_TIME(20);

    while (true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));

        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(20);
            g_Log.println(MsgLog::EnReplay, MsgLog::EnWarning, "TestPotTask Late");
        }

        // Read the test pot for AoA value
        ReadTestPot();

    } // end while true is true

} // end TestPotTask

// ----------------------------------------------------------------------------

void ReadTestPot()
    {
    float   fFlapRawValue = 0;
    float   smoothingAlpha  = 0.04;
    float   fReadAOA;

    // Average some flap pot readings
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)))
    {
        for (int i=0; i<5;i++)
            fFlapRawValue += g_Flaps.Read(); // analogRead(FLAP_PIN);
        xSemaphoreGive(xSensorMutex);
    }
    fFlapRawValue = fFlapRawValue / 5.0;

    // Map the flap pot raw value onto the flap pot raw value limits
    if (g_Config.aFlaps.size() > 1)
        {
        // Convert raw flap pot postion into an AOA between 0 and 20
        fReadAOA = mapfloat(fFlapRawValue, g_Config.aFlaps[0].iPotPosition, g_Config.aFlaps.back().iPotPosition, 0.0, 20.0);
        fReadAOA = constrain(fReadAOA, 0.0, 20.0);
        }

    // Not enough flap positions defined
    else
        fReadAOA = 0.0;

    // Smooth potentiometer AOA (using flap pot input)
    g_Sensors.AOA = fReadAOA * smoothingAlpha + g_Sensors.AOA * (1 - smoothingAlpha);

    // Just make sure g_Flaps.iIndex is set and good things will happen
    g_Flaps.iIndex = 0; // flaps up

    g_Sensors.IAS = 50; // to turn on the tones

    g_AudioPlay.UpdateTones(); // to turn off tone at the end;
    g_Log.printf(MsgLog::EnReplay, MsgLog::EnDebug, "fReadAOA: %0.2f, AOA: %0.2f\n",fReadAOA, g_Sensors.AOA);

#if 0
//// DEFER THIS

    // Breathing LED
    if (millis()-lastLedUpdate>50)
    {
        if (switchState)
        {
            float ledBrightness = 15+(exp(sin(millis()/2000.0*PI)) - 0.36787944)*63.81; // funky sine wave, https://sean.voisen.org/blog/2011/10/breathing-led-with-arduino/
            analogWrite(PIN_LED2, ledBrightness);
        }
        else
            analogWrite(PIN_LED2,0);

        lastLedUpdate=millis();
    }
#endif

}


//-----------------------------------------------------------------------------
// RANGESWEEP data source routines
//-----------------------------------------------------------------------------

#define RANGESWEEP_LOW_AOA     0.0
#define RANGESWEEP_HIGH_AOA   18.0
#define RANGESWEEP_STEP        0.1                  // degrees AOA
#define RANGESWEEP_INTERVAL_MS 100                  // ms to hold each AOA value

// FreeRTOS task for reading test pot

void RangeSweepTask(void *pvParams)
{
    bool            fRangeSweepUp = true;
    float           fCurrentRangeSweepValue = RANGESWEEP_LOW_AOA;

    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime;

    // Get the passed parameters
//    SuParamsReplay    * psuParamsReplay = (SuParamsReplay *)pvParams;

    xLastWakeTime = xLAST_TICK_TIME(RANGESWEEP_INTERVAL_MS);

    while (true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(RANGESWEEP_INTERVAL_MS));

        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(RANGESWEEP_INTERVAL_MS);
            g_Log.println(MsgLog::EnReplay, MsgLog::EnWarning, "RangeSweepTask Late");
        }

        // Sweep the knee, er... range
        if (fRangeSweepUp)
        {
            if      (fCurrentRangeSweepValue <  RANGESWEEP_HIGH_AOA) fCurrentRangeSweepValue += RANGESWEEP_STEP;
            else if (fCurrentRangeSweepValue >= RANGESWEEP_HIGH_AOA) fRangeSweepUp = false;
        }

        else
        {
            if      (fCurrentRangeSweepValue >  RANGESWEEP_LOW_AOA ) fCurrentRangeSweepValue -= RANGESWEEP_STEP;
            else if (fCurrentRangeSweepValue <= RANGESWEEP_LOW_AOA ) fRangeSweepUp = true;
        }

        g_Sensors.AOA = fCurrentRangeSweepValue;
    //    setAOApoints(0); // flaps down (up?)
        g_Sensors.IAS = 50; // to turn on the tones
        g_AudioPlay.UpdateTones();

    } // end while true is true

} // end RangeSweepTask
