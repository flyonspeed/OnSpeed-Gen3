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
// EFIS serial (RS-232 via ADM3202, with TTL bypass on V4B via SW1)
//
// V4P (Phil's Gen2v4): aircraft EFIS RX lands on J1 pin 25, routed through
//   the ADM3202 R1 channel to ESP32 GPIO 11. Always RS-232 levels.
//
// V4B (Bob's box): aircraft EFIS RX lands on DB-15 pin 2 → J1 pin 24,
//   routed through SW1 to ESP32 GPIO 9. SW1 selects between U10's R2
//   channel (RS-232 mode) and a direct TTL bypass; either way, the data
//   ends up on GPIO 9. The ADM3202 R1 channel that drives GPIO 11 is
//   wired to a different J1 pin and is not in Vac's primary EFIS path.
//
// Symptom of getting this wrong on V4B (which v4.21 and earlier did): the
// firmware listens on GPIO 11 while the EFIS data arrives on GPIO 9, so
// VN-300 / Skyview / G3X / etc. appear silent regardless of EFIS_TYPE.
// ---------------------------------------------------------------------------
#ifdef HW_V4P
constexpr int kEfisRx = 11;   // J1 pin 25 → R1_OUT on ADM3202
#else  // HW_V4B
constexpr int kEfisRx = 9;    // DB-15 pin 2 → J1 pin 24 → SW1 → GPIO 9
#endif
constexpr int kEfisTx = -1;   // never transmit to the EFIS

// ---------------------------------------------------------------------------
// Boom serial (TTL)
//
// DB-15 pin 13 → ESP32 GPIO 3, direct TTL, no transceiver, no switch.
// Same on V4P and V4B.
//
// Note: GPIO 3 is the ESP32-S3's UART0 RX (RXD0) at boot. With
// ARDUINO_USB_CDC_ON_BOOT=0 + ARDUINO_USB_MODE=1 (set in platformio.ini),
// the framework's `Serial` object is USB-CDC (HWCDC over native USB-OTG),
// not UART0. UART0 / GPIO 3 is therefore free for Serial1's RX pin. If a
// future framework upgrade changes that default and Serial1 stops
// receiving boom bytes, the most likely cause is UART0 silently
// re-claiming GPIO 3 before Serial1.begin runs.
//
// DO NOT "fix" that by calling Serial.end() — on this hardware Serial is
// HWCDC, and HWCDC::end() deletes its internal tx_lock synchronously
// without waiting for in-flight HWCDC::write callers, producing a
// use-after-free that wedges the chip with no recovery path. See
// software/sketch_common/src/util/Helpers.cpp::_softRestart for the
// full explanation (PR #647 bench-bisected this on the reboot path).
//
// Correct resolutions if the re-claim ever fires: use Serial0.end()
// (which is the real UART0 handle, not HWCDC) or detach UART0 from
// GPIO 3 explicitly via the GPIO matrix.
// ---------------------------------------------------------------------------
constexpr int kBoomRx = 3;
constexpr int kBoomTx = -1;   // never transmit to the boom

// ---------------------------------------------------------------------------
// Display serial (TTL/RS-232, drives J1 pin 13 / DB-15 pin 12 via SW2 on V4B)
// ---------------------------------------------------------------------------
constexpr int kDisplayTx = 10;
constexpr int kDisplayRx = 11;   // normally unused

// ---------------------------------------------------------------------------
// Console UART (always builtin USB-CDC; baud rate only)
// ---------------------------------------------------------------------------
constexpr int kBaudConsole = 921600;

// ---------------------------------------------------------------------------
// Task timing (tied to hardware sampling rates)
// ---------------------------------------------------------------------------
// IMU rate is selected at boot from g_Config.iLogRate (see Globals.h
// for g_imuSampleRateHz and the setup() mapping):
//
//   iLogRate == 416  →  IMU at 416 Hz   (experimental opt-in)
//   iLogRate == 208  →  IMU at 208 Hz   (today's production behavior)
//   iLogRate == 50   →  IMU at 208 Hz   (50 Hz CSV cadence on a 208 Hz IMU,
//                                        same as production today)
//
// TRANSITIONAL POLICY. Until 416 Hz is validated in flight, picking it
// from the dropdown is the only way to bump the IMU above 208 Hz; the
// 50/208 selections preserve bit-identical production behavior. Once
// 416 validates we expect to flip IMU to always-416 and let iLogRate
// become a pure log-cadence selector. That flip needs #645 (LogProducerTask
// reading atomic AHRS/sensor snapshots) to land first so the
// "IMU 416 + log 208" combination has a clean producer path.
//
// EXPERIMENTAL — 416 Hz caveats (why it's gated behind a flight-test
// label even after the dropdown is in place):
//   - AHRS gains (Madgwick β, EKFQ process noise) are tuned for the
//     208 Hz dt. At 416 Hz the dt halves; filter behavior may differ
//     slightly from the validated baseline until follow-up retune
//     work (see #644) completes.
//   - Native test goldens (test_ahrs, test_ekfq_octave, regression
//     snapshot) are still on 208 Hz fixtures.
//   - SavGol derivative window sizes haven't been re-validated.
//
// What WAS proven on the bench (45 min synth+web stress at 416 Hz):
//   - SD writer captures 100% of samples (zero drops)
//   - Web latency stays under 5 sec p95 under aggressive concurrent
//     /api/logs + websocket load
//   - No panics, no WDT, no brownouts on bench power
//   - peak imu_lateMaxUsAT = 780 us (well under the 4.8 ms IMU period)
//
// 416 Hz is the next ODR step on the LSM6-class IMU (CTRL1_XL[3:0]=0110
// and CTRL2_G[3:0]=0110; see IMU330.cpp). 832 Hz is the step above
// that, also viable but not yet bench-validated.
constexpr int kImuSampleRateDefault       = 208;
constexpr int kImuSampleRateExperimental  = 416;

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

#endif  // HARDWARE_MAP_H
