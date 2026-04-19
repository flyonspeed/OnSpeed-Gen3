
#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING
#endif
#include "SdFat.h"

#include "Globals.h"
#include <proto/LogCsv.h>
#include <types/LogRow.h>

using onspeed::m2ft;
using onspeed::mps2fpm;
using onspeed::mps2kts;

#define SYNC_INTERVAL_MS            5000                // How often to sync the log file to disk

// Peformance variables for debugging
volatile uint64_t   uWriteMax;
volatile uint64_t   uSyncMax;

// Log file handle, keep it local in scope
static FsFile       m_hLogFile;

// Count log lines dropped because the logging ring buffer is full. Keep this
// non-blocking so SD-card stalls can't backpressure critical tasks.
static uint32_t      s_uRingDropCount = 0;

// Staging buffer: accumulate log lines and write in 512-byte-aligned
// chunks so SdFat can pass full sectors straight to multi-block SPI.
// Accessed from LogSensorCommitTask and LogSensor::Close(); both code
// paths must hold xWriteMutex when touching szWriteBuf or uBufUsed.
static const size_t  SECTOR_SIZE    = 512;
static const size_t  WRITE_BUF_SIZE = SECTOR_SIZE * 4;    // 2048 bytes
static char          szWriteBuf[WRITE_BUF_SIZE];
static size_t        uBufUsed       = 0;

// Write any remaining bytes in the staging buffer to the log file.
// Caller must hold xWriteMutex and ensure m_hLogFile.isOpen().
static void FlushStagingBufferLocked()
    {
    if (uBufUsed > 0 && m_hLogFile.isOpen())
        {
        m_hLogFile.write(szWriteBuf, uBufUsed);
        uBufUsed = 0;
        }
    }

// ----------------------------------------------------------------------------

// FreeRTOS task to write sensor log data to disk
// Previously formatted strings with log data to be written to disk are sent
// to a ring buffer. This task reads them out of the ring buffer and writes them
// to the open log file. Sometimes the uSD disk will block for a grotesque amount
// of time (300 msec or more) or go into slow down mode for even longer (5 seconds
// or more). The ring buffer needs to be big enough to queue up that data until
// it can finally be written to disk. Doing a flush / sync after every write works
// most of the time... most of the time. But when the disk goes into slow down it
// really messes things up. Flush / sync every 10 seconds or so helps things out
// quite a bit.

