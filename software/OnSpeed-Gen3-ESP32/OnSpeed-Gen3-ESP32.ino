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

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include <HardwareSerial.h>
#include <WiFi.h>

#define MAIN
#include "src/Globals.h"
#include <buildinfo.h>
#include <util/Perf.h>
#include "src/tasks/PerfDump.h"

#ifdef ONSPEED_SYNTH_SENSORS
#include "src/test/SyntheticStream.h"
#include <test_frames/SynthFrames.h>
#endif

#include "src/tasks/EfisRead.h"
#include "src/tasks/BoomRead.h"

// EfisRead.cpp owns the IdfUartStream that replaces Serial2 for the
// EFIS UART.  Forward-declare its accessors so setup() can install the
// driver and wire its stream pointer into g_EfisSerial before
// EfisReadTask is spawned.
bool   EfisReadTaskInit(uint32_t baud);
class IdfUartStream;
IdfUartStream* GetEfisStream();

#ifdef SUPPORT_LITTLEFS
// Undefine SdFat's FILE_READ/FILE_WRITE before including LittleFS which redefines them
#undef FILE_READ
#undef FILE_WRITE
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

// Task to handle web server polling on Core 0
void WebServerTask(void * pvParams)
{
    for(;;)
    {
        // Poll at 200 Hz when a client is connected, 20 Hz when idle
        unsigned uDelay = (WiFi.softAPgetStationNum() > 0) ? 5 : 50;
        vTaskDelay(pdMS_TO_TICKS(uDelay));

        // PERF: time only the work after the sleep.
        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::WebServer,
            uxTaskGetStackHighWaterMark(nullptr));
        CfgWebServerPoll();
    }
}

// Task to handle websocket polling on Core 0
void DataServerTask(void * pvParams)
{
    for(;;)
    {
        // Poll at 200 Hz when a client is connected, 20 Hz when idle
        unsigned uDelay = (WiFi.softAPgetStationNum() > 0) ? 5 : 50;
        vTaskDelay(pdMS_TO_TICKS(uDelay));

        // PERF: time only the work after the sleep.
        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::DataServer,
            uxTaskGetStackHighWaterMark(nullptr));
        DataServerPoll();
    }
}

// ----------------------------------------------------------------------------

