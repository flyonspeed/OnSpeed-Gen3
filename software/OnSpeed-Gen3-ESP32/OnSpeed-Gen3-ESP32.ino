////////////////////////////////////////////////////
// More details at
//      http://www.flyOnSpeed.org
//      and
//      https://github.com/flyonspeed/OnSpeed-Gen3/

/*
To-Do List
----------
Do a text search for comments starting with "////"

*/

#include <HardwareSerial.h>
//#include <SoftwareSerial.h>

#define MAIN
#include "Globals.h"

#ifdef SUPPORT_LITTLEFS
#include <LittleFS.h>
#endif

SuParamsReplay      suLogReplayParams;

// Processing loop performance stats
uint64_t            uLoopStartTime;
uint64_t            uLoopStopTime;
uint64_t            uLoopTime;
uint64_t            uLoopTimeMin = UINT_MAX;
uint64_t            uLoopTimeMax = 0;
unsigned            uLoopCount   = 0;


// ----------------------------------------------------------------------------

void setup() 
{

    delay(100);

//    Serial.begin(115200);
    Serial.begin(921600);
    Serial.print("\nOnSpeed Gen2V4 ");
    Serial.println(VERSION);

    // Setup FreeRTOS semaphores and error logging
    xWriteMutex     = xSemaphoreCreateMutex();
    xSensorMutex    = xSemaphoreCreateMutex();
    xSerialLogMutex = xSemaphoreCreateMutex();

    // Initialize SD card
    // ------------------
    /*  Need to look into something called dedicated SPI.
        https://github.com/greiman/SdFat/issues/244
        says "The only way to get dedicated SPI is to explicitly specify 
        DEDICATED_SPI using a begin call with SdSpiConfig as the argument like this..."
            #define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
            if (!sd.begin(SD_CONFIG)) 
                {
                // handle error
                }

        The problem is that I don't know how to specify custom pins using SdSpiConfig().
        Maybe like this...

        SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;
        SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)

    BTW, this could possibly go in a task so that if someone inserts a card
    while powered up then good things would happen.
    */

    g_SdFileSys.Init();

    if (g_SdFileSys.bSdAvailable == false)
        g_Log.println(MsgLog::EnMain, MsgLog::EnError, "Mount SD card failed");

#ifdef SUPPORT_LITTLEFS
    // Try mounting the LittleFS file system in flash
#if 0
    g_bFlashFS = false;
    if (LittleFS.begin()) 
        g_bFlashFS = true;

    // Mount failed so try formatting and mounting again
    else
        {
        g_Log.println(MsgLog::EnMain, MsgLog::EnError, "Mount LittleFS failed, trying to format");
        if (LittleFS.format()) 
            {
            g_Log.println(MsgLog::EnMain, MsgLog::EnError, "Format LittleFS successful");
            if (LittleFS.begin())
                {
                g_bFlashFS = true;
                g_Log.println(MsgLog::EnMain, MsgLog::EnError, "Mount LittleFS successful");
                // At some point I may want to copy a config file from SD if one exists
                }
            else
                g_Log.println(MsgLog::EnMain, MsgLog::EnError, "Mount LittleFS failed again");
            } 
        else 
            {
            g_Log.println(MsgLog::EnMain, MsgLog::EnError, "Format LittleFS failed");
            }
        }
#else
    // Format if fail
    if (LittleFS.begin(true)) 
        g_bFlashFS = true;
#endif 
#endif // SUPPORT_LITTLEFS

    // Load configuration
    // ------------------
    g_Config.LoadConfig();

    // Init the various serial interfaces
    // ----------------------------------

    // Console is over the USB port
    g_ConsoleSerial.Init();

    // In a more perfect world everyone would have their own serial
    // interfaces. But it isn't a perfect world. I've got three
    // devices that want to run at 115200 and only two hardware
    // UARTs, and that is too fast for a software UART. So we get
    // to share. The EFIS/VN300 interface is going to get it's own
    // hardware and will set it up. The display and boom devices
    // are going to share. That UART will get setup here. At some
    // point I may slow the M5 display down to 9600. Then I can use
    // a dedicated software UART for it.

    // There are a bunch of EFIS types so the EFIS object gets to
    // setup its own hardware serial port
    g_EfisSerial.Init(EfisSerialIO::EnVN300,  &Serial2);

    // The M5 display and the boom get to share
    uint32_t    SerialConfig = SerialConfig::SERIAL_8N1;
    Serial1.begin(115200, SerialConfig, BOOM_SER_RX, DISPLAY_SER_TX, false);

    // Init boom serial
    g_BoomSerial.Init(&Serial1);

    // Init display output serial
    g_DisplaySerial.Init(&Serial1);


    // Get all the chip select pins in the proper state before starting
    // ----------------------------------------------------------------
    pinMode(CS_IMU,    OUTPUT); digitalWrite(CS_IMU,    HIGH);
    pinMode(CS_STATIC, OUTPUT); digitalWrite(CS_STATIC, HIGH);
    pinMode(CS_AOA,    OUTPUT); digitalWrite(CS_AOA,    HIGH);
    pinMode(CS_PITOT,  OUTPUT); digitalWrite(CS_PITOT,  HIGH);
    pinMode(SD_CS,     OUTPUT); digitalWrite(SD_CS,     HIGH);
 
    // Init sensor SPI interface
    //  ------------------------
//  pinMode(SENSOR_SCLK, OUTPUT); digitalWrite(CS_AOA, LOW);
//  pinMode(SENSOR_MOSI, OUTPUT); digitalWrite(CS_AOA, LOW);
//  pinMode(SENSOR_MISO, INPUT);
    g_pSensorSPI = new SpiIO(FSPI, SENSOR_SCLK, SENSOR_MISO, SENSOR_MOSI, CS_IMU);
//  g_pSensorSPI = new SpiIO(HSPI, SENSOR_SCLK, SENSOR_MISO, SENSOR_MOSI, CS_IMU);

    // Initialize IMU class
    // --------------------
    g_pIMU = new IMU330(g_pSensorSPI, CS_IMU);
    delay(100);
    g_pIMU->Init();

    // Configure accelerometer axes
    g_pIMU->ConfigAxes();

#if 0   // IS THIS REALLY NECESSARY???
    // Warmup IMU;
    g_Log.println(MsgLog::EnIMU, MsgLog::EnDebug, "Warming up IMU...");
    delay(100);
    unsigned long   uImuWarmupTime = millis();
    while (millis() - uImuWarmupTime < 2500)
    {
        //pIMU->ReadTempC();
        delayMicroseconds(4808);
    }
#endif

    // Initialize pitch and roll
    g_pIMU->Read();

    g_AHRS.Init(IMU_SAMPLE_RATE);

    // Init pressure sensor classes
    // ----------------------------
    g_pPitot  = new HscPressureSensor(g_pSensorSPI, CS_PITOT,  HSCDRNN1_6BASA3);
    g_pAOA    = new HscPressureSensor(g_pSensorSPI, CS_AOA,    HSCDRNN1_6BASA3);
    g_pStatic = new HscPressureSensor(g_pSensorSPI, CS_STATIC, HSCDRRN100MDSA3);

    // Init Sensors
    g_Sensors.Init();

    // Init audio system
    g_AudioPlay.Init();

    // Setup FreeRTOS tasks
    // --------------------
    xLoggingRingBuffer = xRingbufferCreate(30000, RINGBUF_TYPE_BYTEBUF);    // At least 1 sec of data buffering
    if (xLoggingRingBuffer == NULL)
        g_Log.println(MsgLog::EnMain, MsgLog::EnError, "xLoggingRingBuffer is NULL");

    // Get the right set of tasks running for the selected mode
    if      (g_Config.suDataSrc.enSrc == SuDataSource::EnSensors)
        {
        // Create logfile
        if (g_Config.bSdLogging)
            if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100))) 
                {
                g_LogSensor.Open();
                xSemaphoreGive(xWriteMutex);
                }

        xTaskCreate(SensorReadTask,       "Read Sensors",   5000, NULL, 3, &xTaskReadSensors);
        xTaskCreate(LogSensorCommitTask,  "Write Data",     5000, NULL, 1, &xTaskWriteLog);
