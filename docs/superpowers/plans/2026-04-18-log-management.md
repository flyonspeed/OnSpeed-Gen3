# Log Management UI & Metadata Sidecars Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give OnSpeed pilots a usable `/logs` page — start-time-of-day + duration per log, bulk-delete checkboxes, date-stamped filenames when GPS UTC is captured.

**Architecture:** A platform-independent `LogMetaBuilder` in `onspeed_core/src/log/` accumulates per-row metadata and writes a `log_NNN.meta` sidecar at close. `LogSensor` owns the builder and conditionally renames both files (`log_NNN.{csv,meta}` → `YYYY-MM-DD_NNN.{csv,meta}`) when a full UTC timestamp was captured. The `/logs` web handler reads the sidecars and renders a single server-rendered form with checkboxes + "Delete selected". The Dynon SkyView parser is extended to capture `HH:MM:SS` from the `!1` ADAHRS frame's System Time field (currently skipped).

**Tech Stack:** C++17, PlatformIO native tests with Unity, existing `onspeed_core` library, Arduino-style ESP32 firmware with `WebServer` handlers in `ConfigWebServer.cpp`, `SdFat` via `SdFileSys` wrapper.

---

## Spec

Full spec: `docs/superpowers/specs/2026-04-18-log-management-design.md`

Key file locations to keep in your head while working:

- `software/Libraries/onspeed_core/src/` — platform-independent library. Build with `pio test -e native`. **Do not include Arduino.h or FreeRTOS.h here.**
- `software/sketch_common/src/` — shared sketch code for Gen3 + future Gen2v4 boards. Touches real hardware via Arduino/FreeRTOS.
- `test/test_*/test_*.cpp` — one folder per test suite. Framework is Unity (the PlatformIO Unity, via `<unity.h>`).
- `onspeed_core` uses `GLOB_RECURSE` in its CMakeLists and has no explicit source manifest in `library.json` — adding new `.cpp` files under `src/` picks them up automatically for both native tests and firmware builds.

**Working directory for all tasks**: `/Users/sritchie/code/onspeed/OnSpeed-Gen3/.worktrees/log-management/` (the worktree). Branch: `sritchie/log-management`. Do not cd elsewhere.

---

## File Structure

New files:

| Path | Responsibility |
|---|---|
| `software/Libraries/onspeed_core/src/log/LogMeta.h` | POD struct + small `EfisType` enum. Pure header. |
| `software/Libraries/onspeed_core/src/log/LogMetaBuilder.h` | Accumulator interface. |
| `software/Libraries/onspeed_core/src/log/LogMetaBuilder.cpp` | Accumulator implementation. Per-row hot path: cheap. |
| `software/Libraries/onspeed_core/src/log/LogMetaFile.h` | Pure `WriteMetaFile` / `ParseMetaFile`. |
| `software/Libraries/onspeed_core/src/log/LogMetaFile.cpp` | `key=value\n` format implementation. |
| `test/test_log_meta/test_log_meta.cpp` | Native unit tests for all three new files. |

Modified files:

| Path | Change |
|---|---|
| `software/Libraries/onspeed_core/src/types/EfisFrame.h` | Add `char timeOfDayHms[9]` field (zero-terminated "HH:MM:SS"; empty = absent). |
| `software/Libraries/onspeed_core/src/efis/DynonSkyview.cpp` | In `DecodeAdahrs`, parse System Time from `buf_[3..8]`, populate `out.timeOfDayHms`. |
| `test/test_efis_dynon_skyview/test_efis_dynon_skyview.cpp` | Extend frame builder to take H/M/S params (default 12:34:56 matches existing); add System Time tests. |
| `software/sketch_common/src/drivers/SdFileSys.h` | Declare `bool rename(const char* src, const char* dst)`. |
| `software/sketch_common/src/drivers/SdFileSys.cpp` | Implement rename wrapper around `uSD_FAT.rename`. |
| `software/sketch_common/src/tasks/LogSensor.h` | Add private `LogMetaBuilder m_metaBuilder` and track `m_szBaseName[16]` for rename at close. |
| `software/sketch_common/src/tasks/LogSensor.cpp` | Wire builder through `Open`/`Write`/`Close`. In `Close()` after flush: write sidecar, then conditionally rename. |
| `software/sketch_common/src/web_server/ConfigWebServer.cpp` | Extend `IsSafeLogFilename` to allow `.meta`. Rewrite `HandleLogs`. Add `HandleDeleteBulk`. Extend `HandleDelete` to also remove matching `.meta`. Register `POST /delete-bulk` route. |

No CSV schema changes. No CMakeLists edits (GLOB_RECURSE). No `library.json` edits.

---

## Task 0: Verify clean baseline

**Files:** none

- [ ] **Step 1: Confirm worktree and branch**

Run:
```bash
pwd
git branch --show-current
git status --short
```

Expected output:
```
/Users/sritchie/code/onspeed/OnSpeed-Gen3/.worktrees/log-management
sritchie/log-management
(empty — no modified files)
```

- [ ] **Step 2: Confirm native tests pass before any changes**

Run:
```bash
pio test -e native 2>&1 | tail -5
```

Expected: final line contains `671 test cases: 671 succeeded`.

- [ ] **Step 3: Confirm firmware builds cleanly**

Run:
```bash
pio run -e esp32s3-v4p 2>&1 | tail -3
```

Expected: `SUCCESS` (or equivalent final line with no errors).

If either fails, stop and investigate. Baseline must be clean.

---

## Task 1: Add `timeOfDayHms` field to `EfisFrame`

Rationale: all EFIS parsers will populate this when their protocol carries time-of-day. Starting with just the type change keeps the diff small.

**Files:**
- Modify: `software/Libraries/onspeed_core/src/types/EfisFrame.h`

- [ ] **Step 1: Add the field**

Edit `software/Libraries/onspeed_core/src/types/EfisFrame.h`. Insert a new field into the `EfisFrame` struct, placed **after** the existing `timestampUs` field (keeps the chronological addition order):

```cpp
    // Microsecond-resolution timestamp of the most recent parse.
    // Wraps ~71 minutes; consumers must diff with uint32_t arithmetic.
    uint32_t timestampUs = 0;

    // Wall-clock time-of-day from the EFIS, as "HH:MM:SS" NUL-terminated.
    // Empty string ("") means this frame did not carry a valid time.
    // Parsers populate this only when the underlying protocol provides
    // a real time and the frame's validity sentinel (Dynon's all-dashes,
    // VN-300's GPSFix==0, etc.) doesn't mark it as absent.
    char timeOfDayHms[9] = {};
};
```

- [ ] **Step 2: Build and run tests**

Run:
```bash
pio test -e native 2>&1 | tail -5
```

Expected: still `671 test cases: 671 succeeded`. The field defaults to empty; no existing tests read it.

- [ ] **Step 3: Commit**

```bash
git add software/Libraries/onspeed_core/src/types/EfisFrame.h
git commit -m "$(cat <<'EOF'
Add timeOfDayHms field to EfisFrame

Parsers will populate this with "HH:MM:SS" when the underlying protocol
carries a valid wall-clock time. Empty string means absent. Consumed by
the log metadata sidecar in subsequent commits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Write failing test for Dynon System Time parsing

**Files:**
- Modify: `test/test_efis_dynon_skyview/test_efis_dynon_skyview.cpp`

- [ ] **Step 1: Extend `buildAdahrsFrame` to take H/M/S**

Open `test/test_efis_dynon_skyview/test_efis_dynon_skyview.cpp`. Find the `buildAdahrsFrame` function (starts near line 77 with signature `static void buildAdahrsFrame(char buf[74], ...`). Change its signature and body to accept optional H/M/S/CS parameters. The four fields replace the hard-coded `12`, `34`, `56`, `78`:

```cpp
static void buildAdahrsFrame(char buf[74],
                              float pitchDeg, float rollDeg, int headingDeg,
                              float iasKt, int paltFt,
                              float lateralG, float verticalG,
                              int percentLift,
                              int vsiFpm, float oatC, float tasKt,
                              int hh = 12, int mm = 34, int ss = 56, int cs = 78)
{
    char tmp[16];

    for (int i = 0; i < 72; i++) buf[i] = ' ';
    buf[0] = '!'; buf[1] = '1'; buf[2] = ' ';

    snprintf(tmp, sizeof(tmp), "%02d", hh);   putField(buf,  3, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", mm);   putField(buf,  5, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", ss);   putField(buf,  7, tmp, 2);
    snprintf(tmp, sizeof(tmp), "%02d", cs);   putField(buf,  9, tmp, 2);

    int pitch10 = static_cast<int>(pitchDeg * 10.0f);
    snprintf(tmp, sizeof(tmp), "%+04d", pitch10);   putField(buf, 11, tmp, 4);
    // ... keep the rest of the body unchanged ...
```

The rest of `buildAdahrsFrame` stays exactly as it was — only the four `snprintf` calls at the top change from hard-coded values to the new parameters.

- [ ] **Step 2: Add a helper that writes dashes into the time field**

Immediately after `buildAdahrsFrame`, add a helper that patches the existing time bytes with dashes to simulate the "GPS has never locked" sentinel:

```cpp
// Overwrite the 8 time-field bytes at buf[3..10] with the given 8-char string.
// Lets tests exercise the all-dashes ('--------') sentinel without rebuilding
// the whole frame.
static void patchTimeField(char buf[74], const char* eightChars)
{
    for (int i = 0; i < 8; i++) buf[3 + i] = eightChars[i];
    // Recompute CRC over bytes 0..69.
    int crc = 0;
    for (int i = 0; i <= 69; i++) crc += static_cast<unsigned char>(buf[i]);
    crc &= 0xFF;
    char tmp[3];
    snprintf(tmp, sizeof(tmp), "%02X", crc);
    buf[70] = tmp[0];
    buf[71] = tmp[1];
}
```

- [ ] **Step 3: Add the test cases**

Find the last `TEST_` or `RUN_TEST` line in the file — tests are declared as `static void test_something() { ... }` functions and registered in `main()`. Add these four test functions, just before `main()`:

```cpp
static void test_adahrs_system_time_valid()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     14, 32, 47, 12);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("14:32:47", frame->timeOfDayHms);
}

