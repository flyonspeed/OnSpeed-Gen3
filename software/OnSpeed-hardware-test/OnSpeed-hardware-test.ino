// ============================================================================
// OnSpeed Gen3 Hardware Test
// ============================================================================
//
// Manufacturing/assembly verification sketch for the Gen3 (ESP32-S3) board.
// Exercises every peripheral and reports PASS/FAIL over USB serial (921600 baud).
//
// This sketch is COMPLETELY SELF-CONTAINED — it does not include Globals.h or
// link any firmware .cpp files. All sensor access is inline using the same SPI
// protocols as the production firmware. This avoids dragging in the entire
// firmware dependency tree.
//
// Pin definitions are duplicated from Globals.h. If pins change in firmware,
// update them here too. (Search for "Pin Definitions" in both files.)
//
// Usage:
//   pio run -e hardware_test -t upload
//   pio device monitor          (921600 baud)
//
// Automated tests: pressure sensors, IMU, serial loopback, SD card
// Manual tests:    audio (listen), LED (watch), ADC pots (turn knobs)
//
// Serial loopback requires a jumper wire: GPIO 10 (Display TX) -> GPIO 3 (Boom RX)
//
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#define DISABLE_FS_H_WARNING
#include <SdFat.h>
#include <ESP_I2S.h>

// ============================================================================
// Hardware Variant — must match the board under test
// ============================================================================
// Uncomment exactly one:
//#define HW_V4B  // Bob's hardware
#define HW_V4P    // Phil's hardware (current default)

// ============================================================================
// Pin Definitions (from Globals.h — keep in sync!)
// ============================================================================

// Sensor SPI bus
#define SENSOR_MISO     18
#define SENSOR_MOSI     17
#define SENSOR_SCLK     16

// Sensor chip selects
#define CS_IMU           4
#define CS_STATIC        7

#ifdef HW_V4P
#define CS_AOA           6
#define CS_PITOT        15
#else
#define CS_AOA          15
#define CS_PITOT         6
#endif

// MCP3204 external ADC (V4P only)
#ifdef HW_V4P
#define CS_ADC           5
#define ADC_CH_VOLUME    0
#define ADC_CH_FLAP      1
#endif

// Native ADC pins (V4B only)
#define VOLUME_PIN       1
#define FLAP_PIN         2

// SD card SPI bus
#ifdef HW_V4P
#define SD_SCLK         41
#define SD_MISO         42
#define SD_MOSI         40
#define SD_CS           39
#else
#define SD_SCLK         42
#define SD_MISO         41
#define SD_MOSI         40
#define SD_CS           39
#endif

// I2S audio
#ifdef HW_V4P
#define I2S_BCK         20
#define I2S_DOUT        19
#define I2S_LRCK         8
#else
#define I2S_BCK         45
#define I2S_DOUT        48
#define I2S_LRCK        47
#endif

// Serial ports
#define EFIS_SER_RX      9
#define BOOM_SER_RX      3
#define DISPLAY_SER_TX  10

// GPIO
#define SWITCH_PIN      12
#define PIN_LED_KNOB    13

// ============================================================================
// SPI clock speed (matches firmware SPI_IO.cpp)
// ============================================================================
#define SPI_CLK_HZ    1000000

// ============================================================================
// IMU register definitions (from IMU330.cpp)
// ============================================================================
#define IMU_WHO_AM_I    0x0F
#define IMU_CTRL1_XL    0x10
#define IMU_CTRL2_G     0x11
#define IMU_CTRL3_C     0x12
#define IMU_CTRL4_C     0x13
#define IMU_CTRL6_C     0x15
#define IMU_CTRL7_G     0x16
#define IMU_CTRL9_XL    0x18
#define IMU_FIFO_CTRL4  0x0A
#define IMU_OUTX_L_G    0x22
#define IMU_OUTX_L_A    0x28

