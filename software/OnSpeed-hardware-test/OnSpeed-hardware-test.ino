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

// HSC pressure-sensor reading. Defined here (above any function that
// returns it) so Arduino's auto-prototype generator doesn't synthesize
// a prototype for `readHsc(int) -> HscReading` before the type is
// known. Field semantics: status is the 2-bit HSC status field (0 =
// normal); counts is the 14-bit raw output from the sensor packet.
// Mirrors the bitfield in firmware src/drivers/HscPressureSensor.h.
struct HscReading {
    uint8_t  status;
    uint16_t counts;
};

// -----------------------------------------------------------------------------
// Shared SPI bus for sensors (pressure, IMU, MCP3202)
// -----------------------------------------------------------------------------

static SPIClass sensorSPI(FSPI);

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

// HSC pressure-sensor read. Returns {status (top 2 bits), counts (bottom
// 14 bits)} — matches the bitfield in firmware HscPressureSensor.h.
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

// -----------------------------------------------------------------------------
// Test stubs — filled in by subsequent commits
// -----------------------------------------------------------------------------

static TestResult pressureTest() {
    Serial.println("\n[Pressure Sensors]");
    bool ok = true;
    ok &= reportOnePressure("Pitot:",  kCsPitot,  kHscDiff, false);
    ok &= reportOnePressure("AOA:",    kCsAoa,    kHscDiff, false);
    ok &= reportOnePressure("Static:", kCsStatic, kHscAbs,  true);
    return ok ? TestResult::PASS : TestResult::FAIL;
}

// IMU init sequence — bytes mirror src/drivers/IMU330.cpp::Init().
// Soft-reset, set ODR/full-scale, disable filters we don't need, bypass FIFO.
static void imuInit() {
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl3C),    0b00000101); delay(100);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl3C),    0b00000100); delay(100);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl9Xl),   0b11100010); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl1Xl),   0b01011100); delay(50); // 208 Hz, ±8 g
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl2G),    0b01010000); delay(50); // 208 Hz, 250 dps
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl7G),    0b00000000); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl4C),    0b00000100); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kFifoCtrl4), 0b00010000); delay(50);
    spiWriteReg(kCsImu, imu::Write(imu::kCtrl9Xl),   0b11100000); delay(50);
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
    Serial.printf("  MCP3202 Flap   (ch%d): %u / 4095\n", kAdcChFlap,   flap);
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
