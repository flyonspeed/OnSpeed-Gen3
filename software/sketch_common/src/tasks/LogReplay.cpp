
//#include <Arduino.h>
//#include "SdFat.h" // https://github.com/greiman/SdFat
//#include <fstream>

//#include "../OnSpeedG2V4/Globals.h"
//#define xLAST_TICK_TIME(period_msec)     ((xTaskGetTickCount() / pdMS_TO_TICKS(period_msec)) * pdMS_TO_TICKS(period_msec))

#include "FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/message_buffer.h"
//#include "freertos/ringbuf.h"

#include "src/Globals.h"
#include "src/config/Config.h"

#include <sensors/IasAlive.h>
#include "src/drivers/SensorIO.h"
#include <filters/EMAFilter.h>
#include <proto/LogCsv.h>
#include <proto/LogCsvHeaderIndex.h>
#include <replay/LogReplayEngine.h>
#include <types/LogRow.h>

FsFile                      hReplayFile;
static char                 szInLine[onspeed::proto::log_csv::kRowMaxBytes + 4];

// Header index built from the log file's own header line. Carries the
// column-name -> ordinal mapping plus the boom/EFIS/VN-300 feature flags.
static onspeed::proto::log_csv::HeaderIndex s_HeaderIndex{};

// Per-replay-session engine instance. Constructed in OpenReplayLog once the
// header has been parsed (to know flapsRawAdcAvailable). The engine owns the
// AOA smoothing filter state across rows; recreating it for each new file
// gives a clean EMA start. Pointer so construction is deferred until the
// header is available.
static onspeed::replay::LogReplayEngine* s_pEngine = nullptr;

// Warning sink for BuildHeaderIndex. Reports each missing column on the
// replay channel; LogRow fields for absent columns stay at their default
// and replay continues.
static void ReplayHeaderWarn(const char* col)
    {
    g_Log.printf(MsgLog::EnReplay, MsgLog::EnWarning,
        "Replay log missing column: %s (best-effort, field stays at default)\n",
        col);
    }

