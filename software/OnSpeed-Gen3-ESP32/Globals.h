
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

#define VERSION "4.0"
// Based on OnSpeedTeensy_AHRS "3.3.7a" with later updates merged in

// AOA probe type
//#define SPHERICAL_PROBE // uncomment this if using custom OnSpeed spherical head probe.

// boom type
//#define NOBOOMCHECKSUM    // for booms that don't have a checksum byte in their data stream uncomment this line.

// OAT sensor available
// #define OAT_AVAILABLE  // DS18B20 sensor

#define SUPPORT_LITTLEFS

// Includes
// ========
#define _GNU_SOURCE

#include <stdint.h>

#include <arduino.h>

// FreeRTOS includes
#include "FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"
#include "freertos/ringbuf.h"

// Arduino libraries
#include "SdFat.h" // https://github.com/greiman/SdFat
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
#define CS_AOA               6
#define CS_PITOT            15

#define SD_SCLK             42
#define SD_MISO             41
#define SD_MOSI             40
#define SD_CS               39

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
#define EFIS_SER_RX          9

// Boom Serial
// TTL
#define BOOM_SER_TX          8  // NC
#define BOOM_SER_RX          3

// M5 Display Serial
// TTL/RS232
#define DISPLAY_SER_TX      10
#define DISPLAY_SER_RX      11  // Normally not used

// Data logging frequency
#define LOGDATA_PRESSURE_RATE   // Log at pressure read rate (50 Hz)
//#define LOGDATA_IMU_RATE      // Log at the IMU read rate

// Coefficient of pressure formula
#ifdef SPHERICAL_PROBE
//  #define PCOEFF(p_fwd,p_45)    atan2(p_45,p_fwd); //spherical CP3
//  #define IASCURVE(x)           -3.206e-05*x*x*x+0.008454*x*x+0.3492*x+27.73; // Zlin IAS curve
  #define PCOEFF(p_fwd,p_45)    p_45/p_fwd; //spherical CP3
  #define IASCURVE(x)           x // Zlin IAS curve
#else
  #define PCOEFF(p_fwd,p_45)  p_45/p_fwd; // CP3 // ratiometric CP. CP1 & CP2 are not ratiometric. Can't divide with P45, it goes through zero on Dynon probe.
#endif

#define DEG2RAD(deg)    ((deg)    * 0.0174533)    // degrees to radians
#define RAD2DEG(rad)    ((rad)    * 57.2958)      // radians to degrees
#define G2MPS(gs)       ((gs)     * 9.80665)      // g to m/sec^2
#define MPS2G(mps)      ((mps)    * 0.101971621)  // m/sec^2 to g
#define FT2M(ft)        ((ft)     * 0.3048)       // feet to meters
#define M2FT(meters)    ((meters) * 3.28084)      // meters to feet
#define MPS2FPM(mps)    ((mps)    * 196.85)       // m/sec to fpm
#define MPS2KTS(mps)    ((mps)    * 1.94384)      // m/sec to kts
#define KTS2MPS(kts)    ((kts)    * 0.514444)     // kts to m/sec
#define INHG2MB(inhg)   ((inhg)   * 33.8639)      // pressure inhg to millibars
#define PSI2MB(psi)     ((psi)    * 68.94757)     // pressure PSI to millibars
#define MB2PSI(mb)      ((mb)     * 0.0145038)    // pressure millibars to PSI

#define PITCH(Ax, Ay, Az)   (RAD2DEG( atan2((Ax), sqrt((Ay)*(Ay) + (Az)*(Az)))))  // Pitch from accelerations in degrees
#define ROLL(Ax, Ay, Az)    (RAD2DEG(-atan2((Ay), sqrt((Ax)*(Ax) + (Az)*(Az)))))  // Roll from accelerations in degrees

// Max possible AOA value
#define AOA_MAX_VALUE         40  // was 20 before, but in a sudden stall when the nose quickly goes up, AOA gets larger very quickly and then onspeed goes silent right as the aircraft stalls

 // Min possible AOA value
#define AOA_MIN_VALUE         -20

// Serial baud rates
#define BAUDRATE_CONSOLE       921600

#define IMU_SAMPLE_RATE       50

#define GYRO_SMOOTHING        30

// RTOS Stuff
// ----------

EXTERN_INIT(TaskHandle_t             xTaskAudioPlay,     NULL)
EXTERN_INIT(TaskHandle_t             xTaskReadSensors,   NULL)
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
EXTERN volatile double g_fCoeffP;                 // coefficient of pressure
EXTERN_INIT(volatile bool g_bAudioEnable, true);    //// Move to audio

// Debug data
// ----------

// Enable / disable debug logging
EXTERN_INIT(bool g_bDebugTasks, true)

// DB15 pinout (Gen2 v3 hardware)
//1 - 14V +PWR
//2 - EFIS Serial RX
//3 - PANEL SWITCH
//4 - GPS Serial RX
//5 - LED+ Digital/PWM
//6 - AUDIO RIGHT
//7 - FLAPS Analog IN
//8 - AUDIO LEFT
//9 - OAT Analog IN  (or Display Serial out)
//10 - VOLUME Analog IN
//11 - SENSOR PWR 3.3V
//12 - EFIS Serial TX
//13 - BOOM TTL RX
//14 - GND
//15 - AUDIO GND 

#endif // defined GLOBALS_H