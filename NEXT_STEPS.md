# Next Steps: Continued Refactoring

This document outlines the roadmap for continuing the OnSpeed refactoring work, with emphasis on bug fixes, extracting Arduino-independent code into `onspeed_core`, and adding new capabilities.

**Last updated:** February 2026

---

## Outstanding Bug Fixes

### From BUGFIX_REPORT.md (December 2024 audit)

Seven of the original 15 bugs have been fixed, plus the startup audio issue (PR #42). Six remain.

#### Fixed

| Bug | What | How |
|-----|------|-----|
| BUG-001 | `fDecelRate =- IasDerivative.Compute()` unary minus ambiguity | Now `IasDerivative.Compute() * fDecelSampleHz` with proper dt scaling |
| BUG-002 | `iVolPos` uninitialized on first EMA iteration | Added `bInitialized` flag, seeds first read directly |
| BUG-003 | Missing `fabs()` in G-limit asymmetric check | Both `g_AHRS.gRoll` and `g_AHRS.gYaw` now wrapped in `fabs()` |
| BUG-008 | LED copy-paste: both branches set `PIN_LED_KNOB` to 1 | LED control moved to `HeartbeatLedTask`; direct writes removed |
| BUG-009 | `mapfloat()` division-by-zero checked wrong expression | Now checks `(in_max - in_min) < 0.0001f` |

#### Still Present — High Priority

| Bug | File:Line | What | Risk |
|-----|-----------|------|------|
| BUG-007 (partial) | `AHRS.cpp:229` | `asin(KalmanVSI/fTAS)` — denominator fixed (uses fTAS), but **no clamp to [-1,1]**. NaN if ratio exceeds 1.0 at low TAS. | **High** — NaN propagates to DerivedAOA |
| BUG-004 | `Flaps.cpp:91-95` | `Flaps::Update(int)` — no bounds check on `iFlapsIndex` before indexing `g_Config.aFlaps[]` | **Medium** — out-of-bounds crash |
| BUG-010 | `Audio.cpp:489-524` | `UpdateTones()` uses `g_Flaps.iIndex` to index `g_Config.aFlaps[]` with no bounds check (4 places) | **Medium** — out-of-bounds crash |

#### Still Present — Lower Priority

| Bug | File:Line | What | Risk |
|-----|-----------|------|------|
| BUG-005/006 | `Switch.cpp:7-8` | Button flags `bSwitchDoSingleClick`/`bSwitchDoLongPress` not `volatile`, non-atomic toggle | **Low** — could miss button presses |
| BUG-011 | `Globals.h:317` | `g_fCoeffP` is `volatile double` — 64-bit reads not atomic on 32-bit ESP32, value can tear | **Low** — unlikely but possible corrupt read |
| ~~BUG-012~~ | ~~`AHRS.cpp:143-169`~~ | ~~sin()/cos() of constant bias angles recomputed every cycle at 208 Hz~~ | **Fixed** — PR #41 |

### ~~Startup Audio Failure~~ (PR #42)

**Status:** FIXED. Added I2S 3-attempt retry loop with `i2s.end()` + 50ms delay between retries. Moved `SetVoice(enVoiceEnabled)` from before `AudioPlayTask` creation to after task creation + `delay(100)`. Awaiting hardware validation (20+ power-cycle test).

### MCP3202 ADC Fix (PR #40)

**Status:** PR open, awaiting hardware test.

The external ADC is an MCP3202 (per schematic), but firmware was using MCP3204 protocol. Fixed the SPI command framing, swapped channel assignments to match schematic (CH0=FLAP, CH1=VOLUME), renamed all files/functions. Version bumped to 4.14.

---

## Current State (February 2026)

### Already in onspeed_core (Pure C++, Testable Natively)

```
software/Libraries/onspeed_core/
├── AOACalculator.h/cpp    - Stateful AOA calculator with EMA smoothing
├── CurveCalc.h/cpp        - Polynomial curve calculation
├── EKF6.h/cpp             - 6-state Extended Kalman Filter (attitude/AOA/gyro bias)
├── EMAFilter.h            - Header-only exponential moving average
├── KalmanFilter.h/cpp     - 3-state Kalman filter (altitude/VSI)
├── MadgwickFusion.h/cpp   - 2011 Madgwick AHRS algorithm
├── OnSpeedTypes.h         - Unit conversions, types, constants
└── SavGolDerivative.h     - Savitzky-Golay derivative filter
```

### Test Infrastructure

- **Framework:** Unity via PlatformIO
- **Run tests:** `pio test -e native`
- **9 test suites, 85 tests** covering all onspeed_core modules (including EKF6 + Octave comparison)

---

## Roadmap

### Phase 1: Continue Pure Math Extraction

**Priority:** Medium
**Status:** Candidates identified

#### 3.1 Pressure Calculations → `PressureCalc.h`

**Source:** `HscPressureSensor.cpp:121-137`

```cpp
namespace onspeed {
float countsToPSI(uint16_t counts, int countsMin, int countsMax,
                  float psiMin, float psiMax);
float dynamicPressureToIAS(float pressurePascals);
}
```

#### 3.2 AHRS Math Functions → `AHRSMath.h`

**Source:** `AHRS.cpp:92-181`

- `rotateGyroForBias()` - Installation bias rotation for gyros
- `rotateAccelForBias()` - Installation bias rotation for accelerometers
- `calcCentripetalCompensation()` - TAS-based acceleration compensation
- `calcFlightPathDeg()` - Flight path from VSI/TAS (include asin clamping — fixes BUG-007)
- `calcDerivedAOA()` - AOA from pitch - flight path

#### 3.3 3D Audio → `Audio3DCalc.h`

**Source:** `3DAudio.cpp:14-35`

```cpp
namespace onspeed {
void calc3DAudioGain(float lateralG, float& leftGain, float& rightGain);
}
```

#### 3.4 G-Limit Logic → `GLimitCalc.h`

**Source:** `gLimit.cpp:21-35`

```cpp
namespace onspeed {
void calcEffectiveGLimits(float basePosLimit, float baseNegLimit,
                          float rollRate, float yawRate,
                          float& effectivePosLimit, float& effectiveNegLimit);
bool isOverG(float verticalG, float posLimit, float negLimit);
}
```

#### 3.5 Linear Interpolation → add to `OnSpeedTypes.h`

**Source:** `Helpers.cpp:7-12`

```cpp
namespace onspeed {
inline float mapFloat(float x, float inMin, float inMax, float outMin, float outMax);
}
```

---

### Phase 2: Resource Profiling Infrastructure

**Priority:** Medium
**Status:** Needed for EKF validation

**Goal:** Measure CPU/memory usage to validate 208Hz operation and EKF feasibility.

#### 4.1 Create `Profiler.h`

```cpp
class TimingProfiler {
    void start();
    void stop();
    uint32_t getAvgMicros();
    uint32_t getMaxMicros();
};

uint32_t getTaskStackHighWaterMark(TaskHandle_t task);
uint32_t getFreeHeap();
void printResourceReport();
```

#### 4.2 Native Benchmark Tests

Create `test/test_benchmark/test_benchmark.cpp`:
- Time KalmanFilter::Update() - target <50us
- Time Madgwick::UpdateIMU() - target <100us
- Time EKF6::update() - target <200us (when implemented)

---

### ~~Phase 3: Port MATLAB Extended Kalman Filter~~

**Status:** IMPLEMENTED (`sritchie/ekf` branch)

EKF6 is implemented in `onspeed_core/EKF6.h/cpp` with 17 tests (12 standalone + 5 Octave comparison). Integrated into `AHRS.cpp` behind `g_Config.iAhrsAlgorithm` (0=Madgwick default, 1=EKF6). Uses master's 208Hz variable-dt architecture.

**Remaining EKF6 work:**

#### 3.1 Alpha Guard at Low Airspeed

**Priority:** Medium
**Status:** Not yet implemented

When IAS < 25kt, `AHRS.cpp` forces `KalmanVSI = 0`, making `FlightPath = 0` and `gamma_rad = 0`. The EKF6 alpha measurement becomes `alpha_meas = theta - 0 = theta`, so alpha just tracks theta. This is correct on the ground, but the transition when airspeed comes alive may cause slow re-convergence.

**Fix:** Reset EKF6 alpha covariance (increase `P_[2][2]`) when transitioning from below to above the IAS threshold, so the filter quickly re-acquires alpha from the gamma measurement. Requires adding a `resetAlphaCovariance()` method to EKF6 and calling it from AHRS when IAS crosses 25kt upward.

#### 3.2 Flight Testing and Tuning

**Priority:** High
**Status:** Blocked on flight test

- Fly with `iAhrsAlgorithm = 0` (Madgwick, default) and log EKF6 outputs in parallel
- Compare Madgwick SmoothedPitch/DerivedAOA against EKF6 theta/alpha in post-flight analysis
- Tune Q/R noise parameters if needed based on real sensor data
- Only consider making EKF6 the default after validated convergence in all flight regimes

---

### Phase 8: Add Vy, Vx, and Vbg Speed Indications

**Priority:** Medium
**Status:** Planning (from SETPOINT_IMPROVEMENT_PLAN.md)

**Phase 8a — Display only:**
- Add `fVyAOA`/`fVxAOA` to `SuFlaps` struct in Config.h (per-flap)
- Add wizard inputs for Vy/Vx IAS at gross weight (same weight-correction workflow as L/Dmax)
- Render markers in `html_liveview.h` SVG when set points are non-zero
- For RV-4: Vy ≈ 95 KIAS, Vx ≈ 73 KIAS at gross weight

**Phase 8b — Audio indication (future, after 8a):**
- Brief "gate" chirp as AOA passes through the Vy/Vx set point (not a zone)

---

### Phase 9: Web Server Performance

**Priority:** Low-Medium
**Status:** Planning (from SETPOINT_IMPROVEMENT_PLAN.md)

**Phase 9a — ETag caching (low effort, immediate win):**
Use firmware version string as ETag. A few lines per handler in `ConfigWebServer.cpp`. Returns `304 Not Modified` on revisit.

**Phase 9b — Pre-gzip assets at build time (medium effort):**
PlatformIO `extra_scripts` Python script. Serve gzipped `PROGMEM` arrays with `Content-Encoding: gzip`. Estimated 70-77% payload reduction.

**Phase 9c — Chunked transfer for dynamic pages (lower priority):**
Pattern already used in wizard flydecel page. Extend to other large pages.

**Order:** 9a first (few lines of code), then 9b, then 9c.

---

## Implementation Order

```
1. Bug fixes (BUG-007 asin clamp, BUG-004/010 bounds, ~~startup audio~~ PR #42)
2. MCP3202 ADC hardware validation (PR #40)
3. Math Extraction (Phase 1) — independent, enables testing
4. Profiling Infrastructure (Phase 2) — needed for EKF validation
5. ~~EKF6 Implementation (Phase 3)~~ — DONE, needs flight test
6. EKF6 alpha guard at low airspeed (Phase 3.1)
7. EKF6 flight testing and tuning (Phase 3.2) — after next flight
8. Vy/Vx display markers (Phase 8a) — after next calibration flight
9. Web server performance (Phase 9) — when time permits
```

---

## Previously Completed

### Fix Startup Audio Failure (PR #42)

Fixed ~15% silent boot rate. Added I2S 3-attempt retry loop in `Audio.cpp` (calls `i2s.end()` + 50ms delay between retries). Moved `SetVoice(enVoiceEnabled)` from before `AudioPlayTask` creation to after all tasks created + `delay(100)` in `OnSpeed-Gen3-ESP32.ino`.

### Precompute AHRS Installation Bias Trig (PR #41)

Moved sin()/cos() of constant pitch/roll bias angles from `AHRS::Process()` (called at 208 Hz) into `Init()`. Stored as cached members. Simplified rotation matrix by folding in the hardcoded yaw=0 assumption. Saves ~70-180 us per cycle.

### MCP3202 ADC Fix (v4.14, PR #40)

Fixed external ADC driver: schematic shows MCP3202T-CI/SN but firmware used MCP3204 protocol. Replaced SPI command framing, swapped channel assignments (CH0=FLAP, CH1=VOLUME per schematic), renamed all files/functions from Mcp3204 to Mcp3202.

### Alpha-0 / NAOA Setpoints (PR #37)

Replaced IAS-multiplier setpoints with weight-independent NAOA fractions from the lift-equation fit (`DerivedAOA = K / IAS² + alpha_0`). Added `fAlpha0` and `fAlphaStall` to config, rewrote wizard JS to use IAS-to-AOA fit for all setpoints, fixed PercentLift/AOA needle display bugs. See `docs/ALPHA0_AND_NAOA_SETPOINTS.md`.

### Runtime OAT Sensor & Boom Checksum Config (PR #38)

Converted `OAT_AVAILABLE` and `NOBOOMCHECKSUM` compile-time `#define` flags to runtime config booleans (`bOatSensor`, `bBoomChecksum`) with web UI dropdowns. Improved AHRS TAS to use best available OAT source (EFIS → DS18B20 → ISA approximation). Fixed latent `g_AHRS.TAS` → `g_AHRS.fTAS` bug in LogSensor.cpp.

### AHRS Timing Fix (50Hz → 208Hz)

Fixed critical timing mismatch where IMU hardware ran at 208Hz but software processed at 50Hz:
- Changed `IMU_SAMPLE_RATE` from 50 to 208 in `Globals.h`
- Updated `SensorReadTask` timing from 20ms to 5ms in `SensorIO.cpp`
- Recalculated EMA smoothing constants for new sample rate
- Updated `GYRO_SMOOTHING` from 30 to 125 samples

### Add `onspeed` Namespace

Added proper C++ namespacing to all onspeed_core library files:
- Wrapped all types/classes in `namespace onspeed { ... }`
- Updated application code with `using` declarations
- All tests updated and passing

### AOA/Smoother Separation (Previous PR)

- Created `EMAFilter` class in `onspeed_core/EMA.h`
- Simplified `CalcAOA()` to pure calculation (no smoothing)
- Removed `smoothedAOA` from `AOAResult` struct
- Added `clampAOA()` helper to `OnSpeedTypes.h`
- Created `AOACalculator` class combining pure calculation + smoothing
- Added `AOACalculator AoaCalc` member to `SensorIO` class
- Both live sensors and log replay use `g_Sensors.AoaCalc`
- Comprehensive unit tests for smoother and AOA calculation

### Bug Fixes (from BUGFIX_REPORT.md)

- BUG-001: Decel rate sign/accumulation fix
- BUG-002: Volume EMA initialization
- BUG-003: G-limit fabs() fix
- BUG-008: LED copy-paste fix (moved to HeartbeatTask)
- BUG-009: mapfloat() division-by-zero guard

### Architecture Summary

```
┌─────────────────────────────────────────────────────────────┐
│                    onspeed_core (testable)                  │
├─────────────────────────────────────────────────────────────┤
│  AOACalculator         EMAFilter           CalcAOA()        │
│  ┌─────────────┐       ┌─────────────┐   ┌─────────────┐    │
│  │ calculate() │──────>│ update()    │   │ (pure fn)   │    │
│  │ reset()     │       │ reset()     │   │             │    │
│  │ setSamples()│       └─────────────┘   └─────────────┘    │
│  └─────────────┘                                            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                 Application Layer (ESP32)                   │
├─────────────────────────────────────────────────────────────┤
│  SensorIO                                                   │
│  ┌─────────────────────────────────────┐                    │
│  │ AOACalculator AoaCalc;  // owns it  │                    │
│  │                                     │                    │
│  │ Read():                             │                    │
│  │   result = AoaCalc.calculate(...)   │                    │
│  │   AOA = result.aoa                  │                    │
│  └─────────────────────────────────────┘                    │
│                                                             │
│  LogReplay.cpp also uses g_Sensors.AoaCalc                  │
└─────────────────────────────────────────────────────────────┘
```

---

## Lower Priority Items

### Refactor AHRS.cpp to Use EMAFilter

The AHRS module contains inline exponential smoothing that should use `EMAFilter`:

```cpp
// Current inline smoothing in AHRS.cpp:
TASdiffSmoothed = iasSmoothing * fTASdiff + (1 - iasSmoothing) * TASdiffSmoothed;

// Proposed:
TASdiffSmoothed = _tasDiffFilter.update(fTASdiff);
```

### Move g_fCoeffP into g_Sensors

The global `g_fCoeffP` could be moved to `g_Sensors.coeffP` for better encapsulation.

### Numerical Safety Hardening

Create `SafeMath.h` with guarded operations:
- `safeDiv()` - Division with fallback
- `safeSqrt()` - Returns 0 for negative inputs
- `safeAsin()` - Clamps to valid domain

### Web Server Modernization

Long-term: Move to ESPAsyncWebServer + LittleFS for cleaner architecture.

### Dynon Cross-Calibration

Use OnSpeed deceleration run logs (which log `efisPercentLift`) to fit new Dynon `gain`/`offset` per flap via linear regression. Update Dynon `.dfg` config and audio thresholds (55/85% to match OnSpeed OSFast/StallWarn). Requires multi-run calibration flight first.

---

## Design Principles

1. **Pure functions in onspeed_core** - Math/algorithm code should be stateless and testable
2. **Instance-based state** - Each caller owns their filter instances with independent state
3. **Global state in application** - Config access and hardware globals stay in ESP32 code
4. **Header-only for simple utilities** - `EMAFilter`, `SavGolDerivative` work well header-only
5. **Comprehensive tests** - Every algorithm should have corresponding unit tests
6. **Namespace isolation** - All onspeed_core code in `namespace onspeed`

---

## References

### AHRS Algorithms
- [x-io Fusion Library (Madgwick 2022)](https://github.com/xioTechnologies/Fusion)
- [Madgwick Filter Documentation](https://ahrs.readthedocs.io/en/latest/filters/madgwick.html)
- [Mahony Filter Documentation](https://ahrs.readthedocs.io/en/latest/filters/mahony.html)

### Derived AOA
- [uAvionix Probeless AOA](https://uavionix.com/faq-items/how-is-probeless-angle-of-attack-determined/)
- [FAA/Texas A&M Derived AOA Research](https://vscl.tamu.edu/research/characterization-of-derived-angle-of-attack-and-flight-path-angle-algorithms-for-general-aviation-platforms-phase-i/)

### EKF Resources
- [ArduPilot EKF Documentation](https://ardupilot.org/dev/docs/extended-kalman-filter.html)
- [OnSpeed Gen2 MATLAB EKF](https://github.com/flyonspeed/OnSpeed-Gen2/blob/master/Software/Matlab/Extended_Kalman_Filter_with-alpha.m)
