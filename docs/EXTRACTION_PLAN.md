# onspeed_core Extraction Plan

**Goal:** Move platform-independent business logic out of ESP32-coupled firmware files into `onspeed_core` so it can be natively tested, validated with real flight data, and reused across tools.

**Last updated:** February 2026

---

## Current State

### Already in onspeed_core (9 modules, 114 tests)

```
AOACalculator.h/cpp     CurveCalc.h/cpp        EMAFilter.h
EKF6.h/cpp              KalmanFilter.h/cpp      MadgwickFusion.h/cpp
OnSpeedTypes.h          SavGolDerivative.h      ToneCalc.h/cpp
```

**Recent changes:**
- `mapfloat()` moved from `Helpers.cpp` into `OnSpeedTypes.h` (5 tests added)
- `ToneCalc.h/cpp` extracted from `Audio.cpp:UpdateTones()` (15 tests)
- `Helpers.h` no longer includes `Globals.h` (circular include eliminated)
- Tier 1A (Tone Selection) is **done**

### Untested firmware logic (~6,000+ lines)

The following firmware files contain significant pure computation mixed in with hardware I/O. None of it has test coverage today.

| File | Total lines | Estimated extractable | What's in there |
|------|-------------|----------------------|-----------------|
| `EfisSerial.cpp` | ~800 | ~500 | 5 protocol parsers (Dynon, Garmin, MGL, VN-300) |
| `Audio.cpp` | ~580 | ~120 | Tone selection state machine, waveform synthesis |
| `AHRS.cpp` | ~365 | ~200 | Bias rotation, TAS correction, flight path, derived AOA |
| `Config.cpp` | ~1,050 | ~500 | XML/CSV parsing, serialization, defaults, validation |
| `DisplaySerial.cpp` | ~310 | ~180 | PercentLift, G3X/OnSpeed format encoding |
| `ConfigWebServer.cpp` | ~3,136 | ~200 | Config form parsing, wizard state machine |
| `javascript_calibration.h` | ~460 (JS) | ~100 | Calibration regression, setpoint calculation |
| `Flaps.cpp` | ~85 | ~35 | Flap index lookup from ADC value |
| `LogReplay.cpp` | ~475 | ~60 | Range sweep generation, type conversions |

---

## Extraction Tiers

### Tier 1: Safety-Critical (do first)

These are the functions where a bug means a pilot gets the wrong audio cue near stall. They're pure computation with no hardware coupling — just if/else chains and math that read from config thresholds.

#### 1A. Tone Selection — `ToneCalc.h/cpp` ✓ DONE

**Source:** `Audio.cpp:474-553` → extracted to `onspeed_core/ToneCalc.h/cpp`

The `UpdateTones()` method maps current AOA against per-flap thresholds to decide which tone to play and at what pulse rate. It's the core safety logic of the entire system. Now tested with 15 unit tests covering all AOA regions, boundary values, full-flaps edge case, and muted mode.

```cpp
namespace onspeed {

enum EnToneType { ToneNone, ToneLow, ToneHigh };

struct ToneThresholds {
    float fLDMAXAOA;
    float fONSPEEDFASTAOA;
    float fONSPEEDSLOWAOA;
    float fSTALLWARNAOA;
};

struct ToneResult {
    EnToneType  enTone;
    float       fPulseFreq;     // 0 = solid tone
};

// Pure function: given AOA and thresholds, what tone should play?
ToneResult calculateTone(float fAOA, const ToneThresholds& thresholds);

// Muted variant: only stall warning passes through
ToneResult calculateToneMuted(float fAOA, float fSTALLWARNAOA);

} // namespace onspeed
```

**Tests to write:**
- AOA below L/Dmax → no tone
- AOA in L/Dmax-to-OnSpeedFast → pulsed low tone, frequency scales with AOA
- AOA in OnSpeedFast-to-OnSpeedSlow → solid low tone
- AOA in OnSpeedSlow-to-StallWarn → pulsed high tone, frequency scales
- AOA above StallWarn → 20 PPS high tone (stall)
- Muted mode: only stall warning plays
- Edge case: L/Dmax >= OnSpeedFast (full flaps — skip pulsed low)
- Pulse frequency interpolation matches `mapfloat()` at boundaries

#### 1B. PercentLift Calculation — `PercentLift.h/cpp`

**Source:** `DisplaySerial.cpp:149-174`, `html_liveview.h:133+`

