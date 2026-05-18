# Air-data hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-channel validity bits, ram-rise SAT correction (default K=0.75), and bundle wire-version + CRC-8 into a v4.23 → v4.24 breaking wire change.

**Architecture:** Pure helper `CorrectSat()` + flags type `AirDataValid` in `onspeed_core`. Helper called from `Ahrs::updateTas_`. Wire encoder/decoder bumps to v4.24 with version-dispatch in the M5 reader (v4.23 fallback retained for one release). Python encoder hand-mirrors; parity test pins drift. JS replay filters teach themselves to skip on invalid bits; MP4 burn-in renders dashes.

**Tech Stack:** C++17 (Unity tests via `pio test -e native`), Python (frame.py + pytest), JS (mjs ES modules), MkDocs Material. PlatformIO build env `esp32s3-v4p`.

**Spec:** `docs/superpowers/specs/2026-05-18-airdata-hardening-design.md`

---

## File Structure

**Create (C++):**
- `software/Libraries/onspeed_core/src/types/AirDataValid.h` — flags type + bit enum
- `software/Libraries/onspeed_core/src/sensors/SatCorrect.h` — `CorrectSat()` declaration
- `software/Libraries/onspeed_core/src/sensors/SatCorrect.cpp` — implementation
- `software/Libraries/onspeed_core/src/proto/Crc8.h` — CRC-8 lookup + helper (header-only)
- `test/test_air_data_valid/test_air_data_valid.cpp`
- `test/test_sat_correct/test_sat_correct.cpp`
- `test/test_crc8/test_crc8.cpp`

**Modify (C++):**
- `software/Libraries/onspeed_core/src/types/AhrsOutputs.h` — add `AirDataValid valid;`
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp` — `updateTas_` integrates `CorrectSat`, sets bits
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.h` — `AhrsConfig` gets `oatRecoveryFactor`
- `software/Libraries/onspeed_core/src/proto/DisplaySerial.h` — v4.24 schema, version constants, validFlags
- `software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp` — emit v4.24 frame with CRC-8
- `software/OnSpeed-M5-Display/src/SerialRead.cpp` — version-dispatch parser, v4.23 fallback
- `software/OnSpeed-Gen3-ESP32/Config.h`, `Config.cpp`, `ConfigDefaults.h` — `fOatRecoveryFactor`
- `software/OnSpeed-Gen3-ESP32/ConfigXmlParse.cpp`, `ConfigXmlEmit.cpp` — XML element
- `software/OnSpeed-Gen3-ESP32/ConfigJson.cpp` — JSON key
- `software/OnSpeed-Gen3-ESP32/ConfigWebServer.cpp` — web form field
- `test/test_display_serial/test_display_serial.cpp` — v4.24 + fallback parsing
- `test/test_ahrs/...` — SAT correction integration

**Modify (Python):**
- `tools/onspeed_py/frame.py` — `FRAME_LEN=83`, `PAYLOAD_LEN=79`, validFlags field, CRC-8
- `tools/m5-replay/test_replay.py` — fixture length updates
- `tools/onspeed_py/tests/test_v424_byte_parity.py` (new)

**Modify (JS):**
- `docs/site/docs/data-and-logs/replay/lib/replay/presentationFilter.js` — skip-on-invalid
- `docs/site/docs/data-and-logs/replay/lib/replay/mp4Export.js` — dashes-on-invalid
- `tools/web/test/wire-validity.mjs` (new)
- `docs/site/tests/replay/m5sim-smoke.mjs` — assert wireVersion=24

**Modify (Docs):**
- `docs/site/docs/reference/serial-protocol.md` — v4.24 spec, CRC-8 algorithm, version dispatch
- `docs/site/docs/configuration/advanced.md` — OAT recovery factor
- `docs/site/docs/reference/glossary.md` — TAT/SAT/K/Ram Rise terms
- `docs/site/docs/calibration/how-aoa-works.md` — note on SAT correction
- `tools/m5-replay/README.md` — frame size update

---

## Task 1: `AirDataValid` type + tests

**Files:**
- Create: `software/Libraries/onspeed_core/src/types/AirDataValid.h`
- Test: `test/test_air_data_valid/test_air_data_valid.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// test/test_air_data_valid/test_air_data_valid.cpp
#include <unity.h>
#include <types/AirDataValid.h>

using onspeed::types::AirDataValid;

void setUp(void) {}
void tearDown(void) {}

void test_default_constructed_has_zero_bits(void)
{
    AirDataValid v;
    TEST_ASSERT_EQUAL_UINT32(0u, v.bits);
}

void test_set_marks_bit(void)
{
    AirDataValid v;
    v.set(AirDataValid::kOatRaw);
    TEST_ASSERT_TRUE(v.has(AirDataValid::kOatRaw));
    TEST_ASSERT_FALSE(v.has(AirDataValid::kIas));
}

void test_clear_removes_bit(void)
{
    AirDataValid v;
    v.set(AirDataValid::kIas);
    v.set(AirDataValid::kTas);
    v.clear(AirDataValid::kIas);
    TEST_ASSERT_FALSE(v.has(AirDataValid::kIas));
    TEST_ASSERT_TRUE(v.has(AirDataValid::kTas));
}

void test_bits_unique_and_in_low_16(void)
{
    // All defined bits must fit in low 16 (the wire format slot).
    constexpr uint32_t kLow16Mask = 0x0000FFFFu;
    constexpr uint32_t kAllDefined =
        AirDataValid::kOatRaw | AirDataValid::kOatSat |
        AirDataValid::kIas | AirDataValid::kPalt |
        AirDataValid::kTas | AirDataValid::kDensityAlt |
        AirDataValid::kDerivedAoa | AirDataValid::kVsi |
        AirDataValid::kPitch | AirDataValid::kRoll |
        AirDataValid::kPercentLift | AirDataValid::kFlapsPos |
        AirDataValid::kFrameSelfConsistent;
    TEST_ASSERT_EQUAL_UINT32(kAllDefined, kAllDefined & kLow16Mask);
}

void test_bit_constants_are_distinct(void)
{
    // Sum of single bits == OR of single bits → no two share a position.
    constexpr uint32_t kSum =
        AirDataValid::kOatRaw + AirDataValid::kOatSat +
        AirDataValid::kIas + AirDataValid::kPalt +
        AirDataValid::kTas + AirDataValid::kDensityAlt +
        AirDataValid::kDerivedAoa + AirDataValid::kVsi +
        AirDataValid::kPitch + AirDataValid::kRoll +
        AirDataValid::kPercentLift + AirDataValid::kFlapsPos +
        AirDataValid::kFrameSelfConsistent;
    constexpr uint32_t kOr =
        AirDataValid::kOatRaw | AirDataValid::kOatSat |
        AirDataValid::kIas | AirDataValid::kPalt |
        AirDataValid::kTas | AirDataValid::kDensityAlt |
        AirDataValid::kDerivedAoa | AirDataValid::kVsi |
        AirDataValid::kPitch | AirDataValid::kRoll |
        AirDataValid::kPercentLift | AirDataValid::kFlapsPos |
        AirDataValid::kFrameSelfConsistent;
    TEST_ASSERT_EQUAL_UINT32(kOr, kSum);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_constructed_has_zero_bits);
    RUN_TEST(test_set_marks_bit);
    RUN_TEST(test_clear_removes_bit);
    RUN_TEST(test_bits_unique_and_in_low_16);
    RUN_TEST(test_bit_constants_are_distinct);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd OnSpeed-Gen3 && pio test -e native -f test_air_data_valid
```

Expected: build error — `types/AirDataValid.h: No such file or directory`.

- [ ] **Step 3: Add test env entry to platformio.ini**

The native test env autoincludes new test dirs, but verify by grepping:

```bash
grep "test_filter\|build_src_filter" platformio.ini | head
```

If `[env:native]` already has globbing `test/*/` no change needed. Confirm by listing `[env:native]` block (lines around `env:native`). If no globbing exists, add the new test dir to `test_filter`.

- [ ] **Step 4: Write implementation**

```cpp
// software/Libraries/onspeed_core/src/types/AirDataValid.h
// Per-channel validity flags for the air-data pipeline.
//
// One bit per channel.  Producers set when the value is trustworthy;
// consumers check before use.  Default-constructed == all clear (safe
// boot state).  Lifted from the Dynon adahrs_data_t per-channel flags
// pattern (afs-audit/docs/ONSPEED_PRIOR_ART.md Tier-1 #1).
//
// Low 16 bits ride the v4.24 wire format `validFlags` field.  Upper 16
// bits are firmware-internal only (planned: cross-channel sanity layer,
// EFIS-source provenance flags).

#ifndef ONSPEED_CORE_TYPES_AIR_DATA_VALID_H
#define ONSPEED_CORE_TYPES_AIR_DATA_VALID_H

#include <cstdint>

namespace onspeed::types {

struct AirDataValid {
    uint32_t bits = 0;

    enum Bit : uint32_t {
        kOatRaw              = 1u <<  0,  // raw TAT, passed FilterOat
        kOatSat              = 1u <<  1,  // ram-rise-corrected SAT
        kIas                 = 1u <<  2,  // post iasAlive
        kPalt                = 1u <<  3,
        kTas                 = 1u <<  4,  // requires kOatSat + kIas + kPalt
        kDensityAlt          = 1u <<  5,  // requires kOatSat + kPalt
        kDerivedAoa          = 1u <<  6,  // requires kTas + kVsi
        kVsi                 = 1u <<  7,
        kPitch               = 1u <<  8,
        kRoll                = 1u <<  9,
        kPercentLift         = 1u << 10,
        kFlapsPos            = 1u << 11,
        // bits 12..14 reserved for future channels.
        // Pre-allocated meaning; producer impl follows in a future PR.
        kFrameSelfConsistent = 1u << 15,
        // bits 16..31 are firmware-internal only.
    };

    constexpr bool has(Bit b) const { return (bits & b) != 0; }
    constexpr void set(Bit b)       { bits |= b; }
    constexpr void clear(Bit b)     { bits &= ~static_cast<uint32_t>(b); }
};

}   // namespace onspeed::types

#endif  // ONSPEED_CORE_TYPES_AIR_DATA_VALID_H
```