static void test_adahrs_system_time_all_dashes()
{
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f);
    patchTimeField(buf, "--------");
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

static void test_adahrs_system_time_partial_dashes()
{
    // Any dash in the HHMMSS portion = treat as absent.
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f);
    patchTimeField(buf, "14----99");
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}

static void test_adahrs_system_time_out_of_range()
{
    // HH=25 is out of range (0..23). Treat as absent.
    char buf[74];
    buildAdahrsFrame(buf, 0.0f, 0.0f, 0, 0.0f, 0, 0.0f, 1.0f, 0, 0, 0.0f, 0.0f,
                     25, 30, 30, 0);
    DynonSkyviewParser p;
    auto frame = feedAll(p, buf, 74);
    TEST_ASSERT_TRUE(frame.has_value());
    TEST_ASSERT_EQUAL_STRING("", frame->timeOfDayHms);
}
```

Then register them in `main()` by adding four `RUN_TEST(test_adahrs_system_time_*);` lines in the same block where existing tests are registered.

- [ ] **Step 4: Run the new tests and verify they fail**

Run:
```bash
pio test -e native -f test_efis_dynon_skyview -v 2>&1 | tail -20
```

Expected: all four new `test_adahrs_system_time_*` tests **fail** (the parser doesn't populate `timeOfDayHms` yet). Existing tests in this suite still pass.

- [ ] **Step 5: Commit the failing tests**

```bash
git add test/test_efis_dynon_skyview/test_efis_dynon_skyview.cpp
git commit -m "$(cat <<'EOF'
Test: Dynon SkyView System Time parsing (failing)

Adds four cases that will pass once DecodeAdahrs populates
EfisFrame.timeOfDayHms from buf_[3..8] with range checks and dash
sentinel handling.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Implement Dynon System Time parsing

**Files:**
- Modify: `software/Libraries/onspeed_core/src/efis/DynonSkyview.cpp`

- [ ] **Step 1: Add the parse helper**

In `software/Libraries/onspeed_core/src/efis/DynonSkyview.cpp`, inside the unnamed namespace / static-helpers section near the top (after `parseHexCRC` around line 40), add:

```cpp
// Parse Dynon !1 System Time bytes buf[3..8] ("HHMMSS" — the FF fraction
// at buf[9..10] is ignored). Writes an 8-char "HH:MM:SS" NUL-terminated
// string into `out[9]`. Writes an empty string if any dash is present in
// the 6 HHMMSS bytes or if H/M/S are out of range.
static void parseSystemTime(const char* buf, char out[9])
{
    out[0] = '\0';

    // Any dash in the HHMMSS portion = GPS has never locked. Treat as absent.
    for (int i = 3; i <= 8; i++)
        if (buf[i] == '-')
            return;

    // Parse two-digit fields.
    auto twoDigit = [](const char* p) -> int {
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
        return (p[0] - '0') * 10 + (p[1] - '0');
    };
    int hh = twoDigit(buf + 3);
    int mm = twoDigit(buf + 5);
    int ss = twoDigit(buf + 7);
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
        return;

    snprintf(out, 9, "%02d:%02d:%02d", hh, mm, ss);
}
```

- [ ] **Step 2: Call the helper from `DecodeAdahrs`**

In `DynonSkyviewParser::DecodeAdahrs`, after the existing `out.source = EfisSource::Dynon;` line at the bottom of the function, add:

```cpp
    out.source = EfisSource::Dynon;
    parseSystemTime(buf_, out.timeOfDayHms);
    return true;
}
```

- [ ] **Step 3: Run the SkyView tests**

Run:
```bash
pio test -e native -f test_efis_dynon_skyview -v 2>&1 | tail -15
```

Expected: all tests in the suite pass, including the four new `test_adahrs_system_time_*` cases.

- [ ] **Step 4: Run the full native suite (nothing else should regress)**

Run:
```bash
pio test -e native 2>&1 | tail -3
```

Expected: `671 + 4 = 675 test cases: 675 succeeded` (or similar — the exact count may differ if other suites added tests; the key is 0 failures and an increase of 4 from baseline).

- [ ] **Step 5: Commit**

```bash
git add software/Libraries/onspeed_core/src/efis/DynonSkyview.cpp
git commit -m "$(cat <<'EOF'
Parse Dynon SkyView System Time from !1 ADAHRS frame

The System Time field at buf[3..10] (HHMMSSFF) was previously skipped.
Now populate EfisFrame.timeOfDayHms with "HH:MM:SS" when the six HHMMSS
bytes contain no dashes and fall in valid ranges. The FF fraction is
ignored — we don't need sub-second precision for log metadata.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Create `LogMeta.h` struct + EfisType enum

**Files:**
- Create: `software/Libraries/onspeed_core/src/log/LogMeta.h`

- [ ] **Step 1: Create the header**

Create `software/Libraries/onspeed_core/src/log/LogMeta.h` with:

```cpp
// LogMeta.h
//
// Metadata record written alongside each SD-card log file as `log_NNN.meta`.
// Accumulated during the session by LogMetaBuilder; serialized at close by
// LogMetaFile::WriteMetaFile; parsed on the /logs web page by
// LogMetaFile::ParseMetaFile.

#ifndef ONSPEED_CORE_LOG_LOG_META_H
#define ONSPEED_CORE_LOG_LOG_META_H

#include <cstddef>
#include <cstdint>

namespace onspeed::log {

// Small local EfisType enum. Kept separate from the sketch's EfisSerialPort
// so onspeed_core stays platform-independent (no Arduino.h pulled in).
enum class EfisType : uint8_t {
    None   = 0,
    Dynon  = 1,
    Garmin = 2,
    Mgl    = 3,
    Vn300  = 4,
};

// "YYYY-MM-DDTHH:MM:SSZ" is 20 chars; 24 gives slack for trailing NUL + noise.
inline constexpr size_t kLogMetaUtcLen = 24;

// "HH:MM:SS" is 8 chars + NUL.
inline constexpr size_t kLogMetaHmsLen = 9;

// Firmware version strings; BuildInfo::version is "X.Y.Z" plus optional "-dirty".
inline constexpr size_t kLogMetaFwLen  = 24;
inline constexpr size_t kLogMetaShaLen = 16;

struct LogMeta {
    uint8_t  metaVersion        = 1;
    int      logFormatVersion   = 0;
    char     firmware[kLogMetaFwLen]  = {};
    char     firmwareSha[kLogMetaShaLen] = {};
    uint32_t durationMs         = 0;
    uint32_t rowCount           = 0;
    float    maxIasKt           = 0.0f;
    float    maxPaltFt          = 0.0f;
    EfisType efisType           = EfisType::None;
    bool     gpsFixSeen         = false;
    char     utcStart[kLogMetaUtcLen]  = {};   // "" = absent
    char     timeOfDayStart[kLogMetaHmsLen] = {};   // "" = absent
};

// Convert enum to wire string. Always returns a valid pointer; defaults
// to "none" for anything unrecognised.
const char* EfisTypeToString(EfisType t);

// Inverse of the above. Returns EfisType::None for unknown input.
EfisType EfisTypeFromString(const char* s);

} // namespace onspeed::log

#endif
```

- [ ] **Step 2: Confirm it compiles (header-only, no tests yet)**

Run:
```bash
pio test -e native 2>&1 | tail -3
```

Expected: still 675 test cases passing. The new header is unreferenced and won't be compiled until a `.cpp` includes it, so this just verifies no accidental syntax error in headers reachable by existing tests.

- [ ] **Step 3: Commit**

```bash
git add software/Libraries/onspeed_core/src/log/LogMeta.h
git commit -m "$(cat <<'EOF'
Add LogMeta POD struct + EfisType enum

Platform-independent metadata record used by the log sidecar system.
Lives under onspeed_core/src/log/ with its own small EfisType enum so
nothing in the core library depends on the sketch's EfisSerial.h.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Write failing tests for `LogMetaFile`

**Files:**
- Create: `test/test_log_meta/test_log_meta.cpp`

- [ ] **Step 1: Create test directory**

Run:
```bash
mkdir -p test/test_log_meta
```

- [ ] **Step 2: Write the test file**

Create `test/test_log_meta/test_log_meta.cpp`:

