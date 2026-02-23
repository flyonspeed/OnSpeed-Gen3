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

// Hardware version. Selected via PlatformIO build flag (-DHW_V4P or -DHW_V4B).
// Arduino IDE users: uncomment exactly one line below.
#if !defined(HW_V4B) && !defined(HW_V4P)
#define HW_V4P   // Default for Arduino IDE (no -D flag)
#endif
#if defined(HW_V4B) && defined(HW_V4P)
#error "Cannot define both HW_V4B and HW_V4P. Select one hardware variant."
#endif

// Firmware version is provided by BuildInfo::version (see lib/version/).
// PlatformIO: auto-generated from git tags by scripts/generate_buildinfo.py.
// Arduino IDE: uses weak-symbol defaults in lib/version/buildinfo_default.cpp.

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
#include "SPI.h"

// OnSpeed modules
#include "ErrorLogger.h"
#include "Config.h"
#include "SPI_IO.h"
#include "IMU330.h"
#include "HscPressureSensor.h"
#include "SdFileSys.h"
#include "SensorIO.h"
#include "LogSensor.h"
#include "LogReplay.h"
#include "AHRS.h"
#include "ConsoleSerial.h"
#include "EfisSerial.h"
#include "BoomSerial.h"
#include "DisplaySerial.h"
#include "Switch.h"
#include "Flaps.h"
#include "gLimit.h"
#include "Volume.h"
#include "VnoChime.h"
#include "3DAudio.h"
#include "Audio.h"
#include "HeartBeat.h"
#include "ConfigWebServer.h"
#include "DataServer.h"
#include "Helpers.h"

// Defines
// =======

// ESP32 pin definitions
// ---------------------

#define SENSOR_MISO         18
#define SENSOR_MOSI         17
#define SENSOR_SCLK         16

#define CS_IMU               4
#define CS_STATIC            7

// V4P hardware has PFwd/P45 wired to opposite chip selects.
#ifdef HW_V4P
#define CS_AOA               6
#define CS_PITOT             15
#else
#define CS_AOA               15
#define CS_PITOT             6
#endif

#ifdef HW_V4P
// V4P includes an external MCP3202 ADC on the sensor SPI bus.
// Per schematic: CH0 = FLAP_POS, CH1 = CTRL_VOL
#define CS_ADC               5
#define ADC_CH_FLAP          0
#define ADC_CH_VOLUME        1
#endif

#ifdef HW_V4B
#define SD_SCLK             42
#define SD_MISO             41
#define SD_MOSI             40
#define SD_CS               39
#endif

#ifdef HW_V4P // a couple of pins are swapped here
#define SD_SCLK             41
#define SD_MISO             42
#define SD_MOSI             40
#define SD_CS               39
#endif



//#define TESTPOT_PIN           A20           // pin 39 on Teensy 3.6, pin 10 on DB15
#define VOLUME_PIN           1
#define FLAP_PIN             2

//#define PIN_LED1            38              // internal LED for showing serial input state.
#define PIN_LED_KNOB        13              // external LED for showing AOA status (audio on/off)
#define OAT_PIN             14              // OAT analog input pin
#define SWITCH_PIN          12

// USB is "Serial"

// EFIS / VN300 Serial
// TTL/RS232
#define EFIS_SER_TX         46  // NC
#define EFIS_SER_RX         11  // J1 pin 25 → R1_OUT on ADM3202 (was 9, which is T2_IN)

// Boom Serial
// TTL
#define BOOM_SER_TX          8  // NC
#define BOOM_SER_RX          3

// M5 Display Serial
// TTL/RS232
#define DISPLAY_SER_TX      10
#define DISPLAY_SER_RX      11  // Normally not used (shares R1_OUT with EFIS_SER_RX)

// Data logging frequency
#define LOGDATA_PRESSURE_RATE   // Log at pressure read rate (50 Hz)
//#define LOGDATA_IMU_RATE      // Log at the IMU read rate

#ifdef SPHERICAL_PROBE
  #define IASCURVE(x)           x // Zlin IAS curve
#endif

// Serial baud rates
#define BAUDRATE_CONSOLE       921600

// IMU hardware is configured for 208Hz; AHRS runs at this IMU rate.
#define IMU_SAMPLE_RATE       208

// Pressure sensors (pitot, AOA, static) are read at 50Hz.
#define PRESSURE_SAMPLE_RATE   50
#define PRESSURE_INTERVAL_MS   (1000 / PRESSURE_SAMPLE_RATE)

#define GYRO_SMOOTHING        30

// RTOS Stuff
// ----------

EXTERN_INIT(TaskHandle_t             xTaskAudioPlay,     NULL)
EXTERN_INIT(TaskHandle_t             xTaskReadSensors,   NULL)
EXTERN_INIT(TaskHandle_t             xTaskReadImu,       NULL)
EXTERN_INIT(TaskHandle_t             xTaskWriteLog,      NULL)
EXTERN_INIT(TaskHandle_t             xTaskCheckSwitch,   NULL)
EXTERN_INIT(TaskHandle_t             xTaskDisplaySerial, NULL)
EXTERN_INIT(TaskHandle_t             xTaskGLimit,        NULL)
EXTERN_INIT(TaskHandle_t             xTaskVolume,        NULL)
EXTERN_INIT(TaskHandle_t             xTaskVnoChime,      NULL)
EXTERN_INIT(TaskHandle_t             xTask3dAudio,       NULL)
EXTERN_INIT(TaskHandle_t             xTaskHeartbeat,     NULL)
EXTERN_INIT(TaskHandle_t             xTaskLogReplay,     NULL)
EXTERN_INIT(TaskHandle_t             xTaskTestPot,       NULL)
EXTERN_INIT(TaskHandle_t             xTaskRangeSweep,    NULL)

EXTERN RingbufHandle_t          xLoggingRingBuffer;

EXTERN SemaphoreHandle_t        xWriteMutex;
EXTERN SemaphoreHandle_t        xSensorMutex;
EXTERN SemaphoreHandle_t        xAhrsMutex;
EXTERN SemaphoreHandle_t        xSerialLogMutex;

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

EXTERN_CLASS(AHRS               g_AHRS, GYRO_SMOOTHING)

EXTERN ConsoleSerialIO          g_ConsoleSerial;
EXTERN EfisSerialIO             g_EfisSerial;
EXTERN BoomSerialIO             g_BoomSerial;
EXTERN DisplaySerial            g_DisplaySerial;

EXTERN Flaps                    g_Flaps;

EXTERN_CLASS(OneButton          g_Switch, SWITCH_PIN, true)

EXTERN AudioPlay                g_AudioPlay;

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
