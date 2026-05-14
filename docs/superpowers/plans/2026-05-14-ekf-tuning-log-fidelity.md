# PLAN — EKF Tuning: SD Log Input Fidelity

**Date:** 2026-05-14
**Owner:** Sam
**Status:** Plan — audit complete, **firmware changes required before Vac flies**
**Trigger:** Vac's next flight tests aim to collect data for EKF6 parameter tuning. Audit found the current SD log is insufficient for byte-faithful offline EKF re-execution.
**Predecessor:** Madgwick centripetal-gate work (uncommitted on this branch)

## TL;DR

The SD log captures **most** EKF6 inputs at adequate fidelity, but **six concrete gaps** prevent byte-faithful offline replay. Tuning the EKF without byte-faithful replay means you tune against a residual that's partly real-EKF-error and partly logging-reconstruction-error — you can't tell which is which.

**Five of the six gaps are firmware changes** (3 new log columns, 1 cfg sidecar write, 1 wire-up of existing getters). One gap (G5: hardcoded Q/R) is a tooling concern, not a firmware blocker — offline tuning can vary Q/R outside the firmware without ever flashing.

**Total firmware change: ~5 columns added + one cfg-snapshot call on log open. Half a day of work, plus the test plan in section 6.**

## 1. Background

Vac's RV-4 has the VN-300 EFIS, which provides independent ground-truth attitude (`vnPitch`, `vnRoll`, `vnYaw`) at 0.01° precision plus VN's own 1-σ uncertainty bounds (`vnYawSigma` etc.). This is exactly what we need to tune EKF6: minimize the residual between OnSpeed EKF6 attitude and VN-300 attitude across a representative flight envelope.

**Tuning loop, ideal:**

1. Vac flies a tuning-card flight: straight-and-level, banked turns, climbs, descents, slips, stalls.
2. We pull the SD log offline.
3. We re-run `Ahrs::Step` with the logged inputs and varied EKF6 Q/R/P0 parameters, in a host-side loop.
4. For each (Q, R) candidate: compute RMS residual against vnPitch/vnRoll.
5. Pick the (Q, R) that minimizes the residual.

**Step 3 requires byte-faithful replay** — the offline `Ahrs::Step` must consume the exact same `Measurements` struct each tick that the firmware did. Anywhere the offline replay has to *re-derive* an input from logged columns instead of *reading it directly*, we add reconstruction error. Six places today are reconstruction-only, and three of those are genuinely lossy (dt, comp-fade-in, comp-pipeline EMA states).

## 2. What's in the SD log today

The full EKF input set is:

| EKF input | Source at flight time | In SD log? | Fidelity |
|---|---|---|---|
| `ax/ay/az` (post-comp accel, g) | EMA-filtered raw + centripetal subtraction + compFadeIn ramp | ⚠️ raw accels logged, post-comp values NOT | reconstructible IF G2+G3 closed |
| `p/q/r` (gyro rates, rad/s) | raw IMU after installation bias rotation | ✅ raw rates logged at `%.6f` | full |
| `gamma` (flight path, rad) | asin(kalmanVSI · compFadeIn / tas) | ⚠️ logged at `%.2f` precision | reconstructible IF G3 closed |
| `dtSec` | `(micros() - lastImuReadUs) * 1e-6` | ❌ NOT logged — only `millis()` timestamp | **G1 blocker** |

The intermediate inputs that produce those:

| Pipeline input | In log? | Notes |
|---|---|---|
| Raw IMU (3 accel, 3 gyro) | ✅ `ForwardG/LateralG/VerticalG/RollRate/PitchRate/YawRate` (`%.6f`) | PitchRate is sign-flipped at write — re-flip on read |
| IAS, Palt, OAT | ✅ `IAS, Palt, OAT` (`%.2f`) | adequate |
| `tas` (m/s) | ✅ as `TAS` in kt | reproducible offline |
| `tasDotSmoothed_` | ❌ NOT logged | **S2 — re-derivable IF G2 closed** |
| `iasUpdateTimestampUs` | ❌ NOT logged | **G2 blocker** |
| `iasAlive` | ⚠️ implicit (IAS column empty when false) | reconstructible |
| `compFadeIn_` | ❌ NOT logged | **G3 blocker** |
| Installation bias (pitch/roll) | ⚠️ in `onspeed2.cfg`, not embedded in log | **G4 blocker** |
| Algorithm choice (Madgwick vs EKF6) | ❌ NOT in `LogMeta` | **G4 blocker** |
| `madgwickGateActive` (this branch) | ❌ getter exists, not wired to log | **G6 blocker** |