```cpp
// test_log_meta.cpp
//
// Unit tests for onspeed::log — LogMetaFile (Write/Parse) and LogMetaBuilder.

#include <unity.h>

#include <cstring>
#include <string_view>

#include <log/LogMeta.h>
#include <log/LogMetaBuilder.h>
#include <log/LogMetaFile.h>
#include <types/LogRow.h>

using onspeed::log::LogMeta;
using onspeed::log::LogMetaBuilder;
using onspeed::log::EfisType;
namespace lm = onspeed::log;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static LogMeta MakeFullMeta()
{
    LogMeta m;
    m.metaVersion      = 1;
    m.logFormatVersion = 1;
    strncpy(m.firmware,    "4.19.0",  sizeof(m.firmware)    - 1);
    strncpy(m.firmwareSha, "abc1234", sizeof(m.firmwareSha) - 1);
    m.durationMs       = 5432100u;
    m.rowCount         = 271605u;
    m.maxIasKt         = 142.3f;
    m.maxPaltFt        = 8420.0f;
    m.efisType         = EfisType::Vn300;
    m.gpsFixSeen       = true;
    strncpy(m.utcStart,       "2026-04-18T14:32:07Z", sizeof(m.utcStart)       - 1);
    strncpy(m.timeOfDayStart, "14:32:07",             sizeof(m.timeOfDayStart) - 1);
    return m;
}

// ---------------------------------------------------------------------------
// EfisType round-trip
// ---------------------------------------------------------------------------

static void test_efis_type_to_from_string()
{
    TEST_ASSERT_EQUAL_STRING("none",   lm::EfisTypeToString(EfisType::None));
    TEST_ASSERT_EQUAL_STRING("dynon",  lm::EfisTypeToString(EfisType::Dynon));
    TEST_ASSERT_EQUAL_STRING("garmin", lm::EfisTypeToString(EfisType::Garmin));
    TEST_ASSERT_EQUAL_STRING("mgl",    lm::EfisTypeToString(EfisType::Mgl));
    TEST_ASSERT_EQUAL_STRING("vn300",  lm::EfisTypeToString(EfisType::Vn300));

    TEST_ASSERT_EQUAL(EfisType::None,   lm::EfisTypeFromString("none"));
    TEST_ASSERT_EQUAL(EfisType::Dynon,  lm::EfisTypeFromString("dynon"));
    TEST_ASSERT_EQUAL(EfisType::Garmin, lm::EfisTypeFromString("garmin"));
    TEST_ASSERT_EQUAL(EfisType::Mgl,    lm::EfisTypeFromString("mgl"));
    TEST_ASSERT_EQUAL(EfisType::Vn300,  lm::EfisTypeFromString("vn300"));
    TEST_ASSERT_EQUAL(EfisType::None,   lm::EfisTypeFromString("garbage"));
}

// ---------------------------------------------------------------------------
// Write / parse round-trip
// ---------------------------------------------------------------------------

static void test_round_trip_full_meta()
{
    LogMeta in = MakeFullMeta();
    char buf[512];
    size_t n = lm::WriteMetaFile(in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof(buf));

    LogMeta out;
    bool ok = lm::ParseMetaFile(std::string_view(buf, n), &out);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_UINT8(in.metaVersion,      out.metaVersion);
    TEST_ASSERT_EQUAL_INT(in.logFormatVersion,   out.logFormatVersion);
    TEST_ASSERT_EQUAL_STRING(in.firmware,        out.firmware);
    TEST_ASSERT_EQUAL_STRING(in.firmwareSha,     out.firmwareSha);
    TEST_ASSERT_EQUAL_UINT32(in.durationMs,      out.durationMs);
    TEST_ASSERT_EQUAL_UINT32(in.rowCount,        out.rowCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, in.maxIasKt,  out.maxIasKt);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  in.maxPaltFt, out.maxPaltFt);
    TEST_ASSERT_EQUAL(in.efisType,               out.efisType);
    TEST_ASSERT_EQUAL(in.gpsFixSeen,             out.gpsFixSeen);
    TEST_ASSERT_EQUAL_STRING(in.utcStart,        out.utcStart);
    TEST_ASSERT_EQUAL_STRING(in.timeOfDayStart,  out.timeOfDayStart);
}

static void test_round_trip_minimal_meta_no_times()
{
    LogMeta in;
    in.logFormatVersion = 1;
    strncpy(in.firmware,    "4.19.0", sizeof(in.firmware)    - 1);
    strncpy(in.firmwareSha, "def5678", sizeof(in.firmwareSha) - 1);
    in.durationMs  = 12000u;
    in.rowCount    = 600u;
    in.maxIasKt    = 0.0f;
    in.maxPaltFt   = 0.0f;
    in.efisType    = EfisType::None;
    in.gpsFixSeen  = false;
    // utcStart and timeOfDayStart intentionally empty.

    char buf[512];
    size_t n = lm::WriteMetaFile(in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    LogMeta out;
    bool ok = lm::ParseMetaFile(std::string_view(buf, n), &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("",           out.utcStart);
    TEST_ASSERT_EQUAL_STRING("",           out.timeOfDayStart);
    TEST_ASSERT_EQUAL(EfisType::None,      out.efisType);
    TEST_ASSERT_EQUAL(false,               out.gpsFixSeen);
    TEST_ASSERT_EQUAL_UINT32(12000u,       out.durationMs);
}

// ---------------------------------------------------------------------------
// Parser robustness
// ---------------------------------------------------------------------------

static void test_parse_ignores_unknown_keys()
{
    const char* text =
        "meta_version=1\n"
        "firmware=4.19.0\n"
        "some_future_key=hello\n"
        "duration_ms=42\n"
        "row_count=3\n";
    LogMeta out;
    bool ok = lm::ParseMetaFile(text, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("4.19.0", out.firmware);
    TEST_ASSERT_EQUAL_UINT32(42u, out.durationMs);
    TEST_ASSERT_EQUAL_UINT32(3u,  out.rowCount);
}

static void test_parse_empty_input_returns_false()
{
    LogMeta out;
    // Non-default so we can verify it wasn't clobbered.
    out.rowCount = 12345;
    bool ok = lm::ParseMetaFile("", &out);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(12345u, out.rowCount);
}

static void test_parse_tolerates_trailing_garbage()
{
    const char* text =
        "meta_version=1\n"
        "firmware=4.19.0\n"
        "\n"                          // blank line
        "=no_key\n"                   // no key before =
        "no_equals_sign\n"            // no =
        "duration_ms=99\n";
    LogMeta out;
    bool ok = lm::ParseMetaFile(text, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(99u, out.durationMs);
}

static void test_write_buffer_too_small_returns_zero()
{
    LogMeta in = MakeFullMeta();
    char tiny[8];
    size_t n = lm::WriteMetaFile(in, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_size_t(0u, n);
}

// ---------------------------------------------------------------------------
// LogMetaBuilder
// ---------------------------------------------------------------------------

static onspeed::LogRow MakeRow(uint32_t t, float ias, float palt)
{
    onspeed::LogRow r;
    r.timeStampMs = t;
    r.iasKt       = ias;
    r.paltFt      = palt;
    return r;
}

static void test_builder_zero_rows()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::None);
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_UINT32(0u, m.durationMs);
    TEST_ASSERT_EQUAL_UINT32(0u, m.rowCount);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.maxIasKt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.maxPaltFt);
    TEST_ASSERT_EQUAL(false, m.gpsFixSeen);
    TEST_ASSERT_EQUAL(EfisType::None, m.efisType);
}

static void test_builder_duration_and_running_max()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Dynon);
    b.OnRow(MakeRow(1000, 30.0f, 500.0f),  nullptr, nullptr);
    b.OnRow(MakeRow(2000, 60.0f, 2000.0f), nullptr, nullptr);
    b.OnRow(MakeRow(3000, 45.0f, 1500.0f), nullptr, nullptr);   // lower, shouldn't drop max
    b.OnRow(MakeRow(4500, 99.5f, 2200.0f), nullptr, nullptr);
    LogMeta m = b.Finalize();

    TEST_ASSERT_EQUAL_UINT32(3500u, m.durationMs);           // 4500 - 1000
    TEST_ASSERT_EQUAL_UINT32(4u,    m.rowCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 99.5f,  m.maxIasKt);
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  2200.0f, m.maxPaltFt);
    TEST_ASSERT_EQUAL(EfisType::Dynon, m.efisType);
    TEST_ASSERT_EQUAL(false, m.gpsFixSeen);                  // no time ever seen
}

static void test_builder_captures_first_time_only()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Dynon);
    b.OnRow(MakeRow(0,    0.0f, 0.0f), nullptr,    nullptr);
    b.OnRow(MakeRow(100,  0.0f, 0.0f), "13:01:02", nullptr);
    b.OnRow(MakeRow(200,  0.0f, 0.0f), "13:01:03", nullptr);    // should NOT overwrite
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_STRING("13:01:02", m.timeOfDayStart);
    TEST_ASSERT_EQUAL(true, m.gpsFixSeen);
}

static void test_builder_captures_first_utc_only()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Vn300);
    b.OnRow(MakeRow(0, 0, 0), nullptr, nullptr);
    b.OnRow(MakeRow(100, 0, 0), "14:32:07", "2026-04-18T14:32:07Z");
    b.OnRow(MakeRow(200, 0, 0), "14:32:08", "2026-04-18T14:32:08Z");   // later, ignored
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_STRING("2026-04-18T14:32:07Z", m.utcStart);
    TEST_ASSERT_EQUAL_STRING("14:32:07",              m.timeOfDayStart);
    TEST_ASSERT_EQUAL(true, m.gpsFixSeen);
}

static void test_builder_empty_time_strings_treated_as_null()
{
    LogMetaBuilder b;
    b.Begin("4.19.0", "abc1234", 1, EfisType::Dynon);
    b.OnRow(MakeRow(0, 0, 0), "",         nullptr);    // empty string = absent
    b.OnRow(MakeRow(100, 0, 0), "13:00:00", nullptr);
    LogMeta m = b.Finalize();
    TEST_ASSERT_EQUAL_STRING("13:00:00", m.timeOfDayStart);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_efis_type_to_from_string);
    RUN_TEST(test_round_trip_full_meta);
    RUN_TEST(test_round_trip_minimal_meta_no_times);
    RUN_TEST(test_parse_ignores_unknown_keys);
    RUN_TEST(test_parse_empty_input_returns_false);
    RUN_TEST(test_parse_tolerates_trailing_garbage);
    RUN_TEST(test_write_buffer_too_small_returns_zero);
    RUN_TEST(test_builder_zero_rows);
    RUN_TEST(test_builder_duration_and_running_max);
    RUN_TEST(test_builder_captures_first_time_only);
    RUN_TEST(test_builder_captures_first_utc_only);
    RUN_TEST(test_builder_empty_time_strings_treated_as_null);
    return UNITY_END();
}
```

- [ ] **Step 3: Run the new tests — expect linker failures**

Run:
```bash
pio test -e native -f test_log_meta -v 2>&1 | tail -20
```

Expected: **build/link failures** — `LogMetaBuilder.h`, `LogMetaFile.h` do not exist and `EfisTypeToString`/`FromString` are unresolved symbols. That's what we want at this stage.

- [ ] **Step 4: Commit the failing tests**