void setup()
{

    delay(1000);

    //Serial.begin(115200);
    Serial.begin(921600);
    Serial.print("\nOnSpeed Gen3 ");
    Serial.println(BuildInfo::version);

    // Setup FreeRTOS semaphores and error logging
    xWriteMutex      = xSemaphoreCreateMutex();
    xSensorMutex     = xSemaphoreCreateMutex();
    xAhrsMutex       = xSemaphoreCreateMutex();
    xSerialLogMutex  = xSemaphoreCreateMutex();

    // Boot diagnostics: read reset reason and boot count from NVS and print
    // a summary. Runs after semaphore creation (uses g_Log, which takes
    // xSerialLogMutex), but before any code that could hang or crash, so
    // that even a failed boot leaves ground-truth state in NVS for the
    // NEXT boot to report.
    BootDiag::Init();

    // Initialize SD card
    g_SdFileSys.Init();

    if (g_SdFileSys.bSdAvailable == false)
        g_Log.println(MsgLog::EnMain, MsgLog::EnError, "Mount SD card failed");

    // Append this boot's summary line to /boot_log.txt. No-op if SD is
    // unavailable; NVS state was already captured by BootDiag::Init().
    BootDiag::AppendToSd();

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

    // Latch the IMU hardware rate from the just-loaded config. Read-only
    // for the rest of boot; consumers (IMU330::Init ODR bits, AHRS dt,
    // ImuReadTask period, AHRS reseed callsites) read g_imuSampleRateHz
    // and stay consistent with the ODR the IMU is actually running at.
    // See HardwareMap.h for the transitional policy mapping iLogRate
    // values to IMU rates.
    g_imuSampleRateHz = (g_Config.iLogRate == kImuSampleRateExperimental)
                            ? kImuSampleRateExperimental
                            : kImuSampleRateDefault;

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

    // EFIS UART moves off Arduino HardwareSerial onto the IDF UART driver
    // so EfisReadTask can wake on data via the event queue + bulk-read
    // bytes per wake (see software/sketch_common/src/tasks/EfisRead.cpp).
    // Serial2 is NOT touched here; uart_driver_install in EfisReadTaskInit
    // takes exclusive ownership of UART_NUM_2.  If install fails (hardware
    // fault), the box boots with no EFIS — better to log it than fall back
    // to a HardwareSerial path we'd have to maintain a second time.
    //
    // VN-300 needs 921600 baud (138 B × 400 Hz = 55.2 kB/s on the wire,
    // way over the 115200 ceiling). All other EFIS types use 115200.
    const uint32_t kEfisBaud =
        (g_EfisSerial.enType == EfisSerialPort::EnVN300) ? 921600 : 115200;
    if (EfisReadTaskInit(kEfisBaud)) {
        g_EfisSerial.AttachUart(GetEfisStream(), g_EfisSerial.enType);
    } else {
        g_Log.println(MsgLog::EnEfis, MsgLog::EnError,
                      "EfisReadTaskInit failed; EFIS disabled");
        g_EfisSerial.enType = EfisSerialPort::EnNone;
    }

#ifdef ONSPEED_SYNTH_SENSORS
    // Boom-side synth is kept for the perf-synth env (no real boom probe
    // attached on the bench).  EFIS-side synth is gone — we use the
    // tools/bench/uart_efis_stim.py rig over a real USB-TTL dongle for
    // realistic EFIS bytes now.  See `synth status` console command.
    static SyntheticStream s_synthBoomStream(
        onspeed::test_frames::BoomFrame(),
        onspeed::test_frames::kBoomPeriodUs,
        "SynthBoom");
    g_BoomSerial.Init(&s_synthBoomStream);
    g_Config.bReadBoom = true;
    {
        uint32_t SerialConfig = SerialConfig::SERIAL_8N1;
        Serial1.begin(115200, SerialConfig, kBoomRx, kDisplayTx, false);
    }
    g_Log.printf("synth: boom synth active; EFIS uses real UART (perf-synth build)\n");
    g_pSynthBoomStream = &s_synthBoomStream;
#else
    // Serial1 is shared between boom (RX) and the M5 display (TX) —
    // both still go through Arduino HardwareSerial since the IDF driver
    // can't share a UART.  Boom moves to its own task (BoomReadTask)
    // but keeps reading via Serial1.available()/read().
    uint32_t    SerialConfig = SerialConfig::SERIAL_8N1;
    Serial1.begin(115200, SerialConfig, kBoomRx, kDisplayTx, false);

    // Init boom serial
    g_BoomSerial.Init(&Serial1);
#endif

    // Init display output serial
    g_DisplaySerial.Init(&Serial1);


    // Get all the chip select pins in the proper state before starting
    // ----------------------------------------------------------------
    pinMode(kCsImu,    OUTPUT); digitalWrite(kCsImu,    HIGH);
    pinMode(kCsStatic, OUTPUT); digitalWrite(kCsStatic, HIGH);
    pinMode(kCsAoa,    OUTPUT); digitalWrite(kCsAoa,    HIGH);
    pinMode(kCsPitot,  OUTPUT); digitalWrite(kCsPitot,  HIGH);
    if constexpr (kHasExternalMcp3202)
        {
        pinMode(kCsAdc, OUTPUT); digitalWrite(kCsAdc, HIGH);
        }
    pinMode(kSdCs,     OUTPUT); digitalWrite(kSdCs,     HIGH);

    // Init sensor SPI interface
    g_pSensorSPI = new SpiIO(FSPI, kSensorSclk, kSensorMiso, kSensorMosi, kCsImu);

    // Initialize IMU class
    // --------------------
    g_pIMU = new IMU330(g_pSensorSPI, kCsImu);
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

    // Init pressure sensor classes
    // ----------------------------
    g_pPitot  = new HscPressureSensor(g_pSensorSPI, kCsPitot,  HSCMRRN001PDSA3);
    g_pAOA    = new HscPressureSensor(g_pSensorSPI, kCsAoa,    HSCMRRN001PDSA3);
    g_pStatic = new HscPressureSensor(g_pSensorSPI, kCsStatic, HSCMRNN1_6BASA3);

    // Init Sensors
    g_Sensors.Init();

    // Init AHRS after sensors so Kalman starts with real pressure altitude.
    g_AHRS.Init(g_imuSampleRateHz);

    // Init audio system
    g_AudioPlay.Init();

    // Setup FreeRTOS tasks
    // --------------------
    // Data-path ring buffer: at 208 Hz / ~80 KB/s, 256 KB is ~3.2 s of
    // headroom — enough to absorb the "300+ ms, occasionally several
    // seconds" wear-leveling pauses documented in LogSensorCommitTask.
    // Allocated in PSRAM so it doesn't eat internal DRAM (the V4P
    // board has 8 MB SPIRAM via BOARD_HAS_PSRAM).
    //
    // RINGBUF_TYPE_NOSPLIT is load-bearing: each xRingbufferReceive
    // returns exactly ONE item (one log row, ≤ kRowMaxBytes). The
    // drain loop in LogSensorCommitTask relies on this to bound the
    // "row doesn't fit in staging buffer" case to one row, which the
    // carryover slot then rescues. Switching to BYTEBUF here would
    // reintroduce silent data loss whenever a card stall fills the
    // ring with several KB of queued rows (Receive returns all of
    // them, drain can only fit some, the rest are freed back to the
    // ring's free pool with no way to un-receive).
    //
    // Cost: ~24-byte per-item header overhead. At ~200 byte rows
    // that's 12 %, leaving ~230 KB usable.
    xLoggingRingBuffer = xRingbufferCreateWithCaps(
        kLoggingRingBufferBytes, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool bLoggingRingBufferOk = (xLoggingRingBuffer != NULL);
    if (!bLoggingRingBufferOk)
        g_Log.println(MsgLog::EnMain, MsgLog::EnError, "xLoggingRingBuffer is NULL; SD logging disabled this session");

    // Debug-log ring: 16 KB in PSRAM. Sized for Warning/Error volume
    // plus the 0.1-1 Hz PERF tick; not meant to absorb verbose Debug
    // output, which is gated by the SD threshold in MsgLog. Drained
    // by LogSensorCommitTask under the same xWriteMutex window — no
    // separate writer task (would starve the data writer for the
    // mutex, since SdFat's volume cache is shared).
    xDebugRingBuffer = xRingbufferCreateWithCaps(
        kDebugRingBufferBytes, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (xDebugRingBuffer == NULL)
        g_Log.println(MsgLog::EnMain, MsgLog::EnError, "xDebugRingBuffer is NULL; SD debug log disabled");
    // bSdLogging is intentionally not mutated on alloc failure — the
    // user's saved config is the source of truth. bLoggingRingBufferOk
    // gates Open() and writer-task creation below; Write()'s null-ring
    // path warns and returns. Next boot retries the allocation.

    // Get the right set of tasks running for the selected mode
    if      (g_Config.suDataSrc.enSrc == SuDataSource::EnSensors)
        {
        // Create logfile
        if (g_Config.bSdLogging && bLoggingRingBufferOk)
            if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100)))
                {
                g_LogSensor.Open();
                xSemaphoreGive(xWriteMutex);
                }

        // On perf-synth builds (esp32s3-v4p-perf-synth), auto-enable
        // PERF streaming BEFORE the IMU task starts. At high IMU rates
        // (>208 Hz) the ImuReadTask's busy-wait pattern keeps Core 1
        // fully occupied — including the Arduino loop task that
        // processes ConsoleSerial.Read. Without auto-enable, the
        // capture script's `perf on` command sits in the UART RX buffer
        // forever because the console parser never gets CPU.
        //
        // Production builds (esp32s3-v4p) don't define ONSPEED_PERF_ENABLED,
        // so this block is compiled out — the manual `perf on` console
        // command remains the only path.
#if defined(ONSPEED_PERF_ENABLED) && defined(ONSPEED_SYNTH_SENSORS)
        onspeed::perf_dump::SetStreaming(true);
        g_Log.println("PERF streaming auto-enabled (perf-synth build)");
#endif

        xTaskCreatePinnedToCore(SensorReadTask,       "Read Sensors",   5000, NULL, 5, &xTaskReadSensors, 1);
        // ImuReadTask is deferred to the very end of setup() (see below;
        // "System ready" prints immediately after the IMU task is spawned).
        // At higher IMU sample rates, ImuReadTask's delayMicroseconds()
        // busy-wait at priority 5 can starve the Arduino setup() task
        // (priority 1) on Core 1, which blocks CfgWebServerInit() and
        // prevents the WiFi AP from coming up. Spawning ImuReadTask last
        // means setup() is essentially done by the time the priority-5
        // hot loop begins.
        if (bLoggingRingBufferOk)
            // Writer pinned to Core 0 to keep the SD-bus work (writes,
            // syncs, sidecar refresh) off Core 1 where the IMU
            // task lives. Core 0 already hosts WiFi and the web server;
            // xWriteMutex serializes SD access across cores correctly.
            xTaskCreatePinnedToCore(LogSensorCommitTask,  "Write Data",     5000, NULL, 1, &xTaskWriteLog,     0);
        // Use >= 208 so any future higher rates (e.g. 416) still take
        // the IMU-driven log-write path rather than fall through to
        // the 50 Hz pressure path.
        if (g_Config.iLogRate >= 208)
            Serial.printf("Logging at %iHz\n", int(g_AHRS.fImuSampleRate));
        else
            g_Log.println("Logging at 50Hz");
        g_Log.println("Data Source SENSORS");
        }
    else if (g_Config.suDataSrc.enSrc == SuDataSource::EnReplay)
        {
        suLogReplayParams.sReplayLogFile = g_Config.sReplayLogFileName.c_str();
        xTaskCreatePinnedToCore(LogReplayTask,        "Log Replay",  10000, &suLogReplayParams, 3, &xTaskLogReplay, 1);
        g_Log.println("Data Source REPLAYLOGFILE");
        }
    else if (g_Config.suDataSrc.enSrc == SuDataSource::EnTestPot)
        {
        g_Config.bSdLogging = false;
        xTaskCreatePinnedToCore(SensorReadTask,       "Read Sensors",   5000, NULL, 5, &xTaskReadSensors, 1);
        xTaskCreatePinnedToCore(ImuReadTask,          "Read IMU",       5000, NULL, 5, &xTaskReadImu,     1);
        xTaskCreatePinnedToCore(TestPotTask,          "Test Pot",       2000, NULL, 3, &xTaskTestPot,     1);
        g_Log.println("Data Source TESTPOT");
        }
    else if (g_Config.suDataSrc.enSrc == SuDataSource::EnRangeSweep)
        {
        g_Config.bSdLogging = false;
        xTaskCreatePinnedToCore(SensorReadTask,       "Read Sensors",   5000, NULL, 5, &xTaskReadSensors, 1);
        xTaskCreatePinnedToCore(ImuReadTask,          "Read IMU",       5000, NULL, 5, &xTaskReadImu,     1);
        xTaskCreatePinnedToCore(RangeSweepTask,       "Range Sweep",    2000, NULL, 3, &xTaskRangeSweep,  1);
        g_Log.println("Data Source RANGESWEEP");
        }

    // These always run
    xTaskCreatePinnedToCore(AudioPlayTask,        "AudioPlay",      5000,  NULL, 6, &xTaskAudioPlay,     1);
    xTaskCreatePinnedToCore(WriteDisplayDataTask, "Write Display",  4000,  NULL, 4, &xTaskDisplaySerial, 1);
    xTaskCreatePinnedToCore(SwitchCheckTask,      "Check Switch",   5000,  NULL, 4, &xTaskCheckSwitch,   1);
    xTaskCreatePinnedToCore(HousekeepingTask,     "Housekeeping",   4000,  NULL, 0, &xTaskHousekeeping,  1); // Heartbeat init needed 4 KB in v4.10

    // EFIS + boom UART readers — pinned to Core 0 at priority 3.
    // EfisRead blocks on the IDF UART event queue (wake-on-data) on
    // real hardware, or polls a SyntheticStream in synth builds.
    // BoomRead polls Serial1.available() at 5 ms cadence (Serial1 is
    // shared with Display TX so we can't take exclusive IDF ownership).
    // Stack sizes: 4 KB matches Display, plenty for the parser path.
    xTaskCreatePinnedToCore(EfisReadTask,         "EFIS Read",      4000,  NULL, 3, &xTaskEfisRead,      0);
    xTaskCreatePinnedToCore(BoomReadTask,         "Boom Read",      4000,  NULL, 3, &xTaskBoomRead,      0);

