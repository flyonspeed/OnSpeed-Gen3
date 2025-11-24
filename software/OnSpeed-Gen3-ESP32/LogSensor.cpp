
#include "SdFat.h"

#include "Globals.h"

#include "LogSensor.h"

#define SYNC_RATE                   100

// Peformance variables for debugging
volatile uint64_t   uWriteMax;
volatile uint64_t   uSyncMax;

// Log file handle, keep it local in scope
static FsFile       m_hLogFile;

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
    static unsigned       uSyncDelay = 0;
    static uint64_t       uWriteStart, uWriteEnd, uWriteDur;
    static uint64_t       uSyncStart,  uSyncEnd,  uSyncDur;

    while (true)
    {
        // Receive string from sensor read via Espressif ring buffer
        // Set the timeout to 100 msec just to keep tickling the watchdog
        pchIn = (char *)xRingbufferReceive(xLoggingRingBuffer, &iPrintLen, pdMS_TO_TICKS(100));
        if (pchIn == NULL)
            {
//            Serial.printf("Ring Buffer rcv NULL\n");
            }
        else
            {
            if (g_Log.Test(MsgLog::EnDisk, MsgLog::EnDebug))
                {
                UBaseType_t uxFree;
                UBaseType_t uxRead;
                UBaseType_t uxWrite;
                UBaseType_t uxAcquire;
                UBaseType_t uxItemsWaiting;

                vRingbufferGetInfo(xLoggingRingBuffer, &uxFree, &uxRead, &uxWrite, &uxAcquire, &uxItemsWaiting);
                g_Log.printf(MsgLog::EnDisk, MsgLog::EnDebug, "Ring Buffer - Item Len %d  Waiting %d\n", iPrintLen, uxItemsWaiting);
                }

#if 1
            // Make sure file handle is open
            if (m_hLogFile.isOpen() && (g_bPause == false))
            {
                xSemaphoreTake(xWriteMutex, portMAX_DELAY);

                // If there is actual string data write it to disk
                if (iPrintLen > 0)
                    {
                    uWriteStart = micros();
                    m_hLogFile.write(pchIn, iPrintLen);
                    m_hLogFile.flush();
                    uWriteEnd   = micros();
                    }

                uWriteDur = uWriteEnd - uWriteStart;
                uWriteMax = uWriteDur > uWriteMax ? uWriteDur : uWriteMax;

                if (uSyncDelay == 0)
                {
                    uSyncStart = micros();
                    m_hLogFile.sync();
                    uSyncEnd   = micros();
                    uSyncDur   = uSyncEnd - uSyncStart;
                    uSyncMax   = uSyncDur  > uSyncMax  ? uSyncDur  : uSyncMax;
                    uSyncDelay = SYNC_RATE;
                    g_Log.print(MsgLog::EnDisk, MsgLog::EnDebug, "Sync\n");
                }
                else
                    uSyncDelay--;

                xSemaphoreGive(xWriteMutex);
            } // end if file handle open

             //for (int iIdx = 0; iIdx < iPrintLen; iIdx++)
             //    Serial.print(pchIn[iIdx]);
#endif
            // Remove the log line from the ring buffer
            vRingbufferReturnItem(xLoggingRingBuffer, pchIn);
        }
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
    char        szSensorLogFilename[14];

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

        sprintf(szSensorLogFilename,"log_%03d.csv", iMaxFileNum + 1);

        g_Log.print("Sensor log file:"); g_Log.println(szSensorLogFilename);

        m_hLogFile = g_SdFileSys.open(szSensorLogFilename, O_RDWR | O_CREAT | O_TRUNC);

        if (m_hLogFile.isOpen()) 
        {
            // Write the CSV header line
            m_hLogFile.write("timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,AngleofAttack,flapsPos,DataMark");

#ifdef OAT_AVAILABLE
            m_hLogFile.write(",OAT,TAS");
#endif
            m_hLogFile.write(",imuTemp,VerticalG,LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll");
            if (g_Config.bReadBoom) 
                m_hLogFile.write(",boomStatic,boomDynamic,boomAlpha,boomBeta,boomIAS,boomAge");

            if (g_Config.bReadEfisData)
            {
                if (g_EfisSerial.enType = EfisSerialIO::EnVN300) // VN-300 type data
                    m_hLogFile.write(",vnAngularRateRoll,vnAngularRatePitch,vnAngularRateYaw,vnVelNedNorth,vnVelNedEast,vnVelNedDown,vnAccelFwd,vnAccelLat,vnAccelVert,vnYaw,vnPitch,vnRoll,vnLinAccFwd,vnLinAccLat,vnLinAccVert,vnYawSigma,vnRollSigma,vnPitchSigma,vnGnssVelNedNorth,vnGnssVelNedEast,vnGnssVelNedDown,vnGnssLat,vnGnssLon,vnGPSFix,vnDataAge,vnTimeUTC");
                else
                    m_hLogFile.write(",efisIAS,efisPitch,efisRoll,efisLateralG,efisVerticalG,efisPercentLift,efisPalt,efisVSI,efisTAS,efisOAT,efisFuelRemaining,efisFuelFlow,efisMAP,efisRPM,efisPercentPower,efisMagHeading,efisAge,efisTime");
            } // end if EFIS data

            m_hLogFile.write(",EarthVerticalG, FlightPath, VSI, Altitude");
            m_hLogFile.write("\n");

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

// Not sure how or when the log file would be closed but here you have it

void LogSensor::Close()
{
    m_hLogFile.close();
}

// ----------------------------------------------------------------------------

// Generate a formatted line of sensor data and send it to the ring queue

void LogSensor::Write()
{
    static char     szLogLine[2048];   // Too big for the stack
    unsigned long   uTimeStamp = millis(); // save timestamp for logging
    int             iLineLen   = 0;
    int             BoomAge    = 0;
    int             EfisAge    = 0;

    if (g_Config.bSdLogging)
    {
        iLineLen += sprintf(szLogLine, "%lu,%i,%.2f,%i,%.2f,%.2f,%.2f,%.2f,%.2f,%i,%i",
            uTimeStamp, g_Sensors.iPfwd, g_Sensors.PfwdSmoothed, g_Sensors.iP45,g_Sensors.P45Smoothed,
            g_Sensors.PStatic, g_Sensors.Palt, g_Sensors.IAS, g_Sensors.AOA, 
            g_Flaps.iPosition, g_iDataMark);
        //charsAdded+=sprintf(logLine, "%lu,%i,%.2f,%i,%.2f,%.2f,%.2f,%.2f,%.2f,%i,%i",timeStamp,124,124.56,145,145.00,1013.00,5600.00,110.58,10.25,2,0);
#ifdef OAT_AVAILABLE
        iLineLen += sprintf(szLogLine+iLineLen, ",%.2f,%.2f", g_Sensors.OatC, MPS2KTS(g_AHRS.TAS));
#endif

        iLineLen += sprintf(szLogLine+iLineLen, ",%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f",
            g_pIMU->fTempC, 
            g_pIMU->Az,  g_pIMU->Ay, g_pIMU->Ax, 
            g_pIMU->Gx, -g_pIMU->Gy, g_pIMU->Gz, 
            g_AHRS.SmoothedPitch, g_AHRS.SmoothedRoll);

        if (g_Config.bReadBoom)
        {
            BoomAge     = millis() - g_BoomSerial.uTimestamp;
            iLineLen += sprintf(szLogLine+iLineLen, ",%.2f,%.2f,%.2f,%.2f,%.2f,%i",
                g_BoomSerial.Static, g_BoomSerial.Dynamic, 
                g_BoomSerial.Alpha,  g_BoomSerial.Beta,   
                g_BoomSerial.IAS, BoomAge);
        } // end boom data

        if (g_Config.bReadEfisData)
        {
            EfisAge = millis() - g_EfisSerial.uTimestamp;
            if (g_EfisSerial.enType = EfisSerialIO::EnVN300) // VN-300 type data
            {
                iLineLen += sprintf(szLogLine+iLineLen,",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.6f,%.6f,%i,%i,%s",
                    g_EfisSerial.suVN300.AngularRateRoll, g_EfisSerial.suVN300.AngularRatePitch, g_EfisSerial.suVN300.AngularRateYaw,
                    g_EfisSerial.suVN300.VelNedNorth,     g_EfisSerial.suVN300.VelNedEast,       g_EfisSerial.suVN300.VelNedDown,
                    g_EfisSerial.suVN300.AccelFwd,        g_EfisSerial.suVN300.AccelLat,         g_EfisSerial.suVN300.AccelVert,
                    g_EfisSerial.suVN300.Yaw,             g_EfisSerial.suVN300.Pitch,            g_EfisSerial.suVN300.Roll,
                    g_EfisSerial.suVN300.LinAccFwd,       g_EfisSerial.suVN300.LinAccLat,        g_EfisSerial.suVN300.LinAccVert,
                    g_EfisSerial.suVN300.YawSigma,        g_EfisSerial.suVN300.RollSigma,        g_EfisSerial.suVN300.PitchSigma,
                    g_EfisSerial.suVN300.GnssVelNedNorth, g_EfisSerial.suVN300.GnssVelNedEast,   g_EfisSerial.suVN300.GnssVelNedDown,
                    g_EfisSerial.suVN300.GnssLat,         g_EfisSerial.suVN300.GnssLon,          g_EfisSerial.suVN300.GPSFix,
                    EfisAge, g_EfisSerial.suVN300.TimeUTC.c_str());
            } // end if VN-300

            // Other EFIS data sources
            else
            {
                iLineLen += sprintf(szLogLine+iLineLen, ",%.2f,%.2f,%.2f,%.2f,%.2f,%i,%i,%i,%.2f,%.2f,%.2f,%.2f,%.2f,%i,%i,%i,%i,%ul",
                    g_EfisSerial.suEfis.IAS,           g_EfisSerial.suEfis.Pitch,        g_EfisSerial.suEfis.Roll,
                    g_EfisSerial.suEfis.LateralG,      g_EfisSerial.suEfis.VerticalG,
                    g_EfisSerial.suEfis.PercentLift,   g_EfisSerial.suEfis.Palt,         g_EfisSerial.suEfis.VSI,
                    g_EfisSerial.suEfis.TAS,           g_EfisSerial.suEfis.OAT,
                    g_EfisSerial.suEfis.FuelRemaining, g_EfisSerial.suEfis.FuelFlow,     g_EfisSerial.suEfis.MAP,
                    g_EfisSerial.suEfis.RPM,           g_EfisSerial.suEfis.PercentPower, g_EfisSerial.suEfis.Heading,
                    EfisAge,                           g_EfisSerial.uTimestamp);
            }
        } // end EFIS data

        iLineLen += sprintf(szLogLine+iLineLen, ",%.2f,%.2f,%.2f,%.2f",
            g_AHRS.EarthVertG, g_AHRS.FlightPath, MPS2FPM(g_AHRS.KalmanVSI), M2FT(g_AHRS.KalmanAlt));

        iLineLen += sprintf(szLogLine+iLineLen,"\n");

//      g_Log.printf(MsgLog::EnDisk, MsgLog::EnDebug, "Boom Age %i, EFIS Age %i\n", BoomAge, EfisAge);

//        Serial.printf("%d  %s\n", iLineLen, szLogLine);
#if 1
        // Send the data to the ring buffer for writing
        bool bSendOK = xRingbufferSend(xLoggingRingBuffer, szLogLine, iLineLen, 1000); 
        if (bSendOK == false)
            g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Sensor Read xRingbufferSend() FAIL");
#else
{
    static int iDecimate = 0;
    if (iDecimate == 0)
        {
            UBaseType_t uxFree;
            UBaseType_t uxRead;
            UBaseType_t uxWrite;
            UBaseType_t uxAcquire;
            UBaseType_t uxItemsWaiting;

            vRingbufferGetInfo(xLoggingRingBuffer, &uxFree, &uxRead, &uxWrite, &uxAcquire, &uxItemsWaiting);
            Serial.printf("%d Items\n", uxItemsWaiting);

        // Send the data to the ring buffer for writing
        bool bSendOK = xRingbufferSend(xLoggingRingBuffer, szLogLine, iLineLen, 0);
        if (bSendOK == false)
            {
            vRingbufferGetInfo(xLoggingRingBuffer, &uxFree, &uxRead, &uxWrite, &uxAcquire, &uxItemsWaiting);
            Serial.printf("Sensor Read xRingbufferSend() FAIL - %d Items Waiting\n", uxItemsWaiting);
            }

        iDecimate = 0;
        }
    else
        iDecimate--;
}
#endif

    } // end if logging enabled

} // end logData()