bool OpenReplayLog(String sLogFile);
bool ReadLogLine();
void RemoveSpaces(char * szLine);
static void PublishReplayResult(const onspeed::replay::ReplayStepResult& res);

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

    xLastWakeTime = xLAST_TICK_TIME(kPressureIntervalMs);

    while (bReadStatus == true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kPressureIntervalMs));

        // If this task wasn't delayed before it ran again it means it
        // it ran long for some reason (like the CPU is overloaded) or
        // or was stopped for a time (like during sensor cal). Regardless,
        // make sure the xLastWakeTime parameter is set to an integer
        // multiple of the delay time to maintain time alignment of
        // the data.
        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(kPressureIntervalMs);
            g_Log.println(MsgLog::EnReplay, MsgLog::EnWarning, "LogReplayTask Late");
        }

        // Read the next log line
        bReadStatus = ReadLogLine();

    } // end while read() is OK

    // Drain the streaming synth buffer. For old logs (without flapsRawADC),
    // step() lags output by kSynthHalfWindow rows; the tail rows accumulate
    // in the engine's circular buffer until flush() is called here. Each
    // flushed row is published to globals and drives one tone update.
    // For logs that carry flapsRawADC the engine returns an empty vector.
    if (s_pEngine)
        {
        for (const onspeed::replay::ReplayStepResult& res : s_pEngine->flush())
            {
            xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kPressureIntervalMs));
            PublishReplayResult(res);
            g_AudioPlay.UpdateTones(SnapshotActiveFlap());
            }
        }

    g_Log.println("Finished replaying file.");

    g_AudioPlay.UpdateTones(SnapshotActiveFlap()); // to turn off tone at the end;

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

    // Read the CSV header line.
    iCharsRead = hReplayFile.fgets(szInLine, sizeof(szInLine));
    xSemaphoreGive(xWriteMutex);
    if (iCharsRead <= 0)
        goto fail;

    RemoveSpaces(szInLine);

    // Name-keyed header parse. Tolerates older logs from customer kits —
    // missing columns log a warning and the corresponding LogRow fields
    // stay at default; the rest of the log replays.
    if (!onspeed::proto::log_csv::BuildHeaderIndex(
            std::string_view(szInLine),
            s_HeaderIndex,
            ReplayHeaderWarn))
        {
        g_Log.printf(MsgLog::EnReplay, MsgLog::EnError,
            "Replay header parse failed; see preceding warning for details\n");
        goto fail;
        }

    // Construct a fresh engine now that the header is known. The header
    // tells us whether flapsRawADC is available; the log rate is read from
    // g_Config.iLogRate (50 or 208 Hz). Destroying and recreating the engine
    // each file gives a clean AOA EMA start — the same as if the replay
    // started from a cold state. Behavior is identical to the pre-extraction
    // task code because the engine's step() mirrors ReadLogLine() exactly.
    {
    delete s_pEngine;
    bool bFlapsRawAdcAvail = (s_HeaderIndex.idxFlapsRawAdc >= 0);
    s_pEngine = new onspeed::replay::LogReplayEngine(
            g_Config, g_Config.iLogRate, bFlapsRawAdcAvail);
    }

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
    int iCharsRead = 0;

    // Read until a good line is read or we run out of lines.
    while (true)
        {
        if (!xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            return false;

        iCharsRead = hReplayFile.fgets(szInLine, sizeof(szInLine));
        xSemaphoreGive(xWriteMutex);
        if (iCharsRead <= 0)
            return false;

        // Parse the CSV line via the name-keyed HeaderIndex built in
        // OpenReplayLog. The index drives which optional groups
        // ParseRowByIndex unpacks — no row-side feature flags needed.
        onspeed::LogRow row;
        bool bOk = onspeed::proto::log_csv::ParseRowByIndex(
                std::string_view(szInLine, (size_t)iCharsRead),
                s_HeaderIndex, row);
        if (!bOk)
            continue;

        // Delegate the per-row (LogRow -> globals) pipeline to the engine.
        // The engine is constructed in OpenReplayLog once the header is known;
        // its AOA smoother state persists across rows within this replay session.
        // TestPot and RangeSweep tasks do not use the engine — they generate
        // their own AOA directly and call UpdateTones themselves (see below).
        //
        // When the log lacks flapsRawADC (pre-PR-#221), step() returns empty
        // for the first kSynthHalfWindow rows while the circular buffer fills
        // (the streaming synth ADC lag). During this lag period we silently
        // continue reading the next row without publishing globals. Wire output
        // lags by ~2 sec on old-log replay — acceptable for bench replay.
        const std::optional<onspeed::replay::ReplayStepResult> optRes =
            s_pEngine->step(row);
        if (!optRes.has_value())
            continue;   // still in lag period; read next row

        const onspeed::replay::ReplayStepResult& res = optRes.value();

        // Publish the engine result to sketch globals — mirrors the write-back
        // that was previously inline here.
        PublishReplayResult(res);

        g_AudioPlay.UpdateTones(SnapshotActiveFlap());

        return true;
        } // end reading lines looking for a good one
    }