#ifdef ONSPEED_SYNTH_SENSORS
    // Bind the synth boom stream to its consumer task and arm the
    // cadence timer.  EFIS-side synth was removed once tools/bench/
    // uart_efis_stim.py made real-UART injection possible.
    if (g_pSynthBoomStream != nullptr) {
        g_pSynthBoomStream->SetConsumerTask(xTaskBoomRead);
        if (!g_pSynthBoomStream->Start()) {
            g_Log.println(MsgLog::EnBoom, MsgLog::EnError,
                          "synth: boom cadence timer failed to start");
        }
    }
#endif

    //xTaskCreatePinnedToCore(TaskDummy,     "Dummy",     10000, NULL,              5, &xTaskDummy,     0);

    delay(100);

    // Play the startup prompt now that AudioPlayTask is running.
    g_AudioPlay.SetVoice(enVoiceEnabled);

    // Configuration web server
    CfgWebServerInit();

    // Live data server
    DataServerInit();

    // DataServer (WebSocket broadcast) on Core 0.  Keep it here:
    //   - lwIP/tcpip_thread is pinned to Core 0 by ESP-IDF; cross-core
    //     `client.write()` would add IPC latency every broadcast cycle.
    //   - When the TCP send buffer fills, the write blocks in
    //     lwip_select — on Core 1 that would risk starving IMU/Audio.
    //   - The "WS dies under load" symptom is *not* about which core
    //     DataServer lives on, it's about Core 0 contention from
    //     EfisRead + WebServer + WiFi. Address that on Core 0 itself.
    xTaskCreatePinnedToCore(
        DataServerTask,     // Function to call
        "DataServer",       // Name of task
        8000,               // Stack size
        NULL,               // Parameter
        2,                  // Priority
        NULL,               // Task handle
        0                   // Core ID (0)
    );

    // WebServer on Core 0, priority 1 (matches master).
    // The cc7d7ac1 "raise to 4" change was intended to keep handler work
    // (template render, gzip compress) running without preemption.  But
    // diagnostic data from log_096 shows WebServer at pri 4 holding Core 0
    // for 10 sec inside a stalled client.write starves the DataServer
    // (pri 2) WS broadcast loop, which leads to WS disconnects and a
    // contention spiral.  Returning to pri 1 lets all the lower-priority
    // tasks (BoomRead pri 3, EfisRead pri 3, DataServer pri 2) preempt
    // WebServer freely; lwIP/WiFi at pri 22 still preempt everything.
    xTaskCreatePinnedToCore(
        WebServerTask,      // Function to call
        "WebServer",        // Name of task
        10000,              // Stack size
        NULL,               // Parameter
        1,                  // Priority (matches master)
        NULL,               // Task handle
        0                   // Core ID (0)
    );

    g_ConsoleSerial.DisplayConsoleHelp();

    // ImuReadTask is deferred to the END of setup() (after CfgWebServerInit,
    // DataServerInit, and all other init). At higher IMU sample rates the
    // task's delayMicroseconds() busy-wait at priority 5 starves the
    // Arduino setup() task (priority 1) on Core 1 — at 208 Hz the impact
    // is benign because the busy-wait window is small relative to the
    // period, but at higher rates (PR for 416 Hz forthcoming) it blocks
    // CfgWebServerInit() entirely and the WiFi AP never comes up. Spawning
    // last means steady-state tasks are the only Core 1 competition by
    // the time the hot loop begins. Only fires in the Sensors data-source
    // mode; Replay/TestPot/RangeSweep paths create the task in their own
    // blocks above.
    if (g_Config.suDataSrc.enSrc == SuDataSource::EnSensors)
        xTaskCreatePinnedToCore(ImuReadTask,          "Read IMU",       5000, NULL, 5, &xTaskReadImu,     1);

    // "System ready" is the last line of setup() so it only prints once
    // the IMU hot loop is actually spawned. Otherwise the box claims to
    // be ready before the flight-critical task exists.
    g_Log.println("System ready.");
    g_Log.print("# ");
    }