Piecewise linear mapping from AOA to 0-99% lift scale, used by both the serial display output and the LiveView web UI. Uses alpha_0 as the zero-lift floor.

```cpp
namespace onspeed {

struct PercentLiftThresholds {
    float fAlpha0;
    float fAlphaStall;
    float fLDMAXAOA;
    float fONSPEEDFASTAOA;
    float fONSPEEDSLOWAOA;
    float fSTALLWARNAOA;
};

// Returns 0-99 percent lift from AOA and per-flap thresholds.
int calculatePercentLift(float fAOA, const PercentLiftThresholds& thresholds);

} // namespace onspeed
```

**Tests to write:**
- AOA at alpha_0 → 0%
- AOA at L/Dmax → 50%
- AOA at OnSpeedFast → 55%
- AOA at OnSpeedSlow → 66%
- AOA at StallWarn → 90%
- AOA at alpha_stall → ~100%
- AOA below alpha_0 → clamped to 0
- AOA above stall → clamped to 99
- Interpolation is linear within each segment

#### 1C. Calibration Math — `CalibrationFit.h/cpp`

**Source:** `javascript_calibration.h:286-325`

This is currently **JavaScript running in the pilot's browser**. It performs the IAS-to-AOA curve fit (`DerivedAOA = K/IAS² + alpha_0`) and computes all six setpoints from the fit. This is the code that determines where the stall warning fires. It should be in C++ with tests.

```cpp
namespace onspeed {

struct CalibrationInput {
    const float* afIAS;         // IAS samples from decel run
    const float* afDerivedAOA;  // Corresponding DerivedAOA samples
    int          iCount;
    float        fStallIAS;     // IAS at stall break
};

struct CalibrationResult {
    float fK;               // Lift sensitivity coefficient
    float fAlpha0;          // Zero-lift fuselage AOA (intercept)
    float fAlphaStall;      // AOA at stall break
    float fR2;              // Goodness of fit
    bool  bValid;           // Fit succeeded
};

struct SetpointMultipliers {
    float fOSFast;          // e.g. 1.35 (Vs × 1.35)
    float fOSSlow;          // e.g. 1.25
    float fStallWarnMargin; // IAS margin in knots (e.g. 5)
    float fGLimit;          // Airframe G limit for maneuvering speed
};

struct SetpointResult {
    float fLDMAX;
    float fONSPEEDFAST;
    float fONSPEEDSLOW;
    float fSTALLWARN;
    float fSTALL;
    float fMAN;
};

// Fit DerivedAOA = K / IAS² + alpha_0 via linear regression on (1/IAS², AOA)
CalibrationResult fitIAStoAOA(const CalibrationInput& input);

// Compute setpoints from fit result using NAOA fractions
SetpointResult computeSetpoints(const CalibrationResult& fit,
                                const SetpointMultipliers& mult,
                                float fBestGlideIAS);

} // namespace onspeed
```

**Tests to write:**
- Synthetic decel data with known K and alpha_0 → recovers parameters
- R² > 0.99 for clean synthetic data
- Setpoints match hand-calculated NAOA values (0.549, 0.592, 0.640 for 1.35/1.30/1.25×Vs)
- Maneuvering speed = stall_IAS × sqrt(G_limit)
- Degenerate inputs (too few points, all same IAS) → bValid = false
- Real flight data from `~/Dropbox/N720AK/OnSpeed Cals/` as regression test

**Dependency:** Needs a simple linear regression utility. Could use a minimal least-squares fit (slope + intercept for transformed variables) — no need for a full regression library.

---

### Tier 2: Data Integrity (do second)

These are parsers and formatters where bugs mean corrupted data, not wrong audio — but they're easy to extract and have obvious test vectors.

#### 2A. EFIS Protocol Parsers — `EfisParser.h/cpp`

**Source:** `EfisSerial.cpp` (5 protocol handlers)

Each EFIS protocol parser takes a string or byte buffer and produces a struct of decoded flight data. The parsing logic is entirely self-contained — CRC calculation, substring extraction, unit conversion. The only ESP32 dependency is `millis()` for timestamping (which we can omit from the pure parser).

