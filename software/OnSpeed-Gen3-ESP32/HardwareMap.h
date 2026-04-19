// HardwareMap.h
//
// Board-specific pin assignments and hardware-tied timing constants for the
// OnSpeed Gen3 ESP32-S3 firmware. This is the ONE file that differs per
// board; everything else in the sketch builds the same regardless of which
// board the sketch is running on.
//
// Build-flag selection: one of HW_V4P (Phil's box, default) or HW_V4B
// (Bob's box) must be defined. PlatformIO passes these via -D flags in
// platformio.ini. Arduino IDE users get HW_V4P by default via the guard
// block at the top of this file.
//
// When a new board is added (e.g. Gen2v4), create a new sketch folder with
// its own HardwareMap.h. All shared code under src/** consumes this file
// through Globals.h.
//
#ifndef HARDWARE_MAP_H
#define HARDWARE_MAP_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Hardware-variant guard
//
// Arduino IDE users who build without passing -D flags get HW_V4P by default.
// PlatformIO always passes -DHW_V4P or -DHW_V4B explicitly (see platformio.ini).
// ---------------------------------------------------------------------------
#if !defined(HW_V4B) && !defined(HW_V4P)
#define HW_V4P   // Default for Arduino IDE (no -D flag)
#endif
#if defined(HW_V4B) && defined(HW_V4P)
#error "Cannot define both HW_V4B and HW_V4P. Select one hardware variant."
#endif

// ---------------------------------------------------------------------------
// Sensor SPI bus (shared for pressure sensors, IMU, and MCP3202 ADC on V4P)
// ---------------------------------------------------------------------------
constexpr int kSensorMiso = 18;
constexpr int kSensorMosi = 17;
constexpr int kSensorSclk = 16;

constexpr int kCsImu    = 4;
constexpr int kCsStatic = 7;

// V4P and V4B have PFwd/P45 wired to opposite chip selects.
#ifdef HW_V4P
constexpr int kCsAoa   = 6;
constexpr int kCsPitot = 15;
#else  // HW_V4B
constexpr int kCsAoa   = 15;
constexpr int kCsPitot = 6;
#endif

// ---------------------------------------------------------------------------
// External MCP3202 ADC (V4P has it; V4B uses the internal ADC)
// Per schematic: CH0 = FLAP_POS, CH1 = CTRL_VOL
// ---------------------------------------------------------------------------
#ifdef HW_V4P
constexpr int  kCsAdc              = 5;
constexpr bool kHasExternalMcp3202 = true;
#else  // HW_V4B
constexpr int  kCsAdc              = -1;       // not wired on V4B
constexpr bool kHasExternalMcp3202 = false;
#endif

// MCP3202 channel assignments. These constants are board-agnostic; the
// `if constexpr (kHasExternalMcp3202)` guard at every reader call site
// determines whether they're actually used.
constexpr int kAdcChFlap   = 0;
constexpr int kAdcChVolume = 1;

// ---------------------------------------------------------------------------
// I2S audio output (PCM5102A DAC)
// Pinout differs per board; the I2S driver setup in Audio.cpp reads these.
// ---------------------------------------------------------------------------
#ifdef HW_V4P
constexpr int kI2sBck  = 20;
constexpr int kI2sDout = 19;
constexpr int kI2sLrck = 8;
#else  // HW_V4B
constexpr int kI2sBck  = 45;
constexpr int kI2sDout = 48;
constexpr int kI2sLrck = 47;
#endif

// ---------------------------------------------------------------------------
// IMU physical orientation table
//
// The IMU sits in different physical orientations inside the V4P and V4B
// enclosures, so the mapping from chip XYZ to aircraft body axes differs.
// The 24-row table covers all (sPortsOrientation, sBoxtopOrientation)
// combinations the user can configure. IMU330.cpp reads this table at
// init time and applies the rotation in its ConfigAxes() path, so
// consumers downstream of the IMU see samples already in the canonical
// aircraft frame.
// ---------------------------------------------------------------------------
struct ImuOrientationRow {
    const char* portsOrientation;
    const char* boxtopOrientation;
    const char* verticalGloadAxis;
    const char* lateralGloadAxis;
    const char* forwardGloadAxis;
};

constexpr int kImuOrientationRowCount = 24;