// ----------------------------------------------------------------------------

// In FreeRTOS the loop() function is the IdleHook() function that gets called
// when there is no other task to run. It runs at the lowest FreeRTOS task
// priority.

void loop()
    {
//    uLoopStartTime = uMilliSeconds;

    // FreeRTOS doesn't really have very good support for serial port I/O. The
    // problem is that there is no way to block reading from a serial port in
    // an RTOS friendly way. If you use available() then the task just constantly
    // spins, doing nothing. If you do a read() it spins internal to the read
    // function, again doing nothing. So do serial I/O the old fashioned way.
    // That is, in line with everything else that needs a chance to run.

    // Post-PR #609, loop() drives only g_ConsoleSerial.Read().  EFIS
    // and boom UART drains moved to EfisReadTask / BoomReadTask on
    // Core 0.  No PerfScope is opened from here today; the ring binding
    // that the prior version installed is dropped (it served only to
    // make the EFIS/Boom PerfScopes attributable, and those scopes
    // moved with the work).  If a future caller opens a PerfScope from
    // loop(), restore the bindCurrentTaskToRing(TaskId::ArduinoLoop)
    // call here.

    g_ConsoleSerial.Read();

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

    // CfgWebServerPoll() and DataServerPoll() are moved to WebServerTask on Core 0
    // to prevent blocking of critical flight data processing and display updates.

    // This delay is critical to prevent the loop() task from starving lower-priority tasks.
    vTaskDelay(pdMS_TO_TICKS(10));

#if 0
    // Some performance measurements
    uLoopStopTime = uMilliSeconds;
    uLoopTime     = uLoopStopTime - uLoopStartTime;
    uLoopTimeMin  = MIN(uLoopTime, uLoopTimeMin);
    uLoopTimeMax  = MAX(uLoopTime, uLoopTimeMax);
#endif
    uLoopCount++;

}  // end loop()