// ----------------------------------------------------------------------------
// Publish one ReplayStepResult to sketch globals.  Called from ReadLogLine()
// and from the flush() drain at end-of-file.
// ----------------------------------------------------------------------------
static void PublishReplayResult(const onspeed::replay::ReplayStepResult& res)
    {
    g_Sensors.PfwdSmoothed = res.pfwdSmoothed;
    g_Sensors.P45Smoothed  = res.p45Smoothed;
    g_Flaps.iPosition      = res.flapsPos;
    // When flapsRawAdcPresent is true the synth (or real) ADC value is valid;
    // overwrite g_Flaps.uValue so DisplayPctAnchors sees the correct reading.
    if (res.flapsRawAdcPresent)
        g_Flaps.uValue     = res.flapsRawAdc;

    g_fCoeffP          = res.coeffP;
    g_Flaps.iIndex     = res.flapsIndex;

    g_Sensors.Palt     = res.paltFt;
    g_Sensors.IAS      = res.iasKt;
    // Display gate: re-derive from raw IAS against the current pilot
    // threshold.  `res.iasValid` only says "was the source cell
    // numeric?" — not "should this be displayed."
    g_Sensors.bIasAlive = onspeed::sensors::UpdateIasDisplayable(
        g_Sensors.bIasAlive, res.iasKt,
        static_cast<float>(g_Config.iIasDisplayThresholdKt));
    g_iDataMark        = res.dataMark;
    g_AHRS.KalmanVSI   = res.kalmanVSI;

    g_pIMU->Ax = res.imuForwardG;
    g_pIMU->Ay = res.imuLateralG;
    g_pIMU->Az = res.imuVerticalG;
    g_pIMU->Gx = res.imuRollRateDps;
    g_pIMU->Gy = res.imuPitchRateDps;
    g_pIMU->Gz = res.imuYawRateDps;

    g_AHRS.SmoothedPitch = res.pitchDeg;
    g_AHRS.SmoothedRoll  = res.rollDeg;
    g_AHRS.FlightPath    = res.flightPathDeg;
    g_Sensors.AOA        = res.aoa;

    g_AHRS.AccelLatCorr  = res.accelLatSmoothed;
    g_AHRS.AccelVertCorr = res.accelVertSmoothed;
    }

// ----------------------------------------------------------------------------

void RemoveSpaces(char * szLine)
    {
    // Remove any embedded blank spaces using a read/write pointer approach.
    // The old loop incremented iStrIdx after shifting, skipping consecutive spaces.
    int iWrite = 0;
    for (int iRead = 0; szLine[iRead] != '\0'; iRead++)
        {
        if (szLine[iRead] != ' ')
            szLine[iWrite++] = szLine[iRead];
        }
    szLine[iWrite] = '\0';
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

    xLastWakeTime = xLAST_TICK_TIME(kPressureIntervalMs);

    while (true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kPressureIntervalMs));

        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(kPressureIntervalMs);
            g_Log.println(MsgLog::EnReplay, MsgLog::EnWarning, "TestPotTask Late");
        }

        // Read the test pot for AoA value
        ReadTestPot();

    } // end while true is true

} // end TestPotTask

// ----------------------------------------------------------------------------

constexpr float kTestPotSmoothingAlpha = 0.04f;
static onspeed::EMAFilter sTestPotAoaFilter(kTestPotSmoothingAlpha);

void ReadTestPot()
    {
    float   fFlapRawValue = 0;
    float   fReadAOA;

    // Average some flap pot readings.  Flaps::Read() takes xSensorMutex
    // internally for each SPI transaction.
    for (int i=0; i<5;i++)
        fFlapRawValue += g_Flaps.Read();
    fFlapRawValue = fFlapRawValue / 5.0;

    // Map the flap pot raw value onto the flap pot raw value limits
    if (g_Config.aFlaps.size() > 1)
        {
        // Convert raw flap pot postion into an AOA between -15 and 20
        fReadAOA = mapfloat(fFlapRawValue, g_Config.aFlaps[0].iPotPosition, g_Config.aFlaps.back().iPotPosition, -15.0, 20.0);
        fReadAOA = constrain(fReadAOA, -15.0, 20.0);
        }

    // Not enough flap positions defined
    else
        fReadAOA = 0.0;

    // Smooth potentiometer AOA (using flap pot input)
    g_Sensors.AOA = sTestPotAoaFilter.update(fReadAOA);

    // Just make sure g_Flaps.iIndex is set and good things will happen
    g_Flaps.iIndex = 0; // flaps up

    g_Sensors.IAS = 50; // to turn on the tones

    g_AudioPlay.UpdateTones(SnapshotActiveFlap()); // TestPot tick: feed live snapshot.
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

#define RANGESWEEP_LOW_AOA   -15.0
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
        g_AudioPlay.UpdateTones(SnapshotActiveFlap());

    } // end while true is true

} // end RangeSweepTask