#ifdef HW_V4P
// V4P box: IMU is rotated relative to V4B.
// V4P(X) = V4B(-Y)   V4P(Y) = V4B(-X)   V4P(Z) = V4B(-Z)
inline constexpr ImuOrientationRow kImuOrientationTable[kImuOrientationRowCount] = {
    {"FORWARD", "LEFT",     "X", "-Z",  "Y"},
    {"FORWARD", "RIGHT",   "-X",  "Z",  "Y"},
    {"FORWARD", "UP",       "Z",  "X",  "Y"},
    {"FORWARD", "DOWN",    "-Z", "-X",  "Y"},

    {"AFT",     "LEFT",    "-X", "-Z", "-Y"},
    {"AFT",     "RIGHT",    "X",  "Z", "-Y"},
    {"AFT",     "UP",       "Z", "-X", "-Y"},
    {"AFT",     "DOWN",    "-Z",  "X", "-Y"},

    {"LEFT",    "FORWARD", "-X", "-Y",  "Z"},
    {"LEFT",    "AFT",      "X", "-Y", "-Z"},
    {"LEFT",    "UP",       "Z", "-Y",  "X"},
    {"LEFT",    "DOWN",    "-Z", "-Y", "-X"},

    {"RIGHT",   "FORWARD",  "X",  "Y",  "Z"},
    {"RIGHT",   "AFT",     "-X",  "Y", "-Z"},
    {"RIGHT",   "UP",       "Z",  "Y", "-X"},
    {"RIGHT",   "DOWN",    "-Z",  "Y",  "X"},

    {"UP",      "FORWARD",  "Y", "-X",  "Z"},
    {"UP",      "AFT",      "Y",  "X", "-Z"},
    {"UP",      "LEFT",     "Y", "-Z", "-X"},
    {"UP",      "RIGHT",    "Y",  "Z",  "X"},

    {"DOWN",    "FORWARD", "-Y",  "X",  "Z"},
    {"DOWN",    "AFT",     "-Y", "-X", "-Z"},
    {"DOWN",    "LEFT",    "-Y", "-Z",  "X"},
    {"DOWN",    "RIGHT",   "-Y",  "Z", "-X"}
};
#else  // HW_V4B
// V4B box: +X back (away from pressure ports), +Y right, +Z down.
inline constexpr ImuOrientationRow kImuOrientationTable[kImuOrientationRowCount] = {
    {"FORWARD", "LEFT",    "-Y",  "Z", "-X"}, // TESTED GOOD Paul's
    {"FORWARD", "RIGHT",    "Y", "-Z", "-X"}, // TESTED GOOD
    {"FORWARD", "UP",      "-Z", "-Y", "-X"}, // TESTED GOOD, Vac's RV-4
    {"FORWARD", "DOWN",     "Z",  "Y", "-X"}, // TESTED GOOD

    {"AFT",     "LEFT",     "Y",  "Z",  "X"}, // TESTED GOOD
    {"AFT",     "RIGHT",   "-Y", "-Z",  "X"}, // TESTED GOOD
    {"AFT",     "UP",      "-Z",  "Y",  "X"}, // TESTED GOOD, bench box
    {"AFT",     "DOWN",     "Z", "-Y",  "X"}, // TESTED GOOD

    {"LEFT",    "FORWARD",  "Y",  "X", "-Z"}, // TESTED GOOD
    {"LEFT",    "AFT",     "-Y",  "X",  "Z"}, // TESTED GOOD
    {"LEFT",    "UP",      "-Z",  "X", "-Y"}, // TESTED GOOD, Zlin Z-50
    {"LEFT",    "DOWN",     "Z",  "X",  "Y"}, // TESTED GOOD

    {"RIGHT",   "FORWARD", "-Y", "-X", "-Z"}, // TESTED GOOD
    {"RIGHT",   "AFT",      "Y", "-X",  "Z"}, // TESTED GOOD
    {"RIGHT",   "UP",      "-Z", "-X",  "Y"}, // TESTED GOOD
    {"RIGHT",   "DOWN",     "Z", "-X", "-Y"}, // TESTED GOOD, Tron's RV-8

    {"UP",      "FORWARD", "-X",  "Y", "-Z"}, // TESTED GOOD
    {"UP",      "AFT",     "-X", "-Y",  "Z"}, // TESTED GOOD
    {"UP",      "LEFT",    "-X",  "Z",  "Y"}, // TESTED GOOD
    {"UP",      "RIGHT",   "-X", "-Z", "-Y"}, // TESTED GOOD, Doc's box on Vac's RV-4

    {"DOWN",    "FORWARD",  "X", "-Y", "-Z"}, // TESTED GOOD
    {"DOWN",    "AFT",      "X",  "Y",  "Z"}, // TESTED GOOD, Lenny's RV-10
    {"DOWN",    "LEFT",     "X",  "Z", "-Y"}, // TESTED GOOD
    {"DOWN",    "RIGHT",    "X", "-Z",  "Y"}  // TESTED GOOD
};
#endif

// ---------------------------------------------------------------------------
// SD card SPI (dedicated bus, separate from sensor bus)
// SCLK and MISO pins are swapped between V4P and V4B.
// ---------------------------------------------------------------------------
#ifdef HW_V4B
constexpr int kSdSclk = 42;
constexpr int kSdMiso = 41;
constexpr int kSdMosi = 40;
constexpr int kSdCs   = 39;
#else  // HW_V4P
constexpr int kSdSclk = 41;
constexpr int kSdMiso = 42;
constexpr int kSdMosi = 40;
constexpr int kSdCs   = 39;
#endif

