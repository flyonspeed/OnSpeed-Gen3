// Compiling instructions:
// From the Tools menu select at least the following...

//     Flash Mode: "OPI 80 MHz"
//     Flash Size: "32MB (256 Mb)"
//     Partition Scheme: "32M Flash (4.8MB APP/22MB LittleFS)
//     Upload Speed: "921600"

#ifndef GLOBALS_H
#define GLOBALS_H

// Compile Configuration
// =====================

#ifdef MAIN
#define EXTERN
#define EXTERN_INIT(var, init)    var = init;
#define EXTERN_CLASS(var, ...)    var(__VA_ARGS__);
#else
#define EXTERN                    extern
#define EXTERN_INIT(var, init)    extern var;
#define EXTERN_CLASS(var, ...)    extern var;
#endif

// Board-specific pin and timing definitions. This must be included before any
// module headers that reference pin numbers or sample rates.
#include "HardwareMap.h"

// Firmware version is provided by BuildInfo::version (see software/Libraries/version/).
// PlatformIO: auto-generated from git tags by scripts/generate_buildinfo.py.
// Arduino IDE: uses weak-symbol defaults in software/Libraries/version/buildinfo_default.cpp.

#define SUPPORT_LITTLEFS

// Includes
// ========
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>

#include <Arduino.h>

// FreeRTOS includes
#include "FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"
#include "freertos/ringbuf.h"

// Arduino libraries
#define DISABLE_FS_H_WARNING  // Suppress SdFat warning about FS.h - we use SdFat's File types
#include "SdFat.h"            // https://github.com/greiman/SdFat
#include <OneButton.h>            // button click/double click detection https://github.com/mathertel/OneButton
#include <SPI.h>

// onspeed_core library discovery anchor. Arduino IDE needs a root-level
// header included before any subfolder headers (ahrs/, audio/, types/...)
// can resolve. See software/Libraries/onspeed_core/src/onspeed_core.h.
#include <onspeed_core.h>

// OnSpeed core: pure, platform-free decision modules
#include <audio/GLimitDecision.h>
#include <audio/VnoChimeDecision.h>
#include <audio/VolumeCurve.h>

// OnSpeed modules
#include "src/util/ErrorLogger.h"
#include "src/util/BootDiagnostics.h"
#include "src/config/Config.h"
#include "src/drivers/SPI_IO.h"
#include "src/drivers/IMU330.h"
#include "src/drivers/HscPressureSensor.h"
#include "src/drivers/SdFileSys.h"
#include "src/drivers/SensorIO.h"
#include "src/tasks/LogSensor.h"
#include "src/tasks/DebugLog.h"
#include "src/tasks/LogReplay.h"
#include "src/tasks/AHRS.h"
#include "src/io/ConsoleSerial.h"
#include "src/io/EfisSerialPort.h"
#include "src/io/BoomSerial.h"
#include "src/io/DisplaySerial.h"
#include "src/drivers/Switch.h"
#include "src/tasks/Flaps.h"
#include "src/audio_io/Volume.h"
#include "src/audio_io/Audio.h"
#include "src/tasks/Housekeeping.h"
#include "src/web_server/ConfigWebServer.h"
#include "src/web_server/DataServer.h"
#include "src/util/Helpers.h"

// RTOS Stuff
// ----------

EXTERN_INIT(TaskHandle_t             xTaskAudioPlay,     NULL)
EXTERN_INIT(TaskHandle_t             xTaskReadSensors,   NULL)
EXTERN_INIT(TaskHandle_t             xTaskReadImu,       NULL)
EXTERN_INIT(TaskHandle_t             xTaskWriteLog,      NULL)
EXTERN_INIT(TaskHandle_t             xTaskCheckSwitch,   NULL)
EXTERN_INIT(TaskHandle_t             xTaskDisplaySerial, NULL)
EXTERN_INIT(TaskHandle_t             xTaskHousekeeping,  NULL)
EXTERN_INIT(TaskHandle_t             xTaskLogReplay,     NULL)
EXTERN_INIT(TaskHandle_t             xTaskTestPot,       NULL)
EXTERN_INIT(TaskHandle_t             xTaskRangeSweep,    NULL)
EXTERN_INIT(TaskHandle_t             xTaskDebugLog,      NULL)

EXTERN RingbufHandle_t          xLoggingRingBuffer;
EXTERN RingbufHandle_t          xDebugRingBuffer;

EXTERN SemaphoreHandle_t        xWriteMutex;
EXTERN SemaphoreHandle_t        xSensorMutex;
EXTERN SemaphoreHandle_t        xAhrsMutex;
EXTERN SemaphoreHandle_t        xSerialLogMutex;
// Separate from xWriteMutex on purpose: the very SD stalls we want to
// log about hold xWriteMutex, so debug-side writes must use their own.
EXTERN SemaphoreHandle_t        xDebugWriteMutex;

// Right now there is only one scheduled task, reading sensors. Once it starts
// I want it to always be on the same integer multiple of ticks so it doesn't
// mess up time alignment of the logged data. I want to have in place the structure
// to be able to schedule periodic tasks relative to each other just in case that
// becomes useful in the future.
#define xLAST_TICK_TIME(period_msec)     ((xTaskGetTickCount() / pdMS_TO_TICKS(period_msec)) * pdMS_TO_TICKS(period_msec))

// Global Data
// ===========

// Variables that get used globally get a "g_" prefix, at least the important ones

EXTERN MsgLog                   g_Log;
EXTERN SdFileSys                g_SdFileSys;

EXTERN FOSConfig                g_Config;   // Config file settings

EXTERN IMU330                 * g_pIMU;
EXTERN SpiIO                  * g_pSensorSPI; // SPI interface for sensors
EXTERN HscPressureSensor      * g_pPitot;   // Pitot port pressure
EXTERN HscPressureSensor      * g_pAOA;     // AoA port pressure
EXTERN HscPressureSensor      * g_pStatic;  // Static pressure

EXTERN  SensorIO                g_Sensors;
EXTERN  LogSensor               g_LogSensor;
EXTERN  DebugLog                g_DebugLog;

EXTERN_CLASS(AHRS               g_AHRS, kGyroSmoothing)

EXTERN ConsoleSerialIO          g_ConsoleSerial;
EXTERN EfisSerialPort           g_EfisSerial;
EXTERN BoomSerialIO             g_BoomSerial;
EXTERN DisplaySerial            g_DisplaySerial;

EXTERN Flaps                    g_Flaps;

EXTERN_CLASS(OneButton          g_Switch, kPinSwitch, true)

EXTERN AudioPlay                g_AudioPlay;

// Stateful chime detectors (onspeed_core). Instantiated once; each
// HousekeepingTask iteration passes a freshly-snapshotted input struct.
EXTERN onspeed::audio::GLimitDetector   g_GLimitDetector;
EXTERN onspeed::audio::VnoChimeDetector g_VnoChimeDetector;

EXTERN_INIT(bool g_bFlashFS, false)     // One of the on-board flash file systems (e.g. LittleFS) ready
EXTERN_INIT(bool g_bPause,   false)

//// I GOTTA FIND A BETTER HOME FOR THESE ONE OF THESE DAYS
// Data mark
EXTERN_INIT(volatile int g_iDataMark, 0)
EXTERN volatile float g_fCoeffP;                  // coefficient of pressure
EXTERN_INIT(volatile bool g_bAudioEnable, true);    //// Move to audio

// Debug data
// ----------

// Enable / disable debug logging
EXTERN_INIT(bool g_bDebugTasks, true)

#endif // defined GLOBALS_H