```bash
git add test/test_log_meta/test_log_meta.cpp
git commit -m "$(cat <<'EOF'
Test: LogMeta write/parse/build (failing)

Twelve cases covering round-trip, partial fields, unknown keys, empty
input, buffer-too-small, and LogMetaBuilder accumulation semantics.
Will pass once LogMetaFile and LogMetaBuilder ship in the next commits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Implement `LogMetaFile` (Write + Parse) and `EfisType*String`

**Files:**
- Create: `software/Libraries/onspeed_core/src/log/LogMetaFile.h`
- Create: `software/Libraries/onspeed_core/src/log/LogMetaFile.cpp`

- [ ] **Step 1: Write the header**

Create `software/Libraries/onspeed_core/src/log/LogMetaFile.h`:

```cpp
// LogMetaFile.h
//
// Pure serialization functions for the LogMeta sidecar format. No I/O —
// callers handle read/write against SD. Format is plain `key=value\n`
// text, one pair per line. Unknown keys are ignored on parse.

#ifndef ONSPEED_CORE_LOG_LOG_META_FILE_H
#define ONSPEED_CORE_LOG_LOG_META_FILE_H

#include <cstddef>
#include <string_view>

#include <log/LogMeta.h>

namespace onspeed::log {

// Serialize `meta` into `buf`. Returns bytes written (not including NUL)
// on success, 0 on buffer-too-small. `buf[0]` is set to NUL on failure.
size_t WriteMetaFile(const LogMeta& meta, char* buf, size_t bufLen);

// Parse `text` as a LogMeta sidecar. Unknown keys are ignored; missing
// keys leave the corresponding LogMeta field at its LogMeta{} default.
// Returns true if at least one recognised key was parsed; false if the
// input yielded no recognised keys at all (empty string, all-garbage).
bool ParseMetaFile(std::string_view text, LogMeta* out);

} // namespace onspeed::log

#endif
```

- [ ] **Step 2: Write the implementation**

Create `software/Libraries/onspeed_core/src/log/LogMetaFile.cpp`:

```cpp
// LogMetaFile.cpp

#include <log/LogMetaFile.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace onspeed::log {

// ---------------------------------------------------------------------------
// EfisType <-> string
// ---------------------------------------------------------------------------

const char* EfisTypeToString(EfisType t)
{
    switch (t) {
        case EfisType::Dynon:  return "dynon";
        case EfisType::Garmin: return "garmin";
        case EfisType::Mgl:    return "mgl";
        case EfisType::Vn300:  return "vn300";
        case EfisType::None:
        default:               return "none";
    }
}