- [ ] **Step 5: Run test, verify pass**

```bash
pio test -e native -f test_air_data_valid -v
```

Expected: all 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add software/Libraries/onspeed_core/src/types/AirDataValid.h \
        test/test_air_data_valid/test_air_data_valid.cpp
git commit -m "core: add AirDataValid flags type for per-channel validity

One uint32 with bits for OAT, SAT, IAS, TAS, Palt, density-alt,
DerivedAOA, VSI, pitch, roll, percent-lift, flaps. Low 16 bits ride
the upcoming v4.24 wire format. kFrameSelfConsistent at bit 15 is
pre-allocated for the future cross-channel sanity layer.

Pattern lifted from Dynon adahrs_data_t per-channel flags
(afs-audit/docs/ONSPEED_PRIOR_ART.md Tier-1 #1)."
```

---

## Task 2: CRC-8 helper + tests

**Files:**
- Create: `software/Libraries/onspeed_core/src/proto/Crc8.h`
- Test: `test/test_crc8/test_crc8.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// test/test_crc8/test_crc8.cpp
// CRC-8 with poly 0x07, init 0x00, no XOR-out, no reflection (SMBus).
// Standard test vectors verified against
// https://crccalc.com/?crc=123456789&method=CRC-8&datatype=ascii

#include <unity.h>
#include <cstring>
#include <proto/Crc8.h>

using onspeed::proto::Crc8;

void setUp(void) {}
void tearDown(void) {}

void test_empty_input_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x00, Crc8(nullptr, 0));
}

void test_single_zero_byte(void)
{
    const uint8_t zero = 0x00;
    TEST_ASSERT_EQUAL_UINT8(0x00, Crc8(&zero, 1));
}

void test_standard_check_vector(void)
{
    // CRC-8 of "123456789" == 0xF4 (SMBus check value).
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_UINT8(0xF4, Crc8(reinterpret_cast<const uint8_t*>(s), 9));
}

void test_single_bit_flip_changes_crc(void)
{
    const uint8_t buf1[] = {0x00, 0x00, 0x00, 0x00};
    uint8_t buf2[] = {0x00, 0x00, 0x00, 0x00};
    buf2[1] = 0x01;  // flip one bit
    TEST_ASSERT_NOT_EQUAL_UINT8(Crc8(buf1, 4), Crc8(buf2, 4));
}

void test_known_payload(void)
{
    // Deterministic vector for the spec doc.
    const char* s = "#124";
    TEST_ASSERT_EQUAL_UINT8(0x9A, Crc8(reinterpret_cast<const uint8_t*>(s), 4));
    // Note: this value was hand-computed via the reference algorithm;
    // if the implementation produces something else, fix the impl, not
    // this expected value.
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_input_is_zero);
    RUN_TEST(test_single_zero_byte);
    RUN_TEST(test_standard_check_vector);
    RUN_TEST(test_single_bit_flip_changes_crc);
    RUN_TEST(test_known_payload);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test, verify fail**

```bash
pio test -e native -f test_crc8
```

Expected: build error — header not found.

- [ ] **Step 3: Compute the reference values**

Before writing the impl, verify `"#124"` expected CRC. Use a Python one-liner with the standard CRC-8 SMBus polynomial:

```bash
python3 -c "
def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    return c
print(f'{crc8(b\"#124\"):02X}')
print(f'{crc8(b\"123456789\"):02X}')
"
```

Expected output:
```
9A
F4
```

If output is `9A` and `F4`, the test vectors are correct.

- [ ] **Step 4: Write implementation**

```cpp
// software/Libraries/onspeed_core/src/proto/Crc8.h
// CRC-8 with poly 0x07, init 0x00, no XOR-out, no reflection.
// Algorithm matches the SMBus / I²C-block-write CRC.
//
// Used by the v4.24 display-serial wire format to checksum the
// 79-byte payload (replaces the v4.23 sum-mod-256).  Strictly stronger
// error detection: catches all single-bit flips, all 2-bit errors
// within 256 bits, and most burst errors that the additive sum-mod
// silently masks.
//
// The 256-entry table is computed once at constexpr time; the runtime
// path is one xor + one table lookup per byte (~5 µs for a 79-byte
// frame on ESP32-S3).

#ifndef ONSPEED_CORE_PROTO_CRC8_H
#define ONSPEED_CORE_PROTO_CRC8_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace onspeed::proto {

namespace detail {

constexpr std::array<uint8_t, 256> MakeCrc8Table()
{
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint8_t c = static_cast<uint8_t>(i);
        for (int j = 0; j < 8; ++j) {
            c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ 0x07)
                           : static_cast<uint8_t>(c << 1);
        }
        table[i] = c;
    }
    return table;
}

inline constexpr std::array<uint8_t, 256> kCrc8Table = MakeCrc8Table();

}   // namespace detail

// CRC-8 of `len` bytes starting at `data`.
// `data` may be nullptr iff `len == 0`.
inline uint8_t Crc8(const uint8_t* data, std::size_t len)
{
    uint8_t c = 0;
    for (std::size_t i = 0; i < len; ++i) {
        c = detail::kCrc8Table[c ^ data[i]];
    }
    return c;
}

}   // namespace onspeed::proto

#endif  // ONSPEED_CORE_PROTO_CRC8_H
```

- [ ] **Step 5: Run test, verify pass**

```bash
pio test -e native -f test_crc8 -v
```

Expected: 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add software/Libraries/onspeed_core/src/proto/Crc8.h \
        test/test_crc8/test_crc8.cpp
git commit -m "core: add CRC-8 (poly 0x07, SMBus) for wire v4.24

256-entry compile-time table, ~5 µs to checksum a 79-byte payload on
ESP32-S3. Replaces sum-mod-256 in the upcoming wire bump.
Tier-2 #7 in afs-audit/docs/ONSPEED_PRIOR_ART.md."
```

---

## Task 3: `CorrectSat()` math helper + tests

**Files:**
- Create: `software/Libraries/onspeed_core/src/sensors/SatCorrect.h`
- Create: `software/Libraries/onspeed_core/src/sensors/SatCorrect.cpp`
- Test: `test/test_sat_correct/test_sat_correct.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// test/test_sat_correct/test_sat_correct.cpp
// Ram-rise correction: SAT_K = TAT_K / (1 + K · 0.2 · M²)
// with M = IAS_mps / a0  (a0 = 340.294 m/s).

#include <unity.h>
#include <cmath>
#include <sensors/SatCorrect.h>

using onspeed::sensors::CorrectSat;

void setUp(void) {}
void tearDown(void) {}

// ----- Identity / disable -----

void test_k_zero_is_identity_at_cruise_ias(void)
{
    auto sat = CorrectSat(/*tat*/ +5.0f, /*ias*/ 150.0f, /*k*/ 0.0f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, +5.0f, *sat);
}

void test_k_zero_is_identity_at_zero_ias(void)
{
    auto sat = CorrectSat(+15.0f, 0.001f, 0.0f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, +15.0f, *sat);
}

void test_k_zero_identity_at_cold_temp(void)
{
    auto sat = CorrectSat(-40.0f, 200.0f, 0.0f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -40.0f, *sat);
}

// ----- Worked examples from design spec section 3 -----

void test_n720ak_cruise_example(void)
{
    // N720AK at 8000ft cruise, 147 KIAS, +5°C TAT, K=0.75.
    // Expected SAT ≈ +2.96°C (2.04°C cooler than TAT).
    auto sat = CorrectSat(+5.0f, 147.0f, 0.75f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, +2.96f, *sat);
}

void test_lancair_fl250_example(void)
{
    // Lancair IV-P at FL250, 200 KIAS, -25°C TAT, K=0.75.
    // Expected SAT ≈ -28.34°C (3.34°C cooler).
    auto sat = CorrectSat(-25.0f, 200.0f, 0.75f);
    TEST_ASSERT_TRUE(sat.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -28.34f, *sat);
}

// ----- Monotonicity properties -----

void test_higher_ias_gives_lower_sat(void)
{
    auto sat100 = CorrectSat(+10.0f, 100.0f, 0.75f);
    auto sat200 = CorrectSat(+10.0f, 200.0f, 0.75f);
    TEST_ASSERT_TRUE(sat100.has_value());
    TEST_ASSERT_TRUE(sat200.has_value());
    TEST_ASSERT_LESS_THAN_FLOAT(*sat100, *sat200);  // 200kt SAT colder
}

void test_higher_k_gives_lower_sat(void)
{
    auto sat_low_k  = CorrectSat(+10.0f, 200.0f, 0.5f);
    auto sat_high_k = CorrectSat(+10.0f, 200.0f, 1.0f);
    TEST_ASSERT_TRUE(sat_low_k.has_value());
    TEST_ASSERT_TRUE(sat_high_k.has_value());
    TEST_ASSERT_LESS_THAN_FLOAT(*sat_low_k, *sat_high_k);  // K=1 cooler
}

// ----- Invalid inputs return nullopt -----

void test_nan_tat_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(NAN, 150.0f, 0.75f).has_value());
}

void test_inf_ias_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, INFINITY, 0.75f).has_value());
}

void test_negative_ias_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, -50.0f, 0.75f).has_value());
}

void test_zero_ias_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 0.0f, 0.75f).has_value());
}

