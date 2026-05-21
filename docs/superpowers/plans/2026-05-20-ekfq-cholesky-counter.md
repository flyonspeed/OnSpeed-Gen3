# EKFQ Cholesky-Failure Counter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When `EKFQ::correct()` aborts on a non-PSD Cholesky diagonal, increment a session-persistent counter and record the call number at which it happened. Surface via the existing `TASKS` console command when EKFQ is the active algorithm.

**Architecture:** Two `uint32_t` members on `EKFQ` (`updateCallCount_`, `failedUpdateCount_`, `lastFailedCallNum_`) — incremented at the Cholesky guard site already at `EKFQ.cpp:601`. Thin pass-through accessors on the sketch-side `AHRS` wrapper because `core_` is private. `ConsoleSerial.cpp`'s `TASKS` command appends one line when `g_AHRS.IsEkfqActive()`.

**Tech Stack:** C++17 (`-std=gnu++17` per PIO), Unity for native unit tests, PlatformIO (`pio test -e native`, `pio run -e esp32s3-v4p`), `onspeed_core` platform-purity rule (no `Arduino.h` / `millis()` allowed in core).

**Spec:** `docs/superpowers/specs/2026-05-20-ekfq-failed-update-counter-design.md`

**Worktree:** `/Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter`, branch `chore/ekfq-cholesky-counter`, based off `origin/master` HEAD `5f1918ec`.

---

## File Structure

**Modified files** (all in the worktree above):

| File | Change |
|---|---|
| `software/Libraries/onspeed_core/src/ahrs/EKFQ.h` | +3 private uint32_t members, +3 public accessors. |
| `software/Libraries/onspeed_core/src/ahrs/EKFQ.cpp` | `update()` bumps `updateCallCount_`; Cholesky guard at line ~601 bumps `failedUpdateCount_` + stamps `lastFailedCallNum_` before `return`. |
| `software/sketch_common/src/tasks/AHRS.h` | +3 public pass-through methods. |
| `software/sketch_common/src/io/ConsoleSerial.cpp` | `TASKS` command appends one line iff EKFQ is active. |
| `test/test_ekfq/test_ekfq.cpp` | +5 unit tests (zero state, monotonic count, degenerate-S trigger, persist-across-init, batch-form single-increment). |

**Branch:** `chore/ekfq-cholesky-counter`

---

## Task 0: Baseline verification

**Files:** none (verification only).