EfisType EfisTypeFromString(const char* s)
{
    if (!s) return EfisType::None;
    if (!std::strcmp(s, "dynon"))  return EfisType::Dynon;
    if (!std::strcmp(s, "garmin")) return EfisType::Garmin;
    if (!std::strcmp(s, "mgl"))    return EfisType::Mgl;
    if (!std::strcmp(s, "vn300"))  return EfisType::Vn300;
    return EfisType::None;
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

// Append a printf-formatted line into `buf`. Returns true on success, false
// on buffer-full (in which case *used is unchanged).
static bool appendLine(char* buf, size_t bufLen, size_t* used,
                       const char* fmt, ...)
{
    if (*used >= bufLen) return false;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *used, bufLen - *used, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    size_t nu = static_cast<size_t>(n);
    if (nu >= bufLen - *used) return false;   // truncated
    *used += nu;
    return true;
}

size_t WriteMetaFile(const LogMeta& meta, char* buf, size_t bufLen)
{
    if (bufLen == 0) return 0;
    buf[0] = '\0';

    size_t used = 0;
    bool ok = true;
    ok &= appendLine(buf, bufLen, &used, "meta_version=%u\n",       meta.metaVersion);
    ok &= appendLine(buf, bufLen, &used, "log_format_version=%d\n", meta.logFormatVersion);
    ok &= appendLine(buf, bufLen, &used, "firmware=%s\n",           meta.firmware);
    ok &= appendLine(buf, bufLen, &used, "firmware_sha=%s\n",       meta.firmwareSha);
    ok &= appendLine(buf, bufLen, &used, "duration_ms=%lu\n",       (unsigned long)meta.durationMs);
    ok &= appendLine(buf, bufLen, &used, "row_count=%lu\n",         (unsigned long)meta.rowCount);
    ok &= appendLine(buf, bufLen, &used, "max_ias_kt=%.1f\n",       meta.maxIasKt);
    ok &= appendLine(buf, bufLen, &used, "max_palt_ft=%.0f\n",      meta.maxPaltFt);
    ok &= appendLine(buf, bufLen, &used, "efis_type=%s\n",          EfisTypeToString(meta.efisType));
    ok &= appendLine(buf, bufLen, &used, "gps_fix_seen=%d\n",       meta.gpsFixSeen ? 1 : 0);

    if (meta.utcStart[0] != '\0')
        ok &= appendLine(buf, bufLen, &used, "utc_start=%s\n",      meta.utcStart);
    if (meta.timeOfDayStart[0] != '\0')
        ok &= appendLine(buf, bufLen, &used, "time_of_day_start=%s\n", meta.timeOfDayStart);

    if (!ok) {
        buf[0] = '\0';
        return 0;
    }
    return used;
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

// Safe string copy into a fixed char array: always NUL-terminates, truncates
// at dstLen - 1. `dst` assumed non-null; `src` may contain no NUL within
// srcLen bytes (we stop at srcLen).
static void safeCopy(char* dst, size_t dstLen, const char* src, size_t srcLen)
{
    if (dstLen == 0) return;
    size_t n = (srcLen < dstLen - 1) ? srcLen : dstLen - 1;
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool parseUint32(std::string_view v, uint32_t* out)
{
    char tmp[16];
    if (v.size() >= sizeof(tmp)) return false;
    std::memcpy(tmp, v.data(), v.size());
    tmp[v.size()] = '\0';
    char* end = nullptr;
    unsigned long n = std::strtoul(tmp, &end, 10);
    if (end == tmp) return false;
    *out = static_cast<uint32_t>(n);
    return true;
}

static bool parseInt(std::string_view v, int* out)
{
    char tmp[16];
    if (v.size() >= sizeof(tmp)) return false;
    std::memcpy(tmp, v.data(), v.size());
    tmp[v.size()] = '\0';
    char* end = nullptr;
    long n = std::strtol(tmp, &end, 10);
    if (end == tmp) return false;
    *out = static_cast<int>(n);
    return true;
}

static bool parseFloat(std::string_view v, float* out)
{
    char tmp[24];
    if (v.size() >= sizeof(tmp)) return false;
    std::memcpy(tmp, v.data(), v.size());
    tmp[v.size()] = '\0';
    char* end = nullptr;
    float n = std::strtof(tmp, &end);
    if (end == tmp) return false;
    *out = n;
    return true;
}

bool ParseMetaFile(std::string_view text, LogMeta* out)
{
    if (!out) return false;
    int recognised = 0;

    size_t i = 0;
    while (i < text.size()) {
        // Find end of line.
        size_t j = text.find('\n', i);
        if (j == std::string_view::npos) j = text.size();
        std::string_view line = text.substr(i, j - i);
        i = (j == text.size()) ? j : j + 1;

        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;

        std::string_view key = line.substr(0, eq);
        std::string_view val = line.substr(eq + 1);

        // NUL-terminated key lookup. Copy into small stack buffer.
        char keyBuf[32];
        if (key.size() >= sizeof(keyBuf)) continue;
        std::memcpy(keyBuf, key.data(), key.size());
        keyBuf[key.size()] = '\0';

        if (!std::strcmp(keyBuf, "meta_version")) {
            uint32_t v = 0;
            if (parseUint32(val, &v)) { out->metaVersion = static_cast<uint8_t>(v); recognised++; }
        } else if (!std::strcmp(keyBuf, "log_format_version")) {
            if (parseInt(val, &out->logFormatVersion)) recognised++;
        } else if (!std::strcmp(keyBuf, "firmware")) {
            safeCopy(out->firmware, sizeof(out->firmware), val.data(), val.size());
            recognised++;
        } else if (!std::strcmp(keyBuf, "firmware_sha")) {
            safeCopy(out->firmwareSha, sizeof(out->firmwareSha), val.data(), val.size());
            recognised++;
        } else if (!std::strcmp(keyBuf, "duration_ms")) {
            if (parseUint32(val, &out->durationMs)) recognised++;
        } else if (!std::strcmp(keyBuf, "row_count")) {
            if (parseUint32(val, &out->rowCount)) recognised++;
        } else if (!std::strcmp(keyBuf, "max_ias_kt")) {
            if (parseFloat(val, &out->maxIasKt)) recognised++;
        } else if (!std::strcmp(keyBuf, "max_palt_ft")) {
            if (parseFloat(val, &out->maxPaltFt)) recognised++;
        } else if (!std::strcmp(keyBuf, "efis_type")) {
            char tmp[16];
            if (val.size() < sizeof(tmp)) {
                std::memcpy(tmp, val.data(), val.size());
                tmp[val.size()] = '\0';
                out->efisType = EfisTypeFromString(tmp);
                recognised++;
            }
        } else if (!std::strcmp(keyBuf, "gps_fix_seen")) {
            int v = 0;
            if (parseInt(val, &v)) { out->gpsFixSeen = (v != 0); recognised++; }
        } else if (!std::strcmp(keyBuf, "utc_start")) {
            safeCopy(out->utcStart, sizeof(out->utcStart), val.data(), val.size());
            recognised++;
        } else if (!std::strcmp(keyBuf, "time_of_day_start")) {
            safeCopy(out->timeOfDayStart, sizeof(out->timeOfDayStart), val.data(), val.size());
            recognised++;
        }
        // Unknown keys silently ignored.
    }
    return recognised > 0;
}

} // namespace onspeed::log
```

- [ ] **Step 3: Verify the Parse/Write tests pass**

The builder tests will still fail (no `LogMetaBuilder.h` yet). Run:

```bash
pio test -e native -f test_log_meta -v 2>&1 | tail -25
```

Expected: `test_efis_type_to_from_string`, `test_round_trip_full_meta`, `test_round_trip_minimal_meta_no_times`, `test_parse_ignores_unknown_keys`, `test_parse_empty_input_returns_false`, `test_parse_tolerates_trailing_garbage`, `test_write_buffer_too_small_returns_zero` **pass**. Builder tests fail due to missing header (or link-error if the file fails to build). That's expected.

Wait — if `LogMetaBuilder.h` is missing the whole test file fails to compile. So at this step the test binary won't build. To unblock this task's green-check, add a **minimal stub** `LogMetaBuilder.h` now that declares the class and methods but whose `.cpp` will come in the next task.

- [ ] **Step 4: Write the stub `LogMetaBuilder.h`**

Create `software/Libraries/onspeed_core/src/log/LogMetaBuilder.h` (full, final content — we'll implement the `.cpp` next):

```cpp
// LogMetaBuilder.h
//
// Accumulator for per-session log metadata. Fed one LogRow at a time
// during the session; produces a populated LogMeta at close. Pure,
// platform-independent — no I/O.

#ifndef ONSPEED_CORE_LOG_LOG_META_BUILDER_H
#define ONSPEED_CORE_LOG_LOG_META_BUILDER_H

#include <cstdint>

#include <log/LogMeta.h>
#include <types/LogRow.h>

namespace onspeed::log {

class LogMetaBuilder {
public:
    // Call once at session start. Safe against oversized strings
    // (truncated to fit LogMeta's char arrays).
    void Begin(const char* firmware,
               const char* firmwareSha,
               int         logFormatVersion,
               EfisType    efisType);

    // Call once per row written to the CSV. `hmsOrNull` is an 8-char
    // "HH:MM:SS" NUL-terminated string when the EFIS carries a valid
    // time-of-day this frame, otherwise nullptr or empty string (both
    // treated identically). `utcOrNull` is ISO-8601 UTC (e.g.
    // "2026-04-18T14:32:07Z") with the same convention.
    //
    // Only the FIRST non-empty time string encountered is captured.
    // Subsequent rows update duration and running maxima only.
    void OnRow(const onspeed::LogRow& row,
               const char* hmsOrNull,
               const char* utcOrNull);

    // Produce a populated LogMeta. Safe to call at any time.
    // Duration is last-seen minus first-seen timeStampMs; 0 if no rows
    // have arrived yet.
    LogMeta Finalize() const;

    // Return to a just-constructed state. Mainly for tests.
    void Reset();

private:
    LogMeta  m_meta{};
    uint32_t m_firstTimeMs  = 0;
    uint32_t m_lastTimeMs   = 0;
    bool     m_haveFirstRow = false;
};

} // namespace onspeed::log

#endif
```

- [ ] **Step 5: Re-run Write/Parse tests (builder still fails at link)**

Run:
```bash
pio test -e native -f test_log_meta -v 2>&1 | tail -25
```

Expected: the test binary fails to link because `LogMetaBuilder::Begin` / `OnRow` / `Finalize` are undefined symbols. That's fine — we'll implement them next. The intent of this intermediate state is just that the code compiles (no syntax errors in the header).

If linker errors aren't reported as "undefined reference to", investigate — it probably means something in Step 1–4 has a syntax bug.

- [ ] **Step 6: Commit**

```bash
git add software/Libraries/onspeed_core/src/log/LogMetaFile.h \
        software/Libraries/onspeed_core/src/log/LogMetaFile.cpp \
        software/Libraries/onspeed_core/src/log/LogMetaBuilder.h
git commit -m "$(cat <<'EOF'
Implement LogMetaFile + LogMetaBuilder header

WriteMetaFile/ParseMetaFile serialize LogMeta as key=value text. Parser
tolerates unknown keys and missing fields. EfisTypeToString/FromString
handle all five values plus unknown input. LogMetaBuilder header
declares the accumulator interface; implementation in next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Implement `LogMetaBuilder`

**Files:**
- Create: `software/Libraries/onspeed_core/src/log/LogMetaBuilder.cpp`

- [ ] **Step 1: Write the implementation**

Create `software/Libraries/onspeed_core/src/log/LogMetaBuilder.cpp`:

```cpp
// LogMetaBuilder.cpp

#include <log/LogMetaBuilder.h>

#include <cstring>

namespace onspeed::log {

void LogMetaBuilder::Begin(const char* firmware,
                           const char* firmwareSha,
                           int         logFormatVersion,
                           EfisType    efisType)
{
    m_meta = LogMeta{};   // reset
    if (firmware) {
        std::strncpy(m_meta.firmware, firmware, sizeof(m_meta.firmware) - 1);
        m_meta.firmware[sizeof(m_meta.firmware) - 1] = '\0';
    }
    if (firmwareSha) {
        std::strncpy(m_meta.firmwareSha, firmwareSha, sizeof(m_meta.firmwareSha) - 1);
        m_meta.firmwareSha[sizeof(m_meta.firmwareSha) - 1] = '\0';
    }
    m_meta.logFormatVersion = logFormatVersion;
    m_meta.efisType         = efisType;
    m_firstTimeMs  = 0;
    m_lastTimeMs   = 0;
    m_haveFirstRow = false;
}

void LogMetaBuilder::OnRow(const onspeed::LogRow& row,
                           const char* hmsOrNull,
                           const char* utcOrNull)
{
    if (!m_haveFirstRow) {
        m_firstTimeMs  = row.timeStampMs;
        m_haveFirstRow = true;
    }
    m_lastTimeMs = row.timeStampMs;
    m_meta.rowCount++;

    if (row.iasKt  > m_meta.maxIasKt)  m_meta.maxIasKt  = row.iasKt;
    if (row.paltFt > m_meta.maxPaltFt) m_meta.maxPaltFt = row.paltFt;

    const bool haveHms = (hmsOrNull && hmsOrNull[0] != '\0');
    const bool haveUtc = (utcOrNull && utcOrNull[0] != '\0');

    if (haveHms || haveUtc)
        m_meta.gpsFixSeen = true;

    if (haveHms && m_meta.timeOfDayStart[0] == '\0') {
        std::strncpy(m_meta.timeOfDayStart, hmsOrNull,
                     sizeof(m_meta.timeOfDayStart) - 1);
        m_meta.timeOfDayStart[sizeof(m_meta.timeOfDayStart) - 1] = '\0';
    }
    if (haveUtc && m_meta.utcStart[0] == '\0') {
        std::strncpy(m_meta.utcStart, utcOrNull,
                     sizeof(m_meta.utcStart) - 1);
        m_meta.utcStart[sizeof(m_meta.utcStart) - 1] = '\0';
    }
}

LogMeta LogMetaBuilder::Finalize() const
{
    LogMeta out = m_meta;
    out.durationMs = m_haveFirstRow ? (m_lastTimeMs - m_firstTimeMs) : 0u;
    return out;
}

void LogMetaBuilder::Reset()
{
    m_meta = LogMeta{};
    m_firstTimeMs  = 0;
    m_lastTimeMs   = 0;
    m_haveFirstRow = false;
}

} // namespace onspeed::log
```

- [ ] **Step 2: Run the full `test_log_meta` suite**

Run:
```bash
pio test -e native -f test_log_meta -v 2>&1 | tail -20
```

Expected: all 12 tests pass.

- [ ] **Step 3: Run the full native suite to catch regressions**

Run:
```bash
pio test -e native 2>&1 | tail -3
```

Expected: all existing tests + the 4 new Dynon SkyView tests + the 12 new log-meta tests pass. Count should be baseline + 16 = 687 (check relative, not absolute).

- [ ] **Step 4: Commit**

```bash
git add software/Libraries/onspeed_core/src/log/LogMetaBuilder.cpp
git commit -m "$(cat <<'EOF'
Implement LogMetaBuilder

Accumulator for per-session log metadata: tracks first/last row
timestamp, running max IAS and altitude, first valid time-of-day and
UTC strings seen (later values ignored), row count. Pure, unit-tested.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Add `rename` to `SdFileSys`

**Files:**
- Modify: `software/sketch_common/src/drivers/SdFileSys.h`
- Modify: `software/sketch_common/src/drivers/SdFileSys.cpp`

- [ ] **Step 1: Declare the method**

In `software/sketch_common/src/drivers/SdFileSys.h`, inside the `SdFileSys` class's public section, near the existing `remove` declaration, add:

```cpp
    // Atomic-ish rename via SdFat. Returns true on success. Caller must
    // hold xWriteMutex — same convention as remove().
    bool rename(const char* srcName, const char* dstName);
```

- [ ] **Step 2: Implement it**

In `software/sketch_common/src/drivers/SdFileSys.cpp`, near the existing `remove()` implementation, add:

```cpp
bool SdFileSys::rename(const char* srcName, const char* dstName)
{
    return uSD_FAT.rename(srcName, dstName);
}
```

- [ ] **Step 3: Build firmware**

Run:
```bash
pio run -e esp32s3-v4p 2>&1 | tail -5
```

Expected: clean build, no warnings.

- [ ] **Step 4: Run native tests to confirm no regressions**

Run:
```bash
pio test -e native 2>&1 | tail -3
```

Expected: same count as Task 7 end.

- [ ] **Step 5: Commit**

```bash
git add software/sketch_common/src/drivers/SdFileSys.h \
        software/sketch_common/src/drivers/SdFileSys.cpp
git commit -m "$(cat <<'EOF'
Add SdFileSys::rename wrapper

Delegates to SdFat::rename. Used by LogSensor::Close to rename
log_NNN.{csv,meta} to YYYY-MM-DD_NNN.{csv,meta} when the session
captured a UTC timestamp.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Wire `LogMetaBuilder` into `LogSensor`

This is the biggest firmware-side change. It has three sub-parts: (a) hold the builder and base filename, (b) feed rows into it from `Write()`, (c) at `Close()`, write the sidecar and conditionally rename both files.

**Files:**
- Modify: `software/sketch_common/src/tasks/LogSensor.h`
- Modify: `software/sketch_common/src/tasks/LogSensor.cpp`

- [ ] **Step 1: Add private state to `LogSensor.h`**

In `software/sketch_common/src/tasks/LogSensor.h`, add includes + private members. The full final content:

```cpp
#pragma once

#include "src/Globals.h"
#include <log/LogMetaBuilder.h>

// FreeRTOS task for writing to disk
void LogSensorCommitTask(void *pvParams);

// ============================================================================

class LogSensor
{
public:
    LogSensor();

    // Methods
public:
    void Open();
    void Open(FsFile * phFile);
    void Close();
    void Write();

    // Data
private:
    // Base filename WITHOUT extension, e.g. "log_042". Used at Close()
    // to write the sidecar and conditionally rename both files.
    char                    m_szBaseName[16] = {};

    // Accumulates sidecar metadata across the session. Reset in Open(),
    // fed in Write(), finalised in Close().
    onspeed::log::LogMetaBuilder m_metaBuilder;
};
```

- [ ] **Step 2: Capture the base name in `Open()`**

In `software/sketch_common/src/tasks/LogSensor.cpp`, find the line (near line 276–294 of the current file) where `szSensorLogFilename` is set to `log_NNN.csv`. Immediately after that `snprintf`, add a parallel `snprintf` into the new member:

```cpp
    snprintf(szSensorLogFilename, sizeof(szSensorLogFilename), "log_%03d.csv", iMaxFileNum + 1);
    snprintf(m_szBaseName, sizeof(m_szBaseName), "log_%03d", iMaxFileNum + 1);
```

Then, inside the `if (m_hLogFile.isOpen())` block (after the header write + sync), add a `Begin()` call:

```cpp
        m_hLogFile.sync();

        // Initialise sidecar accumulator for this session.
        onspeed::log::EfisType etype = onspeed::log::EfisType::None;
        if (g_Config.bReadEfisData) {
            switch (g_EfisSerial.enType) {
                case EfisSerialPort::EnDynonSkyview:
                case EfisSerialPort::EnDynonD10:
                    etype = onspeed::log::EfisType::Dynon; break;
                case EfisSerialPort::EnGarminG5:
                case EfisSerialPort::EnGarminG3X:
                    etype = onspeed::log::EfisType::Garmin; break;
                case EfisSerialPort::EnMGL:
                    etype = onspeed::log::EfisType::Mgl; break;
                case EfisSerialPort::EnVN300:
                    etype = onspeed::log::EfisType::Vn300; break;
                default:
                    etype = onspeed::log::EfisType::None; break;
            }
        }
        m_metaBuilder.Begin(BuildInfo::version,
                            BuildInfo::gitShortSha,
                            onspeed::proto::log_csv::kFormatVersion,
                            etype);
```

(If the exact `EfisSerialPort::En*` names differ from those above, grep the codebase first — `grep -n "EfisSerialPort::En" software/sketch_common/src/` — and substitute. The important thing is mapping each vendor enum to the onspeed::log::EfisType equivalent, falling through to `None`.)

- [ ] **Step 3: Feed rows into the builder from `Write()`**

Find the end of `LogSensor::Write()` — specifically, where `snprintf` formats the CSV line and pushes it into the ring buffer. After the ring-buffer push but before returning, add:

```cpp
    // Sidecar: pass the EFIS time-of-day string if fresh; VN-300 UTC if
    // fresh. Empty string / nullptr = absent (builder treats both alike).
    const char* hmsOrNull = nullptr;
    const char* utcOrNull = nullptr;
    if (g_Config.bReadEfisData) {
        // EFIS parsers write time-of-day to g_EfisSerial.szTimeOfDay when they
        // have a valid one; empty string when not. Accept that as our signal.
        if (g_EfisSerial.szTimeOfDay[0] != '\0')
            hmsOrNull = g_EfisSerial.szTimeOfDay;

        // VN-300 carries full UTC in szTimeUTC (already exists today).
        if (g_EfisSerial.enType == EfisSerialPort::EnVN300 &&
            g_EfisSerial.suVN300.szTimeUTC[0] != '\0' &&
            g_EfisSerial.suVN300.iGpsFix > 0)
            utcOrNull = g_EfisSerial.suVN300.szTimeUTC;
    }
    m_metaBuilder.OnRow(row, hmsOrNull, utcOrNull);
```

The reference to `g_EfisSerial.szTimeOfDay` is a field that doesn't exist yet in the sketch's EFIS glue code. Add it in a sub-step:

- [ ] **Step 3a: Add `szTimeOfDay` to the sketch's EFIS state**

Grep for where the sketch keeps its current `g_EfisSerial` data:

```bash
grep -n "szTimeUTC\|efisTime\|suEfis" software/sketch_common/src/io/*.{h,cpp} | head -30
```

Find the struct or class that holds the parsed EFIS output (likely something with `szTimeUTC` already on it). Add a sibling char array:

```cpp
char szTimeOfDay[9] = {};   // "HH:MM:SS" or empty when absent
```

Then find the code that copies fields from a freshly-parsed `EfisFrame` into this struct (look for assignments from `frame.iasKt`, `frame.pitchDeg`, etc.) and add:

```cpp
// Dynon, Garmin, etc: frame.timeOfDayHms holds HH:MM:SS or empty.
std::strncpy(suEfis.szTimeOfDay, frame.timeOfDayHms, sizeof(suEfis.szTimeOfDay) - 1);
suEfis.szTimeOfDay[sizeof(suEfis.szTimeOfDay) - 1] = '\0';
```

If the glue code applies frame fields only when finite (`std::isfinite` pattern for floats), copy `timeOfDayHms` unconditionally — empty string is a valid "absent" signal already.

- [ ] **Step 4: Write sidecar + conditional rename in `Close()`**

Replace the current `LogSensor::Close()` body (which just flushes + closes the CSV) with:

```cpp
void LogSensor::Close()
{
    FlushStagingBufferLocked();
    m_hLogFile.close();

    // Nothing to serialise if we never opened a file. The builder's Finalize
    // is safe on zero rows, but we also guarded CSV open so filename set-up
    // happened there; if it didn't, m_szBaseName will be empty.
    if (m_szBaseName[0] == '\0') return;

    onspeed::log::LogMeta meta = m_metaBuilder.Finalize();

    // Serialise sidecar.
    char buf[512];
    size_t n = onspeed::log::WriteMetaFile(meta, buf, sizeof(buf));
    if (n > 0) {
        char metaPath[32];
        snprintf(metaPath, sizeof(metaPath), "%s.meta", m_szBaseName);
        FsFile f = g_SdFileSys.open(metaPath, O_RDWR | O_CREAT | O_TRUNC);
        if (f.isOpen()) {
            f.write(buf, n);
            f.sync();
            f.close();
        } else {
            g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
                          "Sidecar meta open failed");
        }
    } else {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
                      "Sidecar meta serialize failed (buffer too small?)");
    }

    // Optional rename: only when we captured a full UTC date+time.
    if (meta.utcStart[0] != '\0') {
        // utcStart is "YYYY-MM-DDTHH:MM:SSZ" — take the first 10 chars as date.
        char datePrefix[11];
        std::memcpy(datePrefix, meta.utcStart, 10);
        datePrefix[10] = '\0';

        // New base name: "YYYY-MM-DD_NNN". Extract the NNN from m_szBaseName
        // (which is "log_NNN").
        const char* nnn = m_szBaseName + 4;   // skip "log_"

        char newCsvName[32];
        char newMetaName[32];
        snprintf(newCsvName,  sizeof(newCsvName),  "%s_%s.csv",  datePrefix, nnn);
        snprintf(newMetaName, sizeof(newMetaName), "%s_%s.meta", datePrefix, nnn);

        char oldCsvName[32];
        char oldMetaName[32];
        snprintf(oldCsvName,  sizeof(oldCsvName),  "%s.csv",  m_szBaseName);
        snprintf(oldMetaName, sizeof(oldMetaName), "%s.meta", m_szBaseName);

        if (!g_SdFileSys.exists(newCsvName) && !g_SdFileSys.exists(newMetaName)) {
            g_SdFileSys.rename(oldCsvName,  newCsvName);
            g_SdFileSys.rename(oldMetaName, newMetaName);
        }
    }

    m_szBaseName[0] = '\0';
}
```

Include whatever is needed at the top of `LogSensor.cpp`:

```cpp
#include <log/LogMetaFile.h>
```

- [ ] **Step 5: Build firmware**

Run:
```bash
pio run -e esp32s3-v4p 2>&1 | tail -8
```

Expected: clean build, no warnings.

If you hit errors referencing `g_EfisSerial.szTimeOfDay` or `g_EfisSerial.suVN300.iGpsFix` or similar — those are guesses based on the sketch's existing patterns. Grep for the actual field names and substitute. **Do not delete or rename existing fields.** If a check is unavailable, skip it rather than inventing new state in this task — correctness for today (no VN-300 in pilot's install) is more important than anticipating VN-300 edge cases.

- [ ] **Step 6: Run native tests**

Run:
```bash
pio test -e native 2>&1 | tail -3
```

Expected: no regressions (builder + log-meta tests still pass; existing tests unaffected).

- [ ] **Step 7: Commit**

```bash
git add software/sketch_common/src/tasks/LogSensor.h \
        software/sketch_common/src/tasks/LogSensor.cpp \
        software/sketch_common/src/io/
git commit -m "$(cat <<'EOF'
LogSensor: write metadata sidecar at close, rename on UTC

LogSensor now owns a LogMetaBuilder, feeds each row into it during
Write(), and serialises a log_NNN.meta sidecar at Close(). When the
session captured a full UTC timestamp (VN-300 with GPS fix), also
rename log_NNN.{csv,meta} to YYYY-MM-DD_NNN.{csv,meta}. Collision on
the target name skips the rename (sidecar still identifies the log).

Sketch EFIS state gains a szTimeOfDay field populated from
EfisFrame.timeOfDayHms so LogSensor can pass the fresh value to the
builder without peeking into vendor-specific parser state.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Extend `IsSafeLogFilename` to accept `.meta`

**Files:**
- Modify: `software/sketch_common/src/web_server/ConfigWebServer.cpp`

- [ ] **Step 1: Update the allow-list**

Find `IsSafeLogFilename` around line 137 of `software/sketch_common/src/web_server/ConfigWebServer.cpp`. Change the extension check from:

```cpp
    if (!s.endsWith(".csv") && !s.endsWith(".CSV") &&
        !s.endsWith(".log") && !s.endsWith(".LOG"))
        return false;
```

to:

```cpp
    if (!s.endsWith(".csv")  && !s.endsWith(".CSV") &&
        !s.endsWith(".log")  && !s.endsWith(".LOG") &&
        !s.endsWith(".meta") && !s.endsWith(".META"))
        return false;
```

- [ ] **Step 2: Build firmware**

Run:
```bash
pio run -e esp32s3-v4p 2>&1 | tail -3
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add software/sketch_common/src/web_server/ConfigWebServer.cpp
git commit -m "$(cat <<'EOF'
Allow .meta filenames through IsSafeLogFilename

Bulk-delete handler needs to remove sidecar files alongside their CSVs.
Length cap (32) comfortably covers YYYY-MM-DD_NNN.meta (20 chars).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Extend single-file delete to remove sidecar

**Files:**
- Modify: `software/sketch_common/src/web_server/ConfigWebServer.cpp`

- [ ] **Step 1: Update `HandleDelete`**

Find the `HandleDelete` function (around line 3094) where it does `g_SdFileSys.remove(sFilename.c_str())`. Add sidecar removal before the redirect:

```cpp
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100)))
            {
            g_SdFileSys.remove(sFilename.c_str());

            // Also remove matching sidecar if it exists. Best-effort: absent
            // sidecar (old logs, power-cut, or non-CSV deletion) is fine.
            String sMeta = sFilename;
            int iDot = sMeta.lastIndexOf('.');
            if (iDot > 0) {
                sMeta = sMeta.substring(0, iDot) + ".meta";
                if (g_SdFileSys.exists(sMeta.c_str()))
                    g_SdFileSys.remove(sMeta.c_str());
            }

            xSemaphoreGive(xWriteMutex);
            }
```

- [ ] **Step 2: Build firmware**

Run:
```bash
pio run -e esp32s3-v4p 2>&1 | tail -3
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add software/sketch_common/src/web_server/ConfigWebServer.cpp
git commit -m "$(cat <<'EOF'
HandleDelete: also remove matching .meta sidecar

When a user deletes log_042.csv, we should clean up log_042.meta too.
Best-effort — a missing sidecar is not an error.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Rewrite `HandleLogs` with metadata columns

This is the biggest web-UI change. We add sidecar-reading, table formatting, and a form with checkboxes. The existing single-delete trash icon column stays in place.

**Files:**
- Modify: `software/sketch_common/src/web_server/ConfigWebServer.cpp`

- [ ] **Step 1: Add helper for reading + parsing a sidecar**

Near the top of `ConfigWebServer.cpp`, after other static helpers (around line 140 after `IsSafeLogFilename`), add:

```cpp
// Read the sidecar for a given CSV filename and parse into meta.
// Returns true on success (file existed and parsed). `sCsvName` should
// be the full ".csv" filename; the sidecar is derived by swapping the
// extension to ".meta". Caller should hold xWriteMutex.
static bool TryReadLogMeta(const char* sCsvName, onspeed::log::LogMeta* out)
{
    // Derive .meta filename.
    char sMeta[32];
    size_t len = strlen(sCsvName);
    if (len < 5 || len > 28) return false;   // need room for ".meta"
    memcpy(sMeta, sCsvName, len);
    // Replace the last 4 chars (".csv"/".log") with ".meta".
    const char* dot = strrchr(sMeta, '.');
    if (!dot) return false;
    size_t dotIdx = dot - sMeta;
    snprintf(sMeta + dotIdx, sizeof(sMeta) - dotIdx, ".meta");

    FsFile f = g_SdFileSys.open(sMeta, O_RDONLY);
    if (!f.isOpen()) return false;

    char buf[512];
    int n = f.read(buf, sizeof(buf) - 1);
    f.close();
    if (n <= 0) return false;
    buf[n] = '\0';
    return onspeed::log::ParseMetaFile(std::string_view(buf, n), out);
}

// Format a duration in ms as "1h 30m 42s" / "42s" / "12m 3s". Writes
// into out[16].
static void FormatDurationMs(uint32_t ms, char out[16])
{
    uint32_t total_s = ms / 1000u;
    uint32_t h = total_s / 3600u;
    uint32_t m = (total_s % 3600u) / 60u;
    uint32_t s = total_s % 60u;
    if (h > 0)
        snprintf(out, 16, "%luh %lum %lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    else if (m > 0)
        snprintf(out, 16, "%lum %lus", (unsigned long)m, (unsigned long)s);
    else
        snprintf(out, 16, "%lus", (unsigned long)s);
}

// Format the "Start" column. Prefers UTC date+time, falls back to
// time-of-day, falls back to a single en-dash (U+2014 via HTML entity).
// Writes into out[32].
static void FormatStart(const onspeed::log::LogMeta& meta, char out[32])
{
    if (meta.utcStart[0] != '\0') {
        // "YYYY-MM-DDTHH:MM:SSZ" -> "YYYY-MM-DD HH:MM".
        // Clamp to 16 visible chars.
        char y[5], mo[3], d[3], hh[3], mm[3];
        if (sscanf(meta.utcStart, "%4s-%2s-%2sT%2s:%2s",
                   y, mo, d, hh, mm) == 5) {
            snprintf(out, 32, "%s-%s-%s %s:%s", y, mo, d, hh, mm);
            return;
        }
    }
    if (meta.timeOfDayStart[0] != '\0') {
        // "HH:MM:SS" -> "HH:MM" (drop seconds for list view).
        snprintf(out, 32, "%.5s", meta.timeOfDayStart);
        return;
    }
    snprintf(out, 32, "&mdash;");
}
```

- [ ] **Step 2: Rewrite `HandleLogs`**

Replace the body of `HandleLogs` (around line 3020) with:

```cpp
void HandleLogs()
{
    SdFileSys::SuFileInfoList   suFileList;
    bool        bListStatus = false;
    String      sPage;

    UpdateHeader();
    sPage.reserve(pageHeader.length() + 16384);
    sPage += pageHeader;
    sPage += "<br>\n";

    // Under-pause enumerate + read each sidecar.
    struct PauseGuard
    {
        bool bPrevPause;
        PauseGuard() : bPrevPause(g_bPause) { g_bPause = true; }
        ~PauseGuard() { g_bPause = bPrevPause; }
    } pauseGuard;

    // Collect per-log metadata while we hold the mutex, then render.
    struct Entry {
        String   sName;          // "log_042.csv" or "2026-04-18_042.csv"
        uint64_t uSize = 0;
        bool     bHaveMeta = false;
        onspeed::log::LogMeta meta;
    };
    std::vector<Entry> entries;
    entries.reserve(32);

    uint64_t uTotalSize = 0;

    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000)))
    {
        bListStatus = g_SdFileSys.FileList(&suFileList);
        if (bListStatus) {
            for (size_t i = 0; i < suFileList.size(); i++) {
                const char* name = suFileList[i].szFileName;
                // Skip .meta files — they live as sidecars of their .csv.
                size_t nlen = strlen(name);
                if (nlen >= 5 &&
                    (!strcasecmp(name + nlen - 5, ".meta") ||
                     !strcasecmp(name + nlen - 5, ".META")))
                    continue;
                // Skip anything else that isn't a log.
                if (nlen < 4 ||
                    (strcasecmp(name + nlen - 4, ".csv") != 0 &&
                     strcasecmp(name + nlen - 4, ".log") != 0))
                    continue;

                Entry e;
                e.sName = name;
                e.uSize = suFileList[i].uFileSize;
                e.bHaveMeta = TryReadLogMeta(name, &e.meta);
                uTotalSize += e.uSize;
                entries.push_back(std::move(e));
            }
        }
        xSemaphoreGive(xWriteMutex);
    }
    else
    {
        g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning,
                      "LOGS - SD busy (xWriteMutex)");
    }

    // Summary line.
    {
        char summary[64];
        snprintf(summary, sizeof(summary), "<p>%u logs, %s total</p>\n",
                 (unsigned)entries.size(),
                 sFormatBytes(uTotalSize).c_str());
        sPage += summary;
    }

    sPage += "<form method=\"POST\" action=\"/delete-bulk\">\n";
    sPage += "<table>\n";
    sPage += "<tr>"
             "<th></th>"
             "<th style=\"text-align:left\">Name</th>"
             "<th style=\"text-align:left\">Start</th>"
             "<th style=\"text-align:left\">Duration</th>"
             "<th style=\"text-align:right\">Max IAS</th>"
             "<th style=\"text-align:right\">Max Alt</th>"
             "<th style=\"text-align:right\">Size</th>"
             "<th></th>"
             "</tr>\n";

    if (bListStatus) {
        for (const Entry& e : entries) {
            char durStr[16];
            char startStr[32];
            if (e.bHaveMeta) {
                FormatDurationMs(e.meta.durationMs, durStr);
                FormatStart(e.meta, startStr);
            } else {
                snprintf(durStr,   sizeof(durStr),   "&mdash;");
                snprintf(startStr, sizeof(startStr), "&mdash;");
            }

            char iasStr[16], altStr[16];
            if (e.bHaveMeta) {
                snprintf(iasStr, sizeof(iasStr), "%.0f kt", e.meta.maxIasKt);
                snprintf(altStr, sizeof(altStr), "%.0f ft", e.meta.maxPaltFt);
            } else {
                snprintf(iasStr, sizeof(iasStr), "&mdash;");
                snprintf(altStr, sizeof(altStr), "&mdash;");
            }

            String sLine;
            sLine.reserve(512);
            sLine  = "<tr>";
            sLine += "<td><input type=\"checkbox\" name=\"f\" value=\"";
            sLine += e.sName; sLine += "\"></td>";
            sLine += "<td><a href=\"/download?file="; sLine += e.sName; sLine += "\">";
            sLine += e.sName; sLine += "</a></td>";
            sLine += "<td>"; sLine += startStr; sLine += "</td>";
            sLine += "<td>"; sLine += durStr;   sLine += "</td>";
            sLine += "<td style=\"text-align:right\">"; sLine += iasStr; sLine += "</td>";
            sLine += "<td style=\"text-align:right\">"; sLine += altStr; sLine += "</td>";
            sLine += "<td style=\"text-align:right\">"; sLine += sFormatBytes(e.uSize); sLine += "</td>";
            sLine += "<td>&nbsp;&nbsp;<a href=\"/delete?file="; sLine += e.sName; sLine += "\">";
            sLine += szHtmlTrashcan; sLine += "</a></td>";
            sLine += "</tr>\n";
            sPage += sLine;
        }
    } else {
        sPage += "<tr><td colspan=\"8\"><br><br><span style=\"color:red\">"
                 "SD card busy or not available.</span></td></tr>\n";
    }
    sPage += "</table>\n";
    sPage += "<p><button type=\"submit\">Delete selected</button></p>\n";
    sPage += "</form>\n";

    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
}
```

At the top of `ConfigWebServer.cpp`, add any includes we referenced:

```cpp
#include <log/LogMeta.h>
#include <log/LogMetaFile.h>
#include <vector>
```

- [ ] **Step 3: Build firmware**

Run:
```bash
pio run -e esp32s3-v4p 2>&1 | tail -8
```

Expected: clean build. If warnings/errors appear around `sscanf` format mismatches or `strcasecmp`, pull in the obvious headers (`<cstring>`, `<cstdio>`) — `ConfigWebServer.cpp` is large, so adjust locally.

- [ ] **Step 4: Commit**

```bash
git add software/sketch_common/src/web_server/ConfigWebServer.cpp
git commit -m "$(cat <<'EOF'
/logs: metadata columns + checkboxes + "Delete selected" form