void test_tat_above_range_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+150.0f, 150.0f, 0.75f).has_value());
}

void test_tat_below_range_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(-150.0f, 150.0f, 0.75f).has_value());
}

void test_k_negative_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 150.0f, -0.1f).has_value());
}

void test_k_above_one_returns_nullopt(void)
{
    TEST_ASSERT_FALSE(CorrectSat(+10.0f, 150.0f, 1.1f).has_value());
}

// ----- Pathological inputs -----

void test_extreme_tat_near_absolute_zero(void)
{
    // TAT = -99°C (in range) but produces a SAT_K that's barely above zero
    // at high IAS.  Math should still return a valid value (not nullopt)
    // since SAT_K > 0.
    auto sat = CorrectSat(-99.0f, 200.0f, 1.0f);
    TEST_ASSERT_TRUE(sat.has_value());
    // No specific value check — just must not crash and must produce
    // a finite real number.
    TEST_ASSERT_TRUE(std::isfinite(*sat));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_k_zero_is_identity_at_cruise_ias);
    RUN_TEST(test_k_zero_is_identity_at_zero_ias);
    RUN_TEST(test_k_zero_identity_at_cold_temp);
    RUN_TEST(test_n720ak_cruise_example);
    RUN_TEST(test_lancair_fl250_example);
    RUN_TEST(test_higher_ias_gives_lower_sat);
    RUN_TEST(test_higher_k_gives_lower_sat);
    RUN_TEST(test_nan_tat_returns_nullopt);
    RUN_TEST(test_inf_ias_returns_nullopt);
    RUN_TEST(test_negative_ias_returns_nullopt);
    RUN_TEST(test_zero_ias_returns_nullopt);
    RUN_TEST(test_tat_above_range_returns_nullopt);
    RUN_TEST(test_tat_below_range_returns_nullopt);
    RUN_TEST(test_k_negative_returns_nullopt);
    RUN_TEST(test_k_above_one_returns_nullopt);
    RUN_TEST(test_extreme_tat_near_absolute_zero);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test, verify fail**

```bash
pio test -e native -f test_sat_correct
```

Expected: build error — `sensors/SatCorrect.h: No such file`.

- [ ] **Step 3: Write header**

```cpp
// software/Libraries/onspeed_core/src/sensors/SatCorrect.h
// Ram-rise correction from total air temperature (TAT) to static air
// temperature (SAT).
//
// Formula:
//   M_proxy = IAS_mps / a0,        a0 = 340.294 m/s (ISA sea-level)
//   SAT_K   = TAT_K / (1 + K * 0.2 * M_proxy²)
//   SAT_C   = SAT_K - 273.15
//
// K is the probe recovery factor:
//   K = 0.0   disables correction (returns TAT unchanged)
//   K = 0.75  bare/exposed thermistor (typical GA install, default)
//   K = 1.0   ideal TAT probe (Kiel/shielded)
//
// IAS-as-Mach proxy: at M < 0.4 (every aircraft OnSpeed targets),
// M_IAS is within 1% of M_TAS, producing SAT error <0.1°C.  The
// resulting precision is well below K-uncertainty across published
// probe specs.  See design spec §3 for worked examples.
//
// Returns std::nullopt if any input is non-finite, IAS or TAT are out
// of physical range, or the corrected SAT_K would be non-positive.

#ifndef ONSPEED_CORE_SENSORS_SAT_CORRECT_H
#define ONSPEED_CORE_SENSORS_SAT_CORRECT_H

#include <optional>

namespace onspeed::sensors {

// Valid input bounds (matched to existing OatConvert::FilterOat).
inline constexpr float kSatCorrectMinTatC = -100.0f;
inline constexpr float kSatCorrectMaxTatC =  100.0f;
inline constexpr float kSatCorrectMinK    =   0.0f;
inline constexpr float kSatCorrectMaxK    =   1.0f;

std::optional<float> CorrectSat(float tatCelsius,
                                float iasKt,
                                float recoveryFactorK);

}   // namespace onspeed::sensors

#endif  // ONSPEED_CORE_SENSORS_SAT_CORRECT_H
```

- [ ] **Step 4: Write implementation**

```cpp
// software/Libraries/onspeed_core/src/sensors/SatCorrect.cpp

#include <sensors/SatCorrect.h>

#include <cmath>

#include <util/OnSpeedTypes.h>

namespace onspeed::sensors {

namespace {

// ISA sea-level speed of sound (m/s). Used as the M-proxy reference.
constexpr float kIsaSpeedOfSoundMps = 340.2941f;

// 273.15 — Celsius/Kelvin offset.
constexpr float kKelvinOffset = 273.15f;

// 0.2 = (γ − 1) / 2 for γ = 1.4 (air).
constexpr float kCompFactor = 0.2f;

}   // namespace

std::optional<float> CorrectSat(float tatCelsius,
                                float iasKt,
                                float recoveryFactorK)
{
    // Range and finiteness checks.
    if (!std::isfinite(tatCelsius) || !std::isfinite(iasKt) ||
        !std::isfinite(recoveryFactorK)) {
        return std::nullopt;
    }
    if (tatCelsius < kSatCorrectMinTatC || tatCelsius > kSatCorrectMaxTatC) {
        return std::nullopt;
    }
    if (iasKt <= 0.0f) {
        return std::nullopt;
    }
    if (recoveryFactorK < kSatCorrectMinK ||
        recoveryFactorK > kSatCorrectMaxK) {
        return std::nullopt;
    }

    // Identity short-circuit: K == 0 returns TAT unchanged.
    if (recoveryFactorK == 0.0f) {
        return tatCelsius;
    }

    const float iasMps  = onspeed::kts2mps(iasKt);
    const float mProxy  = iasMps / kIsaSpeedOfSoundMps;
    const float divisor = 1.0f + recoveryFactorK * kCompFactor * mProxy * mProxy;

    const float tatK = tatCelsius + kKelvinOffset;
    const float satK = tatK / divisor;

    if (satK <= 0.0f || !std::isfinite(satK)) {
        return std::nullopt;
    }

    return satK - kKelvinOffset;
}

}   // namespace onspeed::sensors
```

- [ ] **Step 5: Run test, verify pass**

```bash
pio test -e native -f test_sat_correct -v
```

Expected: all 16 tests pass.

- [ ] **Step 6: Commit**

```bash
git add software/Libraries/onspeed_core/src/sensors/SatCorrect.{h,cpp} \
        test/test_sat_correct/test_sat_correct.cpp
git commit -m "core: add CorrectSat() ram-rise correction (TAT → SAT)

SAT_K = TAT_K / (1 + K·0.2·M²) with IAS-as-Mach proxy.  Default K=0.75
covers bare-thermistor GA installs; K=0 disables.  Worked examples
verified against design spec §3 (N720AK cruise: 2.04°C correction;
Lancair FL250: 3.34°C correction)."
```

---

## Task 4: Wire-up `AirDataValid` into `AhrsOutputs`

**Files:**
- Modify: `software/Libraries/onspeed_core/src/types/AhrsOutputs.h`

- [ ] **Step 1: Read current `AhrsOutputs.h`**

```bash
cat software/Libraries/onspeed_core/src/types/AhrsOutputs.h | head -60
```

Note where existing fields end and find a logical insertion point (after `tasDotMps2`).

- [ ] **Step 2: Add the include and field**

Insert near the existing includes:

```cpp
#include <types/AirDataValid.h>
```

After the last numeric output field (likely `tasDotMps2` or `vsiFpm`), add:

```cpp
// Per-channel validity bitmap.  Producers (Ahrs::Step, SensorIO,
// EFIS parsers) set bits when their values are trustworthy; consumers
// (DisplaySerial encoder, log writer, tone calc) check before use.
// Default-constructed == all clear, which means "nothing trusted."
onspeed::types::AirDataValid valid;
```

- [ ] **Step 3: Build the existing AHRS test to confirm no break**

```bash
pio test -e native -f test_ahrs -v
```

Expected: existing tests still pass. The new field is default-constructed to zero and not yet read, so behavior is unchanged.

- [ ] **Step 4: Commit**

```bash
git add software/Libraries/onspeed_core/src/types/AhrsOutputs.h
git commit -m "core: add AirDataValid field to AhrsOutputs

Producers will populate bits in subsequent tasks; consumers check
before use.  Default-constructed (all clear) is fail-safe."
```

---

## Task 5: Integrate `CorrectSat` into `Ahrs::updateTas_`

**Files:**
- Modify: `software/Libraries/onspeed_core/src/ahrs/Ahrs.h`
- Modify: `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp`
- Modify: `test/test_ahrs/...` (extend existing tests)

- [ ] **Step 1: Read existing `AhrsConfig` and `updateTas_`**

```bash
grep -n "oatRecoveryFactor\|fOatRecovery\|struct AhrsConfig" software/Libraries/onspeed_core/src/ahrs/Ahrs.h
sed -n '140,210p' software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp
```

Confirm: `AhrsConfig` has no `oatRecoveryFactor` field yet; `updateTas_` uses `fOatC` directly in the density math.

- [ ] **Step 2: Add `oatRecoveryFactor` to `AhrsConfig`**

In `Ahrs.h`, find `struct AhrsConfig` and add (after the existing float fields, before any constructors):

```cpp
// Probe recovery factor for ram-rise SAT correction (0..1).
// 0 disables correction; 0.75 is the default for bare/exposed
// thermistor installs.  See sensors/SatCorrect.h.
float oatRecoveryFactor = 0.75f;
```

- [ ] **Step 3: Add test for SAT integration**

Append to the existing AHRS test file (`test/test_ahrs/test_ahrs.cpp`) — locate the file first:

```bash
find test/test_ahrs -name "*.cpp" -type f
```