#ifdef LOGDATA_PRESSURE_RATE  // sd card write rate
        g_Log.println("Logging at 50Hz");
#else
        Serial.printf("Logging at %iHz\n",int(g_AHRS.fImuSampleRate));
#endif
        g_Log.println("Data Source SENSORS");
        }
    else if (g_Config.suDataSrc.enSrc == SuDataSource::EnReplay)
        {
        suLogReplayParams.sReplayLogFile = g_Config.sReplayLogFileName;
        xTaskCreate(LogReplayTask,        "Log Replay",  10000, &suLogReplayParams, 3, &xTaskLogReplay);
        g_Log.println("Data Source REPLAYLOGFILE");
        }
    else if (g_Config.suDataSrc.enSrc == SuDataSource::EnTestPot)
        {
        g_Config.bSdLogging = false;
        xTaskCreate(SensorReadTask,       "Read Sensors",   5000, NULL, 3, &xTaskReadSensors);
        xTaskCreate(TestPotTask,          "Test Pot",       2000, NULL, 3, &xTaskTestPot);
        g_Log.println("Data Source TESTPOT");
        }
    else if (g_Config.suDataSrc.enSrc == SuDataSource::EnRangeSweep)
        {
        g_Config.bSdLogging = false;
        xTaskCreate(SensorReadTask,       "Read Sensors",   5000, NULL, 3, &xTaskReadSensors);
        xTaskCreate(RangeSweepTask,       "Range Sweep",    2000, NULL, 3, &xTaskRangeSweep);
        g_Log.println("Data Source RANGESWEEP");
        }

    // These always run
    xTaskCreate(AudioPlayTask,        "AudioPlay",      5000, NULL, 6, &xTaskAudioPlay);
    xTaskCreate(WriteDisplayDataTask, "Write Display", 10000, NULL, 5, &xTaskDisplaySerial);
    xTaskCreate(SwitchCheckTask,      "Check Switch",   5000, NULL, 5, &xTaskCheckSwitch);
    xTaskCreate(CheckGLimitTask,      "Check G Limit",  2000, NULL, 0, &xTaskGLimit);
    xTaskCreate(CheckVolumeTask,      "Check Volume",   2000, NULL, 0, &xTaskVolume);
    xTaskCreate(CheckVnoChimeTask,    "Check Vno",      2000, NULL, 0, &xTaskVnoChime);
    xTaskCreate(Check3DAudioTask,     "Check 3D Audio", 2000, NULL, 0, &xTask3dAudio);
    xTaskCreate(HeartbeatLedTask,     "Heartbeat",      2000, NULL, 0, &xTaskHeartbeat);

    //xTaskCreatePinnedToCore(TaskDummy,     "Dummy",     10000, NULL,              5, &xTaskDummy,     0);

    delay(100);

    // Configuration web server
    CfgWebServerInit();

    // Live data server
    DataServerInit();

    g_ConsoleSerial.DisplayConsoleHelp();

    g_Log.println("System ready.");
    g_Log.print("# ");
    }


