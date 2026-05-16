# Gen3 hardware-test sketch — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a standalone PlatformIO env (`hardware_test`) plus a `.ino` sketch at `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` that exercises every peripheral on a Gen3 ESP32-S3 board, prints human-readable per-test output, and emits a single machine-parseable `RESULT,...` summary line per cycle.

**Architecture:** Standalone sketch in a sibling directory; `[env:hardware_test]` inherits from `[esp32s3_common]`; `scripts/hwtest_srcdir.py` redirects `PROJECT_SRC_DIR` to the hwtest folder so only the test compiles. Sketch links `onspeed_core` (for `CountsToPsi` and `BuildDisplayFrame`) and `version` (for `BuildInfo::version`). All pin defs come from `#include "HardwareMap.h"` — no duplication.

**Tech Stack:** Arduino-ESP32 framework (Core 3.x via pioarduino 55.03.35), ESP32-S3 board, SPI, I2S, SdFat, onspeed_core (C++17 platform-free library).

**Reference for verification:** Spec at `docs/superpowers/specs/2026-04-26-hardware-test-sketch-design.md`.

---

## File structure

| Path | Action | Purpose |
|------|--------|---------|
| `platformio.ini` | Modify | Add `[env:hardware_test]` and `[env:hardware_test-v4b]` envs at the bottom of the firmware envs block. |
| `scripts/hwtest_srcdir.py` | Create | Pre-build hook that overrides `PROJECT_SRC_DIR` to point at the hwtest sketch folder. |
| `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` | Create | The test sketch. |