Add a new test that mirrors an existing TAS test pattern. Find a representative test (`grep -n "test_tas\|test_ias" test/test_ahrs/*.cpp` will point at the established setup).

```cpp
// At the top of the file, add:
#include <types/AirDataValid.h>
using onspeed::types::AirDataValid;

// New test, near the other TAS tests:
void test_updateTas_sets_kOatSat_bit_when_correction_applies(void)
{
    AhrsConfig cfg{};
    cfg.oatRecoveryFactor = 0.75f;
    Ahrs ahrs(cfg);

    AhrsInputs in = makeRepresentativeInputs();  // use whatever existing test helper
    in.sensors.oatCelsius = +5.0f;
    in.sensors.iasKt      = 150.0f;
    in.sensors.paltFt     = 8000.0f;
    in.useInternalOat     = true;
    in.iasUpdateTimestampUs = 1000;

    ahrs.Step(in);
    TEST_ASSERT_TRUE(ahrs.Outputs().valid.has(AirDataValid::kOatSat));
    TEST_ASSERT_TRUE(ahrs.Outputs().valid.has(AirDataValid::kTas));
}

void test_updateTas_clears_kOatSat_when_no_oat(void)
{
    AhrsConfig cfg{};
    cfg.oatRecoveryFactor = 0.75f;
    Ahrs ahrs(cfg);

    AhrsInputs in = makeRepresentativeInputs();
    in.useInternalOat = false;
    in.useEfisOat     = false;
    in.iasUpdateTimestampUs = 1000;

    ahrs.Step(in);
    TEST_ASSERT_FALSE(ahrs.Outputs().valid.has(AirDataValid::kOatSat));
}

void test_updateTas_k_zero_matches_legacy_tas(void)
{
    // K=0 disables SAT correction; TAS should match the pre-PR formula
    // bit-exactly for a given input fixture.
    AhrsConfig cfg{};
    cfg.oatRecoveryFactor = 0.0f;
    Ahrs ahrs(cfg);

    AhrsInputs in = makeRepresentativeInputs();
    in.sensors.oatCelsius = +5.0f;
    in.sensors.iasKt      = 147.0f;
    in.sensors.paltFt     = 8000.0f;
    in.useInternalOat     = true;
    in.iasUpdateTimestampUs = 1000;

    ahrs.Step(in);
    const float fTas = ahrs.TasMps();
    // Hand-compute against the legacy formula:
    //   DA from (TAT, Palt); divisor = 1 - 6.8755856e-6 * DA;
    //   TAS_mps = kts2mps(ias) / pow(divisor, 2.12794)
    const float kelvin = 273.15f;
    const float tRate  = 0.00198119993f;
    const float isaK   = 15.0f - tRate * 8000.0f + kelvin;
    const float oatK   = 5.0f + kelvin;
    const float da     = 8000.0f + (isaK/tRate) * (1.0f - std::pow(isaK/oatK, 0.2349690f));
    const float divisor = 1.0f - 6.8755856e-6f * da;
    const float legacyTas = onspeed::kts2mps(147.0f) / std::pow(divisor, 2.12794f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, legacyTas, fTas);
}
```

Register the new tests in `main()` at the bottom of the file.

If `makeRepresentativeInputs` doesn't exist by that name, look for the existing test helper — there's almost certainly one. Adapt the call accordingly.

- [ ] **Step 4: Run new tests, verify fail**

```bash
pio test -e native -f test_ahrs
```

Expected: the three new tests fail (kOatSat bit not set; TAS differs from legacy formula because the SAT correction is active).

- [ ] **Step 5: Modify `Ahrs.cpp::updateTas_`**

Find the block at lines ~158-198 (use `grep -n "if (bHaveOat)" software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp` to confirm). Replace it:

```cpp
    float fOatC = 0.0f;
    bool  bHaveOat = false;

    if (in.useEfisOat) {
        fOatC    = in.efisOatCelsius;
        bHaveOat = oatInBand(fOatC);
    }

    if (!bHaveOat && in.useInternalOat) {
        fOatC    = in.sensors.oatCelsius;
        bHaveOat = oatInBand(fOatC);
    }

    if (bHaveOat) {
        outputs_.valid.set(onspeed::types::AirDataValid::kOatRaw);
    } else {
        outputs_.valid.clear(onspeed::types::AirDataValid::kOatRaw);
    }

    // Apply ram-rise correction.  If the helper returns nullopt
    // (e.g. IAS not yet alive), fall through to raw TAT in the
    // legacy density math — soft-degrade rather than hard-fail.
    float fSatC = fOatC;
    bool  bHaveSat = false;
    if (bHaveOat) {
        auto satC = onspeed::sensors::CorrectSat(fOatC,
                                                 in.sensors.iasKt,
                                                 cfg_.oatRecoveryFactor);
        if (satC.has_value()) {
            fSatC    = *satC;
            bHaveSat = true;
            outputs_.valid.set(onspeed::types::AirDataValid::kOatSat);
        } else {
            outputs_.valid.clear(onspeed::types::AirDataValid::kOatSat);
        }
    } else {
        outputs_.valid.clear(onspeed::types::AirDataValid::kOatSat);
    }

    if (bHaveOat) {
        const float Kelvin    = 273.15f;
        const float Temp_rate = 0.00198119993f;
        float fISA_temp_k = 15.0f - Temp_rate * in.sensors.paltFt + Kelvin;
        float fSAT_k      = fSatC + Kelvin;

        if (fSAT_k > 0.0f) {
            float fDA      = in.sensors.paltFt + (fISA_temp_k / Temp_rate)
                             * (1.0f - std::pow(fISA_temp_k / fSAT_k, 0.2349690f));
            float fDivisor = 1.0f - 6.8755856e-6f * fDA;
            if (fDivisor > 0.0f) {
                tas_ = onspeed::kts2mps(in.sensors.iasKt
                                        / std::pow(fDivisor, 2.12794f));
                outputs_.valid.set(onspeed::types::AirDataValid::kTas);
            } else {
                tas_ = onspeed::kts2mps(in.sensors.iasKt
                                        * (1.0f + in.sensors.paltFt / 1000.0f * 0.02f));
                outputs_.valid.clear(onspeed::types::AirDataValid::kTas);
            }
        } else {
            tas_ = onspeed::kts2mps(in.sensors.iasKt
                                    * (1.0f + in.sensors.paltFt / 1000.0f * 0.02f));
            outputs_.valid.clear(onspeed::types::AirDataValid::kTas);
        }
    } else {
        tas_ = onspeed::kts2mps(in.sensors.iasKt
                                * (1.0f + in.sensors.paltFt / 1000.0f * 0.02f));
        outputs_.valid.clear(onspeed::types::AirDataValid::kTas);
    }

    (void)bHaveSat;  // currently informational; kTas covers the consumer signal
```

Add include at top of `Ahrs.cpp`:

```cpp
#include <sensors/SatCorrect.h>
#include <types/AirDataValid.h>
```

- [ ] **Step 6: Run tests, verify pass**

```bash
pio test -e native -f test_ahrs -v
```

Expected: all three new tests pass, plus all existing AHRS tests still pass.

- [ ] **Step 7: Run the full regression harness**

```bash
./tools/regression/run_snapshot.py
```

Expected: golden CSV diff shows changes in TAS column (because K=0.75 default now corrects). Inspect the diff carefully; document the magnitude in the commit.

If only the TAS column has changed and the values are within the expected 0.2–0.6% band, regenerate the golden:

```bash
./tools/regression/run_snapshot.py --update-golden
```

- [ ] **Step 8: Commit**

```bash
git add software/Libraries/onspeed_core/src/ahrs/Ahrs.{h,cpp} \
        test/test_ahrs/*.cpp \
        tools/regression/fixtures/golden.csv
git commit -m "ahrs: apply ram-rise correction to OAT before density math

updateTas_ now routes through CorrectSat() before feeding the
density-altitude / TAS formula.  Sets AirDataValid kOatRaw, kOatSat,
kTas bits per state.  K=0 in config restores legacy behavior bit-for-
bit (test_updateTas_k_zero_matches_legacy_tas pins this).

Regression golden updated: TAS column shifts 0.2–0.6% across the
fixture log, matching design §3 worked examples."
```

---

## Task 6: Config plumbing for `fOatRecoveryFactor`

**Files:**
- Modify: `software/OnSpeed-Gen3-ESP32/ConfigDefaults.h`
- Modify: `software/OnSpeed-Gen3-ESP32/Config.h`
- Modify: `software/OnSpeed-Gen3-ESP32/Config.cpp`
- Modify: `software/OnSpeed-Gen3-ESP32/ConfigXmlParse.cpp`
- Modify: `software/OnSpeed-Gen3-ESP32/ConfigXmlEmit.cpp`
- Modify: `software/OnSpeed-Gen3-ESP32/ConfigJson.cpp` (and `.h` if present)
- Modify: `software/OnSpeed-Gen3-ESP32/ConfigWebServer.cpp`

Approach: small, focused edits at each layer, building up. No new test file — the existing `test_config_*` suites will catch regression.

- [ ] **Step 1: Find conventional shape for similar config**

```bash
grep -n "fPitchBias\|kfPitchBias" software/OnSpeed-Gen3-ESP32/ConfigDefaults.h \
                                  software/OnSpeed-Gen3-ESP32/Config.{h,cpp}
```

This shows the established pattern for a single-float aircraft-tunable parameter. Mirror that exactly.

- [ ] **Step 2: Add default**

In `ConfigDefaults.h`, near other AHRS/sensor defaults:

```cpp
// Probe recovery factor for OAT ram-rise correction.  Range [0.0, 1.0]:
//   0.0  = disable (TAS uses raw TAT, legacy behavior)
//   0.75 = bare/exposed thermistor (default, typical GA install)
//   1.0  = ideal TAT probe (Kiel/shielded)
static constexpr float kfOatRecoveryFactor = 0.75f;
```