// ---------------------------------------------------------------------------
// Analog / digital GPIO
// ---------------------------------------------------------------------------
constexpr int kPinVolume  = 1;    // analog (via MCP3202 CH1 on V4P; internal ADC on V4B)
constexpr int kPinFlap    = 2;    // analog (via MCP3202 CH0 on V4P; internal ADC on V4B)
constexpr int kPinLedKnob = 13;   // external LED (AOA status / audio on-off)
constexpr int kPinOat     = 14;   // 1-wire OAT sensor
constexpr int kPinSwitch  = 12;   // pilot pushbutton

// ---------------------------------------------------------------------------
// EFIS serial (RS-232 via ADM3202)
// ---------------------------------------------------------------------------
constexpr int kEfisRx = 11;   // J1 pin 25 → R1_OUT on ADM3202
constexpr int kEfisTx = 46;   // NC (we do not transmit to the EFIS)

// ---------------------------------------------------------------------------
// Boom serial (TTL)
// ---------------------------------------------------------------------------
constexpr int kBoomRx = 3;
constexpr int kBoomTx = 8;    // NC

// ---------------------------------------------------------------------------
// Display serial (TTL/RS-232, shares R1_OUT with EFIS)
// ---------------------------------------------------------------------------
constexpr int kDisplayTx = 10;
constexpr int kDisplayRx = 11;   // normally unused; shares EFIS RX

// ---------------------------------------------------------------------------
// Console UART (always builtin USB-CDC; baud rate only)
// ---------------------------------------------------------------------------
constexpr int kBaudConsole = 921600;

// ---------------------------------------------------------------------------
// Task timing (tied to hardware sampling rates)
// ---------------------------------------------------------------------------
// IMU hardware is configured for 208 Hz; AHRS runs at this rate.
constexpr int kImuSampleRateHz = 208;

// Pressure sensors (pitot, AOA, static) are polled at 50 Hz.
constexpr int kPressureSampleRateHz = 50;
constexpr int kPressureIntervalMs   = 1000 / kPressureSampleRateHz;

// Display / panel serial cadence — single source of truth for the
// WriteDisplayDataTask and DataServer broadcast rate. Consumers on the
// other side of the wire (e.g. the M5Stack secondary display) should not
// assume this value — they measure their own frame dt.
constexpr int kDisplaySerialPeriodMs = 50;

// Gyro running-average window length (samples).
constexpr int kGyroSmoothing = 30;

// ---------------------------------------------------------------------------
// Legacy #define aliases (transitional shim — removed in PR 0.4 cleanup).
//
// These let unmodified code that still uses SCREAMING_SNAKE_CASE names
// compile unchanged. Once every file is migrated to the constexpr names
// (PR 0.4 sketch root cleanup completes the migration), this block goes
// away.
// ---------------------------------------------------------------------------
#define SENSOR_MISO               kSensorMiso
#define SENSOR_MOSI               kSensorMosi
#define SENSOR_SCLK               kSensorSclk
#define CS_IMU                    kCsImu
#define CS_STATIC                 kCsStatic
#define CS_AOA                    kCsAoa
#define CS_PITOT                  kCsPitot
#define CS_ADC                    kCsAdc
#define ADC_CH_FLAP               kAdcChFlap
#define ADC_CH_VOLUME             kAdcChVolume
#define SD_SCLK                   kSdSclk
#define SD_MISO                   kSdMiso
#define SD_MOSI                   kSdMosi
#define SD_CS                     kSdCs
#define VOLUME_PIN                kPinVolume
#define FLAP_PIN                  kPinFlap
#define PIN_LED_KNOB              kPinLedKnob
#define OAT_PIN                   kPinOat
#define SWITCH_PIN                kPinSwitch
#define EFIS_SER_TX               kEfisTx
#define EFIS_SER_RX               kEfisRx
#define BOOM_SER_TX               kBoomTx
#define BOOM_SER_RX               kBoomRx
#define DISPLAY_SER_TX            kDisplayTx
#define DISPLAY_SER_RX            kDisplayRx
#define BAUDRATE_CONSOLE          kBaudConsole
#define IMU_SAMPLE_RATE           kImuSampleRateHz
#define PRESSURE_SAMPLE_RATE      kPressureSampleRateHz
#define PRESSURE_INTERVAL_MS      kPressureIntervalMs
#define DISPLAY_SERIAL_PERIOD_MS  kDisplaySerialPeriodMs
#define GYRO_SMOOTHING            kGyroSmoothing

#endif  // HARDWARE_MAP_H