VN-300 ground-truth is in good shape: all attitude / velocity / accel / GNSS / sigma columns are present and decoded at sensible precision.

## 3. The six blockers

### G1 — `dtSec` is not logged per sample 🚀

EKF6's predict step injects process noise as `Q·dt`. The CSV `timeStamp` is `millis()` (1 ms granularity). At 208 Hz, samples are ~4.81 ms apart, but millisecond timestamps quantize them to 4 or 5 ms — **19% per-step rate error**.

`IMU330::Snapshot()` zeroes out the IMU read timestamp before passing it to `Ahrs::Step` (`IMU330.cpp:383: out.timestampUs = 0`). The actual `fDtSeconds` is computed in `SensorIO.cpp:225` and used in `Process()` but never reaches the log row.

**Fix**: write the IMU read `micros()` into a new column. Two choices:
- `imuTimeUs` (uint32) — exact, but adds 10 bytes per row in ASCII representation.
- `dtUs` (uint16) — cheaper, but requires re-computation across log boundary if dt > 65 ms (won't happen at 208 Hz; 65 ms = 15 Hz).

**Recommend** `dtUs` as a uint16. ~6 bytes/row in CSV.

### G2 — `iasUpdateTimestampUs` is not logged 🚀

`updateTas_` (`Ahrs.cpp:207-222`) computes a variable-rate EMA on TAS derivative. Its α depends on the actual elapsed time between IAS pressure-sample arrivals, not the IMU sample rate. Pressure samples arrive at ~50 Hz with jitter.

Without this timestamp, the offline replay must guess when each IAS update arrived — and the rate-adjusted EMA's state diverges over time.

**Fix**: log either:
- `uIasUpdateUs` directly (uint32, ~10 bytes/row), OR
- a delta-from-row-timestamp `iasUpdateOffsetUs` (uint16, ~6 bytes/row).

**Recommend** the delta form.

### G3 — `compFadeIn_` state is not logged 🚀

A τ=0.5 s EMA that ramps 0→1 each time `iasAlive` rises (`Ahrs.cpp:309-314`); reset to 0 on `iasAlive` fall (`Ahrs.cpp:344`).

**The killer detail**: log files don't start at boot. A log file rotates or restarts mid-flight, and the box may already be airborne with `compFadeIn_=1`. From the log alone, the offline replay cannot tell what the value was at row 0. Re-running from `compFadeIn_=0` (cold-start assumption) injects a half-second of bad EKF behavior at the start of every offline replay.

**Fix**: log `compFadeIn` per row. Either:
- uint8 with ·100 quantization (4 bytes/row, 0.01 resolution — plenty for a 0..1 ramp), OR
- float at `%.4f` (~6-8 bytes/row, exact).

**Recommend** the uint8 form unless the float cost is negligible.

### G4 — AHRS config not embedded in the log 🚀

`LogMeta.h:37-50` records firmware version + a handful of summary fields. It does NOT record `iAhrsAlgorithm`, `fPitchBias`, `fRollBias`, `iGyroSmoothing`, `iLogRate`, `madgwickGateHiG/LoG`, or EKF6 Q/R values.

The cfg lives separately in `onspeed2.cfg` on the SD card. **If Vac connects to the AP mid-flight and saves cfg, the cfg-at-log-time is silently overwritten.** Offline analysts see post-edit cfg, not the cfg the firmware was running under.

**Fix**: two changes:
1. On `LogSensor::Open`, copy the contents of `onspeed2.cfg` into `<log_NNN>.cfg` alongside the CSV and `.meta`. The log+cfg+meta triple becomes self-describing.
2. Add `iAhrsAlgorithm`, `iLogRate` directly to `LogMeta` so the algorithm-in-effect is discoverable without parsing the XML cfg.

### G5 — EKF6 Q/R/P0 hardcoded in `EKF6.h` ⚙️

`EKF6.h:258-269`, `EKF6.cpp:97-108` — covariance/process-noise parameters are compile-time constants. To tune them you currently have to recompile.

**This is NOT a firmware blocker for Vac's tuning flight.** The offline replay can instantiate `onspeed::EKF6` directly with chosen Q/R, run the same logged inputs through it, and compute the residual. Q/R discovery happens offline; the firmware change to plumb the chosen values into cfg comes *after* tuning identifies them.

**Recommend**: defer. After Vac's tuning campaign identifies the best (Q, R), open a follow-up PR to plumb them through cfg so the next build embeds them.

### G6 — `madgwickGateActive` getter exists but isn't logged 🚀

The current branch (`feature/bundler-esbuild`) has uncommitted Madgwick-gate work that exposes `Ahrs::madgwickGateActive()` and `Ahrs::madgwickGateMetricG()` getters (`Ahrs.h:143-144`). When the gate fires, Madgwick's accel-correction is suppressed entirely — radically different filter behavior.

Even though Vac will be flying EKF6 (not Madgwick) for the tuning campaign, **the gate state matters for any Madgwick-reference baseline pass**. Without it, you can't distinguish gate-induced attitude excursions from real-motion excursions.

**Fix**: wire the existing getters into `LogRow` and `LogCsv`. Two new columns: `madgwickGate` (1 bit, encoded as `0`/`1`) and `madgwickGateMetricG` (float, optional but cheap at `%.4f`).

## 4. Subtle gaps (not blockers, worth fixing)

### S1 — `FlightPath` logged at `%.2f` (0.01°)

EKF6's α measurement input is `gamma = θ − α_meas`. At small flight-path angles the 0.01° LSB is a meaningful fraction. Bumping to `%.4f` is cheap and removes the precision loss.

### S2 — `tasDotSmoothed_` is unobservable

Feeds the forward-accel compensation factor (`Ahrs.cpp:291,316-317`). Already in `AhrsOutputs.tasDotMps2`. Just needs to be wired to a column. Re-derivable from G2, but logging it directly catches reconstruction bugs.

### S3 — Final `accelFwdComp/LatComp/VertComp` not logged

These are the actual EKF inputs. We log raw accels (pre-comp) and EKF outputs (Pitch/Roll), but not the inputs that produced them. Logging the comp triple lets us *verify* the offline reconstruction is correct, not just trust it.

## 5. Recommended firmware changes (before Vac flies)

### Critical (must-fix-before-flight) 🚀

These are the blockers. Each is a small change:

1. **Add `dtUs` (uint16) column** — Fix G1. Wire `SensorIO::Process` → `LogRow::dtUs` → CSV emit.
2. **Add `iasUpdateOffsetUs` (uint16) column** — Fix G2. Wire `g_Sensors.uIasUpdateUs - timeStampMs*1000` to a new column.
3. **Add `compFadeIn` (uint8·100) column** — Fix G3. Wire `g_AHRS.getCompFadeIn()` (or whatever the accessor is — likely needs to be added) to a new column.
4. **Embed cfg snapshot on log open** — Fix G4. In `LogSensor::Open`, after creating `log_NNN.csv`, copy `onspeed2.cfg` to `log_NNN.cfg` alongside.
5. **Add `iAhrsAlgorithm` + `iLogRate` to `LogMeta`** — Fix G4 part 2. Single byte each, written into the `.meta` sidecar.
6. **Wire `madgwickGate` + `madgwickGateMetricG` columns** — Fix G6. Already-exposed getters → LogRow → CSV.

Total firmware changes: 5 new CSV columns, 1 cfg-copy call, 2 fields added to `LogMeta`. **No algorithm changes.** No risk to flight behavior.

### Should-fix (significantly helps tuning) ⚙️

7. **Add `tasDotMps2` column** — Fix S2. Already in `AhrsOutputs`; wire to CSV.
8. **Add `accelFwdComp`, `accelLatComp`, `accelVertComp` columns** — Fix S3. Three new floats at `%.6f`.

### Nice-to-have 💡

9. **Bump `FlightPath` precision to `%.4f` or `%.6f`** — Fix S1.
10. **Log-rate change marker** — if `iLogRate` changes mid-flight, write a marker into the next row's reserved field.

## 6. Test plan: verify logs are sufficient before Vac flies

Once the critical fixes are in:

1. **Bench fixture replay-equivalence test.** Build a synthetic input stream (raw IMU + IAS + Palt + OAT at 208 Hz, 60 sec). Feed through live `Ahrs::Step` AND through a host-side re-execution path that reads only the new logged columns. Assert byte-identical EKF6 state output every tick.

2. **Real-flight replay-equivalence test.** Take one of Vac's existing logs *after the new columns are in place*. Build a host_main subcommand (`ahrs_replay_from_log`) that reads CSV → reconstructs `AhrsInputs` per row → calls `Ahrs::Step` with the logged `dtUs`. Output its EKF6 state. Compare against the logged `Pitch`/`Roll`/`DerivedAOA` columns. Tolerance: zero ULP for non-quantized fields, ≤1 LSB for `%.2f`-quantized.

3. **VN-300 ground-truth cross-check.** With the same fixture, plot OnSpeed EKF6 `Pitch` vs `vnPitchDeg` over a banked turn. Compute residual RMS. This is the metric the offline tuner optimizes.

4. **Cold-start vs warm-start parity.** Cut the same log in half, start replay from the middle row. Confirm that *with* G3's `compFadeIn` logged, the warm-start replay matches the cold-start replay byte-identically from that row forward. If it doesn't, there's still an unlogged state.

5. **Cfg sidecar round-trip.** Open a log, modify cfg via web UI, save, close log. Verify `<log_NNN>.cfg` reflects pre-edit cfg, NOT post-edit.

## 7. PR sequence

| # | Title | Effort | Blocks |
|---|---|---|---|
| 1 | Add `dtUs` + `iasUpdateOffsetUs` + `compFadeIn` columns | 0.5 day | Vac's flight |
| 2 | Embed `onspeed2.cfg` snapshot on `LogSensor::Open`; add `iAhrsAlgorithm` + `iLogRate` to `LogMeta` | 0.5 day | Vac's flight |
| 3 | Wire `madgwickGate` + `madgwickGateMetricG` columns from existing getters | 0.5 day | Vac's flight (if any Madgwick-reference passes planned) |
| 4 | Add `tasDotMps2` + `accelFwdComp/LatComp/VertComp` columns | 0.5 day | tuning quality |
| 5 | Bench fixture + real-flight replay-equivalence native test | 1 day | confidence |
| 6 | `host_main ahrs_replay_from_log` subcommand | 1 day | offline tuning loop |

PRs 1-3 are the must-haves. Total ~1.5 days for the critical path; ~4 days for the full set including the bench test that proves byte-faithful replay works.

## 8. Acceptance criteria

### Pre-flight (firmware ready for Vac)

- [ ] `dtUs`, `iasUpdateOffsetUs`, `compFadeIn` columns appear in the CSV header at log creation.
- [ ] `<log_NNN>.cfg` appears next to `<log_NNN>.csv` after each log open.
- [ ] `<log_NNN>.meta` carries `iAhrsAlgorithm` and `iLogRate`.
- [ ] `madgwickGate` column populates correctly (0 in level flight, 1 in steep turn).
- [ ] All 1080 existing native tests still pass.

### Post-flight (tuning loop works)

- [ ] `host_main ahrs_replay_from_log` consumes Vac's flight log and emits per-row EKF6 state byte-identical to the logged `Pitch`/`Roll`/`DerivedAOA` columns (within float precision).
- [ ] Replaying the same log with varied Q/R parameters produces different residuals against `vnPitch`/`vnRoll` — the tuning loop is functional.
- [ ] Warm-start from a non-zero row of the log matches cold-start byte-identically (G3 fix verified).

## 9. Out of scope

- **G5: plumb EKF6 Q/R/P0 into cfg.** Tuning identifies values offline; cfg plumbing happens after. Separate follow-up PR.
- **Madgwick gate firmware ship.** That's a separate decision pending more flight data; the column wiring is independent of the ship decision.
- **VN-300 quaternion decoding.** Currently only Euler is extracted; Euler-derived quaternions lose information near gimbal lock, but Vac won't be flying ±90° pitch in normal envelopes. Defer.

## Appendix A: File citations

All paths under `/Users/sritchie/code/onspeed/onspeed-worktrees/bundler-esbuild/`:

- `software/Libraries/onspeed_core/src/ahrs/EKF6.h:278-286` — `Measurements` struct (the EKF input contract)
- `software/Libraries/onspeed_core/src/ahrs/EKF6.h:224-269` — `Config` defaults (Q, R, P0 hardcoded — G5)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:228-551` — `Ahrs::Step` (the orchestrator)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:145-224` — `updateTas_` (the variable-rate EMA dependent on G2)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:290-345` — comp-factor pipeline (G3's `compFadeIn_`)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:353-423` — EKF measurement adapter (the actual `Measurements` build)
- `software/Libraries/onspeed_core/src/ahrs/Ahrs.h:143-144` — `madgwickGateActive()` / `madgwickGateMetricG()` getters (G6)
- `software/Libraries/onspeed_core/src/types/LogRow.h` — row struct (where new columns get added)
- `software/Libraries/onspeed_core/src/proto/LogCsv.cpp:267-309` — CSV header definition
- `software/Libraries/onspeed_core/src/proto/LogCsv.cpp:311-435` — CSV `FormatRow` (where new columns get written)
- `software/Libraries/onspeed_core/src/log/LogMeta.h:37-50` — `LogMeta` struct (G4: needs `iAhrsAlgorithm`, `iLogRate`)
- `software/Libraries/onspeed_core/src/efis/Vn300.h:35` — `Vn300Data` (Euler only; no quaternion decoded)
- `software/sketch_common/src/drivers/SensorIO.cpp:162-240` — IMU read task + dt computation (G1's `fDtSeconds` lives here)
- `software/sketch_common/src/drivers/SensorIO.cpp:412` — `uIasUpdateUs` assignment (G2's source)
- `software/sketch_common/src/drivers/IMU330.h:88-89` — `Snapshot` interface
- `software/sketch_common/src/drivers/IMU330.cpp:383` — `out.timestampUs = 0` (G1's leak — IMU timestamp zeroed before downstream)
- `software/sketch_common/src/tasks/AHRS.cpp:41-74` — `SnapshotInputs_` (where the EKF input struct is built)
- `software/sketch_common/src/tasks/AHRS.cpp:201-216` — `Process` (calls `Ahrs::Step`)
- `software/sketch_common/src/tasks/LogSensor.cpp:270-380` — `Open` (G4's cfg-copy hook lives here)
- `software/sketch_common/src/tasks/LogSensor.cpp:539-655` — row build (where new columns get populated)

## Appendix B: VN-300 column coverage (already good — for reference)

Logged columns in the VN-300 row block (`LogCsv.cpp:282-289`, all at `%.2f`):

- `vnAngularRateRoll/Pitch/Yaw` (rad/s) — ground-truth gyro
- `vnAccelFwd/Lat/Vert` — total body-frame accel (g+linear)
- `vnLinAccFwd/Lat/Vert` — gravity-subtracted linear accel
- `vnYaw/Pitch/Roll` (deg) — INS attitude (the tuning target)
- `vnYawSigma/PitchSigma/RollSigma` — VN's 1-σ uncertainty (excellent for weighting tuning residuals)
- `vnVelNedNorth/East/Down` — inertial-fused velocity
- `vnGnssVelNedNorth/East/Down`, `vnGnssLat/Lon` — raw GNSS observables
- `vnEstAltFt`, `vnGpsFix`, `vnDataAgeMs`, `vnTimeUtc`

Cadence: VN frames at ~50 Hz; logged at log-row cadence (50 or 208 Hz). When log rate > VN rate, the same VN values repeat across rows. `vnDataAgeMs` discloses staleness — use it to deduplicate offline.

## Appendix C: Why this matters in numbers

To set expectations on residual quality:

- Madgwick pitch bias in steep turns: **+3 to +4°** (measured on Vac's log_007, three steady 60° turns).
- VN-300 pitch 1-σ uncertainty (`vnPitchSigma`): typically **<0.1°** for VN-300 in normal flight.
- Target EKF6 pitch residual after tuning: **<1° RMS** in steady turns, **<2°** in dynamic turns.

If G1 reconstruction error introduces 0.5° of attitude drift over a 30-second turn (plausible at 19% per-step dt error), the tuner can't tell whether residual=0.8° means "EKF is well-tuned" or "EKF is slightly off but log reconstruction is hiding it." Closing G1+G2+G3 brings the reconstruction floor below 0.1° — well below the residual we're optimizing against.