- [ ] **Step 3: Add to `SuConfig` struct**

In `Config.h`, find `struct SuConfig` and add a field near the other float aircraft-config members:

```cpp
float fOatRecoveryFactor;
```

- [ ] **Step 4: Initialize in `Config::SetDefaults()`**

In `Config.cpp`, in `SetDefaults()`, near other float assignments:

```cpp
fOatRecoveryFactor = kfOatRecoveryFactor;
```

- [ ] **Step 5: XML parse**

In `ConfigXmlParse.cpp`, near other simple float-parse calls (find one like `fPitchBias`):

```cpp
} else if (strcmp(elName, "OatRecoveryFactor") == 0) {
    float v;
    if (ParseFloat(elText, v) && v >= 0.0f && v <= 1.0f) {
        out.fOatRecoveryFactor = v;
    } else {
        out.fOatRecoveryFactor = kfOatRecoveryFactor;
        g_ErrorLogger.warn(ErrorLogger::kModConfig,
                           "OatRecoveryFactor out of range, using default");
    }
}
```

Exact else-if placement depends on file layout; insert alphabetically among elements where possible.

- [ ] **Step 6: XML emit**

In `ConfigXmlEmit.cpp`, near other float emit calls (find one like `AddFloat("PitchBias"`):

```cpp
emit.AddFloat("OatRecoveryFactor", in.fOatRecoveryFactor);
```

- [ ] **Step 7: JSON**

Find the conventional pattern (`grep -n "fPitchBias\|pitchBias" software/OnSpeed-Gen3-ESP32/ConfigJson.{cpp,h}`). Mirror exactly. Likely:

```cpp
// emit
doc["oatRecoveryFactor"] = in.fOatRecoveryFactor;

// parse  (with clamp)
{
    float v = doc["oatRecoveryFactor"] | kfOatRecoveryFactor;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    out.fOatRecoveryFactor = v;
}
```

- [ ] **Step 8: Web form**

In `ConfigWebServer.cpp`, find the AHRS/air-data form section (`grep -n "PitchBias\|pitch.bias" ConfigWebServer.cpp`). Add a form field. Mirror an existing slider:

```cpp
// Form input HTML
html += "<tr><td>OAT probe recovery factor (K):</td>"
        "<td><input type='number' name='oatRecoveryFactor' "
        "min='0.0' max='1.0' step='0.05' value='";
html += String(g_Config.fOatRecoveryFactor, 2);
html += "'></td><td>0 = disable, 0.75 = bare probe, 1.0 = TAT probe</td></tr>";
```

```cpp
// Form POST handler — near other float-field reads
if (server.hasArg("oatRecoveryFactor")) {
    float v = server.arg("oatRecoveryFactor").toFloat();
    if (v >= 0.0f && v <= 1.0f) {
        g_Config.fOatRecoveryFactor = v;
    }
}
```

Wire it into `Ahrs::Reconfigure` — find where `g_Config.fPitchBias` is propagated to `AhrsConfig.pitchBiasDeg` (likely in `Config.cpp::Apply` or in the web-save handler). Add an analogous line:

```cpp
ahrsCfg.oatRecoveryFactor = g_Config.fOatRecoveryFactor;
```

- [ ] **Step 9: Build firmware**

```bash
pio run -e esp32s3-v4p 2>&1 | tail -10
```

Expected: clean build, no warnings (zero-warning policy).

- [ ] **Step 10: Run all native tests**

```bash
pio test -e native 2>&1 | tail -20
```

Expected: all suites pass. Existing config tests should accept the new field gracefully (parsers tolerate missing elements with defaults).

- [ ] **Step 11: Commit**

```bash
git add software/OnSpeed-Gen3-ESP32/Config*.{h,cpp}
git commit -m "config: add fOatRecoveryFactor parameter (default 0.75)

Plumbed through XML, JSON, web form, and Config::Apply to
AhrsConfig.oatRecoveryFactor.  Range [0.0, 1.0] enforced at every
layer; out-of-range falls back to default with ErrorLogger warning.
K=0 is the user-side kill switch for the SAT correction."
```

---

## Task 7: Wire format v4.24 — encoder

**Files:**
- Modify: `software/Libraries/onspeed_core/src/proto/DisplaySerial.h`
- Modify: `software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp`
- Modify: `test/test_display_serial/test_display_serial.cpp`

- [ ] **Step 1: Read existing header to find exact insertion points**

```bash
sed -n '1,40p' software/Libraries/onspeed_core/src/proto/DisplaySerial.h
grep -n "kDisplayFrameSizeBytes\|kPayload\|FrameVersion\|kWireVersion" \
     software/Libraries/onspeed_core/src/proto/DisplaySerial.h
```

- [ ] **Step 2: Update size and version constants in `DisplaySerial.h`**

Replace `kDisplayFrameSizeBytes` constant:

```cpp
// v4.24 frame: 79-byte payload + 2-byte CRC-8 + CRLF = 83 bytes.
inline constexpr size_t kDisplayFrameSizeBytes = 83;
inline constexpr size_t kDisplayFrameChecksumLen = 79;  // bytes 0..78
inline constexpr unsigned kWireVersion = 24;
```

Update the frame-layout block-comment at top of file to the v4.24 layout (full table from spec section 4).

Add `AirDataValid` to `DisplayBuildInputs`:

```cpp
#include <types/AirDataValid.h>

struct DisplayBuildInputs {
    // ... existing fields ...

    // Per-channel validity. Low 16 bits ride the wire's validFlags
    // field. Producers populate this; the encoder propagates it.
    onspeed::types::AirDataValid valid;

    // DEPRECATED: use `valid.has(AirDataValid::kIas)` instead.
    // Kept for one release for compatibility with in-flight branches.
    [[deprecated("use valid.has(AirDataValid::kIas)")]]
    bool iasValid = true;
};
```

- [ ] **Step 3: Write new v4.24 test cases**

Append to `test/test_display_serial/test_display_serial.cpp`:

```cpp
#include <proto/Crc8.h>
#include <types/AirDataValid.h>

using onspeed::types::AirDataValid;

void test_v424_frame_is_83_bytes(void)
{
    DisplayBuildInputs in = makeRepresentativeInputs();
    in.valid.set(AirDataValid::kIas);
    in.valid.set(AirDataValid::kOatRaw);
    uint8_t buf[128] = {0};
    size_t n = BuildDisplayFrame(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(83u, n);
    TEST_ASSERT_EQUAL_UINT8('#', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('1', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('2', buf[2]);
    TEST_ASSERT_EQUAL_UINT8('4', buf[3]);
}

void test_v424_validFlags_field_round_trips(void)
{
    DisplayBuildInputs in = makeRepresentativeInputs();
    in.valid.set(AirDataValid::kIas);
    in.valid.set(AirDataValid::kPalt);
    in.valid.set(AirDataValid::kOatSat);
    uint8_t buf[128] = {0};
    BuildDisplayFrame(in, buf, sizeof(buf));
    // validFlags at offset 75, width 4 hex chars.
    char flagStr[5] = {0};
    memcpy(flagStr, buf + 75, 4);
    uint32_t flags = strtoul(flagStr, nullptr, 16);
    TEST_ASSERT_EQUAL_UINT32(in.valid.bits & 0xFFFFu, flags);
}

void test_v424_crc8_matches_payload(void)
{
    DisplayBuildInputs in = makeRepresentativeInputs();
    in.valid.set(AirDataValid::kIas);
    uint8_t buf[128] = {0};
    BuildDisplayFrame(in, buf, sizeof(buf));
    // Checksum at offset 79, width 2 hex chars.
    char crcStr[3] = {0};
    memcpy(crcStr, buf + 79, 2);
    uint8_t crcFromFrame = static_cast<uint8_t>(strtoul(crcStr, nullptr, 16));
    uint8_t crcExpected  = onspeed::proto::Crc8(buf, 79);
    TEST_ASSERT_EQUAL_UINT8(crcExpected, crcFromFrame);
}

void test_v424_invalid_ias_emits_9999_and_clears_bit(void)
{
    DisplayBuildInputs in = makeRepresentativeInputs();
    in.iasKt = 100.0f;
    // valid.kIas left clear → encoder must emit 9999.
    uint8_t buf[128] = {0};
    BuildDisplayFrame(in, buf, sizeof(buf));
    // iasKt at offset 13, width 4.
    char iasStr[5] = {0};
    memcpy(iasStr, buf + 13, 4);
    TEST_ASSERT_EQUAL_STRING("9999", iasStr);
}

void test_v424_terminator_is_crlf(void)
{
    DisplayBuildInputs in = makeRepresentativeInputs();
    uint8_t buf[128] = {0};
    BuildDisplayFrame(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(0x0D, buf[81]);
    TEST_ASSERT_EQUAL_UINT8(0x0A, buf[82]);
}
```

Register them all in `main()`.

- [ ] **Step 4: Run tests, verify fail**

```bash
pio test -e native -f test_display_serial
```

Expected: tests fail — `BuildDisplayFrame` still produces a 77-byte v4.23 frame, no validFlags, sum-mod-256 checksum.

- [ ] **Step 5: Modify the encoder**

Open `DisplaySerial.cpp`, find `BuildDisplayFrame`. Update format string (insert `%02u` after `#1`, insert `%04X` before checksum), update the `snprintf` arg list, swap the checksum compute. The full new payload-emit block:

```cpp
const int iChars = std::snprintf(
    staging,
    sizeof(staging),
    "#1%02u"                           // magic + wireVersion
    "%+04i%+05i%04u%+06i%+05i%+03i"    // pitch, roll, ias, palt, turn, latG
    "%+03i%03u%+04i%+03i%+04i%+03i"    // vertG, pct, vsi, oat, fpa, flaps
    "%02u%02u%02u%02u%+03i%+03i"       // tonesOn, fast, slow, warn, fMin, fMax
    "%+04i%+02i%02u%02u"               // onset, spinCue, dataMark, pipPct
    "%04X",                            // validFlags
    onspeed::proto::kWireVersion,
    iPitch10, iRoll10, uIas10, iPaltFt, iYaw10, iLatG100,
    iVertG10, uPctLift, iVsi10, iOatC, iFpa10, iFlapsDeg,
    uTonesOnPct, uFastPct, uSlowPct, uWarnPct, iFlapsMin, iFlapsMax,
    iOnset100, iSpinCue, uDataMark2, uPipPct,
    static_cast<unsigned>(in.valid.bits & 0xFFFFu));

if (iChars != static_cast<int>(kDisplayFrameChecksumLen)) return 0;

// CRC-8 of payload bytes [0..79).
const uint8_t crc = onspeed::proto::Crc8(
    reinterpret_cast<const uint8_t*>(staging), kDisplayFrameChecksumLen);

const int iAppended = std::snprintf(
    staging + kDisplayFrameChecksumLen,
    sizeof(staging) - kDisplayFrameChecksumLen,
    "%02X\r\n", crc);
if (iAppended != 4) return 0;

memcpy(out, staging, kDisplayFrameSizeBytes);
return kDisplayFrameSizeBytes;
```

Replace the existing iasValid-driven sentinel logic with:

```cpp
const unsigned uIas10 = in.valid.has(onspeed::types::AirDataValid::kIas)
    ? SafeScaledUInt(in.iasKt, 10.0f, 0, 9999)
    : static_cast<unsigned>(kIasInvalidWireSentinel);
```

Add include at top:

```cpp
#include <proto/Crc8.h>
#include <types/AirDataValid.h>
```

- [ ] **Step 6: Run tests, verify pass**

```bash
pio test -e native -f test_display_serial -v
```

Expected: all v4.24 tests pass. Existing v4.23 byte-equivalence tests will now fail — that's expected, they need to be rewritten or deleted (next step).

- [ ] **Step 7: Update v4.23 byte-equivalence test → v4.24 fixture**

Find any tests pinning v4.23 exact bytes:

```bash
grep -n "byte_equivalence_v423\|test_byte_equiv\|77u" test/test_display_serial/test_display_serial.cpp
```

For each, either:
- **Update** the expected bytes to v4.24 if the test still has value (encoder consistency).
- **Delete** if redundant with the new v4.24 test coverage.

Update the test file's top-of-file comment to note v4.23 → v4.24.

- [ ] **Step 8: Commit**

```bash
git add software/Libraries/onspeed_core/src/proto/DisplaySerial.{h,cpp} \
        test/test_display_serial/*.cpp
git commit -m "proto: wire format v4.24 (version field + validFlags + CRC-8)

Frame grows 77 → 83 bytes:
- New wireVersion field at offset 2 (\"24\")
- New validFlags hex field at offset 75 (low 16 bits of AirDataValid)
- Checksum switches from sum-mod-256 to CRC-8 (poly 0x07, SMBus)

Encoder takes AirDataValid via DisplayBuildInputs::valid; legacy
iasValid bool kept as [[deprecated]] for one release.  Closes
issue #402 (wire-version field for forward-compat dispatch)."
```

---

## Task 8: M5 decoder with version dispatch + v4.23 fallback

**Files:**
- Modify: `software/OnSpeed-M5-Display/src/SerialRead.cpp` (and any header)

- [ ] **Step 1: Read existing parser**

```bash
grep -n "ParseFrame\|parseFrame\|kFrameLen\|77\|#1" \
     software/OnSpeed-M5-Display/src/SerialRead.cpp | head -20
```

Find where the 77-byte frame is read and how `iasValid` is currently inferred (probably by checking for `9999`).

- [ ] **Step 2: Add the version-dispatch entry point**

Wrap the existing parser as `ParseFrameV423` (rename in place). Add a new `ParseFrameV424` for the 83-byte path. Add an entry point:

```cpp
// Wire frame entry point.  Peeks at byte 4 to dispatch:
//   - digit  '0'..'9' → v4.24+, read wireVersion field at offset 2
//   - sign   '+'/'-'  → v4.23 legacy, use ParseFrameV423
//
// Documented version-detection invariant.  v4.25+ relies solely on
// the wireVersion field at offset 2 (this byte-4 fallback is for
// one-release compat with v4.23 producers).
bool ParseFrame(const uint8_t* buf, size_t len, DisplayFrame& out)
{
    if (len < 5) return false;
    if (buf[0] != '#' || buf[1] != '1') return false;

    const uint8_t b4 = buf[4];
    if (b4 >= '0' && b4 <= '9') {
        // v4.24+: read version from offset 2.
        return ParseFrameV424(buf, len, out);
    } else if (b4 == '+' || b4 == '-') {
        // v4.23 legacy.
        return ParseFrameV423(buf, len, out);
    }
    return false;
}
```

- [ ] **Step 3: Write `ParseFrameV424`**

Mirror the existing V423 parser layout but with v4.24 offsets. Validate frame length 83, CRC-8 (use the same `Crc8.h`), and parse the `validFlags` hex field. Populate the `valid` member of `DisplayFrame`.

```cpp
bool ParseFrameV424(const uint8_t* buf, size_t len, DisplayFrame& out)
{
    if (len != 83) return false;

    // CRC-8 validate bytes 0..78 against the 2-hex-char field at 79..80.
    char crcStr[3] = { static_cast<char>(buf[79]),
                       static_cast<char>(buf[80]), 0 };
    char* end = nullptr;
    unsigned long crcFromFrame = strtoul(crcStr, &end, 16);
    if (end != crcStr + 2) return false;
    if (Crc8(buf, 79) != static_cast<uint8_t>(crcFromFrame)) return false;

    // Verify wireVersion field == 24.
    char verStr[3] = { static_cast<char>(buf[2]),
                       static_cast<char>(buf[3]), 0 };
    if (strtoul(verStr, nullptr, 10) != 24) return false;

    // Parse fields. Mirror v4.23 field-parse helpers but with new offsets.
    out.pitchDeg     = parseSignedScaled(buf + 4,  4, 10);
    out.rollDeg      = parseSignedScaled(buf + 8,  5, 10);
    // ... continue for all 24 fields per design §4 ...

    // validFlags at offset 75, width 4.
    char flagStr[5] = { static_cast<char>(buf[75]),
                        static_cast<char>(buf[76]),
                        static_cast<char>(buf[77]),
                        static_cast<char>(buf[78]), 0 };
    out.valid.bits = strtoul(flagStr, nullptr, 16);

    // IAS validity now comes from the bit (not from 9999 sentinel),
    // but keep iasIsValid derived from the bit for legacy consumers.
    out.iasIsValid = out.valid.has(AirDataValid::kIas);

    return true;
}
```

Use existing `parseSignedScaled` / `parseUnsignedScaled` helpers in `SerialRead.cpp` if they exist; otherwise define small static helpers.

- [ ] **Step 4: Add `valid` field to `DisplayFrame` struct**

In `SerialRead.h` (or wherever `DisplayFrame` lives):

```cpp
#include <types/AirDataValid.h>

struct DisplayFrame {
    // ... existing fields ...
    onspeed::types::AirDataValid valid;
};
```

- [ ] **Step 5: Build M5 firmware**

```bash
pio run -e m5-basic 2>&1 | tail -10
```

(Check `platformio.ini` for the actual M5 env name — could be `m5-core2`, `m5-display`, etc.)

Expected: clean build.

- [ ] **Step 6: Bench replay smoke test**

If `tools/m5-replay/` has a bench harness for the M5 parser, run it. Otherwise:

```bash
pio test -e native -f test_display_serial -v
```

— but extend `test_display_serial.cpp` first with a round-trip test that encodes a v4.24 frame, parses it back, and checks fields match. (Optional; flag as test debt if time-constrained.)

- [ ] **Step 7: Commit**

```bash
git add software/OnSpeed-M5-Display/src/
git commit -m "m5: parse v4.24 wire frames with v4.23 fallback

M5 SerialRead dispatches on byte 4 (digit = v4.24, sign = v4.23
legacy).  v4.24 parser validates CRC-8, reads the validFlags hex
field, and populates DisplayFrame::valid.  iasIsValid now comes from
the bit, not the 9999 sentinel.

v4.23 path retained for one release for old-Gen3-with-new-M5 graceful
degrade; removal scheduled for v4.25."
```

---

## Task 9: Python `frame.py` parity update + new parity test

**Files:**
- Modify: `tools/onspeed_py/frame.py`
- Modify: `tools/m5-replay/test_replay.py` (fixture lengths)
- Create: `tools/onspeed_py/tests/test_v424_byte_parity.py`

- [ ] **Step 1: Update `frame.py` constants and format**

In `tools/onspeed_py/frame.py`:

```python
PAYLOAD_LEN = 79   # bytes 0..78 — v4.24 ASCII fields up to and including validFlags
FRAME_LEN   = 83   # PAYLOAD_LEN + 2 hex CRC + CRLF
WIRE_VERSION = 24
```

Add `validity` (or `valid`) field to the `Frame` dataclass:

```python
@dataclass
class Frame:
    # ... existing fields ...
    validity: int = 0  # 16-bit; low bits match AirDataValid
```

Add CRC-8 helper:

```python
_CRC8_TABLE = [0] * 256
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = ((_c << 1) ^ 0x07) & 0xFF if (_c & 0x80) else (_c << 1) & 0xFF
    _CRC8_TABLE[_i] = _c

def _crc8(data: bytes) -> int:
    c = 0
    for b in data:
        c = _CRC8_TABLE[c ^ b]
    return c
```