#define IMU_READ(reg)   ((uint8_t)(0x80 | (reg)))
#define IMU_WRITE(reg)  ((uint8_t)(0x7F & (reg)))

#define ACCEL_SCALE     (8.0f / 32768.0f)   // +/-8G full scale
#define GYRO_SCALE      (245.0f / 32768.0f)  // 245 dps full scale

// ============================================================================
// Pressure sensor constants (from HscPressureSensor.cpp)
// ============================================================================
// Differential: HSCDRNN1.6BASA3 (Pitot, AOA)
#define DIFF_COUNTS_MIN   409
#define DIFF_COUNTS_MAX  3686
#define DIFF_PSI_MIN     -1.0f
#define DIFF_PSI_MAX      1.0f

// Absolute: HSCDRRN100MDSA3 (Static)
#define ABS_COUNTS_MIN    409
#define ABS_COUNTS_MAX   3686
#define ABS_PSI_MIN       0.0f
#define ABS_PSI_MAX      23.2f

// ============================================================================
// Audio constants
// ============================================================================
#define SAMPLE_RATE     16000
#define TONE_BUF_LEN    (SAMPLE_RATE / 10)  // 100ms of audio

// ============================================================================
// Custom types (must be defined before any function that uses them,
// because Arduino .ino preprocessing inserts auto-prototypes after includes)
// ============================================================================

struct PressureResult {
    uint8_t  status;   // 2-bit status (0 = normal)
    uint16_t counts;   // 12-bit raw counts (valid range: 409-3686)
};

// ============================================================================
// Global SPI bus instance
// ============================================================================
static SPIClass sensorSPI(FSPI);

// ============================================================================
// Inline SPI helper functions
// ============================================================================

// Read raw bytes from a device (no register address — used by pressure sensors)
static void spiReadBytes(int cs, uint8_t* buf, int len) {
    sensorSPI.beginTransaction(SPISettings(SPI_CLK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    for (int i = 0; i < len; i++)
        buf[i] = sensorSPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
}

// Read a single register byte (register-addressed SPI, read bit = 0x80)
static uint8_t spiReadReg(int cs, uint8_t reg) {
    uint8_t val;
    sensorSPI.beginTransaction(SPISettings(SPI_CLK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    sensorSPI.transfer(reg);
    val = sensorSPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
    return val;
}

// Write a single register byte
static void spiWriteReg(int cs, uint8_t reg, uint8_t val) {
    sensorSPI.beginTransaction(SPISettings(SPI_CLK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    sensorSPI.transfer(reg);
    sensorSPI.transfer(val);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
}

// Read multiple register bytes
static void spiReadRegs(int cs, uint8_t reg, uint8_t* buf, int len) {
    sensorSPI.beginTransaction(SPISettings(SPI_CLK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(cs, LOW);
    sensorSPI.transfer(reg);
    for (int i = 0; i < len; i++)
        buf[i] = sensorSPI.transfer(0x00);
    digitalWrite(cs, HIGH);
    sensorSPI.endTransaction();
}

// ============================================================================
// Pressure sensor functions
// ============================================================================

static PressureResult readPressure(int cs) {
    uint8_t data[2];
    spiReadBytes(cs, data, 2);

    // The HSC sensor returns [status(2)|counts(12)|extra(2)] packed into 16 bits.
    // Firmware uses a bitfield union; we extract manually here.
    PressureResult r;
    r.status = (data[0] >> 6) & 0x03;
    r.counts = ((uint16_t)(data[0] & 0x3F) << 6) | ((data[1] >> 2) & 0x3F);
    return r;
}

static float countsToPsi(uint16_t counts, uint16_t cMin, uint16_t cMax,
                          float pMin, float pMax) {
    return (float)(counts - cMin) * (pMax - pMin) / (float)(cMax - cMin) + pMin;
}

static float psiToMillibars(float psi) {
    return psi * 68.947572932f;
}

// ============================================================================
// IMU functions
// ============================================================================

static uint8_t imuWhoAmI() {
    return spiReadReg(CS_IMU, IMU_READ(IMU_WHO_AM_I));
}

static void imuInit() {
    // Soft reset
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL3_C), 0b00000101);
    delay(100);
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL3_C), 0b00000100);
    delay(100);

    // SPI mode, device config
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL9_XL), 0b11100010);
    delay(50);

    // Accelerometer: 208Hz ODR, +/-8G
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL1_XL), 0b01011100);
    delay(50);

    // Gyroscope: 208Hz, 250dps
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL2_G), 0b01010000);
    delay(50);

    // Disable gyro high-pass filter
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL7_G), 0b00000000);
    delay(50);

    // Disable LPF1, disable I2C
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL4_C), 0b00000100);
    delay(50);

    // Bypass FIFO
    spiWriteReg(CS_IMU, IMU_WRITE(IMU_FIFO_CTRL4), 0b00010000);
    delay(50);

    spiWriteReg(CS_IMU, IMU_WRITE(IMU_CTRL9_XL), 0b11100000);
    delay(50);
}