```cpp
namespace onspeed {

struct EfisData {
    float   fIAS;           // knots
    float   fPitch;         // degrees
    float   fRoll;          // degrees
    float   fLateralG;
    float   fVerticalG;
    int     iPercentLift;   // 0-99
    int     iPalt;          // feet
    int     iVSI;           // ft/min
    float   fTAS;           // knots
    float   fOAT;           // Celsius
    int     iHeading;       // degrees
    bool    bValid;         // CRC passed and data parsed
};

// Text protocol parsers
EfisData parseDynonSkyviewADAHRS(const char* szLine, int iLen);
EfisData parseDynonD10(const char* szLine, int iLen);
EfisData parseGarminG5(const char* szLine, int iLen);
EfisData parseGarminG3X(const char* szLine, int iLen);

// Binary protocol parser
EfisData parseMglBinary(const uint8_t* pBuffer, int iLen);

// VN-300 binary (separate output struct due to extra fields)
struct VN300Data { /* angular rates, velocities, lin accel, GNSS, etc. */ };
VN300Data parseVN300Binary(const uint8_t* pBuffer, int iLen);

} // namespace onspeed
```

**Tests to write (per protocol):**
- Valid message with known values → correct parsed output
- CRC failure → `bValid = false`
- Wrong message length → `bValid = false`
- "XXXX" placeholder fields (Dynon) → sentinel values
- Unit conversion accuracy (m/s → knots, 10ths → actual)
- Real captured messages from SD card logs as regression vectors

#### 2B. Display Serial Format Encoders — `DisplayFormat.h/cpp`

**Source:** `DisplaySerial.cpp:184-304`

Two output format encoders (G3X and OnSpeed) that pack flight data into fixed-width serial strings with CRC. Pure string formatting and clamping math.

```cpp
namespace onspeed {

struct DisplayData {
    float fPitch, fRoll, fIAS, fPaltFt;
    float fLateralG, fVerticalG;
    int   iPercentLift;
    float fAOA, fDerivedAOA, fFlightPath;
    float fOAT;
    int   iFlaps;
};

// Returns formatted string with CRC byte appended
// G3X format: 55 bytes + 2-byte hex CRC + CR LF
std::string formatG3X(const DisplayData& data);

// OnSpeed format: variable fields + CRC + CR LF
std::string formatOnSpeed(const DisplayData& data);

} // namespace onspeed
```

**Tests to write:**
- Known input → exact expected output string (byte-for-byte)
- Field clamping at protocol limits (e.g., pitch ±99.9°, roll ±999.9°)
- CRC calculation matches hand-calculated values
- NaN/Inf inputs → safe clamped output (no buffer overrun)

#### 2C. Config Parsing & Serialization — `ConfigParser.h/cpp`

**Source:** `Config.cpp:368-846`

The XML serializer (`ConfigurationToString()`) and XML parser (`LoadConfigFromString()`) use `tinyxml2` which is already a portable library with no ESP32 dependencies. The CSV parser for legacy v1 configs is pure string tokenization.

```cpp
namespace onspeed {

// Forward-declare or include the config struct (FOSConfig is already
// plain data — std::vector<SuFlaps>, ints, floats, bools, Strings).

// Parse CONFIG2 XML into a config struct. Returns true on success.
bool parseConfigXML(const char* szXml, FOSConfig& config);

// Serialize config struct to CONFIG2 XML string.
std::string serializeConfigXML(const FOSConfig& config);

// Parse legacy CONFIG v1 CSV format (for upgrade path).
bool parseConfigV1(const char* szCsv, FOSConfig& config);

// Validate config values are within sane ranges (CFG-04).
struct ValidationResult {
    bool bValid;
    std::vector<std::string> asErrors;
};
ValidationResult validateConfig(const FOSConfig& config);

} // namespace onspeed
```

**Tests to write:**
- Round-trip: serialize → parse → compare all fields
- Default config parses without error
- Invalid XML → returns false, doesn't crash
- V1 format with varying flap counts
- Range validation catches out-of-bounds values (CFG-04)
- Setpoint ordering validation (`AreSetpointsOrdered()`)
- Missing optional fields use defaults

**Note:** The main challenge here is the Arduino `String` type used throughout `FOSConfig`. Options:
1. **Typedef `String` to `std::string` in native builds** — cleanest, but needs a compat header
2. **Replace `String` with `std::string` in FOSConfig** — bigger diff but removes the Arduino dependency permanently
3. **Keep FOSConfig in firmware, extract only the parsing functions** — less refactoring but messier boundary

