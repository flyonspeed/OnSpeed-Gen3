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