void LogSensorCommitTask(void *pvParams)
{
    static size_t         iPrintLen;
    static char         * pchIn;
    static TickType_t     xLastSyncTime = xTaskGetTickCount();
    static uint64_t       uWriteStart, uWriteEnd, uWriteDur;
    static uint64_t       uSyncStart,  uSyncEnd,  uSyncDur;
    static uint32_t       uPendingDrops = 0;
    static unsigned long  uLastDropWarnMs = 0;

    while (true)
    {
        // Report (rate-limited) if the producer is dropping lines due to SD stalls.
        uPendingDrops += __atomic_exchange_n(&s_uRingDropCount, 0u, __ATOMIC_RELAXED);
        if (uPendingDrops > 0)
            {
            unsigned long uNow = millis();
            if ((uNow - uLastDropWarnMs) > 2000)
                {
                g_Log.printf(MsgLog::EnDisk, MsgLog::EnWarning,
                    "Logging ring buffer full; dropped %lu log lines\n",
                    (unsigned long)uPendingDrops);
                uPendingDrops = 0;
                uLastDropWarnMs = uNow;
                }
            }

        if (xLoggingRingBuffer == nullptr)
            {
            static bool bWarned = false;
            if (!bWarned)
                {
                g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Logging ring buffer not allocated; LogSensorCommitTask idle");
                bWarned = true;
                }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
            }

        // When paused (e.g. during web file downloads/listing), stay out
        // of the mutex entirely so web handlers can access the SD card
        // without contention.  Just do the ring buffer receive to keep
        // the watchdog happy, then loop back.
        if (g_bPause)
            {
            pchIn = (char *)xRingbufferReceive(xLoggingRingBuffer, &iPrintLen, pdMS_TO_TICKS(100));
            if (pchIn != NULL)
                vRingbufferReturnItem(xLoggingRingBuffer, pchIn);
            continue;
            }

        // Take the write mutex for the full drain+write cycle. Both the
        // staging buffer (szWriteBuf/uBufUsed) and the log file handle
        // are shared with LogSensor::Close() and other SD-consuming paths,
        // so serialize all access through xWriteMutex. The first receive
        // below waits up to 100 ms, so this task will hold the mutex for
        // up to ~100 ms when idle — still well under the 1000+ ms timeouts
        // used by every other taker.
        if (!xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            {
            static unsigned long uLastWarnMs = 0;
            unsigned long uNow = millis();
            if ((uNow - uLastWarnMs) > 2000)
                {
                g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning, "SD busy (xWriteMutex); skipping drain");
                uLastWarnMs = uNow;
                }
            continue;
            }

        // Drain available items from the ring buffer into the staging buffer.
        // First receive blocks up to 100 ms (keeps watchdog happy); subsequent
        // receives are non-blocking to batch everything currently queued.
        bool bGotData = false;
        TickType_t xWait = pdMS_TO_TICKS(100);

        while (true)
            {
            pchIn = (char *)xRingbufferReceive(xLoggingRingBuffer, &iPrintLen, xWait);
            xWait = 0;     // subsequent receives are non-blocking

            if (pchIn == NULL)
                break;

            // If the next item won't fit, stop draining and write what we
            // have. We'll pick up this item on the next iteration.
            if (uBufUsed + iPrintLen > WRITE_BUF_SIZE)
                {
                vRingbufferReturnItem(xLoggingRingBuffer, pchIn);
                pchIn = NULL;
                break;
                }

            memcpy(szWriteBuf + uBufUsed, pchIn, iPrintLen);
            uBufUsed += iPrintLen;
            vRingbufferReturnItem(xLoggingRingBuffer, pchIn);
            pchIn = NULL;
            bGotData = true;
            }

        if (g_Log.Test(MsgLog::EnDisk, MsgLog::EnDebug) && bGotData)
            {
            UBaseType_t uxFree;
            UBaseType_t uxRead;
            UBaseType_t uxWrite;
            UBaseType_t uxAcquire;
            UBaseType_t uxItemsWaiting;

            vRingbufferGetInfo(xLoggingRingBuffer, &uxFree, &uxRead, &uxWrite, &uxAcquire, &uxItemsWaiting);
            g_Log.printf(MsgLog::EnDisk, MsgLog::EnDebug, "Write buf %d bytes  Ring waiting %d\n", uBufUsed, uxItemsWaiting);
            }

        // Write 512-byte-aligned chunks to disk
        bool bDidSync = false;
        if (uBufUsed > 0 && m_hLogFile.isOpen())
        {
            // Write full sectors. Any remainder stays in the buffer for
            // the next batch, keeping writes 512-byte-aligned.
            size_t uAligned   = (uBufUsed / SECTOR_SIZE) * SECTOR_SIZE;
            size_t uRemainder = uBufUsed - uAligned;

            if (uAligned > 0)
                {
                uWriteStart = micros();
                m_hLogFile.write(szWriteBuf, uAligned);
                uWriteEnd   = micros();
                uWriteDur   = uWriteEnd - uWriteStart;
                uWriteMax   = uWriteDur > uWriteMax ? uWriteDur : uWriteMax;
                }

            // Shift remainder to front of buffer
            if (uRemainder > 0)
                memmove(szWriteBuf, szWriteBuf + uAligned, uRemainder);
            uBufUsed = uRemainder;

            // Sync periodically. This is a blocking call, so we don't want to do it too often.
            // Flush any remaining partial sector before sync so the data is on disk.
            if ((xTaskGetTickCount() - xLastSyncTime) > pdMS_TO_TICKS(SYNC_INTERVAL_MS))
            {
                FlushStagingBufferLocked();
                uSyncStart = micros();
                m_hLogFile.sync();
                uSyncEnd   = micros();
                uSyncDur   = uSyncEnd - uSyncStart;
                uSyncMax   = uSyncDur  > uSyncMax  ? uSyncDur  : uSyncMax;
                xLastSyncTime = xTaskGetTickCount();
                bDidSync = true;
            }
        } // end if data to write

        xSemaphoreGive(xWriteMutex);

        // Never block SD access while waiting on serial output.
        if (bDidSync)
            g_Log.print(MsgLog::EnDisk, MsgLog::EnDebug, "Sync\n");
    } // end while forever

} // end TaskWriteSensorLog()