Update `to_bytes()`:

```python
def to_bytes(self) -> bytes:
    ias_field = (
        9999 if not self.ias_valid
        else _clamp_uint(self.ias_kts * 10, 0, 9999)
    )
    payload = (
        f"#1"
        f"{WIRE_VERSION:02d}"
        f"{_clamp_int(self.pitch_deg * 10, -999, 999):+04d}"
        f"{_clamp_int(self.roll_deg * 10, -9999, 9999):+05d}"
        f"{ias_field:04d}"
        f"{_clamp_int(self.palt_ft, -99999, 99999):+06d}"
        f"{_clamp_int(self.turnrate_dps * 10, -9999, 9999):+05d}"
        # ... continue field-by-field, exactly matching the C++ format string ...
        f"{(self.validity & 0xFFFF):04X}"
    )
    if len(payload) != PAYLOAD_LEN:
        raise ValueError(
            f"payload length {len(payload)} != {PAYLOAD_LEN}: {payload!r}"
        )
    crc = _crc8(payload.encode("ascii"))
    return f"{payload}{crc:02X}\r\n".encode("ascii")
```

Replace the existing `sum(...) & 0xFF` checksum with `_crc8(...)`.

- [ ] **Step 2: Update fixture-length expectations in test_replay.py**

```bash
grep -n "FRAME_LEN\|PAYLOAD_LEN\|77\|73\b" tools/m5-replay/test_replay.py
```

For each match, update from `77` → `83` and `73` → `79` where appropriate. The `FRAME_LEN + 20 = 97` extended-frame test calculation becomes `FRAME_LEN + 20 = 103`.

- [ ] **Step 3: Write Python parity test**

Create `tools/onspeed_py/tests/test_v424_byte_parity.py`:

```python
"""Byte-for-byte parity between Python frame.py and the C++ DisplaySerial
encoder.  Fixture is regenerated by tools/regression/host_main.cpp on
demand; check it in alongside the C++ change.
"""
import pathlib
from onspeed_py.frame import Frame, FRAME_LEN, WIRE_VERSION

FIXTURE = pathlib.Path(__file__).parent / "fixtures" / "v424_golden.bin"

def test_python_encoder_matches_cpp_golden():
    """Verify Python frame.py produces byte-identical output to the
    C++ encoder for a known representative input."""
    # Inputs match those used to produce the golden in C++; if you change
    # one, regenerate the golden with `tools/regression/run_snapshot.py
    # --regenerate-v424-fixture`.
    f = Frame(
        pitch_deg=2.3,
        roll_deg=-1.5,
        ias_kts=147.0,
        palt_ft=8000,
        turnrate_dps=0.5,
        lateral_g=0.02,
        vertical_g=1.05,
        percent_lift_pct=55.4,
        vsi_fpm=200,
        oat_c=5,
        flight_path_deg=1.8,
        flaps_deg=0,
        tones_on_pct_lift=63,
        on_speed_fast_pct_lift=70,
        on_speed_slow_pct_lift=80,
        stall_warn_pct_lift=95,
        flaps_min_deg=0,
        flaps_max_deg=30,
        g_onset_rate=0.0,
        spin_recovery_cue=0,
        data_mark=0,
        pip_pct_lift=63,
        ias_valid=True,
        validity=0x004F,  # kOatRaw|kOatSat|kIas|kPalt|kTas (low 6 bits)
    )
    expected = FIXTURE.read_bytes()
    actual = f.to_bytes()
    assert len(actual) == FRAME_LEN
    assert len(expected) == FRAME_LEN
    assert actual == expected, (
        f"\nexpected: {expected!r}\n  actual: {actual!r}\n"
        f"This drift indicates frame.py and DisplaySerial.cpp have\n"
        f"diverged. Regenerate fixture or fix the Python encoder."
    )

def test_wire_version_is_24():
    assert WIRE_VERSION == 24
```

- [ ] **Step 4: Generate the golden fixture**

The C++ regression harness (`tools/regression/run_snapshot.py`) needs a `--regenerate-v424-fixture` flag, or we write a small standalone C++ program.

Option A (simpler): extend `tools/regression/host_main.cpp` with a `--emit-v424-fixture <path>` mode that calls `BuildDisplayFrame` with the exact same field values as the Python test above and writes the bytes. Rebuild the harness and run:

```bash
cd OnSpeed-Gen3 && ./tools/regression/build_host_main.sh
./tools/regression/host_main --emit-v424-fixture \
    tools/onspeed_py/tests/fixtures/v424_golden.bin
```

Option B (fallback): hand-write a 3-line C++ test that emits the fixture once. Stage it under `tools/m5-replay/gen_v424_fixture.cpp` with a one-shot Makefile target.

Either path: commit the resulting binary fixture under `tools/onspeed_py/tests/fixtures/v424_golden.bin`.

- [ ] **Step 5: Run Python tests**

```bash
cd OnSpeed-Gen3 && python3 -m pytest tools/onspeed_py/tests/ -v
python3 -m pytest tools/m5-replay/test_replay.py -v
```

Expected: all pass. `test_python_encoder_matches_cpp_golden` is the new drift-prevention test.

- [ ] **Step 6: Commit**

```bash
git add tools/onspeed_py/frame.py \
        tools/onspeed_py/tests/test_v424_byte_parity.py \
        tools/onspeed_py/tests/fixtures/v424_golden.bin \
        tools/m5-replay/test_replay.py \
        tools/regression/host_main.cpp  # if extended for fixture gen
git commit -m "python: frame.py to v4.24 wire (83 bytes, CRC-8, validFlags)

PAYLOAD_LEN 73→79, FRAME_LEN 77→83, sum-mod-256 → CRC-8 (poly 0x07).
New \`validity\` field on Frame; encoded as %04X hex.

test_v424_byte_parity.py pins Python encoder against C++ golden
fixture, closing the manual-sync drift risk between frame.py and
onspeed_core/proto/DisplaySerial.cpp."
```

---

## Task 10: JS replay — skip-on-invalid filters + dashes in MP4 burn-in

**Files:**
- Modify: `docs/site/docs/data-and-logs/replay/lib/replay/presentationFilter.js`
- Modify: `docs/site/docs/data-and-logs/replay/lib/replay/mp4Export.js`
- Create: `tools/web/test/wire-validity.mjs`
- Modify: `docs/site/tests/replay/m5sim-smoke.mjs`

- [ ] **Step 1: Inspect presentationFilter.js**

```bash
sed -n '1,50p' docs/site/docs/data-and-logs/replay/lib/replay/presentationFilter.js
grep -n "update\|filter\|ema" docs/site/docs/data-and-logs/replay/lib/replay/presentationFilter.js | head
```

Determine the per-channel update entry points. Typical shape: each channel has `filter.update(value)`; we want a `filter.skip()` alternative or a check-then-skip wrapper.

- [ ] **Step 2: Add `skip()` semantics to the filter**

Either extend the existing filter class with a `skip()` method that holds the previous value, or add a check-then-call wrapper in the consumer code:

```js
// presentationFilter.js — pattern:
//   if (frame.valid.has(KIASBIT)) { filter.ias.update(frame.ias); }
//   else                          { filter.ias.skip(); }
//
// `skip()` retains the previous filter output but advances any time-
// based state (no-op for a basic EMA; relevant for rate-aware filters).
```

Lift the Dynon `filter->skip()` pattern (`dynon-audit/docs/architecture/ONSPEED_TRANSFERABLE.md:18`).

- [ ] **Step 3: Update mp4Export.js**

Find where field values are drawn into the HUD overlay. Replace direct numeric rendering with a check:

```js
// mp4Export.js — pattern:
function fmt(value, validBit) {
  return frame.valid.has(validBit) ? value.toFixed(0) : "--";
}
ctx.fillText(fmt(frame.iasKt, AirDataValid.kIas), x, y);
```

- [ ] **Step 4: Write the JS test**

```js
// tools/web/test/wire-validity.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { presentationFilter } from
  '../../docs/site/docs/data-and-logs/replay/lib/replay/presentationFilter.js';

test('presentationFilter skips on cleared validity bit', () => {
  const f = presentationFilter.create();
  presentationFilter.update(f, {
    iasKt: 100, valid: { bits: 0x4 }  // kIas set
  });
  presentationFilter.update(f, {
    iasKt: 999, valid: { bits: 0x0 }  // kIas clear — should skip
  });
  assert.equal(presentationFilter.iasOf(f), 100,
    'filter should hold last good value, not absorb the 999');
});

// Add 2-3 more tests covering MP4 burn-in dashes-on-invalid.
```

- [ ] **Step 5: Update m5sim-smoke.mjs**

```bash
grep -n "wireVersion\|FRAME_LEN\|77\|83" docs/site/tests/replay/m5sim-smoke.mjs
```