- [ ] **Step 1: Confirm working directory and branch**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git branch --show-current
git log --oneline -1
```

Expected:
```
chore/ekfq-cholesky-counter
5f1918ec fix(perf): replace TLS slot 1 with TaskHandle→Ring registry (closes #611) (#615)
```

- [ ] **Step 2: Run native unit tests to confirm baseline**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio test -e native 2>&1 | tail -20
```

Expected: all tests pass (the master HEAD is post-#615 and stable). Count of passing tests recorded — exact number isn't load-bearing; just used to spot regressions later.

- [ ] **Step 3: Verify the Cholesky guard site**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
sed -n '595,610p' software/Libraries/onspeed_core/src/ahrs/EKFQ.cpp
```

Expected output contains:
```cpp
        if (sum <= 0.0f) {
            // S lost positive-definiteness due to fp32 round-off. Abort
            // the update — same failure mode as the Python try/except
            // around np.linalg.solve (LinAlgError on a singular S).
            return;
        }
```

If absent or moved: stop and re-check; the plan's line-number references need updating.

---

## Task 1: Add counter state and accessors to EKFQ

**Files:**
- Modify: `software/Libraries/onspeed_core/src/ahrs/EKFQ.h` (private member section + public accessors)

- [ ] **Step 1: Add member fields**

Find the private member block in `EKFQ.h` (around line 247 — `float x_[N_STATES];`). Immediately after `bool   initialized_;` (around line 250), insert:

```cpp
    // Cholesky-failure observability. See issue #593 item #1.
    //
    // updateCallCount_  — monotonic count of update() invocations since
    //                     construction. Never reset (not even by init()).
    // failedUpdateCount_ — number of correct() calls aborted at the
    //                     Cholesky guard (sum <= 0.0f).
    // lastFailedCallNum_ — value of updateCallCount_ at the most recent
    //                     failure; 0 if none.
    //
    // Counters intentionally survive init() — a failure burst right
    // before a reseed is still diagnostic.
    uint32_t updateCallCount_   = 0;
    uint32_t failedUpdateCount_ = 0;
    uint32_t lastFailedCallNum_ = 0;
```

Use the Edit tool with `old_string` exactly matching the line `bool   initialized_;` and `new_string` containing that line plus a blank line plus the block above. Verify `bool   initialized_;` is unique in the file before using Edit; if not, include surrounding context.

- [ ] **Step 2: Add public accessors**

Find the existing public accessors near line 222 (`const Config& getConfig() const { return config_; }`). Immediately after that line and before the existing `setConfig()` declaration, insert:

```cpp

    /// Lifetime count of update() invocations. Monotonic across init().
    uint32_t getUpdateCallCount() const { return updateCallCount_; }

    /// Number of correct() calls aborted at the Cholesky guard.
    /// Monotonic across init() so a pre-reseed burst stays visible.
    uint32_t getFailedUpdateCount() const { return failedUpdateCount_; }

    /// Value of getUpdateCallCount() at the most recent failure; 0 if none.
    uint32_t getLastFailedCallNum() const { return lastFailedCallNum_; }
```

- [ ] **Step 3: Compile-check (header-only change)**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio test -e native --without-testing 2>&1 | tail -10
```

Expected: clean build. No tests run yet; this is a compilability check only. If errors appear, fix them before continuing (likely culprits: missing `<cstdint>` include — check existing header at top, it's already there).

- [ ] **Step 4: Commit**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git add software/Libraries/onspeed_core/src/ahrs/EKFQ.h
git commit -m "$(cat <<'EOF'
EKFQ: add Cholesky-failure counter members and accessors

Adds three uint32_t instance members and three public accessors for
post-flight observability of correct()'s non-PSD Cholesky abort path.
Counters survive init() — a failure burst before a reseed is still
diagnostic.

Refs #593 item #1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Wire counters into EKFQ::correct() and EKFQ::update()

**Files:**
- Modify: `software/Libraries/onspeed_core/src/ahrs/EKFQ.cpp` (two sites: `update()` body, Cholesky guard inside `correct()`)

- [ ] **Step 1: Increment updateCallCount_ at the top of update()**

Read the current `update()` body to locate the exact line. It's at `EKFQ.cpp:172` and looks like:

```cpp
void EKFQ::update(const Measurements& m, float dt) {
    if (!initialized_) init();
    predict(m.p, m.q, m.r, m.ax, m.ay, m.az, m.tasMps, dt);
    correct(m.ax, m.ay, m.az, m.tasMps, m.tasDotMps2,
            m.q, m.r, m.baroAltMeters, m.updateBaro);
}
```

Use Edit. Match the line `void EKFQ::update(const Measurements& m, float dt) {` and replace the function body's leading lines to insert the increment after the `init()` check but before `predict()`:

```cpp
void EKFQ::update(const Measurements& m, float dt) {
    if (!initialized_) init();
    ++updateCallCount_;
    predict(m.p, m.q, m.r, m.ax, m.ay, m.az, m.tasMps, dt);
    correct(m.ax, m.ay, m.az, m.tasMps, m.tasDotMps2,
            m.q, m.r, m.baroAltMeters, m.updateBaro);
}
```

- [ ] **Step 2: Increment failedUpdateCount_ at the Cholesky guard**

Locate the guard in `correct()` (currently `EKFQ.cpp:601`). The exact existing code:

```cpp
        if (sum <= 0.0f) {
            // S lost positive-definiteness due to fp32 round-off. Abort
            // the update — same failure mode as the Python try/except
            // around np.linalg.solve (LinAlgError on a singular S).
            return;
        }
```

Replace with:

```cpp
        if (sum <= 0.0f) {
            // S lost positive-definiteness due to fp32 round-off. Abort
            // the update — same failure mode as the Python try/except
            // around np.linalg.solve (LinAlgError on a singular S).
            // Bump observability counters before returning so post-flight
            // review (TASKS console command) can spot recurrence. See
            // issue #593 item #1.
            ++failedUpdateCount_;
            lastFailedCallNum_ = updateCallCount_;
            return;
        }
```

- [ ] **Step 3: Compile-check**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio test -e native --without-testing 2>&1 | tail -10
```

Expected: zero warnings, clean build.

- [ ] **Step 4: Run existing EKFQ tests to confirm no behavior change**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio test -e native -f test_ekfq 2>&1 | tail -15
```

Expected: all 7 existing `test_ekfq_*` tests still pass. The counter additions are pure observability; algorithmic behavior is unchanged.

- [ ] **Step 5: Run regression-golden to confirm bit-identical output**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
./tools/regression/run_snapshot.py 2>&1 | tail -10
```

Expected: all goldens match. The counter logic only fires on the `sum <= 0.0f` path that the synthetic golden inputs don't exercise; if a golden diff appears, stop and investigate before continuing.

- [ ] **Step 6: Commit**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git add software/Libraries/onspeed_core/src/ahrs/EKFQ.cpp
git commit -m "$(cat <<'EOF'
EKFQ: increment counters at update() and Cholesky guard

update() bumps updateCallCount_ once per call. The Cholesky guard
inside correct() bumps failedUpdateCount_ and stamps
lastFailedCallNum_ before the early return. Pure observability; no
algorithmic change. Regression goldens unchanged.

Refs #593 item #1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Write unit tests for the counter

**Files:**
- Modify: `test/test_ekfq/test_ekfq.cpp` (append 5 new test functions + register them in `main()`)

- [ ] **Step 1: Write the test functions**

Read the existing `test_ekfq.cpp` and find the location of `int main(int, char**) {` (currently line 153). Insert the following five test functions immediately above that `main()`:

```cpp
// ============================================================
// Cholesky-failure counter tests (issue #593 item #1)
// ============================================================

void test_ekfq_counter_starts_at_zero(void) {
    EKFQ ekfq;
    ekfq.init();
    TEST_ASSERT_EQUAL_UINT32(0u, ekfq.getUpdateCallCount());
    TEST_ASSERT_EQUAL_UINT32(0u, ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(0u, ekfq.getLastFailedCallNum());
}

void test_ekfq_counter_unchanged_on_normal_update(void) {
    EKFQ ekfq;
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.ax = 0.0f;
    meas.ay = 0.0f;
    meas.az = -G;
    meas.p  = 0.0f;
    meas.q  = 0.0f;
    meas.r  = 0.0f;
    meas.tasMps       = 0.0f;
    meas.tasDotMps2   = 0.0f;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    for (int i = 0; i < 10; ++i) {
        ekfq.update(meas, DT);
    }
    TEST_ASSERT_EQUAL_UINT32(10u, ekfq.getUpdateCallCount());
    TEST_ASSERT_EQUAL_UINT32(0u,  ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(0u,  ekfq.getLastFailedCallNum());
}

void test_ekfq_counter_bumps_on_degenerate_S(void) {
    // Poison r_baro to drive the Cholesky diagonal negative on the
    // baro row. With a strongly negative R diagonal entry, the
    // corresponding row of S = H·P·H^T + R sums into a negative
    // diagonal entry within the j-loop, triggering sum<=0.0f.
    EKFQ::Config cfg = EKFQ::Config::defaults();
    cfg.r_baro = -1.0e9f;
    EKFQ ekfq(cfg);
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.ax = 0.0f;
    meas.ay = 0.0f;
    meas.az = -G;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    ekfq.update(meas, DT);
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getUpdateCallCount());
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getLastFailedCallNum());
}

void test_ekfq_counter_persists_across_init(void) {
    // Bump the counter via a degenerate-S update, then reseed via
    // init(). Counters must survive — a failure burst right before a
    // reseed is the kind of pattern we want post-flight reviewers
    // to spot.
    EKFQ::Config cfg = EKFQ::Config::defaults();
    cfg.r_baro = -1.0e9f;
    EKFQ ekfq(cfg);
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.az = -G;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    ekfq.update(meas, DT);
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getFailedUpdateCount());
    const uint32_t failBefore = ekfq.getFailedUpdateCount();
    const uint32_t lastBefore = ekfq.getLastFailedCallNum();
    const uint32_t callBefore = ekfq.getUpdateCallCount();
    ekfq.init(0.0f, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT32(failBefore, ekfq.getFailedUpdateCount());
    TEST_ASSERT_EQUAL_UINT32(lastBefore, ekfq.getLastFailedCallNum());
    TEST_ASSERT_EQUAL_UINT32(callBefore, ekfq.getUpdateCallCount());
}

void test_ekfq_counter_increments_by_one_on_batch_failure(void) {
    // Master's EKFQ::correct() is a pure batch update — one Cholesky
    // guard for all 8 measurements. Even with every R_diag entry
    // poisoned, a single failed update() must bump
    // failedUpdateCount_ by exactly 1, not 8. This test documents the
    // batch semantics so a future port back to scalar updates won't
    // silently change counter semantics without also changing this
    // test.
    EKFQ::Config cfg = EKFQ::Config::defaults();
    cfg.r_ax   = -1.0e9f;
    cfg.r_ay   = -1.0e9f;
    cfg.r_az   = -1.0e9f;
    cfg.r_baro = -1.0e9f;
    EKFQ ekfq(cfg);
    ekfq.init();
    EKFQ::Measurements meas{};
    meas.az = -G;
    meas.baroAltMeters = 0.0f;
    meas.updateBaro    = true;
    ekfq.update(meas, DT);
    TEST_ASSERT_EQUAL_UINT32(1u, ekfq.getFailedUpdateCount());
}
```

- [ ] **Step 2: Register the new tests in main()**

Find the existing `main()` block (currently at line 153). Add `RUN_TEST(...)` lines after `RUN_TEST(test_ekfq_pipeline_config_override);`:

```cpp
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ekfq_init_default);
    RUN_TEST(test_ekfq_init_with_attitude);
    RUN_TEST(test_ekfq_level_flight);
    RUN_TEST(test_ekfq_pitched_static);
    RUN_TEST(test_ekfq_quaternion_stays_unit);
    RUN_TEST(test_ekfq_defaults_finite);
    RUN_TEST(test_ekfq_pipeline_config_override);
    RUN_TEST(test_ekfq_counter_starts_at_zero);
    RUN_TEST(test_ekfq_counter_unchanged_on_normal_update);
    RUN_TEST(test_ekfq_counter_bumps_on_degenerate_S);
    RUN_TEST(test_ekfq_counter_persists_across_init);
    RUN_TEST(test_ekfq_counter_increments_by_one_on_batch_failure);
    return UNITY_END();
}
```

- [ ] **Step 3: Run tests to verify all pass**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio test -e native -f test_ekfq -v 2>&1 | tail -30
```

Expected: 12 tests pass (7 existing + 5 new). Specifically look for:
```
test_ekfq_counter_starts_at_zero:PASS
test_ekfq_counter_unchanged_on_normal_update:PASS
test_ekfq_counter_bumps_on_degenerate_S:PASS
test_ekfq_counter_persists_across_init:PASS
test_ekfq_counter_increments_by_one_on_batch_failure:PASS
```

**If `test_ekfq_counter_bumps_on_degenerate_S` fails:** The negative-R injection might not deterministically force `sum <= 0.0f` depending on how P seeds the first frame. Investigate by reading the test's expected path through `correct()`'s lines 595-610 — if the j-loop for the baro row arrives at `sum = S[j][j] - Σ S[j][k]² ` and the initial `S[j][j] = P[Z][Z] + r_baro = config_.p_z + (-1e9)` is already <0 on iteration j where baro lives, the guard fires. With `config_.p_z = 100.0f` and `r_baro = -1e9`, S[baro][baro] = -1e9+100 ≈ -1e9 — clearly negative. The guard should fire on or before the baro-row iteration. If still failing, increase `|r_baro|` to `-1.0e18f` or also poison `cfg.p_z = -1.0e9f`.

- [ ] **Step 4: Commit**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git add test/test_ekfq/test_ekfq.cpp
git commit -m "$(cat <<'EOF'
test(ekfq): unit tests for Cholesky-failure counter

Five tests covering: zero-init, monotonic call-count on normal updates,
counter bump on a synthetically degenerate measurement, counter
persistence across init(), and the batch-form single-increment
semantic (one failure per update(), not one per measurement).

Refs #593 item #1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Add pass-through accessors to the sketch-side AHRS wrapper

**Files:**
- Modify: `software/sketch_common/src/tasks/AHRS.h` (3 new public methods; no .cpp change because they're one-liners)

- [ ] **Step 1: Add methods on AHRS wrapper**

Find the `public:` section of class `AHRS` in `AHRS.h`. Look for the line `float   PitchWithBias();` (around line 96). Above that line, after the existing `void    Process(float deltaTimeSeconds);` (around line 94), insert:

```cpp

    // === EKFQ Cholesky-failure observability (issue #593 item #1) ===
    //
    // Pass-through to core_.GetEkfqPipeline().getEkfq().getX(). Returns
    // the underlying EKFQ instance's counters regardless of which
    // algorithm is currently active — callers (ConsoleSerial TASKS)
    // should gate display on IsEkfqActive() to avoid reporting
    // failures for an algorithm not in use.
    uint32_t EkfqUpdateCallCount()   const;
    uint32_t EkfqFailedUpdateCount() const;
    uint32_t EkfqLastFailedCallNum() const;

    /// True if the active algorithm is EKFQ.
    bool IsEkfqActive() const;
```

- [ ] **Step 2: Implement them in AHRS.cpp**

Find AHRS.cpp's existing method implementations (the file is roughly structured with `Init`/`Process`/etc. methods). Append the four implementations at the bottom of the file, just before the final closing brace (if any) or at end-of-file:

```cpp

// ---- EKFQ observability pass-throughs (issue #593 item #1) ----

uint32_t AHRS::EkfqUpdateCallCount() const
{
    return core_.GetEkfqPipeline().getEkfq().getUpdateCallCount();
}

uint32_t AHRS::EkfqFailedUpdateCount() const
{
    return core_.GetEkfqPipeline().getEkfq().getFailedUpdateCount();
}

uint32_t AHRS::EkfqLastFailedCallNum() const
{
    return core_.GetEkfqPipeline().getEkfq().getLastFailedCallNum();
}

bool AHRS::IsEkfqActive() const
{
    return core_.algorithm() == onspeed::ahrs::Algorithm::Ekfq;
}
```

Before adding, verify the existing includes in AHRS.cpp pull in `<ahrs/Ahrs.h>` and `<ahrs/EkfqPipeline.h>` transitively. Run:

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
grep -n "include" software/sketch_common/src/tasks/AHRS.cpp | head -10
```

If neither header is included directly, add `#include <ahrs/EkfqPipeline.h>` near the existing AHRS.cpp includes. The `Algorithm` enum is defined in `<ahrs/Ahrs.h>`, which AHRS.h already includes — so `Algorithm::Ekfq` should resolve.

- [ ] **Step 3: Compile-check the firmware build**

This is the first firmware-side change in the plan, so we need the actual ESP32 build now, not just native:

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio run -e esp32s3-v4p 2>&1 | tail -15
```

Expected: zero warnings, successful build. If the build fails with `framework-arduinoespressif32` Python TypeError (per the project memory note), the fix is `rm -rf ~/.platformio/packages/framework-arduinoespressif32` then retry.

- [ ] **Step 4: Commit**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git add software/sketch_common/src/tasks/AHRS.h software/sketch_common/src/tasks/AHRS.cpp
git commit -m "$(cat <<'EOF'
AHRS wrapper: EKFQ Cholesky-counter pass-through

Three accessors exposing EKFQ's getUpdateCallCount /
getFailedUpdateCount / getLastFailedCallNum through the sketch-side
AHRS wrapper, plus IsEkfqActive() so ConsoleSerial can decide
whether to print the diagnostic line.

Refs #593 item #1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Extend the TASKS console command

**Files:**
- Modify: `software/sketch_common/src/io/ConsoleSerial.cpp` (one line added to the TASKS section at line ~568)

- [ ] **Step 1: Append EKFQ diagnostic line to TASKS**

Find the TASKS section (line ~557):

```cpp
            // TASKS
            // -----
            else if (strncasecmp(szCmdToken, "TASKS", 5) == 0)
                {
                PrintTaskInfo(xTaskReadSensors);
                PrintTaskInfo(xTaskAudioPlay);
                PrintTaskInfo(xTaskWriteLog);
                PrintTaskInfo(xTaskCheckSwitch);
                PrintTaskInfo(xTaskDisplaySerial);
                PrintTaskInfo(xTaskHousekeeping);
                PrintTaskInfo(xTaskLogReplay);
                PrintTaskInfo(xTaskTestPot);
                PrintTaskInfo(xTaskRangeSweep);
                } // end TASKS
```

Replace with:

```cpp
            // TASKS
            // -----
            else if (strncasecmp(szCmdToken, "TASKS", 5) == 0)
                {
                PrintTaskInfo(xTaskReadSensors);
                PrintTaskInfo(xTaskAudioPlay);
                PrintTaskInfo(xTaskWriteLog);
                PrintTaskInfo(xTaskCheckSwitch);
                PrintTaskInfo(xTaskDisplaySerial);
                PrintTaskInfo(xTaskHousekeeping);
                PrintTaskInfo(xTaskLogReplay);
                PrintTaskInfo(xTaskTestPot);
                PrintTaskInfo(xTaskRangeSweep);
                if (g_AHRS.IsEkfqActive())
                    {
                    g_Log.printf("%-16s  %u failed updates (last @ call #%u, %u total)\n",
                                 "EKFQ",
                                 g_AHRS.EkfqFailedUpdateCount(),
                                 g_AHRS.EkfqLastFailedCallNum(),
                                 g_AHRS.EkfqUpdateCallCount());
                    }
                } // end TASKS
```

Format string chosen to match the existing `"%-16s  %d min stack\n"` width convention in `PrintTaskInfo`.

- [ ] **Step 2: Verify the includes don't need updating**

`ConsoleSerial.cpp` already references `g_AHRS` indirectly through `g_Config` access patterns? Let me confirm. Run:

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
grep -n "g_AHRS\|include.*AHRS" software/sketch_common/src/io/ConsoleSerial.cpp | head -10
```

If `g_AHRS` is not already used in this file: add `#include "src/tasks/AHRS.h"` near the existing includes at the top of the file. The `g_AHRS` global itself is declared in `Globals.h`, which is presumably already pulled in by the existing includes in this file — verify by reading the top 30 lines of ConsoleSerial.cpp.

- [ ] **Step 3: Compile-check the firmware**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio run -e esp32s3-v4p 2>&1 | tail -15
```

Expected: zero warnings, clean build. The `-Werror -Wformat=2` policy on `build_src_flags` will catch format-string mismatches; if it fires, the most likely culprits are `%u` vs `%lu` mismatches on the `uint32_t` (it's platform-dependent on ESP32; if so, use `%lu` and cast to `unsigned long`).

- [ ] **Step 4: Final native test pass**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
pio test -e native 2>&1 | tail -10
```

Expected: same passing test count as baseline + 5 new tests passing.

- [ ] **Step 5: Final regression-golden pass**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
./tools/regression/run_snapshot.py 2>&1 | tail -10
```

Expected: all goldens match.

- [ ] **Step 6: Core purity check**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
./scripts/check_core_purity.sh 2>&1 | tail -5
```

Expected: pass. No platform API calls (millis/Arduino.h/FreeRTOS) leaked into onspeed_core.

- [ ] **Step 7: Commit**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git add software/sketch_common/src/io/ConsoleSerial.cpp
git commit -m "$(cat <<'EOF'
ConsoleSerial: print EKFQ Cholesky-failure stats in TASKS

When the active algorithm is EKFQ, the TASKS command appends one
line summarising the Cholesky-failure counter, last-failed call
number, and total update() call count. Omitted when Madgwick is
active so the output isn't misleading about an algorithm that's
not running.

Refs #593 item #1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Open the pull request

**Files:** none (PR creation).

- [ ] **Step 1: Push the branch**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git push -u origin chore/ekfq-cholesky-counter 2>&1 | tail -10
```

- [ ] **Step 2: Verify the commits look right**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
git log --oneline origin/master..HEAD
```

Expected: 5 commits, one per task (Tasks 1, 2, 3, 4, 5).

- [ ] **Step 3: Create the PR**

```bash
cd /Users/sritchie/code/onspeed/onspeed-worktrees/ekfq-cholesky-counter
gh pr create --head chore/ekfq-cholesky-counter --title "EKFQ: Cholesky-failure observability counter (closes #593 partially)" --body "$(cat <<'EOF'
## EKFQ: Cholesky-failure observability counter

Surfaces silent measurement-update aborts in `EKFQ::correct()` so pilots can spot recurrence post-flight. Strict subset of [#593](https://github.com/flyonspeed/OnSpeed-Gen3/issues/593) item #1; items #2 (stack hoist) and #4 (audio-sweep τ) deferred.

### Changes

- `EKFQ` gains three private `uint32_t` members and three public accessors: `getUpdateCallCount()`, `getFailedUpdateCount()`, `getLastFailedCallNum()`. Counters survive `init()` — a failure burst before a reseed is still diagnostic.
- `EKFQ::update()` bumps `updateCallCount_` once per call.
- The single Cholesky guard inside `EKFQ::correct()` (at `sum <= 0.0f`) bumps `failedUpdateCount_` and stamps `lastFailedCallNum_` before its `return`.
- The sketch-side `AHRS` wrapper exposes three pass-through accessors and `IsEkfqActive()`.
- The `TASKS` console command appends one line when EKFQ is active:
  ```
  EKFQ              0 failed updates (last @ call #0, 12459 total)
  ```
- Five new unit tests in `test_ekfq.cpp` covering zero state, monotonic call count, the degenerate-S trigger via negative `r_baro`, persistence across `init()`, and the batch-form single-increment semantic.

### Relationship to PERF (#605/#612/#615)

This counter is intentionally **separate** from the PERF subsystem. PERF measures wall-clock duration of `EkfqCorrect` scopes and is compile-gated (`-DONSPEED_PERF_ENABLED`, only built in the `esp32s3-v4p-perf` env). Production firmware carries zero PERF instrumentation by design.

PERF cannot answer this counter's question: "of the 208 `EkfqCorrect` calls this second, did the Cholesky guard fire?" A scope emits one event whether `correct()` did real work or hit `sum<=0` and bailed. The new counter is always-on in production firmware and per-session persistent.

### Out of scope

- **#2 (stack hoist):** filed separately as #617. The 2.4 KB transient matrices `correct()` allocates aren't on fire today (PERF watches stack high-water), but the hoist is a free win.
- **#3 (log provenance sidecar):** the `.meta` sidecar already exists; adding `ahrs_algorithm` and EKFQ tuning fields will land in a coordinated log-with-config provenance PR rather than here.
- **#4 (audio-sweep τ):** master's `kCompFadeTauSec = 0.5f` is shared between Madgwick and EKFQ in `Ahrs.cpp`, so sweep.py's hardcoded `0.5` is currently correct. Future-proofing deferred unless/until #592 splits τ per algorithm.

### Testing

```bash
pio test -e native -f test_ekfq          # 12 tests pass
pio run -e esp32s3-v4p                   # zero warnings
./tools/regression/run_snapshot.py       # goldens bit-identical
./scripts/check_core_purity.sh           # core stays platform-pure
```

Bench check (post-merge): boot with EKFQ active, `TASKS` at the console — should see `EKFQ              0 failed updates (last @ call #0, ... total)`. With Madgwick active, the line is absent.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)" 2>&1 | tail -10
```

Expected: PR URL printed. Capture it and return it.

---

## Self-Review

Walking the plan against the spec at `docs/superpowers/specs/2026-05-20-ekfq-failed-update-counter-design.md`:

| Spec requirement | Task |
|---|---|
| Three private uint32_t members on EKFQ | Task 1 |
| Three public accessors on EKFQ | Task 1 |
| `update()` increments `updateCallCount_` | Task 2 |
| Cholesky guard bumps `failedUpdateCount_` + `lastFailedCallNum_` | Task 2 |
| No new `Ahrs` accessor (use existing `GetEkfqPipeline().getEkfq()` path) | Task 4 (via sketch wrapper) |
| `TASKS` line when EKFQ active, omitted when Madgwick | Task 5 |
| 5 unit tests | Task 3 |
| Regression golden bit-identical | Tasks 2, 5 verification steps |
| `pio run -e esp32s3-v4p` zero warnings | Tasks 4, 5 verification |
| `check_core_purity.sh` passes | Task 5 |
| PERF distinction in PR body | Task 6 PR body |

All spec requirements mapped to tasks. No placeholders. Type names consistent across tasks (`getUpdateCallCount`/`getFailedUpdateCount`/`getLastFailedCallNum` match throughout). The wrapper methods (`EkfqUpdateCallCount`/`EkfqFailedUpdateCount`/`EkfqLastFailedCallNum`/`IsEkfqActive`) match between Task 4 declaration and Task 5 usage. Format string in Task 5 uses `%u` consistently; fallback to `%lu` documented if `-Wformat=2` complains.