// ============================================================================

// Not much to construct
LogSensor::LogSensor()
{

}

// ----------------------------------------------------------------------------

// Open the next sensor log file in sequence
// Be sure to wrap in semaphore

void LogSensor::Open()
{
    char        szSensorLogFilename[32];

    if (g_Config.suDataSrc.enSrc == SuDataSource::EnSensors && g_SdFileSys.bSdAvailable)
    {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnDebug, "Open log file for writing");

        // Find the next file number.
        SdFileSys::SuFileInfoList   suFileList;
        int     iMaxFileNum = 0;

        if (g_SdFileSys.FileList(&suFileList))
            {
            for (int iIdx=0; iIdx<suFileList.size(); iIdx++)
                {
                if (strncasecmp("log_", suFileList[iIdx].szFileName, 4) == 0)
                    {
                    int iFileNum = atoi(&suFileList[iIdx].szFileName[4]);
                    iMaxFileNum = iFileNum > iMaxFileNum ? iFileNum : iMaxFileNum;
                    } // end if is a log file
                } // end for all files
            }
        else
            g_Log.print(MsgLog::EnDisk, MsgLog::EnError, "LOGSENSOR FileList() fail");

        snprintf(szSensorLogFilename, sizeof(szSensorLogFilename), "log_%03d.csv", iMaxFileNum + 1);

        g_Log.print("Sensor log file:"); g_Log.println(szSensorLogFilename);

        m_hLogFile = g_SdFileSys.open(szSensorLogFilename, O_RDWR | O_CREAT | O_TRUNC);

        if (m_hLogFile.isOpen())
        {
            // Build a feature-flag sentinel row so WriteHeader emits the
            // right optional column groups for this session.
            onspeed::LogRow headerRow;
            headerRow.boomEnabled = g_Config.bReadBoom;
            headerRow.efisEnabled = g_Config.bReadEfisData;
            headerRow.efisIsVn300 = g_Config.bReadEfisData &&
                                    (g_EfisSerial.enType == EfisSerialPort::EnVN300);

            static char szHeader[onspeed::proto::log_csv::kHeaderMaxBytes];
            size_t hdrLen = onspeed::proto::log_csv::WriteHeader(headerRow, szHeader, sizeof(szHeader));
            if (hdrLen > 0)
                m_hLogFile.write(szHeader, hdrLen);
            m_hLogFile.write("\n", 1);

            m_hLogFile.sync();
        } // end if file open OK

        else
        {
            g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "SensorFile opening error. Logging disabled.");
            g_Config.bSdLogging = false;
        }
    } // end if sensor data and SD disk available

    else
        g_Log.println(MsgLog::EnDisk, MsgLog::EnDebug, "Log file not opened for writing");

}

// ----------------------------------------------------------------------------

// Close the log file. Caller must hold xWriteMutex.
// Flushes any remaining bytes from the staging buffer before closing, so
// the last ~1-2 log lines are not lost on graceful shutdown (LOG DISABLE,
// FORMAT, soft restart). The consumer task holds xWriteMutex while touching
// szWriteBuf, so this is safe to call from any mutex-holding context.

void LogSensor::Close()
{
    FlushStagingBufferLocked();
    m_hLogFile.close();
}

// ----------------------------------------------------------------------------

// Generate a formatted line of sensor data and send it to the ring queue