Option 1 is the pragmatic choice for now: a small `ArduinoCompat.h` that typedefs `String = std::string` and provides `toFloat()`, `toInt()`, `substring()` as free functions or a thin wrapper.

---

### Tier 3: Flight Physics (do third)

Already identified in NEXT_STEPS.md Phase 1. These are the AHRS math functions that compute derived quantities from sensor inputs.

#### 3A. AHRS Math — `AHRSMath.h/cpp`

**Source:** `AHRS.cpp:92-361` (as specified in NEXT_STEPS.md §3.2)

```cpp
namespace onspeed {

// Installation bias rotation (precomputed sin/cos)
struct BiasRotation {
    float fSinPitch, fCosPitch;
    float fSinRoll,  fCosRoll;
};
BiasRotation precomputeBiasRotation(float fPitchBiasDeg, float fRollBiasDeg);

void rotateGyro(float gx, float gy, float gz,
                const BiasRotation& bias,
                float& gxOut, float& gyOut, float& gzOut);

void rotateAccel(float ax, float ay, float az,
                 const BiasRotation& bias,
                 float& axOut, float& ayOut, float& azOut);

// TAS from IAS with density correction
float calcTAS(float fIAS_kts, float fPaltFt, float fOAT_C);

// Flight path angle from VSI and TAS
float calcFlightPathDeg(float fVSI_fpm, float fTAS_kts);

// Derived AOA = pitch - flight_path
float calcDerivedAOA(float fPitchDeg, float fFlightPathDeg);

// Centripetal acceleration compensation
float calcCentripetalAccel(float fTAS_mps, float fYawRate_rps);

} // namespace onspeed
```

**Tests to write:**
- Zero bias → identity rotation
- 90° bias → axes swap correctly
- TAS = IAS at sea level / ISA
- TAS > IAS at altitude
- Flight path = 0° in level flight (VSI = 0)
- Flight path clamped when TAS is near zero (safeAsin)
- Derived AOA = pitch in level flight

#### 3B. Pressure Calculations — `PressureCalc.h`

**Source:** `HscPressureSensor.cpp:121-137` (as specified in NEXT_STEPS.md §3.1)

```cpp
namespace onspeed {
float countsToPSI(uint16_t counts, int countsMin, int countsMax,
                  float psiMin, float psiMax);
float dynamicPressureToIAS(float pressurePascals);
}
```

#### 3C. Remaining small extractions from NEXT_STEPS.md

- `mapFloat()` → add to `OnSpeedTypes.h` (§3.5)
- `calc3DAudioGain()` → `Audio3DCalc.h` (§3.3)
- `calcEffectiveGLimits()` / `isOverG()` → `GLimitCalc.h` (§3.4)

These are small, self-contained, and already spec'd out in NEXT_STEPS.md.

---

### Tier 4: Config & Wizard Logic (do when Tiers 1-3 are solid)

#### 4A. Config Form Parsing — `ConfigUpdate.h/cpp`

**Source:** `ConfigWebServer.cpp:1532-1687`

The `HandleConfigSave()` handler parses 50+ form fields from HTTP POST params into `FOSConfig`. Currently zero validation. Extract as a pure function that takes key-value pairs and returns an updated config + validation errors.

```cpp
namespace onspeed {

// Apply a set of key-value pairs (from HTTP form or any source) to a config.
// Returns validation errors for any out-of-range or invalid values.
ValidationResult applyConfigUpdate(FOSConfig& config,
                                   const std::map<std::string, std::string>& params);

} // namespace onspeed
```

#### 4B. Flap Index Lookup — `FlapIndex.h`

**Source:** `Flaps.cpp:51-84`

```cpp
namespace onspeed {

struct FlapPosition {
    int iPotPosition;   // ADC threshold
    int iDegrees;       // Flap angle
};

// Given a raw ADC value, find the matching flap index.
// Returns -1 if no flap configs exist.
int findFlapIndex(uint16_t uRawValue, const FlapPosition* pFlaps, int iCount);

} // namespace onspeed
```

**Tests:** Boundary values, ascending/descending pot order, single flap, no flaps.

#### 4C. Wizard State Machine

**Source:** `ConfigWebServer.cpp:2348-2527`

The 4-step wizard (aircraft params → decel instructions → live capture → save results) can be modeled as a state enum with transition validation. Low priority since the steps are simple — the real value was already captured in 1C (calibration math).

---