Rewrites HandleLogs to read each log's sidecar and render Start (date
or HH:MM), Duration, Max IAS, Max Alt columns alongside the existing
Name, Size, and single-delete icon. Adds a single form wrapping all
checkboxes, submitting to POST /delete-bulk (handler in next commit).

Graceful degradation for logs missing a .meta: en-dash in all
metadata columns.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: Implement `HandleDeleteBulk` and register route

**Files:**
- Modify: `software/sketch_common/src/web_server/ConfigWebServer.cpp`

- [ ] **Step 1: Declare the handler prototype**

Near line 109 where `HandleLogs` / `HandleDelete` / `HandleDownload` are declared, add:

```cpp
void HandleDeleteBulk();
```

- [ ] **Step 2: Register the route**

Around line 225–227 where `/delete`, `/download`, `/logs` are registered, add:

```cpp
    CfgServer.on("/delete-bulk",     HTTP_POST, HandleDeleteBulk);
```

- [ ] **Step 3: Implement the handler**

After `HandleDelete` (which ends around line 3148), add:

```cpp
void HandleDeleteBulk()
{
    // Gather selected filenames. WebServer collects repeated "f" form args
    // as separate arg() entries — we iterate until exhausted.
    std::vector<String> selected;
    for (int i = 0; i < CfgServer.args(); i++) {
        if (CfgServer.argName(i) == "f") {
            String v = CfgServer.arg(i);
            if (IsSafeLogFilename(v))
                selected.push_back(v);
            // Silently drop unsafe filenames — don't expose parse errors.
        }
    }

    if (selected.empty()) {
        CfgServer.sendHeader("Location", "/logs");
        CfgServer.send(301, "text/html", "");
        return;
    }

    // Not yet confirmed: show a confirmation page listing selected files.
    if (CfgServer.arg("confirm").indexOf("yes") < 0) {
        String sPage;
        UpdateHeader();
        sPage.reserve(pageHeader.length() + 2048);
        sPage += pageHeader;
        sPage += "<br><br><p style=\"color:red\">Delete these ";
        sPage += String(selected.size());
        sPage += " file(s)?</p>\n<form method=\"POST\" action=\"/delete-bulk\">\n";
        sPage += "<input type=\"hidden\" name=\"confirm\" value=\"yes\">\n";
        sPage += "<ul>\n";
        for (const String& f : selected) {
            sPage += "<li>";
            sPage += f;
            sPage += "<input type=\"hidden\" name=\"f\" value=\"";
            sPage += f;
            sPage += "\"></li>\n";
        }
        sPage += "</ul>\n";
        sPage += "<button type=\"submit\" class=\"button\">Delete</button>\n";
        sPage += "<a href=\"/logs\">Cancel</a>\n";
        sPage += "</form>\n";
        sPage += pageFooter;
        CfgServer.send(200, "text/html", sPage);
        return;
    }

    // Confirmed: delete each file + its matching sidecar.
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000))) {
        for (const String& f : selected) {
            g_SdFileSys.remove(f.c_str());
            // Derive sidecar name by swapping extension to .meta.
            int iDot = f.lastIndexOf('.');
            if (iDot > 0) {
                String sMeta = f.substring(0, iDot) + ".meta";
                if (g_SdFileSys.exists(sMeta.c_str()))
                    g_SdFileSys.remove(sMeta.c_str());
            }
        }
        xSemaphoreGive(xWriteMutex);
    }

    CfgServer.sendHeader("Location", "/logs");
    CfgServer.send(301, "text/html", "");
}
```