// ----------------------------------------------------------------------------

// In FreeRTOS the loop() function is the IdleHook() function that gets called
// when there is no other task to run. It runs at the lowest FreeRTOS task
// priority.

void loop()
    {
    bool                bLoopStats;

//    uLoopStartTime = uMilliSeconds;

    // FreeRTOS doesn't really have very good support for serial port I/O. The
    // problem is that there is no way to block reading from a serial port in
    // an RTOS friendly way. If you use available() then the task just constantly
    // spins, doing nothing. If you do a read() it spins internal to the read 
    // function, again doing nothing. So do serial I/O the old fashioned way.
    // That is, in line with everything else that needs a chance to run.

//    readWifiSerial();
    g_ConsoleSerial.Read();
    g_EfisSerial.Read();
    g_BoomSerial.Read();

#if 0

    // check for Serial input lockups
    if (millis()-looptime > 1000)
        {
        //Serial.printf("\nloopcount: %i",loopcount);
        checkSerial();
        looptime = millis();
        }

    // wifi data
    if (sendWifiData && millis()-wifiDataLastUpdate>98) // update every 100ms (10Hz) (89ms to avoid processing delays)
        {
        SendWifiData();
        wifiDataLastUpdate = millis();
        }

#endif

    CfgWebServerPoll();
    DataServerPoll();

#if 0
    // Some performance measurements
    uLoopStopTime = uMilliSeconds;
    uLoopTime     = uLoopStopTime - uLoopStartTime;
    uLoopTimeMin  = MIN(uLoopTime, uLoopTimeMin);
    uLoopTimeMax  = MAX(uLoopTime, uLoopTimeMax);
#endif
    uLoopCount++;

}  // end loop()