## Implementation Strategy

### Ordering

```
Tier 1A  Tone selection         ✓ DONE (ToneCalc.h/cpp, 15 tests)
---      Helpers.h cleanup      ← next: kill Helpers.h, replace MIN/MAX with std::min/max
Tier 1B  PercentLift            ← safety-relevant, small, has known bug
Tier 1C  Calibration math       ← safety-critical, currently JS, needs C++ port
Tier 2A  EFIS parsers           ← largest volume of extractable code, easy
Tier 2B  Display formats        ← small, obvious test vectors
Tier 2C  Config parsing         ← enables CFG-04 validation, needs String compat
Tier 3A  AHRS math              ← already spec'd in NEXT_STEPS.md Phase 1
Tier 3B  Pressure calcs         ← already spec'd in NEXT_STEPS.md Phase 1
Tier 3C  Small extractions      ← 3D audio, G-limit (mapFloat already done)
Tier 4A  Config form parsing    ← depends on 2C
Tier 4B  Flap index lookup      ← small, independent
Tier 4C  Wizard state machine   ← low priority
```

### Helpers.h elimination (next PR)

`Helpers.h` is now down to 3 remaining symbols after `mapfloat` moved to `OnSpeedTypes.h`:
- `_softRestart()` / `freeMemory()` — ESP32-specific, move into callers or a `SystemUtils.h`
- `MIN()` / `MAX()` macros — replace with `std::min` / `std::max` in EfisSerial.cpp and .ino

After that, `Helpers.h` and `Helpers.cpp` can be deleted entirely.

### Pattern for each extraction

1. **Read the source** — identify exact lines and all dependencies
2. **Create the header** in `onspeed_core/` with pure function signatures
3. **Copy the logic** into the `.cpp`, removing global state reads (pass as params instead)
4. **Write tests** in `test/test_<module>/` using Unity framework
5. **Update the firmware call site** to call the new onspeed_core function, passing globals as arguments
6. **Run `pio test -e native`** to verify tests pass
7. **Run `pio run`** to verify firmware still compiles
8. **PR with both the extraction and the firmware shim update**

### Arduino `String` compatibility

For modules that touch `FOSConfig` (Tiers 2C, 4A), we need a compatibility layer:

```cpp
// onspeed_core/ArduinoCompat.h — only included in native builds
#ifndef ARDUINO
#include <string>
using String = std::string;
// Add Arduino String methods as needed: toFloat(), toInt(), substring(), etc.
#endif
```

This lets `FOSConfig` and the parsers compile in both environments without modifying the struct definition.

### What stays in firmware

These are genuinely hardware-coupled and should remain as thin wrappers:

- **WiFi/HTTP** — `ConfigWebServer.cpp` routing, HTML generation, WebSocket I/O
- **I2S audio** — DMA buffer writes, task scheduling
- **SPI sensor reads** — IMU, pressure, ADC chip select and transfer
- **SD/Flash I/O** — file open/read/write/close
- **FreeRTOS** — task creation, semaphores, `millis()`, `delay()`
- **Serial ports** — byte-level read/write for EFIS, boom, display, console

The firmware files become thin "glue" that reads hardware → calls onspeed_core → writes hardware.

---

## Estimated Impact

| Metric | Before | After Tier 1A | After (all tiers) |
|--------|--------|---------------|-------------------|
| Lines in onspeed_core | ~2,100 | ~2,200 | ~4,000 |
| Test count | 94 | 114 | ~200+ |
| Tested logic coverage | Filters & math only | + tone selection, mapfloat | Filters, math, audio, parsing, calibration, display |
| Safety-critical code tested | 0% | Tone selection covered | ~90% (tone selection, PercentLift, calibration) |

---

## Relationship to NEXT_STEPS.md

This plan supersedes NEXT_STEPS.md Phase 1 (§3.1-3.5) by expanding the scope. The NEXT_STEPS items map into this plan as:

| NEXT_STEPS.md | This plan |
|---------------|-----------|
| §3.1 PressureCalc | Tier 3B |
| §3.2 AHRSMath | Tier 3A |
| §3.3 Audio3DCalc | Tier 3C |
| §3.4 GLimitCalc | Tier 3C |
| §3.5 mapFloat | Tier 3C |

Tiers 1 and 2 are **new scope** — the safety-critical and data-integrity logic that NEXT_STEPS.md hadn't identified for extraction.