- [ ] **Step 4: Build firmware**

Run:
```bash
pio run -e esp32s3-v4p 2>&1 | tail -5
```

Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add software/sketch_common/src/web_server/ConfigWebServer.cpp
git commit -m "$(cat <<'EOF'
HandleDeleteBulk: confirmation page + multi-file delete

POST /delete-bulk. First pass shows a confirmation page listing the
selected filenames; second pass (with confirm=yes) removes each
file + matching sidecar under xWriteMutex. Unsafe filenames are
silently dropped from the selection. Redirects back to /logs.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Full-stack verification

**Files:** none (build and test only)

- [ ] **Step 1: Native test suite**

Run:
```bash
pio test -e native 2>&1 | tail -5
```

Expected: all tests pass, count = baseline (671) + 4 (Dynon SkyView) + 12 (log meta) = 687.

- [ ] **Step 2: Firmware build, clean**

Run:
```bash
pio run -t clean -e esp32s3-v4p 2>&1 | tail -3
pio run -e esp32s3-v4p 2>&1 | tail -10
```

Expected: clean build with no warnings or errors.

- [ ] **Step 3: cppcheck**

Run:
```bash
scripts/cppcheck.sh 2>&1 | tail -20
```

Expected: no new issues introduced by our code under `software/sketch_common/src/` or `software/Libraries/onspeed_core/src/log/`. Pre-existing warnings unrelated to our changes are acceptable.