static void imuReadAccelGyro(float accel[3], float gyro[3]) {
    uint8_t buf[6];

    // Read accelerometer (6 bytes at OUTX_L_A)
    spiReadRegs(CS_IMU, IMU_READ(IMU_OUTX_L_A), buf, 6);
    int16_t ax = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ay = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t az = (int16_t)((buf[5] << 8) | buf[4]);
    accel[0] = ax * ACCEL_SCALE;
    accel[1] = ay * ACCEL_SCALE;
    accel[2] = az * ACCEL_SCALE;

    // Read gyroscope (6 bytes at OUTX_L_G)
    spiReadRegs(CS_IMU, IMU_READ(IMU_OUTX_L_G), buf, 6);
    int16_t gx = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t gy = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t gz = (int16_t)((buf[5] << 8) | buf[4]);
    gyro[0] = gx * GYRO_SCALE;
    gyro[1] = gy * GYRO_SCALE;
    gyro[2] = gz * GYRO_SCALE;
}

// ============================================================================
// MCP3204 ADC functions (V4P only)
// ============================================================================

#ifdef HW_V4P
static uint16_t mcp3204Read(uint8_t ch) {
    ch &= 0x03;
    sensorSPI.beginTransaction(SPISettings(SPI_CLK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(CS_ADC, LOW);
    sensorSPI.transfer(0x06 | (ch >> 2));
    uint8_t hi = sensorSPI.transfer((ch & 0x03) << 6);
    uint8_t lo = sensorSPI.transfer(0x00);
    digitalWrite(CS_ADC, HIGH);
    sensorSPI.endTransaction();
    return ((uint16_t)(hi & 0x0F) << 8) | lo;
}
#endif

// ============================================================================
// Test functions
// ============================================================================

// --- Switch test (informational, always passes) ---
static void switchTest() {
    int state = digitalRead(SWITCH_PIN);
    Serial.printf("  Switch (GPIO %d): %s\n", SWITCH_PIN,
                  state == LOW ? "PRESSED (LOW)" : "RELEASED (HIGH)");
}

// --- Pressure sensor tests ---
static bool testOnePressure(const char* name, int cs,
                             uint16_t cMin, uint16_t cMax,
                             float pMin, float pMax,
                             bool showMillibars) {
    PressureResult r = readPressure(cs);

    float psi = countsToPsi(r.counts, cMin, cMax, pMin, pMax);
    bool pass = (r.status == 0) && (r.counts >= cMin) && (r.counts <= cMax);

    if (showMillibars) {
        float mb = psiToMillibars(psi);
        Serial.printf("  %-7s %s  status=%d  counts=%-5u  %+.3f PSI  %.1f mb\n",
                      name, pass ? "PASS" : "FAIL", r.status, r.counts, psi, mb);
    } else {
        Serial.printf("  %-7s %s  status=%d  counts=%-5u  %+.3f PSI\n",
                      name, pass ? "PASS" : "FAIL", r.status, r.counts, psi);
    }

    return pass;
}

static bool pressureTest() {
    Serial.println("\n[Pressure Sensors]");
    bool pitot  = testOnePressure("Pitot:",  CS_PITOT,
                                   DIFF_COUNTS_MIN, DIFF_COUNTS_MAX,
                                   DIFF_PSI_MIN, DIFF_PSI_MAX, false);
    bool aoa    = testOnePressure("AOA:",    CS_AOA,
                                   DIFF_COUNTS_MIN, DIFF_COUNTS_MAX,
                                   DIFF_PSI_MIN, DIFF_PSI_MAX, false);
    bool pstatic = testOnePressure("Static:", CS_STATIC,
                                    ABS_COUNTS_MIN, ABS_COUNTS_MAX,
                                    ABS_PSI_MIN, ABS_PSI_MAX, true);
    return pitot && aoa && pstatic;
}

// --- IMU test ---
static bool imuTest() {
    Serial.println("\n[IMU]");

    uint8_t whoami = imuWhoAmI();
    // 0x6B = ISM330DHCX, 0x6C = LSM6DSO32/LSM6DSOX
    bool knownChip = (whoami == 0x6B) || (whoami == 0x6C);

    if (!knownChip) {
        Serial.printf("  IMU:    FAIL  WHO_AM_I=0x%02X (expected 0x6B or 0x6C)\n", whoami);
        return false;
    }

    imuInit();
    delay(100);  // let sensors settle

    float accel[3], gyro[3];
    imuReadAccelGyro(accel, gyro);

    Serial.printf("  IMU:    PASS  WHO_AM_I=0x%02X\n", whoami);
    Serial.printf("          Accel: X=%+.3f  Y=%+.3f  Z=%+.3f g\n",
                  accel[0], accel[1], accel[2]);
    Serial.printf("          Gyro:  X=%+.2f  Y=%+.2f  Z=%+.2f dps\n",
                  gyro[0], gyro[1], gyro[2]);

    return true;
}

// --- ADC test (informational — read pot values for manual verification) ---
static bool adcTest() {
    Serial.println("\n[ADC / Pots]");

#ifdef HW_V4P
    uint16_t vol  = mcp3204Read(ADC_CH_VOLUME);
    uint16_t flap = mcp3204Read(ADC_CH_FLAP);
    Serial.printf("  MCP3204 Volume (ch%d): %u / 4095\n", ADC_CH_VOLUME, vol);
    Serial.printf("  MCP3204 Flap   (ch%d): %u / 4095\n", ADC_CH_FLAP, flap);
    // Basic sanity: if the ADC returns all zeros or all ones, something's wrong
    bool volOk  = (vol > 0 && vol < 4095);
    bool flapOk = (flap > 0 && flap < 4095);
    if (!volOk)  Serial.println("  WARNING: Volume reads 0 or 4095 — check wiring");
    if (!flapOk) Serial.println("  WARNING: Flap reads 0 or 4095 — check wiring");
    return volOk && flapOk;
#else
    uint16_t vol  = analogRead(VOLUME_PIN);
    uint16_t flap = analogRead(FLAP_PIN);
    Serial.printf("  ESP32 ADC Volume (pin %d): %u\n", VOLUME_PIN, vol);
    Serial.printf("  ESP32 ADC Flap   (pin %d): %u\n", FLAP_PIN, flap);
    return true;  // ESP32 ADC always returns something
#endif
}

// --- Serial loopback test ---
// Requires jumper wire: Display TX (GPIO 10) -> Boom RX (GPIO 3)
static bool serialLoopbackTest() {
    Serial.println("\n[Serial Loopback]");

    // Serial1 with TX=DISPLAY_SER_TX, RX=BOOM_SER_RX
    Serial1.begin(115200, SERIAL_8N1, BOOM_SER_RX, DISPLAY_SER_TX);
    delay(50);

    // Flush any stale data
    while (Serial1.available()) Serial1.read();

    const char* testMsg = "HWTEST\n";
    Serial1.print(testMsg);
    Serial1.flush();
    delay(100);

    String received = "";
    unsigned long deadline = millis() + 200;
    while (millis() < deadline) {
        while (Serial1.available()) {
            char c = (char)Serial1.read();
            received += c;
        }
        if (received.indexOf("HWTEST") >= 0) break;
        delay(10);
    }

    bool pass = (received.indexOf("HWTEST") >= 0);
    Serial.printf("  Serial1 loopback (TX=%d -> RX=%d): %s\n",
                  DISPLAY_SER_TX, BOOM_SER_RX, pass ? "PASS" : "FAIL");
    if (!pass) {
        Serial.printf("  (Received: \"%s\")\n", received.c_str());
        Serial.println("  Check jumper wire: GPIO 10 -> GPIO 3");
    }

    // Also report EFIS serial (RX-only, can't loopback)
    Serial2.begin(115200, SERIAL_8N1, EFIS_SER_RX, -1);
    Serial.printf("  Serial2 EFIS (RX=%d): initialized (RX-only, no loopback)\n",
                  EFIS_SER_RX);

    Serial1.end();
    Serial2.end();
    return pass;
}

// --- SD card test ---
static bool sdCardTest() {
    Serial.println("\n[SD Card]");

    SPIClass sdSPI(HSPI);
    sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

    SdFat sd;
    SdSpiConfig spiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(10), &sdSPI);

    if (!sd.begin(spiConfig)) {
        Serial.println("  SD Card: FAIL (cannot initialize)");
        sdSPI.end();
        return false;
    }

    // Try to get card info
    uint32_t sizeMB = (uint32_t)(sd.card()->sectorCount() / 2048);
    Serial.printf("  SD Card: PASS  size=%lu MB\n", (unsigned long)sizeMB);

    // Try creating and deleting a test file
    FsFile testFile = sd.open("_hwtest.tmp", O_WRONLY | O_CREAT | O_TRUNC);
    if (testFile) {
        testFile.println("OnSpeed hardware test");
        testFile.close();
        sd.remove("_hwtest.tmp");
        Serial.println("  SD Card: write/delete test PASS");
    } else {
        Serial.println("  SD Card: write test FAIL (could not create file)");
        sdSPI.end();
        return false;
    }

    sdSPI.end();
    return true;
}

// --- Audio test (manual verification — listen for tones) ---
static void audioTest() {
    Serial.println("\n[Audio - verify by ear]");

    I2SClass i2s;
    i2s.setPins(I2S_BCK, I2S_LRCK, I2S_DOUT);
    if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        Serial.println("  Audio: FAIL (I2S init failed)");
        return;
    }

    // Generate tone buffers
    int16_t tone400[TONE_BUF_LEN];
    int16_t tone1600[TONE_BUF_LEN];
    for (int i = 0; i < TONE_BUF_LEN; i++) {
        tone400[i]  = (int16_t)(25000.0f * cosf(2.0f * M_PI * i * 400.0f / SAMPLE_RATE));
        tone1600[i] = (int16_t)(25000.0f * cosf(2.0f * M_PI * i * 1600.0f / SAMPLE_RATE));
    }

    // Helper: write tone buffer as stereo frames with per-channel gain
    auto writeTone = [&](const int16_t* tone, int len, float leftGain, float rightGain) {
        // Pack stereo frames: low 16 bits = left, high 16 bits = right
        for (int i = 0; i < len; i++) {
            int16_t left  = (int16_t)(tone[i] * leftGain);
            int16_t right = (int16_t)(tone[i] * rightGain);
            uint32_t frame = (uint16_t)left | ((uint32_t)(uint16_t)right << 16);
            i2s.write((const uint8_t*)&frame, sizeof(frame));
        }
    };

    // Play 400Hz left-only for 1.5 seconds (15 x 100ms buffers)
    Serial.println("  Playing 400Hz LEFT channel (1.5s)...");
    for (int rep = 0; rep < 15; rep++)
        writeTone(tone400, TONE_BUF_LEN, 1.0f, 0.0f);

    delay(200);  // brief silence

    // Play 1600Hz right-only for 1.5 seconds
    Serial.println("  Playing 1600Hz RIGHT channel (1.5s)...");
    for (int rep = 0; rep < 15; rep++)
        writeTone(tone1600, TONE_BUF_LEN, 0.0f, 1.0f);

    i2s.end();
    Serial.println("  Audio: done (verify L=400Hz, R=1600Hz by ear)");
}

