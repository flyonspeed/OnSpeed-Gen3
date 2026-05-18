# PLAN — EKF Tuning: SD Log Input Fidelity

**Date:** 2026-05-14 (rewritten 2026-05-18)
**Owner:** Sam
**Status:** Plan — two firmware columns needed before Vac flies
**Trigger:** Vac's next flight tests aim to collect data for EKF6 parameter tuning. Audit found the SD log was missing a handful of inputs needed for byte-faithful offline EKF re-execution.

## What changed between the 2026-05-14 draft and now

The original audit listed six blockers (G1–G6). Three are no longer blockers:

- **G1 (per-row dt)** is closed. PR #551 (commit `fbeb9888`, merged 2026-05-15) added a `timeStampUs` column to the CSV — a uint64 from `esp_timer_get_time()`, captured back-to-back with `timeStamp` in `LogSensor::Process`. Per-row dt is recoverable as `row[n].timeStampUs - row[n-1].timeStampUs`, byte-faithful. The uint64-absolute choice is *better* than the original plan's uint16-delta proposal: no rollover at flight timescales (`esp_timer_get_time()` doesn't wrap, unlike `micros()`), and the column doesn't need an anchor row to reconstruct absolute time across log rotations.

- **G4 (cfg snapshot embedded in log)** is dropped. The replay tool (`tools/web/lib/pages/ReplayPage.js:703`) and the Python analysis path both require the user to upload a `.cfg` separately. The analyst already has the cfg-at-time-of-flight from their own filesystem / Dropbox history; the "Vac saves cfg mid-flight via the AP and silently overwrites it" failure mode is one we accept rather than guard against in firmware. If we want to be paranoid about it later, we add it then — it's not blocking tuning.