void LogSensor::Write()
{
    static char szLogLine[onspeed::proto::log_csv::kRowMaxBytes + 2]; // +2 for "\n\0"

    // Used during SD file downloads (and other future pause cases).
    // Avoid queuing data while the writer task is paused.
    if (g_bPause)
        return;

    if (!g_Config.bSdLogging)
        return;

    // --- Snapshot sensor state into a LogRow ---
    unsigned long uTimeStamp = millis();

    onspeed::LogRow row;
    row.boomEnabled = g_Config.bReadBoom;
    row.efisEnabled = g_Config.bReadEfisData;
    row.efisIsVn300 = g_Config.bReadEfisData &&
                      (g_EfisSerial.enType == EfisSerialPort::EnVN300);

    row.timeStampMs      = (uint32_t)uTimeStamp;
    row.pfwdCounts       = g_Sensors.iPfwd;
    row.pfwdSmoothed     = g_Sensors.PfwdSmoothed;
    row.p45Counts        = g_Sensors.iP45;
    row.p45Smoothed      = g_Sensors.P45Smoothed;
    row.pStaticMbar      = g_Sensors.PStatic;
    row.paltFt           = g_Sensors.Palt;
    row.iasKt            = g_Sensors.IAS;
    row.angleOfAttackDeg = g_Sensors.AOA;
    row.flapsPos         = g_Flaps.iPosition;
    row.dataMark         = g_iDataMark;
    row.oatCelsius       = g_Sensors.OatC;
    row.tasKt            = mps2kts(g_AHRS.fTAS);

    // IMU — imuPitchRateDps holds the raw (un-negated) gyro value.
    // FormatRow applies the sign flip when writing the PitchRate column (issue #182).
    row.imuTempCelsius   = g_pIMU->fTempC;
    row.imuVerticalG     = g_pIMU->Az;
    row.imuLateralG      = g_pIMU->Ay;
    row.imuForwardG      = g_pIMU->Ax;
    row.imuRollRateDps   = g_pIMU->Gx;
    row.imuPitchRateDps  = g_pIMU->Gy;   // un-negated; FormatRow emits -imuPitchRateDps
    row.imuYawRateDps    = g_pIMU->Gz;
    row.pitchDeg         = g_AHRS.SmoothedPitch;
    row.rollDeg          = g_AHRS.SmoothedRoll;

    if (g_Config.bReadBoom)
        {
        int boomAge        = (int)((unsigned long)millis() - g_BoomSerial.uTimestamp);
        row.boomStatic     = g_BoomSerial.Static;
        row.boomDynamic    = g_BoomSerial.Dynamic;
        row.boomAlpha      = g_BoomSerial.Alpha;
        row.boomBeta       = g_BoomSerial.Beta;
        row.boomIasKt      = g_BoomSerial.IAS;
        row.boomAgeMs      = boomAge;
        }

    if (g_Config.bReadEfisData)
        {
        int efisAge = (int)((unsigned long)millis() - g_EfisSerial.uTimestamp);
        if (g_EfisSerial.enType == EfisSerialPort::EnVN300)
            {
            row.vnAngularRateRoll  = g_EfisSerial.suVN300.AngularRateRoll;
            row.vnAngularRatePitch = g_EfisSerial.suVN300.AngularRatePitch;
            row.vnAngularRateYaw   = g_EfisSerial.suVN300.AngularRateYaw;
            row.vnVelNedNorth      = g_EfisSerial.suVN300.VelNedNorth;
            row.vnVelNedEast       = g_EfisSerial.suVN300.VelNedEast;
            row.vnVelNedDown       = g_EfisSerial.suVN300.VelNedDown;
            row.vnAccelFwd         = g_EfisSerial.suVN300.AccelFwd;
            row.vnAccelLat         = g_EfisSerial.suVN300.AccelLat;
            row.vnAccelVert        = g_EfisSerial.suVN300.AccelVert;
            row.vnYawDeg           = g_EfisSerial.suVN300.Yaw;
            row.vnPitchDeg         = g_EfisSerial.suVN300.Pitch;
            row.vnRollDeg          = g_EfisSerial.suVN300.Roll;
            row.vnLinAccFwd        = g_EfisSerial.suVN300.LinAccFwd;
            row.vnLinAccLat        = g_EfisSerial.suVN300.LinAccLat;
            row.vnLinAccVert       = g_EfisSerial.suVN300.LinAccVert;
            row.vnYawSigma         = g_EfisSerial.suVN300.YawSigma;
            row.vnRollSigma        = g_EfisSerial.suVN300.RollSigma;
            row.vnPitchSigma       = g_EfisSerial.suVN300.PitchSigma;
            row.vnGnssVelNedNorth  = g_EfisSerial.suVN300.GnssVelNedNorth;
            row.vnGnssVelNedEast   = g_EfisSerial.suVN300.GnssVelNedEast;
            row.vnGnssVelNedDown   = g_EfisSerial.suVN300.GnssVelNedDown;
            row.vnGnssLat          = g_EfisSerial.suVN300.GnssLat;
            row.vnGnssLon          = g_EfisSerial.suVN300.GnssLon;
            row.vnGpsFix           = g_EfisSerial.suVN300.GPSFix;
            row.vnDataAgeMs        = efisAge;
            strncpy(row.vnTimeUtc, g_EfisSerial.suVN300.szTimeUTC,
                    onspeed::kLogRowUtcTimeLen - 1);
            row.vnTimeUtc[onspeed::kLogRowUtcTimeLen - 1] = '\0';
            }
        else
            {
            row.efisIasKt         = g_EfisSerial.suEfis.IAS;
            row.efisPitchDeg      = g_EfisSerial.suEfis.Pitch;
            row.efisRollDeg       = g_EfisSerial.suEfis.Roll;
            row.efisLateralG      = g_EfisSerial.suEfis.LateralG;
            row.efisVerticalG     = g_EfisSerial.suEfis.VerticalG;
            row.efisPercentLift   = g_EfisSerial.suEfis.PercentLift;
            row.efisPaltFt        = g_EfisSerial.suEfis.Palt;
            row.efisVsiFpm        = g_EfisSerial.suEfis.VSI;
            row.efisTasKt         = g_EfisSerial.suEfis.TAS;
            row.efisOatCelsius    = g_EfisSerial.suEfis.OAT;
            row.efisFuelRemaining = g_EfisSerial.suEfis.FuelRemaining;
            row.efisFuelFlow      = g_EfisSerial.suEfis.FuelFlow;
            row.efisMap           = g_EfisSerial.suEfis.MAP;
            row.efisRpm           = g_EfisSerial.suEfis.RPM;
            row.efisPercentPower  = g_EfisSerial.suEfis.PercentPower;
            row.efisMagHeading    = g_EfisSerial.suEfis.Heading;
            row.efisAgeMs         = efisAge;
            row.efisTimestampMs   = (uint32_t)g_EfisSerial.uTimestamp;
            }
        }

    row.earthVerticalG = g_AHRS.EarthVertG;
    row.flightPathDeg  = g_AHRS.FlightPath;
    row.vsiFpm         = mps2fpm(g_AHRS.KalmanVSI);
    row.altitudeFt     = m2ft(g_AHRS.KalmanAlt);
    row.derivedAoaDeg  = g_AHRS.DerivedAOA;
    row.coeffP         = g_fCoeffP;

    // --- Format the row into the log line buffer ---
    size_t lineLen = onspeed::proto::log_csv::FormatRow(row, szLogLine, sizeof(szLogLine));
    if (lineLen == 0)
        {
        static unsigned long uLastWarnMs = 0;
        unsigned long uNow = millis();
        if ((uNow - uLastWarnMs) > 2000)
            {
            g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Log line overflow; dropping line");
            uLastWarnMs = uNow;
            }
        return;
        }

    // Append the newline that the SD write task expects.
    szLogLine[lineLen]     = '\n';
    szLogLine[lineLen + 1] = '\0';
    lineLen += 1;

    // Send to the ring buffer for writing
    if (xLoggingRingBuffer == nullptr)
        {
        static unsigned long uLastWarnMs = 0;
        unsigned long uNow = millis();
        if ((uNow - uLastWarnMs) > 2000)
            {
            g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Logging ring buffer not allocated; dropping log line");
            uLastWarnMs = uNow;
            }
        g_Config.bSdLogging = false;
        return;
        }

    bool bSendOK = xRingbufferSend(xLoggingRingBuffer, szLogLine, lineLen, 0);
    if (!bSendOK)
        __atomic_fetch_add(&s_uRingDropCount, 1u, __ATOMIC_RELAXED);

} // end LogSensor::Write()