- [ ] **Step 4: Review the full diff**

Run:
```bash
git log --oneline origin/master..HEAD
git diff --stat origin/master..HEAD
```

Confirm:
- 14 commits, roughly one per task.
- Changes concentrated in: `onspeed_core/src/log/`, `onspeed_core/src/efis/DynonSkyview.cpp`, `onspeed_core/src/types/EfisFrame.h`, `sketch_common/src/tasks/LogSensor.{cpp,h}`, `sketch_common/src/drivers/SdFileSys.{cpp,h}`, `sketch_common/src/web_server/ConfigWebServer.cpp`, `sketch_common/src/io/` (the EFIS glue for `szTimeOfDay`), `test/test_log_meta/`, `test/test_efis_dynon_skyview/test_efis_dynon_skyview.cpp`.
- Design doc under `docs/superpowers/specs/`.

- [ ] **Step 5: Manual end-to-end (needs hardware)**

This step requires a V4P device and an SD card. Skip if hardware isn't accessible; flag for the pilot to run before merging.

Flash firmware to the V4P:
```bash
pio run -e esp32s3-v4p -t upload
pio device monitor
```

Then, with the device powered and WiFi connected:

1. Confirm SD logging is on (console `SENSORS` or web UI status).
2. Let the device run ~30 seconds, then power off.
3. Power on, connect to OnSpeed WiFi, visit `http://192.168.0.1/logs`.
4. The `/logs` page should show a single row for the new `log_NNN.csv` — `.meta` files are filtered out of the listing by design.
5. To confirm the sidecar actually exists on disk, pull the SD card into a reader and check for `log_NNN.meta` next to `log_NNN.csv`.
6. Back on the `/logs` page: the row should show a Duration column (~30s). If the Dynon SkyView was connected and GPS-locked during the run, a Start column with `HH:MM`; otherwise `—`.
7. Tick the checkbox for the new log, click "Delete selected". The confirmation page lists only the `.csv` by name (the `.meta` is cleaned up transparently). Click "Delete" — both files vanish from the SD card and the page redirects back.

If any of the above is off, open a bug ticket against this PR before merging — don't bundle fixes into this plan's execution.

- [ ] **Step 6: Push the branch**

```bash
git push -u origin sritchie/log-management
```

---

## Self-review checklist (run once after all tasks complete)

- [ ] Every spec section has a corresponding task. (Task 1–2 → EfisFrame + Dynon; Task 4–7 → sidecar; Task 8–9 → rename; Task 10–13 → web UI.)
- [ ] No placeholders (`TBD`, `TODO`, "similar to") remain in tasks.
- [ ] Types referenced in later tasks were defined in earlier ones (`LogMetaBuilder` defined in Task 4's header → used in Task 9).
- [ ] Each commit is independently useful and leaves the tree in a buildable state. (Tasks 5+6 briefly have failing tests — that's the TDD cycle; Tasks 1–4 and 7+ should always build clean and pass tests.)

---

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-04-18-log-management.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks.
2. **Inline Execution** — execute tasks in this session with checkpoints.

Which approach?