**No** sketch-side firmware files are touched. **No** test files in `test/` are added (this is hardware-bound code; native unit tests aren't applicable).

---

## Task 1: Pre-build src_dir redirect script

**Files:**
- Create: `scripts/hwtest_srcdir.py`

- [ ] **Step 1: Create the script**

```python
"""Pre-build hook: override PROJECT_SRC_DIR for the hardware_test env.

PlatformIO's `src_dir` is a [platformio]-level setting and cannot be
overridden per-env. This script redirects PROJECT_SRC_DIR so the
hardware-test build compiles only the standalone .ino sketch in
software/OnSpeed-hardware-test/, not the full firmware tree under
software/OnSpeed-Gen3-ESP32/.

Pattern: identical to other PlatformIO projects that need per-env src
selection. Note that this also changes the include search root, so the
sketch's "#include" paths are relative to the hardware-test folder, not
the firmware sketch folder. We add the firmware folder to the include
path explicitly via build_flags in platformio.ini so the sketch can
include "HardwareMap.h" directly.
"""

Import("env")  # noqa: F821 (provided by SCons)

import os

hwtest_dir = os.path.join(env.subst("$PROJECT_DIR"), "software", "OnSpeed-hardware-test")
env.Replace(PROJECT_SRC_DIR=hwtest_dir)
```

- [ ] **Step 2: Commit**

```bash
git add scripts/hwtest_srcdir.py
git commit -m "$(cat <<'EOF'
hwtest: pre-build script to redirect PROJECT_SRC_DIR

Lets a single PlatformIO project host both the firmware (default
src_dir = software/OnSpeed-Gen3-ESP32) and a separate hardware-test
sketch at software/OnSpeed-hardware-test/. The hwtest env runs this
pre-script to point the build at the test folder.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Add `hardware_test` envs to platformio.ini

**Files:**
- Modify: `platformio.ini` (append after the existing `[env:esp32s3-v4b]` block, before `[env:native]`)

- [ ] **Step 1: Read the existing platformio.ini**

```bash
sed -n '110,140p' platformio.ini
```

Confirm the layout: `[env:esp32s3-v4p]` block ends near line 119, then `[env:esp32s3-v4b]` follows, then a `; =====` separator, then `[env:native]`.

- [ ] **Step 2: Insert two new env blocks before `[env:native]`**

Add immediately before `; =============================================================================` that introduces `[env:native]`:

```ini
; =============================================================================
; Hardware test environment — manufacturing & diagnostic verification sketch
; =============================================================================
; Self-contained sketch that exercises every peripheral on a Gen3 board
; and reports PASS/FAIL over USB serial (921600 baud). Inherits the
; same flash/USB/warning settings as the firmware so any drift from
; production binaries is caught at build time.
;
; Commands:
;   pio run -e hardware_test               - Build V4P hwtest
;   pio run -e hardware_test -t upload     - Build + flash V4P
;   pio run -e hardware_test-v4b           - Build V4B hwtest
;   pio device monitor                     - Monitor output (921600)
;
; Wiring for the optional serial-loopback test: jumper GPIO 10 (Display
; TX) to GPIO 3 (Boom RX). Without the jumper the test prompts the
; operator and skips on timeout — no false FAIL.
;
[env:hardware_test]
extends = esp32s3_common
extra_scripts =
    pre:scripts/generate_buildinfo.py
    pre:scripts/hwtest_srcdir.py
build_flags =
    ${esp32s3_common.build_flags}
    -DHW_V4P
lib_deps =
    ${esp32s3_common.lib_deps}
    version
    SdFat

[env:hardware_test-v4b]
extends = esp32s3_common
extra_scripts =
    pre:scripts/generate_buildinfo.py
    pre:scripts/hwtest_srcdir.py
build_flags =
    ${esp32s3_common.build_flags}
    -DHW_V4B
lib_deps =
    ${esp32s3_common.lib_deps}
    version
    SdFat

```

- [ ] **Step 3: Verify the file parses by listing envs**

Run:

```bash
pio project config 2>&1 | grep -E "^env:" | head -20
```

Expected output includes both `env:hardware_test` and `env:hardware_test-v4b` alongside the existing envs. If `pio` is not installed, skip — Task 5 will catch any parse error during the actual build.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini
git commit -m "$(cat <<'EOF'
hwtest: add hardware_test and hardware_test-v4b PIO envs

Both envs inherit from [esp32s3_common] so they pick up the same
flash mode, USB CDC, partition layout, and -Werror warning regime
as production firmware. Differ only in the -DHW_V4P / -DHW_V4B
hardware-variant flag. Both run the buildinfo generator (so the
test sketch can print its build version) and the src_dir redirect
script (so only the hwtest .ino compiles).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Sketch — banner, structure, pin includes, infrastructure

**Files:**
- Create: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino`

This task lays down the file with empty test stubs and a working `setup()` / `loop()` that prints the banner and the (currently all-MANUAL) `RESULT,...` line. Subsequent tasks fill in each test.

- [ ] **Step 1: Create the file with the scaffold**

```cpp
// =============================================================================
// OnSpeed Gen3 Hardware Test
// =============================================================================
//
// Manufacturing/diagnostic verification sketch. Exercises every peripheral
// on a Gen3 ESP32-S3 board and reports PASS/FAIL/SKIP/MANUAL over USB
// serial (921600 baud).
//
// Two intended uses:
//   1. Bench: run on freshly assembled boards before flashing production
//      firmware. Eyeball the per-test output, decide whether the box is good.
//   2. Field diagnostic: flash onto a customer's returned box to triage
//      a hardware issue. Safe on configured boxes — does not touch .cfg
//      files or logs.
//
// Pin definitions come from "HardwareMap.h" in the firmware sketch tree
// (single source of truth; no duplication). The hwtest env adds the
// firmware sketch directory to the include path explicitly so this
// include resolves.
//
// Build & flash:
//   pio run -e hardware_test               - V4P
//   pio run -e hardware_test-v4b           - V4B
//   pio run -e hardware_test -t upload
//   pio device monitor                     - 921600 baud
//
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#define DISABLE_FS_H_WARNING
#include <SdFat.h>
#include <ESP_I2S.h>

#include "HardwareMap.h"
#include <buildinfo.h>
#include <proto/DisplaySerial.h>
#include <sensors/PressureConvert.h>

// -----------------------------------------------------------------------------
// Test result enum + per-test slots used by the summary line
// -----------------------------------------------------------------------------

enum class TestResult : uint8_t { PASS, FAIL, SKIP, MANUAL };

static const char* ResultStr(TestResult r) {
    switch (r) {
        case TestResult::PASS:   return "PASS";
        case TestResult::FAIL:   return "FAIL";
        case TestResult::SKIP:   return "SKIP";
        case TestResult::MANUAL: return "MANUAL";
    }
    return "?";
}

struct CycleResults {
    TestResult pressure = TestResult::SKIP;
    TestResult imu      = TestResult::SKIP;
    TestResult adc      = TestResult::SKIP;
    TestResult loopback = TestResult::SKIP;
    TestResult sd       = TestResult::SKIP;
    TestResult audio    = TestResult::MANUAL;
    TestResult led      = TestResult::MANUAL;
    TestResult m5       = TestResult::MANUAL;
};

// -----------------------------------------------------------------------------
// Shared SPI bus for sensors (pressure, IMU, MCP3202)
// -----------------------------------------------------------------------------

static SPIClass sensorSPI(FSPI);

// -----------------------------------------------------------------------------
// Test stubs — filled in by subsequent tasks
// -----------------------------------------------------------------------------

static TestResult pressureTest()        { return TestResult::SKIP; }
static TestResult imuTest()             { return TestResult::SKIP; }
static TestResult adcTest()             { return TestResult::SKIP; }
static TestResult serialLoopbackTest()  { return TestResult::SKIP; }
static TestResult sdCardTest()          { return TestResult::SKIP; }
static void       audioTest()           { Serial.println("\n[Audio] (stub)"); }
static void       ledTest()             { Serial.println("\n[LED] (stub)"); }
static void       m5DisplayWireTest()   { Serial.println("\n[M5] (stub)"); }

// -----------------------------------------------------------------------------
// setup() — runs once. Prints banner, initializes shared peripherals.
// -----------------------------------------------------------------------------

void setup() {
    delay(1000);              // let USB enumerate
    Serial.begin(921600);
    delay(500);

    Serial.println();
    Serial.println("=======================================");
    Serial.println("  OnSpeed Gen3 Hardware Test");
    Serial.printf( "  Build: %s (%s)\n",
                   BuildInfo::version, BuildInfo::buildDate);
#ifdef HW_V4P
    Serial.println("  Hardware variant: V4P");
#else
    Serial.println("  Hardware variant: V4B");
#endif
    Serial.println("=======================================");

    // Initialize all chip select pins HIGH (deselected) before starting SPI.
    pinMode(kCsImu,    OUTPUT); digitalWrite(kCsImu,    HIGH);
    pinMode(kCsStatic, OUTPUT); digitalWrite(kCsStatic, HIGH);
    pinMode(kCsAoa,    OUTPUT); digitalWrite(kCsAoa,    HIGH);
    pinMode(kCsPitot,  OUTPUT); digitalWrite(kCsPitot,  HIGH);
    pinMode(kSdCs,     OUTPUT); digitalWrite(kSdCs,     HIGH);
    if constexpr (kHasExternalMcp3202) {
        pinMode(kCsAdc, OUTPUT); digitalWrite(kCsAdc, HIGH);
    }

    sensorSPI.begin(kSensorSclk, kSensorMiso, kSensorMosi);

    pinMode(kPinSwitch, INPUT_PULLUP);
    ledcAttach(kPinLedKnob, 5000, 8);
}

// -----------------------------------------------------------------------------
// loop() — runs the full test sequence on a 10-second cycle.
// -----------------------------------------------------------------------------

static void printSummary(const CycleResults& r) {
    bool overallOk =
        (r.pressure == TestResult::PASS) &&
        (r.imu      == TestResult::PASS) &&
        (r.adc      == TestResult::PASS) &&
        (r.sd       == TestResult::PASS) &&
        (r.loopback != TestResult::FAIL);     // SKIP and PASS both OK

    Serial.println("\n=======================================");
    Serial.printf("  Pressure: %s   IMU: %s   ADC: %s\n",
                  ResultStr(r.pressure), ResultStr(r.imu), ResultStr(r.adc));
    Serial.printf("  Loopback: %s   SD: %s\n",
                  ResultStr(r.loopback), ResultStr(r.sd));
    Serial.println("  Verify audio L/R, LED ramp, and M5 attitude/AOA manually.");
    Serial.printf("  >>> OVERALL: %s <<<\n", overallOk ? "PASS" : "FAIL");
    Serial.println("=======================================");

    // Single machine-parseable summary line — last line of each cycle.
    const char* hwStr =
#ifdef HW_V4P
        "V4P";
#else
        "V4B";
#endif
    Serial.printf(
        "RESULT,fw=%s,hw=%s,pressure=%s,imu=%s,adc=%s,loopback=%s,sd=%s,"
        "audio=%s,led=%s,m5=%s,overall=%s\n",
        BuildInfo::gitShortSha, hwStr,
        ResultStr(r.pressure), ResultStr(r.imu), ResultStr(r.adc),
        ResultStr(r.loopback), ResultStr(r.sd),
        ResultStr(r.audio), ResultStr(r.led), ResultStr(r.m5),
        overallOk ? "PASS" : "FAIL");
}

void loop() {
    Serial.println("\n=======================================");
    Serial.println("Starting hardware tests...");
    Serial.println("=======================================");

    // Switch state is informational only.
    Serial.printf("\n[Switch] GPIO %d = %s\n",
                  kPinSwitch,
                  digitalRead(kPinSwitch) == LOW ? "PRESSED" : "RELEASED");

    // LED on as a "testing in progress" indicator.
    ledcWrite(kPinLedKnob, 200);

    CycleResults r;
    r.pressure = pressureTest();
    r.imu      = imuTest();
    r.adc      = adcTest();
    r.loopback = serialLoopbackTest();
    r.sd       = sdCardTest();

    audioTest();
    ledTest();
    m5DisplayWireTest();

    ledcWrite(kPinLedKnob, 0);

    printSummary(r);

    delay(10000);
}
```

- [ ] **Step 2: Build the V4P env to confirm the scaffold compiles cleanly**

Run:

```bash
pio run -e hardware_test 2>&1 | tail -20
```

Expected: clean build, ends with `========================== [SUCCESS] ==========================` and a flash/RAM-usage summary. If the build fails, stop and address the error before moving to Task 4.

Common error: `'HardwareMap.h' file not found` — this means `[esp32s3_common]`'s `-Isoftware/OnSpeed-Gen3-ESP32` flag was not inherited. Verify the `extends = esp32s3_common` line in Task 2 didn't drop `${esp32s3_common.build_flags}`.

- [ ] **Step 3: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: scaffold sketch with banner, summary, and test stubs

setup() prints version banner and inits shared SPI bus + GPIOs from
HardwareMap.h. loop() runs the test sequence on a 10s cycle, prints
human-readable per-test output, and ends with a single-line
RESULT,fw=...,overall=... summary suitable for grep / paste.

All tests are stubs that return SKIP / MANUAL. Subsequent commits
fill in pressure, IMU, ADC, loopback, SD, audio, LED, M5 wire.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Pressure sensor test

**Files:**
- Modify: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` — replace the `pressureTest()` stub.

The HSC sensor returns 16 bits over SPI: top 2 bits are status (0 = normal), bottom 14 bits are counts. PASS criteria: status == 0 AND counts in (1638, 14745) — i.e., not saturated low or high. We use `onspeed::sensors::CountsToPsi` for the conversion, mirroring the firmware's path (firmware also calls this helper).

- [ ] **Step 1: Add SPI helpers near the top of the file (after the `sensorSPI` declaration)**

```cpp
// SPI bus speed for sensor reads. Conservative 1 MHz works for all parts
// (firmware uses 4 MHz for HSC/IMU and 1 MHz for MCP3202). For a hwtest
// where speed doesn't matter, 1 MHz everywhere keeps the code simple
// and avoids any signal-integrity surprises on a freshly assembled
// board where wire lengths and CS routing haven't been validated.
static constexpr uint32_t kHwtestSpiClkHz = 1'000'000;

// Read N bytes from a device that does not use a register address byte
// (this is the HSC pressure sensor's protocol — chip select low, clock
// in N bytes, chip select high).
static void spiReadBytes(int cs, uint8_t* buf, int len) {
    sensorSPI.beginTransaction(SPISettings(kHwtestSpiClkHz, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    for (int i = 0; i < len; i++) buf[i] = sensorSPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
}
```

- [ ] **Step 2: Replace the `pressureTest` stub**

```cpp
// HSC pressure-sensor read. Returns {status (top 2 bits), counts (bottom
// 14 bits)} — matches the bitfield in firmware HscPressureSensor.h.
struct HscReading {
    uint8_t  status;
    uint16_t counts;
};

static HscReading readHsc(int cs) {
    uint8_t b[2];
    spiReadBytes(cs, b, 2);
    uint16_t raw = (uint16_t(b[0]) << 8) | b[1];
    return { uint8_t((raw >> 14) & 0x03), uint16_t(raw & 0x3FFF) };
}

// HSC sensor configurations — match firmware drivers/HscPressureSensor.cpp.
// Differential parts (Pitot, AOA) are HSCMRRN001PDSA3 (±1 PSI).
// Absolute part (Static) is HSCMRNN1.6BASA3 (0–23.2 PSI).
static constexpr onspeed::sensors::HscRange kHscDiff{
    /*countsMin=*/1638u, /*countsMax=*/14745u,
    /*psiMin=*/-1.0f,    /*psiMax=*/+1.0f
};
static constexpr onspeed::sensors::HscRange kHscAbs{
    /*countsMin=*/1638u, /*countsMax=*/14745u,
    /*psiMin=*/0.0f,     /*psiMax=*/23.2f
};

static bool reportOnePressure(const char* label, int cs,
                              const onspeed::sensors::HscRange& range,
                              bool showMillibars) {
    HscReading r = readHsc(cs);
    auto psiOpt  = onspeed::sensors::CountsToPsi(r.counts, range);
    bool pass    = (r.status == 0) && psiOpt.has_value();

    if (psiOpt.has_value() && showMillibars) {
        float psi = *psiOpt;
        float mb  = psi * 68.947572932f;
        Serial.printf("  %-7s %s  status=%u  counts=%-5u  %+6.3f PSI  %.1f mb\n",
                      label, pass ? "PASS" : "FAIL", r.status, r.counts, psi, mb);
    } else if (psiOpt.has_value()) {
        Serial.printf("  %-7s %s  status=%u  counts=%-5u  %+6.3f PSI\n",
                      label, pass ? "PASS" : "FAIL", r.status, r.counts, *psiOpt);
    } else {
        Serial.printf("  %-7s FAIL  status=%u  counts=%-5u  (saturated/disconnected)\n",
                      label, r.status, r.counts);
    }
    return pass;
}

static TestResult pressureTest() {
    Serial.println("\n[Pressure Sensors]");
    bool ok = true;
    ok &= reportOnePressure("Pitot:",  kCsPitot,  kHscDiff, false);
    ok &= reportOnePressure("AOA:",    kCsAoa,    kHscDiff, false);
    ok &= reportOnePressure("Static:", kCsStatic, kHscAbs,  true);
    return ok ? TestResult::PASS : TestResult::FAIL;
}
```

- [ ] **Step 3: Build and confirm clean compile**

Run:

```bash
pio run -e hardware_test 2>&1 | tail -10
```

Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: pressure-sensor test (Pitot, AOA, Static)

Reads each HSC sensor's 16-bit packet, splits into 2-bit status + 14-bit
counts (matches firmware HscPressureSensor.h bitfield), passes counts
through onspeed_core's CountsToPsi (so saturated/disconnected sensors
are detected). PASS iff status == 0 and counts within sensor's valid
output window. Static reading also reported in millibars.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: IMU test

**Files:**
- Modify: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` — replace `imuTest()` stub, add IMU register helpers.

Init sequence and full-scale constants are copied from `src/drivers/IMU330.cpp` exactly. Critical: gyro full-scale is **250 dps**, not 245 (PR #316 fixed this; do not regress).

- [ ] **Step 1: Add IMU register defs and SPI register helpers (place above pressure helpers)**

```cpp
// IMU330 (ISM330DHCX / LSM6DSO-family) register definitions.
// Mirror src/drivers/IMU330.cpp — keep in sync with that file.
namespace imu {
    constexpr uint8_t kWhoAmI     = 0x0F;  // expected 0x6B (ISM330) or 0x6C (LSM6DSO)
    constexpr uint8_t kCtrl1Xl    = 0x10;
    constexpr uint8_t kCtrl2G     = 0x11;
    constexpr uint8_t kCtrl3C     = 0x12;
    constexpr uint8_t kCtrl4C     = 0x13;
    constexpr uint8_t kCtrl7G     = 0x16;
    constexpr uint8_t kCtrl9Xl    = 0x18;
    constexpr uint8_t kFifoCtrl4  = 0x0A;
    constexpr uint8_t kOutXLG     = 0x22;
    constexpr uint8_t kOutXLA     = 0x28;

    constexpr uint8_t Read (uint8_t r) { return uint8_t(0x80 | r); }
    constexpr uint8_t Write(uint8_t r) { return uint8_t(0x7F & r); }

    // Full-scale constants — match firmware GYRO_RES / ACCEL_RES exactly.
    // Gyro is 250 dps at FS_G=00 (per ISM330DHCX datasheet); do not use
    // 245 — PR #316 fixed that drift, this sketch must agree.
    constexpr float kAccelRes = 8.0f / 32768.0f;     // ±8 g full-scale
    constexpr float kGyroRes  = 250.0f / 32768.0f;   // 250 dps full-scale
}

// Read a single register byte from a register-addressed SPI device.
static uint8_t spiReadReg(int cs, uint8_t reg) {
    sensorSPI.beginTransaction(SPISettings(kHwtestSpiClkHz, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    sensorSPI.transfer(reg);
    uint8_t v = sensorSPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
    return v;
}

// Write one register byte.
static void spiWriteReg(int cs, uint8_t reg, uint8_t v) {
    sensorSPI.beginTransaction(SPISettings(kHwtestSpiClkHz, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    sensorSPI.transfer(reg);
    sensorSPI.transfer(v);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
}

// Burst-read N register bytes starting at `reg`.
static void spiReadRegs(int cs, uint8_t reg, uint8_t* buf, int len) {
    sensorSPI.beginTransaction(SPISettings(kHwtestSpiClkHz, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    sensorSPI.transfer(reg);
    for (int i = 0; i < len; i++) buf[i] = sensorSPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
}
```

- [ ] **Step 2: Replace the `imuTest` stub**

```cpp
// IMU init sequence — bytes mirror src/drivers/IMU330.cpp::Init().
// Soft-reset, set ODR/full-scale, disable filters we don't need, bypass FIFO.
static void imuInit() {
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl3C), 0b00000101); delay(100);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl3C), 0b00000100); delay(100);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl9Xl),    0b11100010); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl1Xl),    0b01011100); delay(50); // 208 Hz, ±8 g
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl2G),     0b01010000); delay(50); // 208 Hz, 250 dps
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl7G),     0b00000000); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl4C),     0b00000100); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kFifoCtrl4),  0b00010000); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl9Xl),    0b11100000); delay(50);
}

static void imuReadAccelGyro(float a[3], float g[3]) {
    uint8_t b[6];
    spiReadRegs(kCsImu, imu::Read(imu::kOutXLA), b, 6);
    a[0] = int16_t((b[1] << 8) | b[0]) * imu::kAccelRes;
    a[1] = int16_t((b[3] << 8) | b[2]) * imu::kAccelRes;
    a[2] = int16_t((b[5] << 8) | b[4]) * imu::kAccelRes;
    spiReadRegs(kCsImu, imu::Read(imu::kOutXLG), b, 6);
    g[0] = int16_t((b[1] << 8) | b[0]) * imu::kGyroRes;
    g[1] = int16_t((b[3] << 8) | b[2]) * imu::kGyroRes;
    g[2] = int16_t((b[5] << 8) | b[4]) * imu::kGyroRes;
}

static TestResult imuTest() {
    Serial.println("\n[IMU]");
    uint8_t whoami = spiReadReg(kCsImu, imu::Read(imu::kWhoAmI));
    bool known = (whoami == 0x6B) || (whoami == 0x6C);
    if (!known) {
        Serial.printf("  IMU:    FAIL  WHO_AM_I=0x%02X (expected 0x6B or 0x6C)\n", whoami);
        return TestResult::FAIL;
    }

    imuInit();
    delay(100);

    float a[3], g[3];
    imuReadAccelGyro(a, g);

    Serial.printf("  IMU:    PASS  WHO_AM_I=0x%02X\n", whoami);
    Serial.printf("          Accel: X=%+6.3f  Y=%+6.3f  Z=%+6.3f g\n",   a[0], a[1], a[2]);
    Serial.printf("          Gyro:  X=%+6.2f  Y=%+6.2f  Z=%+6.2f dps\n", g[0], g[1], g[2]);
    return TestResult::PASS;
}
```

- [ ] **Step 3: Build**

```bash
pio run -e hardware_test 2>&1 | tail -10
```

Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: IMU test (WHO_AM_I + accel/gyro readback)

Init sequence and full-scale constants mirror src/drivers/IMU330.cpp
exactly. Gyro full-scale is 250 dps (post-#316), not 245 — keeping
this sketch in agreement with the firmware so a tech reading the
two values side-by-side sees the same numbers.

PASS iff WHO_AM_I matches 0x6B (ISM330DHCX) or 0x6C (LSM6DSO-family).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: MCP3202 ADC test (V4P only)

**Files:**
- Modify: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` — replace `adcTest()` stub.

Use the **MCP3202** protocol (`0x01` start byte, `0xA0 | (channel << 6)` config byte, 3-byte transaction) — copied from `src/drivers/Mcp3202Adc.cpp`. **Not** the MCP3204 protocol the original PR used. Channels: 0 = Flap, 1 = Volume (matches `kAdcChFlap`/`kAdcChVolume` in `HardwareMap.h`).

- [ ] **Step 1: Replace the `adcTest` stub**

```cpp
#ifdef HW_V4P
// MCP3202 protocol — mirrors src/drivers/Mcp3202Adc.cpp::Mcp3202Read.
// 3-byte SPI transaction:
//   byte 0: 0x01 (start bit)
//   byte 1: 0xA0 | (channel << 6) — SGL=1, ODD/SIGN=channel, MSBF=1
//   byte 2: 0x00 — clock out remaining 8 data bits
// Returns 12-bit unsigned value 0..4095.
static uint16_t mcp3202Read(uint8_t channel) {
    channel &= 0x01;
    const uint8_t configByte = uint8_t(0xA0 | (channel << 6));

    sensorSPI.beginTransaction(SPISettings(kHwtestSpiClkHz, MSBFIRST, SPI_MODE0));
    digitalWrite(kCsAdc, LOW);
    (void)sensorSPI.transfer(0x01);
    uint8_t hi = sensorSPI.transfer(configByte);
    uint8_t lo = sensorSPI.transfer(0x00);
    digitalWrite(kCsAdc, HIGH);
    sensorSPI.endTransaction();

    return uint16_t(((hi & 0x0F) << 8) | lo);
}
#endif

static TestResult adcTest() {
    Serial.println("\n[ADC / Pots]");
#ifdef HW_V4P
    // Channel constants from HardwareMap.h: kAdcChFlap=0, kAdcChVolume=1.
    uint16_t flap = mcp3202Read(kAdcChFlap);
    uint16_t vol  = mcp3202Read(kAdcChVolume);
    Serial.printf("  MCP3202 Flap   (ch%d): %u / 4095\n", kAdcChFlap, flap);
    Serial.printf("  MCP3202 Volume (ch%d): %u / 4095\n", kAdcChVolume, vol);

    // Sanity: 0 or 4095 means stuck rail (open input or shorted). Either pot
    // mid-travel during test would land somewhere in (0, 4095).
    bool flapOk = (flap > 0u && flap < 4095u);
    bool volOk  = (vol  > 0u && vol  < 4095u);
    if (!flapOk) Serial.println("  WARNING: Flap reads at rail — check pot wiring");
    if (!volOk)  Serial.println("  WARNING: Volume reads at rail — check pot wiring");
    return (flapOk && volOk) ? TestResult::PASS : TestResult::FAIL;
#else
    // V4B uses the ESP32's internal ADC. analogRead always returns
    // *something*, so this is informational only.
    uint16_t flap = analogRead(kPinFlap);
    uint16_t vol  = analogRead(kPinVolume);
    Serial.printf("  ESP32 ADC Flap   (pin %d): %u\n", kPinFlap,   flap);
    Serial.printf("  ESP32 ADC Volume (pin %d): %u\n", kPinVolume, vol);
    return TestResult::PASS;
#endif
}
```

- [ ] **Step 2: Build both V4P and V4B to make sure both compile**

```bash
pio run -e hardware_test 2>&1 | tail -5
pio run -e hardware_test-v4b 2>&1 | tail -5
```

Both expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: MCP3202 ADC test for flap/volume pots (V4P)

3-byte SPI transaction matches src/drivers/Mcp3202Adc.cpp byte-for-byte
(start 0x01, config 0xA0 | (ch<<6), then clock out). Channel 0 = Flap,
channel 1 = Volume (matches kAdcChFlap/kAdcChVolume in HardwareMap.h).

PASS if both reads land strictly within (0, 4095) — a value at a rail
means the pot wiper is open or shorted to a supply.

V4B falls back to the internal ESP32 ADC (informational only).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Serial loopback test (PROMPT, default-skip)

**Files:**
- Modify: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` — replace `serialLoopbackTest()` stub.

Asks the operator if a jumper is installed. If yes, runs the loopback. If no/timeout, SKIP. The 5-second timeout means the sketch never hangs on an unattended box.

- [ ] **Step 1: Add a small input helper (place above the test stubs)**

```cpp
// Read a single character from USB serial with a millisecond timeout.
// Returns -1 on timeout. Echoes received chars so the operator can see
// what they typed.
static int readCharWithTimeout(uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        if (Serial.available()) {
            int c = Serial.read();
            if (c >= 0x20 && c <= 0x7E) Serial.write((uint8_t)c);   // echo printable
            return c;
        }
        delay(10);
    }
    return -1;
}
```

- [ ] **Step 2: Replace the `serialLoopbackTest` stub**

```cpp
static TestResult serialLoopbackTest() {
    Serial.println("\n[Serial Loopback]");
    Serial.print("  Is the GPIO 10 -> GPIO 3 jumper installed? [y/n, 5s default=skip]: ");
    int ch = readCharWithTimeout(5000);
    Serial.println();

    if (ch != 'y' && ch != 'Y') {
        Serial.println("  Serial1 loopback: SKIP");
        return TestResult::SKIP;
    }

    // Serial1 with TX=kDisplayTx (10), RX=kBoomRx (3).
    Serial1.begin(115200, SERIAL_8N1, kBoomRx, kDisplayTx);
    delay(50);
    while (Serial1.available()) Serial1.read();   // flush stale bytes

    const char* probe = "HWTEST\n";
    Serial1.print(probe);
    Serial1.flush();
    delay(100);

    String received;
    uint32_t deadline = millis() + 200;
    while (millis() < deadline) {
        while (Serial1.available()) received += char(Serial1.read());
        if (received.indexOf("HWTEST") >= 0) break;
        delay(10);
    }
    Serial1.end();

    bool pass = (received.indexOf("HWTEST") >= 0);
    Serial.printf("  Serial1 loopback (TX=%d -> RX=%d): %s\n",
                  kDisplayTx, kBoomRx, pass ? "PASS" : "FAIL");
    if (!pass) {
        Serial.printf("  (Received: \"%s\")\n", received.c_str());
        Serial.println("  Check jumper wire between GPIO 10 and GPIO 3.");
    }
    return pass ? TestResult::PASS : TestResult::FAIL;
}
```

- [ ] **Step 3: Build**

```bash
pio run -e hardware_test 2>&1 | tail -5
```

Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: serial loopback PROMPT (jumper required, default-skip)

Asks the operator at runtime whether the GPIO 10 -> GPIO 3 jumper is
installed. 5-second timeout returns SKIP, not FAIL — keeps the sketch
safe to flash on a customer's returned box for field diagnostics, where
no jumper is present and a FAIL would be a false negative.

If jumper-y, sends "HWTEST\n" out Display TX (Serial1) and waits up to
200 ms for it to roundtrip on Boom RX. PASS on receipt.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: SD card test

**Files:**
- Modify: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` — replace `sdCardTest()` stub.

Init at the SD-card SPI bus, log the card size, write+delete `_hwtest.tmp` at the filesystem root. **Important:** does not touch `.cfg`, log files, or any other content — safe to run on a configured customer box.

- [ ] **Step 1: Replace the `sdCardTest` stub**

```cpp
static TestResult sdCardTest() {
    Serial.println("\n[SD Card]");

    SPIClass sdSPI(HSPI);
    sdSPI.begin(kSdSclk, kSdMiso, kSdMosi, kSdCs);

    SdFat sd;
    SdSpiConfig spiConfig(kSdCs, DEDICATED_SPI, SD_SCK_MHZ(10), &sdSPI);

    if (!sd.begin(spiConfig)) {
        Serial.println("  SD Card: FAIL (cannot initialize — no card or wrong CS)");
        sdSPI.end();
        return TestResult::FAIL;
    }

    uint32_t sizeMB = uint32_t(sd.card()->sectorCount() / 2048);
    Serial.printf("  SD Card: PASS  size=%lu MB\n", (unsigned long)sizeMB);

    // Write + delete a small temp file. Filename starts with "_" so it
    // sorts to the top in directory listings; it doesn't collide with
    // any firmware-written log or config name.
    FsFile f = sd.open("_hwtest.tmp", O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) {
        Serial.println("  SD Card: write FAIL (could not create file)");
        sdSPI.end();
        return TestResult::FAIL;
    }
    f.println("OnSpeed hardware test — safe to delete");
    f.close();
    sd.remove("_hwtest.tmp");
    Serial.println("  SD Card: write/delete PASS");

    sdSPI.end();
    return TestResult::PASS;
}
```

- [ ] **Step 2: Build**

```bash
pio run -e hardware_test 2>&1 | tail -5
```

Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: SD card init + write/delete probe

Inits SdFat at the SD-card SPI bus, reports card size, then creates and
removes _hwtest.tmp at the filesystem root. Filename leads with "_" so
it sorts to the top and doesn't collide with any firmware-written log
or config name — safe on a configured customer box.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Audio + LED tests (MANUAL)

**Files:**
- Modify: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` — replace `audioTest()` and `ledTest()` stubs.

These are unchanged in spirit from the original PR — operator listens for tones and watches the LED.

- [ ] **Step 1: Add audio constants near the top of the file**

```cpp
static constexpr int kAudioSampleRate = 16000;
static constexpr int kToneBufLen      = kAudioSampleRate / 10;   // 100 ms of audio
```

- [ ] **Step 2: Replace `audioTest` and `ledTest`**

```cpp
static void audioTest() {
    Serial.println("\n[Audio - listen for L/R tones]");

    I2SClass i2s;
    i2s.setPins(kI2sBck, kI2sLrck, kI2sDout);
    if (!i2s.begin(I2S_MODE_STD, kAudioSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        Serial.println("  Audio: FAIL (I2S init failed)");
        return;
    }

    int16_t tone400 [kToneBufLen];
    int16_t tone1600[kToneBufLen];
    for (int i = 0; i < kToneBufLen; i++) {
        tone400 [i] = int16_t(25000.0f * cosf(2.0f * float(M_PI) * i *  400.0f / kAudioSampleRate));
        tone1600[i] = int16_t(25000.0f * cosf(2.0f * float(M_PI) * i * 1600.0f / kAudioSampleRate));
    }

    auto writeTone = [&](const int16_t* tone, float lGain, float rGain) {
        for (int i = 0; i < kToneBufLen; i++) {
            int16_t left  = int16_t(tone[i] * lGain);
            int16_t right = int16_t(tone[i] * rGain);
            uint32_t frame = uint16_t(left) | (uint32_t(uint16_t(right)) << 16);
            i2s.write((const uint8_t*)&frame, sizeof(frame));
        }
    };

    Serial.println("  400 Hz LEFT only (1.5 s)...");
    for (int rep = 0; rep < 15; rep++) writeTone(tone400, 1.0f, 0.0f);

    delay(200);

    Serial.println("  1600 Hz RIGHT only (1.5 s)...");
    for (int rep = 0; rep < 15; rep++) writeTone(tone1600, 0.0f, 1.0f);

    i2s.end();
    Serial.println("  Audio: done — verify L=400 Hz, R=1600 Hz by ear.");
}

static void ledTest() {
    Serial.println("\n[LED - watch the knob LED]");
    for (int duty = 0;   duty <= 255; duty++) { ledcWrite(kPinLedKnob, duty); delay(4); }
    for (int duty = 255; duty >= 0;   duty--) { ledcWrite(kPinLedKnob, duty); delay(4); }
    Serial.println("  LED: ramp complete.");
}
```

- [ ] **Step 3: Build**

```bash
pio run -e hardware_test 2>&1 | tail -5
```

Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: audio + LED MANUAL checks

Audio plays 1.5s 400 Hz on left channel only, then 1.5s 1600 Hz on right
channel only. Operator listens. LED ramps PWM 0->255->0 on the knob LED;
operator watches.

Both tests are MANUAL — no programmatic pass/fail since neither has a
sense path back to the MCU.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: M5 display wire test (MANUAL, sweeping frames)

**Files:**
- Modify: `software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino` — replace `m5DisplayWireTest()` stub.

Stream 10 seconds of valid `#1` frames at 20 Hz (200 frames) on Display TX (`kDisplayTx`) at 115200 baud. Pitch sweeps −10°→+10°, AOA `percentLift` ramps 30→90 so the M5's chevron rises through OnSpeed/StallWarn bands. The operator confirms the M5 wakes and the indicators move.

- [ ] **Step 1: Replace the `m5DisplayWireTest` stub**

```cpp
static void m5DisplayWireTest() {
    Serial.println("\n[M5 Display Wire]");
    Serial.println("  Streaming 200 #1 frames over Display TX at 20 Hz...");
    Serial.println("  M5 should show pitch sweep -10..+10 deg and AOA chevron rising.");

    Serial1.begin(115200, SERIAL_8N1, kBoomRx, kDisplayTx);
    delay(50);

    constexpr int kFrames = 200;
    constexpr int kPeriodMs = onspeed::proto::kDisplayFramePeriodMs;   // 50 ms

    for (int i = 0; i < kFrames; i++) {
        // Sweep pitch -10..+10 over the 200 frames (full sweep in 10 s).
        float t  = float(i) / float(kFrames - 1);     // 0..1
        float pitch = -10.0f + 20.0f * t;             // -10..+10 deg

        // Ramp percentLift 30..90 so the chevron rises across the band
        // anchors (OnSpeedFast~50, OnSpeedSlow~70, StallWarn~85).
        int pct = int(30.0f + 60.0f * t);

        onspeed::proto::DisplayBuildInputs in;
        in.pitchDeg            = pitch;
        in.rollDeg             = 0.0f;
        in.iasKt               = 80.0f;
        in.paltFt              = 1000.0f;
        in.percentLift         = pct;
        in.tonesOnPctLift      = 35;
        in.onSpeedFastPctLift  = 50;
        in.onSpeedSlowPctLift  = 70;
        in.stallWarnPctLift    = 85;
        in.flapsDeg            = 0;
        in.flapsMinDeg         = 0;
        in.flapsMaxDeg         = 30;

        uint8_t buf[onspeed::proto::kDisplayFrameSizeBytes];
        size_t n = onspeed::proto::BuildDisplayFrame(in, buf, sizeof(buf));
        if (n == onspeed::proto::kDisplayFrameSizeBytes) {
            Serial1.write(buf, n);
        }
        Serial1.flush();
        delay(kPeriodMs);
    }

    Serial1.end();
    Serial.println("  M5 wire test: done — verify M5 attitude moved and chevron rose.");
}
```

- [ ] **Step 2: Build both envs**

```bash
pio run -e hardware_test 2>&1 | tail -5
pio run -e hardware_test-v4b 2>&1 | tail -5
```

Both expected: SUCCESS.

- [ ] **Step 3: Final binary-size check**

```bash
pio run -e hardware_test 2>&1 | grep -E "(RAM|Flash):"
```

Expected: under ~600 KB flash and under ~100 KB RAM. Actual numbers should be reported in the PR description so reviewers see this is a small standalone binary, not pulling in the full firmware tree.

- [ ] **Step 4: Commit**

```bash
git add software/OnSpeed-hardware-test/OnSpeed-hardware-test.ino
git commit -m "$(cat <<'EOF'
hwtest: M5 display wire test (10 s sweeping frames)

Streams 200 valid #1 DisplayFrame frames at 20 Hz on Display TX
(kDisplayTx, 115200 8N1). Pitch sweeps -10..+10 over the 10 s window;
percentLift ramps 30..90 so the M5's AOA chevron rises through
OnSpeedFast / OnSpeedSlow / StallWarn band anchors.

Frames built via onspeed_core's BuildDisplayFrame, so any future
wire-format change rebuilds the hwtest — no silent drift between
the box and the M5.

The operator confirms the M5 wakes, the attitude indicator moves, and
the chevron rises. No programmatic check (M5 is one-way over the wire
from Gen3's perspective).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Verify, push, open PR

- [ ] **Step 1: Final-build sanity check on both envs and confirm full firmware still builds**

```bash
pio run -e hardware_test     2>&1 | tail -3
pio run -e hardware_test-v4b 2>&1 | tail -3
pio run -e esp32s3-v4p       2>&1 | tail -3
pio test -e native           2>&1 | tail -3
```

All four expected: SUCCESS / passing tests. The full-firmware and native-test runs prove this PR is additive — no shared file was modified.

- [ ] **Step 2: Push the branch**

```bash
git push -u origin sritchie/hwtest-rebuild
```

- [ ] **Step 3: Close PR #43 and open the new PR with `--head sritchie/hwtest-rebuild`**

PR title: `Add Gen3 hardware-test sketch (rebuild from master, supersedes #43)`

PR body — see the "PR description" section at the bottom of this plan.

```bash
# Close #43 with a comment pointing at the new PR (don't open the new
# PR first if we want #43 closed automatically by a "supersedes" link;
# safer to: open new PR, then close #43 with a manual comment).
gh pr create --head sritchie/hwtest-rebuild --title "..." --body "$(cat <<'EOF'
...PR body...
EOF
)"
gh pr comment 43 --body "Superseded by #<new-pr-number> — closing in favor of a clean rebuild from current master."
gh pr close 43
```

---

## PR description (for Task 11)

```markdown
## Add Gen3 hardware-test sketch (rebuild from master)

Standalone PlatformIO env + sketch that exercises every peripheral on a
Gen3 ESP32-S3 board and reports PASS/FAIL/SKIP/MANUAL per test plus a
single machine-parseable `RESULT,...` summary line. Intended for two
uses:

- **Bench**: run on freshly assembled boards before flashing production
  firmware. Eyeball the output, decide whether the box is good.
- **Field diagnostic**: flash onto a customer's returned box to triage
  hardware issues. Safe on configured boxes — does not touch `.cfg` or
  log files.

Supersedes #43, which targeted a Feb-18 master that has since been
heavily refactored. Concrete drift fixed in this rebuild:

- Picks up `HardwareMap.h` directly (PR #43's pin defs were copies of
  the now-deleted `Globals.h`)
- MCP3202 SPI protocol matches `src/drivers/Mcp3202Adc.cpp` (PR #43
  used MCP3204 protocol — wrong chip)
- ADC channel assignment matches `kAdcChFlap=0`/`kAdcChVolume=1` (PR
  #43 had them swapped)
- Gyro full-scale at 250 dps (PR #316 fix; PR #43 used 245)
- Pressure-sensor counts window matches HSC 14-bit packet (PR #43
  treated it as 12-bit, off by 4×)
- EFIS RX correctly at GPIO 11 (PR #43 had GPIO 9)
- Inherits `[esp32s3_common]` so flash mode, USB CDC, partition, and
  warning regime match production firmware

### What it tests

| # | Test | Class | What it verifies |
|---|------|-------|------------------|
| 1 | Pressure (Pitot, AOA, Static) | AUTO | SPI read; status bits; counts in valid HSC window. Uses `onspeed::sensors::CountsToPsi`. |
| 2 | IMU | AUTO | WHO_AM_I = 0x6B/0x6C; accel + gyro readback at 250 dps full-scale. |
| 3 | MCP3202 ADC | AUTO | Flap (ch0) and Volume (ch1) pots read in (0, 4095). |
| 4 | Serial loopback | PROMPT | Asks "GPIO 10↔3 jumper installed? [y/n, 5s default=skip]". y → roundtrip "HWTEST". n / timeout → SKIP. |
| 5 | SD card | AUTO | Init, log size, write+delete `_hwtest.tmp` at root. |
| 6 | Audio | MANUAL | 400 Hz left, 1600 Hz right, 1.5 s each. |
| 7 | LED ramp | MANUAL | PWM 0→255→0 on the knob LED. |
| 8 | M5 display wire | MANUAL | 10 s of valid `#1` DisplayFrame at 20 Hz on Display TX, sweeping pitch −10°→+10° and AOA chevron through the bands. Built via `onspeed::proto::BuildDisplayFrame`. |

### Output format

Per-test prose plus a single machine-parseable line at end of cycle:

```
RESULT,fw=4.21.0-2-gabc123,hw=V4P,pressure=PASS,imu=PASS,adc=PASS,loopback=SKIP,sd=PASS,audio=MANUAL,led=MANUAL,m5=MANUAL,overall=PASS
```

`overall=PASS` iff every AUTO test passed. SKIP and MANUAL never count
as failures, so the same binary is safe on a configured customer box
(no jumper present → loopback SKIP) and on a freshly assembled board
(jumper present → loopback PASS).

### Build & flash

```bash
pio run -e hardware_test               # V4P (default)
pio run -e hardware_test -t upload     # build + flash V4P
pio run -e hardware_test-v4b           # V4B
pio device monitor                     # 921600 baud
```

### Testing

```bash
pio run -e hardware_test               # builds clean
pio run -e hardware_test-v4b           # both variants build
pio run -e esp32s3-v4p                 # firmware still builds (no shared files modified)
pio test -e native                     # native tests still pass
```
```

---

## Self-review

- **Spec coverage**:
  - Sketch path, env split, src_dir redirect ✓ (Tasks 1–3)
  - 8 tests with correct classes ✓ (Tasks 4–10)
  - Banner with build version ✓ (Task 3)
  - Per-test prose format ✓ (each test task)
  - Single `RESULT,...` summary line ✓ (Task 3, used in `printSummary`)
  - Field-diagnostic safety: SD writes only `_hwtest.tmp`, loopback default-skip ✓ (Tasks 7, 8)
  - onspeed_core reuse: `CountsToPsi`, `BuildDisplayFrame` ✓ (Tasks 4, 10)
  - `HardwareMap.h` direct include — no pin duplication ✓ (Task 3)
  - V4P/V4B variants ✓ (Task 2)

- **Placeholder scan**: every step has either complete code or a complete bash command. No "TBD" / "implement later".

- **Type consistency**: `TestResult` enum, `CycleResults` struct, `pressureTest`/`imuTest`/etc. signature is `static TestResult name()` everywhere. `DisplayBuildInputs` field names match what `BuildDisplayFrame` actually accepts (verified against `proto/DisplaySerial.h`).

- **Function names**: `BuildDisplayFrame` (not `BuildFrame`); `kDisplayFrameSizeBytes` (74); `kDisplayFramePeriodMs` (50). All match current onspeed_core API.