If the smoke test asserts frame length or version, update to `83` and `24`. If it merely round-trips bytes, no change needed (it'll auto-pick-up the new wire from the C++ wasm).

- [ ] **Step 6: Run the JS tests**

```bash
node --test tools/web/test/wire-validity.mjs
node --test docs/site/tests/replay/m5sim-smoke.mjs
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add docs/site/docs/data-and-logs/replay/lib/replay/{presentationFilter,mp4Export}.js \
        tools/web/test/wire-validity.mjs \
        docs/site/tests/replay/m5sim-smoke.mjs
git commit -m "replay: skip-on-invalid filters + dashes in MP4 burn-in

presentationFilter holds last-good value when AirDataValid bit is
clear (Dynon filter->skip() pattern, dynon-audit/.../ONSPEED_TRANSFERABLE.md:18).
mp4Export burn-in renders \"--\" for fields whose bit is clear,
matching the live-page StaleOverlay convention."
```

---

## Task 11: Documentation updates

**Files:**
- Modify: `docs/site/docs/reference/serial-protocol.md`
- Modify: `docs/site/docs/configuration/advanced.md`
- Modify: `docs/site/docs/reference/glossary.md`
- Modify: `docs/site/docs/calibration/how-aoa-works.md`
- Modify: `tools/m5-replay/README.md`

- [ ] **Step 1: Serial protocol doc**

Replace the v4.23 frame layout in `docs/site/docs/reference/serial-protocol.md` with the full v4.24 layout from design spec §4. Include:
- The 83-byte byte map
- The version-detection invariant (byte-4 dispatch for one release)
- The CRC-8 algorithm spec (poly 0x07, init 0x00, no XOR-out, no reflection, SMBus)
- The validFlags bit layout (table mapping bits 0-15 to channels)

- [ ] **Step 2: Advanced configuration doc**

Add a new section to `docs/site/docs/configuration/advanced.md`:

```markdown
## OAT recovery factor (K)

The OAT probe reads total air temperature (TAT) — the temperature of
air brought to rest at the probe surface, including the ram-rise
heating from kinetic energy.  TAS, density altitude, and the AOA
derivation use *static* air temperature (SAT) — the freestream
temperature.

OnSpeed corrects TAT → SAT using:

  SAT_K = TAT_K / (1 + K · 0.2 · M²)

where K is the **probe recovery factor**:

- **K = 0.75** (default) — a bare/exposed thermistor probe.  This is
  the typical GA installation.
- **K = 1.0** — an ideal TAT probe (Kiel or shielded design).  Set
  this if your probe vendor publishes K = 1.0.
- **K = 0** — disables the correction.  Set this if your EFIS supplies
  an already-corrected SAT through its serial wire (most don't).

Above ~120 KIAS in cold air, the correction is meaningful: ~2°C at
N720AK cruise, ~3.3°C at Lancair FL250.  Below ~80 KIAS the
correction is well under 0.5°C and pilots won't notice.
```

- [ ] **Step 3: Glossary**

Add four entries to `docs/site/docs/reference/glossary.md`:

```markdown
**SAT** — Static Air Temperature.  The freestream air temperature
without the heating effect from ram compression at the probe.  Used
internally by OnSpeed for TAS and density-altitude calculations.

**TAT** — Total Air Temperature.  What an OAT probe physically reads
in flight — freestream temperature *plus* ram rise from kinetic
energy.  Pilot sees TAT on the panel.

**Recovery Factor (K)** — A probe-specific constant [0, 1]
describing how much of the ideal ram-rise the probe actually
captures.  A bare thermistor captures ~75%; a properly shielded TAT
probe captures ~100%.  Config parameter: `fOatRecoveryFactor`.

**Ram Rise** — The temperature rise at an OAT probe due to kinetic
heating of air brought to rest at the probe surface.  Approximately
K · 0.2 · M² · TAT_K (a few degrees Celsius at typical GA cruise
speeds; tens of degrees on transports).
```

- [ ] **Step 4: How-AOA-works**

Add one paragraph near the existing density-correction discussion in `docs/site/docs/calibration/how-aoa-works.md`:

```markdown
> **OAT ram-rise correction:** OnSpeed corrects the OAT probe reading
> for ram heating before computing TAS, using the configured probe
> recovery factor (default K = 0.75).  Above ~120 KIAS this matters:
> at Lancair-class cruise speeds the correction is ~3°C, shifting
> density altitude by ~400 ft.  See [advanced configuration](../configuration/advanced.md#oat-recovery-factor-k).
```

- [ ] **Step 5: m5-replay README**

```bash
grep -n "77\|v4\.23\|73" tools/m5-replay/README.md
```

Update every `77` → `83`, every `v4.23` → `v4.24`, every `73-byte` → `79-byte`.

- [ ] **Step 6: Build docs locally to catch broken links**

```bash
cd docs/site && uv run --with "mkdocs>=1.6,<2" --with mkdocs-material mkdocs build --strict
```

Expected: clean build, no warnings.

- [ ] **Step 7: Commit**

```bash
git add docs/site/docs/reference/{serial-protocol,glossary}.md \
        docs/site/docs/configuration/advanced.md \
        docs/site/docs/calibration/how-aoa-works.md \
        tools/m5-replay/README.md
git commit -m "docs: wire v4.24, OAT recovery factor, TAT/SAT glossary"
```

---

## Task 12: Final integration — Playwright browser check + V4P build verify

**Files:**
- No new files.  Verification step.

- [ ] **Step 1: Build V4P firmware**

```bash
cd OnSpeed-Gen3 && pio run -e esp32s3-v4p 2>&1 | tail -15
```

Expected: clean build, zero warnings.

- [ ] **Step 2: Run full native test suite**

```bash
pio test -e native 2>&1 | tail -20
```

Expected: all suites pass (the new ones plus all existing).

- [ ] **Step 3: Static analysis**

```bash
./scripts/cppcheck.sh 2>&1 | tail -20
./scripts/check_core_purity.sh
./scripts/check_board_flags.sh
```

Expected: clean, no new findings.

- [ ] **Step 4: Start both dev servers (parallel terminals)**

```bash
# Terminal A:
node tools/web/dev-server/server.mjs --mock --port 9001

# Terminal B:
cd docs/site && uv run --with "mkdocs>=1.6,<2" --with mkdocs-material mkdocs serve
```

- [ ] **Step 5: Playwright check live indexer**

Drive `http://localhost:9001/indexer`.  Verify:
- OAT displays raw TAT value (not SAT).
- `validFlags` value decodes correctly in dev console (`window.lastFrame.valid.bits`).
- StaleOverlay behavior unchanged when feed goes stale.
- Console empty except known `versions.json` 404.

- [ ] **Step 6: Playwright check replay page**

Drive `http://127.0.0.1:8000/data-and-logs/replay/`.  Upload an N720AK fixture log (anything from `~/Dropbox/N720AK/OnSpeed Cals/`).  Verify:
- HUD renders normally.
- Scrub through the timeline; no console errors.
- Synthetic OAT-loss test: in a clip with `validity & kOatRaw == 0` rows, HUD shows "--" in OAT and TAS fields.
- MP4 export: same segment shows dashes in burn-in (export a short clip and inspect a frame).

- [ ] **Step 7: Take screenshots of changed UI**

Screenshot the indexer with new fields rendering correctly. Screenshot a replay-page HUD frame with dashes. Attach to the PR description.

- [ ] **Step 8: Self-review of git log**

```bash
git log --oneline origin/master..HEAD
```

Expected: ~11 atomic commits, one per task.

- [ ] **Step 9: Create the PR**

```bash
gh pr create --head feat/airdata-hardening --title \
  "air-data hardening: SAT correction + validity bits + wire v4.24" \
  --body "$(cat <<'EOF'
## Summary
Bundles three air-data improvements into a single dual-flash wire bump:
- Ram-rise SAT correction (default K=0.75) in `Ahrs::updateTas_`
- Per-channel `AirDataValid` flags type, plumbed through the firmware
- Wire format v4.23 → v4.24 with new wireVersion field (closes #402),
  16-bit validFlags field, CRC-8 checksum (poly 0x07)

Design spec: `docs/superpowers/specs/2026-05-18-airdata-hardening-design.md`

## Changes
- `onspeed_core/types/AirDataValid.h` — new flags type
- `onspeed_core/sensors/SatCorrect.{h,cpp}` — ram-rise correction
- `onspeed_core/proto/Crc8.h` — CRC-8 helper (256-entry table)
- `onspeed_core/proto/DisplaySerial.{h,cpp}` — v4.24 encoder
- `onspeed_core/ahrs/Ahrs.cpp` — SAT correction in TAS path
- `OnSpeed-M5-Display/src/SerialRead.cpp` — version-dispatch parser
- `Config*.{h,cpp}` — `fOatRecoveryFactor` parameter
- `tools/onspeed_py/frame.py` + parity test — Python encoder updated
- JS replay: presentationFilter skip-on-invalid, MP4 burn-in dashes
- Full doc updates (serial-protocol, advanced config, glossary, how-aoa)

## Testing
- New unit tests: `test_air_data_valid`, `test_sat_correct`, `test_crc8`
- Extended: `test_display_serial`, `test_ahrs` (K=0 pin matches legacy)
- Python parity: `test_v424_byte_parity.py` (drift prevention)
- Regression harness golden updated (TAS shifts 0.2–0.6%)
- Playwright verified on `/indexer` and replay page

## Rollback
- User-side: set `fOatRecoveryFactor = 0` to disable SAT correction
- Wire-side: M5 fallback parser handles v4.23 producers for one release
- Git: single squash-merge, atomic revert
EOF
)"
```

Expected: PR created.  Report URL.

- [ ] **Step 10: Final commit (if anything residual)**

```bash
git status
# If there are leftover files (regenerated docs, etc.), commit them.
```

---

## Out of scope (deferred follow-ups)

- **Probe thermal-lag correction (τ).**  Slot for `fOatProbeTauSec`
  config parameter reserved; design defers until in-flight OAT logs
  from N720AK climb/descent profiles are available.
- **`kFrameSelfConsistent` bit producer impl.**  Bit pre-allocated at
  position 15 in `AirDataValid`; never cleared in this PR.  Future
  cross-channel sanity layer (Dynon `ADAHRSSensorChecker` lift from
  `dynon-audit/.../ONSPEED_TRANSFERABLE.md:99`) populates it.
- **v4.23 fallback parser removal.**  Scheduled for v4.25 along with
  removal of the `DisplayBuildInputs::iasValid` deprecated bool.