// --- LED test (manual verification — watch the knob LED) ---
static void ledTest() {
    Serial.println("\n[LED - verify visually]");
    Serial.printf("  LED (GPIO %d): ramping up...\n", PIN_LED_KNOB);

    // Ramp up 0->255 over 1 second
    for (int duty = 0; duty <= 255; duty++) {
        ledcWrite(PIN_LED_KNOB, duty);
        delay(4);  // ~1 second total
    }
    Serial.println("  LED: ramping down...");

    // Ramp down 255->0 over 1 second
    for (int duty = 255; duty >= 0; duty--) {
        ledcWrite(PIN_LED_KNOB, duty);
        delay(4);
    }

    Serial.println("  LED: ramp complete");
}

// ============================================================================
// setup()
// ============================================================================

void setup() {
    delay(1000);  // let USB enumerate
    Serial.begin(921600);
    delay(500);

    Serial.println();
    Serial.println("=======================================");
    Serial.println("  OnSpeed Gen3 Hardware Test v1.0");
#ifdef HW_V4P
    Serial.println("  Hardware variant: V4P");
#else
    Serial.println("  Hardware variant: V4B");
#endif
    Serial.println("=======================================");

    // Initialize all chip select pins HIGH (deselected) before starting SPI
    const int csPins[] = { CS_IMU, CS_STATIC, CS_AOA, CS_PITOT, SD_CS
#ifdef HW_V4P
        , CS_ADC
#endif
    };
    for (int cs : csPins) {
        pinMode(cs, OUTPUT);
        digitalWrite(cs, HIGH);
    }

    // Initialize sensor SPI bus
    sensorSPI.begin(SENSOR_SCLK, SENSOR_MISO, SENSOR_MOSI);

    // Initialize switch input
    pinMode(SWITCH_PIN, INPUT_PULLUP);

    // Initialize LED PWM
    ledcAttach(PIN_LED_KNOB, 5000, 8);
}

// ============================================================================
// loop()
// ============================================================================

void loop() {
    Serial.println("\n=======================================");
    Serial.println("Starting hardware tests...");
    Serial.println("=======================================");

    int passed = 0;
    int total  = 0;

    // Turn LED on as a "testing in progress" indicator
    ledcWrite(PIN_LED_KNOB, 200);

    // --- Informational: switch state ---
    switchTest();

    // --- Automated tests ---
    total++; if (pressureTest())       passed++;
    total++; if (imuTest())            passed++;
    total++; if (adcTest())            passed++;
    total++; if (serialLoopbackTest()) passed++;
    total++; if (sdCardTest())         passed++;

    // --- Manual verification tests ---
    audioTest();
    ledTest();

    // Turn LED off
    ledcWrite(PIN_LED_KNOB, 0);

    // --- Summary ---
    Serial.println("\n=======================================");
    Serial.printf("  %d/%d automated tests PASSED\n", passed, total);
    if (passed == total)
        Serial.println("  >>> ALL AUTOMATED TESTS PASSED <<<");
    else
        Serial.println("  >>> SOME TESTS FAILED <<<");
    Serial.println("  Verify audio L/R and LED ramp manually.");
    Serial.println("=======================================");

    delay(10000);
}