- **G6 (`madgwickGate` columns)** is dropped. The "uncommitted Madgwick centripetal-gate work on `feature/bundler-esbuild`" referenced in the original draft was abandoned; that branch was repurposed for esbuild work (PR #547 / commit `ca7112d4`) and the gate getters never made it to git. The INDEX doc records the decision: "Bench-tested... Decision deferred pending more work." If the gate ever does ship, wiring its state to the log is a one-line follow-up.

Two firmware blockers remain:

- **G2** (`iasUpdateTimestampUs` not logged) — TAS-derivative EMA depends on it
- **G3** (`compFadeIn_` state not logged) — cold-start vs warm-start cannot be reconciled

Plus four should-fix items (S1–S3 from the original draft, plus a log-rate marker) that materially improve tuning quality at near-zero cost.

## 1. Background

Vac's RV-4 has the VN-300 EFIS, which provides independent ground-truth attitude (`vnPitch`, `vnRoll`, `vnYaw`) at 0.01° precision plus VN's own 1-σ uncertainty bounds (`vnYawSigma` etc.). This is exactly what we need to tune EKF6: minimize the residual between OnSpeed EKF6 attitude and VN-300 attitude across a representative flight envelope.

**Tuning loop, ideal:**

1. Vac flies a tuning-card flight: straight-and-level, banked turns, climbs, descents, slips, stalls.
2. We pull the SD log offline. We already have the cfg from Dropbox.
3. We re-run `Ahrs::Step` with the logged inputs and varied EKF6 Q/R/P0 parameters, in a host-side loop.
4. For each (Q, R) candidate: compute RMS residual against vnPitch/vnRoll.
5. Pick the (Q, R) that minimizes the residual.

**Step 3 requires byte-faithful replay** — the offline `Ahrs::Step` must consume the exact same `Measurements` struct each tick that the firmware did. Anywhere the offline replay has to *re-derive* an input from logged columns instead of *reading it directly*, we add reconstruction error. With G1 closed by PR #551, two reconstruction-only paths remain (G2 and G3); both are genuinely lossy without firmware help.

## 2. What's in the SD log today

The full EKF input set:

| EKF input | Source at flight time | In SD log? | Fidelity |
|---|---|---|---|
| `ax/ay/az` (post-comp accel, g) | EMA-filtered raw + centripetal subtraction + compFadeIn ramp | ⚠️ raw accels logged, post-comp values NOT | reconstructible IF G2+G3 closed |
| `p/q/r` (gyro rates, rad/s) | raw IMU after installation bias rotation | ✅ raw rates logged at `%.6f` | full |
| `gamma` (flight path, rad) | asin(kalmanVSI · compFadeIn / tas) | ⚠️ logged at `%.2f` precision | reconstructible IF G3 closed |
| `dtSec` | `row[n].timeStampUs - row[n-1].timeStampUs` (offline) | ✅ via `timeStampUs` (PR #551) | full |

The intermediate inputs that produce those:

| Pipeline input | In log? | Notes |
|---|---|---|
| Raw IMU (3 accel, 3 gyro) | ✅ `ForwardG/LateralG/VerticalG/RollRate/PitchRate/YawRate` (`%.6f`) | PitchRate is sign-flipped at write — re-flip on read |
| IAS, Palt, OAT | ✅ `IAS, Palt, OAT` (`%.2f`) | adequate |
| `tas` (m/s) | ✅ as `TAS` in kt | reproducible offline |
| `timeStampUs` (uint64 µs from boot) | ✅ adjacent to `timeStamp` (PR #551) | full — closes G1 |
| `iasUpdateTimestampUs` | ❌ NOT logged | **G2 blocker** |
| `iasAlive` | ⚠️ implicit (IAS column empty when false) | reconstructible |
| `compFadeIn_` | ❌ NOT logged | **G3 blocker** |
| `tasDotSmoothed_` | ❌ NOT logged | S2 — re-derivable IF G2 closed |

VN-300 ground-truth is in good shape: all attitude / velocity / accel / GNSS / sigma columns are present and decoded at sensible precision (see Appendix B).

## 3. The two remaining blockers

### G2 — `iasUpdateTimestampUs` is not logged 🚀

`updateTas_` (`Ahrs.cpp:207-222`) computes a variable-rate EMA on TAS derivative. Its α depends on the actual elapsed time between IAS pressure-sample arrivals, not the IMU sample rate. Pressure samples arrive at ~50 Hz with jitter.

Without this timestamp, the offline replay must guess when each IAS update arrived — and the rate-adjusted EMA's state diverges over time.

The source variable is `g_Sensors.uIasUpdateUs`, assigned at `SensorIO.cpp:445` from `micros()`. Note: `micros()` wraps every ~71 min, so for storage we want a delta-from-row form, not the raw value.

**Fix**: add a column `iasUpdateOffsetUs` (uint16, ~6 bytes/row in CSV). Value is `(int32_t)(g_Sensors.uIasUpdateUs - (uint32_t)(rowTimeStampUs & 0xFFFFFFFF))` clamped to the column width — effectively "how many µs ago, relative to this row's `timeStampUs`, did the last IAS update arrive." At 50 Hz IAS cadence the offset is bounded well under uint16 range; if cadence ever stretches past 65 ms (won't at flight time) we'd need to widen.

### G3 — `compFadeIn_` state is not logged 🚀

A τ=0.5 s EMA that ramps 0→1 each time `iasAlive` rises (`Ahrs.cpp:309-314`); reset to 0 on `iasAlive` fall (`Ahrs.cpp:344`).

**The killer detail**: log files don't start at boot. A log file rotates or restarts mid-flight, and the box may already be airborne with `compFadeIn_=1`. From the log alone, the offline replay cannot tell what the value was at row 0. Re-running from `compFadeIn_=0` (cold-start assumption) injects a half-second of bad EKF behavior at the start of every offline replay.

The getter `Ahrs::compFadeIn()` already exists at `Ahrs.h:121`, so this is purely a LogRow → CSV wiring change.

**Fix**: log `compFadeIn` per row. Either:
- uint8 with ·100 quantization (4 bytes/row, 0.01 resolution — plenty for a 0..1 ramp), OR
- float at `%.4f` (~6-8 bytes/row, exact).

**Recommend** the uint8 form unless the float cost is negligible.

## 4. Subtle gaps (not blockers, worth fixing)

### S1 — `FlightPath` logged at `%.2f` (0.01°)

EKF6's α measurement input is `gamma = θ − α_meas`. At small flight-path angles the 0.01° LSB is a meaningful fraction. Bumping to `%.4f` is cheap and removes the precision loss.

### S2 — `tasDotSmoothed_` is unobservable

Feeds the forward-accel compensation factor (`Ahrs.cpp:291,316-317`). Already in `AhrsOutputs.tasDotMps2`. Just needs to be wired to a column. Re-derivable from G2, but logging it directly catches reconstruction bugs.

### S3 — Final `accelFwdComp/LatComp/VertComp` not logged

These are the actual EKF inputs. We log raw accels (pre-comp) and EKF outputs (Pitch/Roll), but not the inputs that produced them. Logging the comp triple lets us *verify* the offline reconstruction is correct, not just trust it.

### S4 — Log-rate change marker

If `iLogRate` changes mid-flight (`iLogRate` is read live — saving 50→208 mid-recording produces a single CSV with two cadences, per the memory in MEMORY.md), the offline replay needs to notice. Either bump LogMeta to record "rate changes seen during this log" or write a marker row when the rate transitions. Cheap and forestalls a confusing debugging session.

## 5. Recommended firmware changes (before Vac flies)

### Critical (must-fix-before-flight) 🚀

1. **Add `iasUpdateOffsetUs` (uint16) column** — Fix G2. Wire `g_Sensors.uIasUpdateUs - (uint32_t)rowTimeStampUs` to a new column.
2. **Add `compFadeIn` (uint8·100 or float) column** — Fix G3. Wire `g_AHRS.compFadeIn()` to a new column.

Total firmware changes: 2 new CSV columns. Touched files: `LogRow.h`, `LogCsv.cpp` (header + format + parse), `LogCsvHeaderIndex.{h,cpp}`, `LogSensor.cpp`. **No algorithm changes.** No risk to flight behavior.

### Should-fix (significantly helps tuning) ⚙️

3. **Add `tasDotMps2` column** — Fix S2. Already in `AhrsOutputs`; wire to CSV.
4. **Add `accelFwdComp`, `accelLatComp`, `accelVertComp` columns** — Fix S3. Three new floats at `%.6f`.

### Nice-to-have 💡

5. **Bump `FlightPath` precision to `%.4f` or `%.6f`** — Fix S1. One-line change.
6. **Log-rate change marker** — Fix S4. Either LogMeta field "rateChangesSeen" or a sentinel row.

## 6. Test plan: verify logs are sufficient before Vac flies

Once the critical fixes are in:

1. **Bench fixture replay-equivalence test.** Build a synthetic input stream (raw IMU + IAS + Palt + OAT at 208 Hz, 60 sec). Feed through live `Ahrs::Step` AND through a host-side re-execution path that reads only the new logged columns. Assert byte-identical EKF6 state output every tick.

2. **Real-flight replay-equivalence test.** Take one of Vac's existing logs *after the new columns are in place*. Build a host_main subcommand (`ahrs_replay_from_log`) that reads CSV → reconstructs `AhrsInputs` per row → calls `Ahrs::Step` with row-to-row dt derived from `timeStampUs`. Output its EKF6 state. Compare against the logged `Pitch`/`Roll`/`DerivedAOA` columns. Tolerance: zero ULP for non-quantized fields, ≤1 LSB for `%.2f`-quantized.

3. **VN-300 ground-truth cross-check.** With the same fixture, plot OnSpeed EKF6 `Pitch` vs `vnPitchDeg` over a banked turn. Compute residual RMS. This is the metric the offline tuner optimizes.

4. **Cold-start vs warm-start parity.** Cut the same log in half, start replay from the middle row. Confirm that *with* G3's `compFadeIn` logged, the warm-start replay matches the cold-start replay byte-identically from that row forward. If it doesn't, there's still an unlogged state.

## 7. PR sequence

| # | Title | Effort | Blocks |
|---|---|---|---|
| 1 | Add `iasUpdateOffsetUs` + `compFadeIn` columns | 0.5 day | Vac's flight |
| 2 | Add `tasDotMps2` + `accelFwdComp/LatComp/VertComp` columns + bump FlightPath precision | 0.5 day | tuning quality |
| 3 | Bench fixture + real-flight replay-equivalence native test | 1 day | confidence |
| 4 | `host_main ahrs_replay_from_log` subcommand | 1 day | offline tuning loop |

PR 1 is the must-have. Total ~0.5 day for the critical path; ~3 days for the full set including the bench test that proves byte-faithful replay works.

## 8. Acceptance criteria

### Pre-flight (firmware ready for Vac)

- [ ] `iasUpdateOffsetUs` and `compFadeIn` columns appear in the CSV header at log creation.
- [ ] `compFadeIn` column reads ~1.0 in steady cruise, drops to 0 on IAS dropout, ramps back over ~0.5 s on recovery.
- [ ] All existing native tests still pass; new tests cover round-trip emit/parse of both columns.

### Post-flight (tuning loop works)

- [ ] `host_main ahrs_replay_from_log` consumes Vac's flight log and emits per-row EKF6 state byte-identical to the logged `Pitch`/`Roll`/`DerivedAOA` columns (within float precision).
- [ ] Replaying the same log with varied Q/R parameters produces different residuals against `vnPitch`/`vnRoll` — the tuning loop is functional.
- [ ] Warm-start from a non-zero row of the log matches cold-start byte-identically (G3 fix verified).

## 9. Out of scope

- **G1 — per-row dt.** Solved by PR #551's `timeStampUs` column. Offline replay computes dt as `timeStampUs[n] − timeStampUs[n-1]`.
- **G4 — cfg snapshot embedded in log.** The replay tool and Python tooling require the user to upload a `.cfg` alongside the log. Cfg-at-time-of-flight comes from the analyst's filesystem / Dropbox, not the log file. If "Vac saves cfg mid-flight via the AP and overwrites it" ever bites us, revisit.
- **G5 — plumb EKF6 Q/R/P0 into cfg.** Tuning identifies values offline; cfg plumbing happens after. Separate follow-up PR.
- **G6 — `madgwickGate` columns.** The underlying centripetal-gate firmware was abandoned. If it ships, wiring is a one-line follow-up.
- **VN-300 quaternion decoding.** Currently only Euler is extracted; Euler-derived quaternions lose information near gimbal lock, but Vac won't be flying ±90° pitch in normal envelopes. Defer.

## Appendix A: File citations

All paths relative to repo root on `origin/master` (as of 2026-05-18):

- `software/Libraries/onspeed_core/src/types/LogRow.h:32-45` — `timeStampMs` + `timeStampUs` fields (G1 closed)
- `software/Libraries/onspeed_core/src/ahrs/EKF6.h:278-286` — `Measurements` struct (the EKF input contract)
- `software/Libraries/onspeed_core/src/ahrs/EKF6.h:224-269` — `Config` defaults (Q, R, P0 hardcoded — G5)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:228-551` — `Ahrs::Step` (the orchestrator)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:145-224` — `updateTas_` (the variable-rate EMA dependent on G2)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:290-345` — comp-factor pipeline (G3's `compFadeIn_`)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:353-423` — EKF measurement adapter (the actual `Measurements` build)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.h:121` — `compFadeIn()` getter (G3: already exists, just needs wiring)
- `software/Libraries/onspeed_core/src/proto/LogCsv.cpp:269` — CSV header (where new columns get added)
- `software/Libraries/onspeed_core/src/proto/LogCsv.cpp:320+` — CSV `FormatRow` (where new columns get written)
- `software/Libraries/onspeed_core/src/proto/LogCsvHeaderIndex.{h,cpp}` — header→column-index map (new columns need entries)
- `software/sketch_common/src/drivers/SensorIO.cpp:445` — `uIasUpdateUs = micros()` (G2's source)
- `software/sketch_common/src/tasks/LogSensor.cpp:866-895` — row build with both timestamps captured back-to-back (G2/G3 wire-up sits here)

## Appendix B: VN-300 column coverage (already good — for reference)

Logged columns in the VN-300 row block (`LogCsv.cpp` VN-300 section, all at `%.2f`):

- `vnAngularRateRoll/Pitch/Yaw` (rad/s) — ground-truth gyro
- `vnAccelFwd/Lat/Vert` — total body-frame accel (g+linear)
- `vnLinAccFwd/Lat/Vert` — gravity-subtracted linear accel
- `vnYaw/Pitch/Roll` (deg) — INS attitude (the tuning target)
- `vnYawSigma/PitchSigma/RollSigma` — VN's 1-σ uncertainty (excellent for weighting tuning residuals)
- `vnVelNedNorth/East/Down` — inertial-fused velocity
- `vnGnssVelNedNorth/East/Down`, `vnGnssLat/Lon` — raw GNSS observables
- `vnWindSpd/Dir/Vertical` — onboard-derived wind triangle (PR #568, May 2026)
- `vnEstAltFt`, `vnGpsFix`, `vnDataAgeMs`, `vnTimeUtc`

Cadence: VN frames at ~50 Hz; logged at log-row cadence (50 or 208 Hz). When log rate > VN rate, the same VN values repeat across rows. `vnDataAgeMs` discloses staleness — use it to deduplicate offline.

## Appendix C: Why this matters in numbers

To set expectations on residual quality:

- Madgwick pitch bias in steep turns: **+3 to +4°** (measured on Vac's log_007, three steady 60° turns).
- VN-300 pitch 1-σ uncertainty (`vnPitchSigma`): typically **<0.1°** for VN-300 in normal flight.
- Target EKF6 pitch residual after tuning: **<1° RMS** in steady turns, **<2°** in dynamic turns.

With G1 already closed (timeStampUs is byte-faithful), the remaining reconstruction-error sources are G2 (variable-rate TAS-EMA drift) and G3 (cold-start vs warm-start ambiguity at the start of each log file). Closing both brings the reconstruction floor well below 0.1° RMS — well under the residual we're optimizing against.
