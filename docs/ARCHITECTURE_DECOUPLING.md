# OnSpeed-Gen3 Architecture: Decoupling Model

This document is a **contract** for how OnSpeed firmware should be structured as we deploy to more environments (real planes, simulators, log replay, synthetic data) and as we add more outputs (M5, browser, HUD glasses, X-Plane plugin). It is also an **honest audit** of where the current code honors the contract and where it does not.

It is not a reorganization plan. Migration sequencing belongs in its own document; this one defines what we are reorganizing toward.

> **Last reconciled with master:** 2026-06-02, against tip `bd650a7d`. Two more days, and the snapshot architecture is **deployed across four of six flight-state surfaces**: PR #653 merged the seqcount primitive (`SnapshotPublisher<T>`), PR #660 unified EFIS+boom onto it (replacing the dedicated-mutex pattern from PR #656), PR #662 migrated AHRS output, PR #665 moved M5+Housekeeping to Core 0 (validated at 416 Hz: display 0→19.9 Hz, heartbeat 0→10.0 Hz, IMU lateMax 4166→784 µs), PR #666 corrected the multi-writer comment, PRs #667/#668 fixed hardware pins and replaced bit-bang OneWire with RMT-backed OAT (eliminates 18-20 ms preemption window). **Plus:** a `feat/imu-snapshot` worktree with 17 commits adds `g_FlapSnapshot`, `g_SensorSnapshot`, `g_ImuSnapshot` — the remaining three flight-state publishers — with all consumers migrated and a coherence-review audit done yesterday confirming the four-snapshot architecture is one coherent pattern. **No bench validation of the worktree yet.** See **"Six snapshot surfaces, four-and-a-half landed (2026-06-02)"** below for the close read, and **"Next priority"** for the load-bearing-next-step.

## The model

OnSpeed is **a stream-processing system, not a sensor reader with extras bolted on**. The processing core (AHRS fusion, AOA calculation, percent-lift mapping, tone decisions, audio synthesis) is the same regardless of where the bytes come from or where the answers go. The only thing that changes between an aircraft, a simulator, a replayed flight, and a unit test is the **adapter layer** at each end.

```
                           ┌─────────────────────────────┐
                           │  PROCESSING (onspeed_core)  │
                           │  pure C++17, no platform    │
                           │  AHRS · AOA · ToneCalc ·    │
                           │  ToneSynth · ConfigParse ·  │
                           │  filters · proto codecs     │
                           └──────────────┬──────────────┘
                                          │
                ┌─────────────────────────┼─────────────────────────┐
                ▼                         ▼                         ▼
       ┌───────────────┐         ┌───────────────┐         ┌───────────────┐
       │ DATA SOURCES  │         │  ESTIMATORS   │         │     SINKS     │
       │   (adapters)  │         │   (parallel)  │         │   (encoders)  │
       └───────┬───────┘         └───────┬───────┘         └───────┬───────┘
               │                         │                         │
   ┌───────────┴────────────┐  ┌─────────┴──────────┐  ┌───────────┴──────────┐
   │ • real sensors (IMU,   │  │ • Madgwick θ-γ     │  │ • Audio I2S           │
   │   pressures, ADC, EFIS,│  │ • EKF6 alpha       │  │ • DisplaySerial wire │
   │   boom, OAT)           │  │ • CP-polynomial    │  │ • WebSocket JSON      │
   │ • LogReplay (SD CSV)   │  │ • IAS-to-AOA fit   │  │ • SD log CSV          │
   │ • X-Plane plugin       │  │   (live)           │  │ • HUD frame (future)  │
   │ • Simulator UDP/TCP    │  │ • boom probe alpha │  │ • EFIS RS-232 (future)│
   │   (future)             │  │   (when present)   │  │                       │
   │ • Test fixtures        │  │                    │  │                       │
   │   (TestPot, RangeSweep)│  │                    │  │                       │
   └────────────────────────┘  └────────────────────┘  └───────────────────────┘
```

**Three rules:**

1. **The processing core does not name its source.** AHRS doesn't know whether the IMU bytes came from an ICM-42688 or X-Plane's `sim/flightmodel/...` datarefs. It takes an `AhrsInputs` struct.

2. **Every adapter has a schema.** Source adapters produce frames in a named, versioned format. Sink adapters consume frames in a named, versioned format. The format is enforced at the boundary, not assumed. PR #353 (LogReplay name-keyed parsing) is the first place this rule got real teeth — and the bug-fix story behind it (replay broke when the log format drifted) is exactly why this rule exists.

3. **Estimators are parallel and named.** AOA can be computed multiple ways from the same sensor stream. Each is a stateful object, not a global. A fusion or selection layer lives downstream and names its inputs. This is the foundation for live calibration monitoring and auto-calibration.

The architecture matches what the user is describing. **What we have not internalized yet** is rule 2 — schemas as first-class artifacts — and rule 3 — estimators as parallel, named, swappable.

## The pipeline is now real upstream (PR #590, 2026-05-20)

When this doc was first drafted, the "stream processing" framing was an architectural argument. Two days ago it became code. **PR #590** ("AHRS prep refactor: four-stage pipeline") landed on master with this principle stated in its commit message verbatim:

> *"The single principle: **stream-processing model. Sensor → AHRS algorithm → smoothing → outputs.** Each stage owns its concerns; no shared retunable state between AHRS algorithms."*

The C++ AHRS step (`software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp:240-449`) is now structured exactly that way. Stage markers are named in source comments:

```
// Stage 1 — Sensor.        (TAS update, installation-bias rotation)
// Stage 2 — AHRS algorithm. (Madgwick or EKFQ, owns its own EMA + gates + alpha)
// Stage 3 — Smoothing.      (wire-spec τ accel/gyro smoothing, Kalman alt/VSI for Madgwick)
// Stage 4 — Outputs.        (assemble AhrsOutputs struct)
```

Each algorithm is a wrapper class (`onspeed::ahrs::Madgwick`, `EkfqPipeline`) that owns its own internal pre-filtering, IAS gating, and comp-fade ramp. They are not interchangeable at the field level — each algorithm's tuning constants are its own — but they expose a uniform handoff: input is sensor-stage state, output is `AhrsOutputs`. The standalone `KalmanFilter` is bypassed when EKFQ owns the vertical channel (z/vz as filter states).

**This is the doc's argument, in production**, applied to the upstream half (sensors → algorithm → smoothing → outputs).

What the in-production pipeline does NOT yet have:

- **Stage 5 — Sink-tier composition.** What this doc has been calling `LiveDataFrame`. After Stage 4 hands off `AhrsOutputs`, the data scatters into `g_AHRS`, `g_Sensors`, `g_EfisSerial`, `g_Flaps`, `g_Config`, and the sink layer (DataServer, DisplaySerial, LogSensor) reads each one ad-hoc. The pipeline discipline ends at Stage 4.
- **Parallel estimators.** Stage 2 picks one algorithm (config-driven Madgwick vs EKFQ). Today they don't run in parallel; EKF6's per-tick alpha-tracking is gone (PR #591 moved AOA derivation to a kinematic formula post-step). The doc's "parallel AOA estimators" vision needs explicit upstream support to land — see §6 and step #7 below.

**The richer framing** for this doc, going forward:

> The C++ side has shipped the stream-processing pattern at the upstream half (PR #590, four-stage AHRS). The JS side has shipped it at the downstream half (PR #526, M5State + per-source adapters + one renderer family). The Python side declared its half explicitly (PR #380, `LiveSnapshot` citing this doc by name). **The missing seam is the C++ sink-side closure — Stage 5, the `LiveDataFrame` snapshot that hands a single typed value off to every sink.** This is no longer "the doc invented an abstraction"; it's "we shipped the philosophy in three places, the firmware sinks haven't caught up."

## Stream topology and rate planning (2026-05-23)

Three days of post-#590 work formalized the upstream task layout. The streams are now genuinely independent at the FreeRTOS-task level, not just structurally:

```
                              Core 1                              Core 0
                              ──────                              ──────
ImuReadTask          208 Hz, prio 5, IMU SPI@8MHz, ~67 µs/read
SensorReadTask        50 Hz, prio 5, pressures (pitot/AOA/static)
                                      via xSensorMutex
AhrsTask                                              EfisReadTask    ~50 Hz native (VN-300, IDF event queue,
                                                                      wake-on-data, 2 KB IDF buffer + 2048-entry
                                                                      PERF ring)
                                                       BoomReadTask    ~50 Hz native (5 ms poll, 128 B HW FIFO,
                                                                      512-entry PERF ring)
                                                       LogSensorCommit (writer drain, FormatRow runs here per
                                                                      PR #625, snapshots g_AHRS under xAhrsMutex
                                                                      per PR #634)
                                                       WebServer / WiFi
```

Each stream has its own task, its own buffer, its own clock domain. The synthetic stream generator (PR #618) lets us drive any of them at arbitrary rates on bench without hardware.

### Current verified rates

| Stream | Rate | Source of truth | Notes |
|---|---|---|---|
| IMU | 208 Hz | `HardwareMap.h:226` (`kImuSampleRateHz`); variable-period scheduling in `SensorIO.cpp:187-205` for exact 4807 µs + remainder | Core 1, prio 5. 8 MHz SPI. Already a config field in `AhrsConfig::imuSampleRateHz`. |
| Pressures (pitot/AOA/static) | 50 Hz | `HardwareMap.h:229-230` (`kPressureSampleRateHz`) | Core 1, prio 5. 800 kHz SPI per HSC datasheet absmax (PR #635). |
| EFIS (VN-300, primary) | ~50 Hz native | VN-300 default config, 127 B per frame at 115200 baud | Core 0, prio 3 (PR #622). Interrupt-driven via IDF UART event queue. |
| Boom probe | **~50 Hz** | PR #622 commit message: "~30 B at 50 Hz" | Core 0, prio 3, 5 ms poll cadence. **Not 10 Hz** — common misconception worth correcting. |
| Flap pot | ~1 Hz | `Flaps.cpp` task | Deliberately slow for denoising. |
| OAT | ~1 Hz | DS18B20 1-wire | Slow update; consumers should treat as quasi-static. |
| Display sinks (M5 wire, WS JSON) | 20 Hz | `kDisplaySerialPeriodMs = 50` | Sink-side merge happens here. |
| Log CSV writer | 50 Hz (configurable to 208 Hz via `iLogRate`) | `OnSpeedConfig::iLogRate` | Per-row LogRow currently combines fields from all upstreams at writer-tick. |
| Audio decisions | 20 Hz | Audio task | Reads `g_Sensors.AOA` + flap config. |

### Bumping IMU + EKFQ to 400/800 Hz — what the architecture supports today and where it pinches

The user has been thinking about this. Three honest answers:

**IMU SPI headroom: comfortable up to ~600 Hz, plausible to 832 Hz.** ISM330DHCX is rated to 10 MHz SPI (PR #635 set it to 8 MHz). A single 6-axis read is ~67 µs at 8 MHz. At 416 Hz that's 28 ms/sec of bus time (2.8% of the bus). At 832 Hz that's 56 ms/sec (5.6%). Bus time is not the bottleneck.

**Core 1 CPU budget: comfortable at 416 Hz, tight at 832 Hz.** The bench at 208 Hz showed ~18.4% Core 1 (PR #625 bench notes). Roughly doubling: ~35% at 416 Hz, ~65% at 832 Hz. The 832 Hz case leaves ~30% for other Core 1 work (50 Hz pressure read, occasional flap, AHRS task itself). Real-world margin would need a bench measurement at 832 Hz with synthetic IMU rate (PR #618 infrastructure makes that runnable).

**Log throughput: this is the real bottleneck.** At 208 Hz IMU + AHRS + writer, the ring is 60 KB (BYTEBUF) holding ~3.5 sec of LogRow items. At 416 Hz, that drops to ~1.7 sec; at 832 Hz, ~0.9 sec. A single 50 ms WiFi-induced writer stall at 832 Hz overflows the ring and silently drops rows (the kind of bug PR #530 closed in another guise). **Bumping the IMU rate without bumping the ring is the first thing to break.** Mitigations: bigger ring (use PSRAM via PR #626), faster writer (SD SPI is 20 MHz post-#623; bus time isn't the issue, the latency of the SD card's internal block-erase is), or — more interesting — *split logging streams*. See below.

**VN-300 at 400 Hz: requires VectorNav config + UART baud changes, not just "ask for it faster."** At 115200 baud and 127-byte frames, the wire physically supports at best ~90 Hz. VN-300 supports baud up to 921600; at that rate, 127-byte frames could go up to ~720 Hz. The VN-300 itself supports internal IMU sampling up to ~800 Hz; what's currently being sent over UART is the EKF-output state, not raw IMU. To clone the VN-300's IMU you'd configure VN-300 to emit its `Sensor` outputs (raw accel/gyro/mag) at, say, 400 Hz, bump the UART to 460800, and increase the IDF buffer accordingly. **Three independent config changes; none architectural.**

### What this means for multi-rate logging — the user's binary + pilot split

The user is sketching two distinct logging streams with different consumers:

1. **Binary "cloning" log**: raw VN-300 frames + raw IMU samples at maximum producer rate, unsmoothed, designed for offline replay through *any* fusion algorithm (the cloning use case: feed the same bytes into a desktop EKFQ implementation and verify byte-for-byte agreement, or substitute a different filter and compare).
2. **Pilot tuning log**: unsmoothed EKF outputs at high rate, designed for offline tuning of the smoothing stage *without re-running the EKF*. The offline tuner reads post-Stage-2 / pre-Stage-3 values and explores smoothing parameter changes.

These need different sub-stream selections from the canonical snapshot:

- Binary cloning log: `{ImuSample.raw, EfisFrame.raw_bytes_or_struct}` at IMU rate (208/416/832 Hz). Possibly with the *raw VN-300 protocol bytes* alongside the parsed struct, so the offline replay can re-parse and verify the parser too.
- Pilot tuning log: `{ImuSample, SensorSample, AhrsOutputs-pre-smoothing}` at IMU rate. The "pre-smoothing AhrsOutputs" is a *new variant* — today Stage 4's `AhrsOutputs` is the post-Stage-3 result. Capturing pre-Stage-3 values would require either a sibling output struct or a flag.

Both of these are **specific subset projections of LiveDataFrame** with their own destination, ring, and writer. The current `LogSensor` (one ring, one writer, CSV output, all sub-structs combined per row) is the wrong shape for either. **The user's plan, if pursued, makes the LiveDataFrame design's payoff concrete: each new log destination is "pick the sub-structs you care about, attach a ring, register a writer task" — no whole-system refactor.**

### Two analysis workflows, not one

This is worth saying explicitly because the doc previously implied one replay path:

- **Pilot replay** (existing): CSV log → docs-site `/data-and-logs/replay/` UI (PRs #543–#548) → visual tuning of smoothing constants and setpoints.
- **Cloning / fusion analysis** (new, implied by the binary-log plan): raw binary log → `host_main` harness (already extended by PR #595 for EKFQ Optuna tuning) → offline EKF substitution, parameter sweep, byte-equality verification against the production firmware's output.

The two workflows want different formats (CSV vs binary), different tooling (web UI vs Python+host_main), different fidelities (post-smoothing visual vs raw-stream byte-exact). **They should be separate logging paths, not one path with options.** Forcing them through one log format would compromise both.

### Implications for the LiveDataFrame plan

The user's question turned the abstraction into something concrete. The composed `LiveDataFrame` design — `ImuSample imu; SensorSample sensors; AhrsOutputs ahrs; EfisFrame efis; BoomFrame boom; FlapState flaps` — naturally accommodates:

- The pilot CSV log: project to LogRow-shape, write all sub-structs combined per writer-tick
- The binary cloning log: project to `{imu, efis.rawBytes}`, write at IMU rate
- The pilot tuning log: project to `{imu, sensors, ahrs.preSmoothing}`, write at IMU rate
- The 20 Hz WS JSON: project to display-tier subset, write at sink rate
- The 20 Hz M5 wire: project to wire-format subset, write at sink rate
- Future MAVLink emit: project to ATTITUDE + AOA_SSA + VFR_HUD shapes, write at MAVLink rate

The struct doesn't grow with each new consumer. The set of *projections* grows, and the set of *writer tasks* grows. **The pre-condition for this design to pay off is the snapshot itself: until LiveDataFrame exists, every new logging path has to invent its own snapshot.** This is precisely the architectural argument for step 1, restated for the user's specific upcoming work.

## Six snapshot surfaces, four-and-a-half landed (2026-06-02)

In the past two days, the snapshot architecture has gone from "EFIS/boom + uncertain about AHRS" to **four flight-state surfaces on the seqcount primitive in production, three more staged in a worktree, with a coherence-review audit confirming the architecture is one coherent pattern**. This is the architectural milestone the doc has been pointing at for two months.

### Current state map

| Surface | Primitive | Status | Producer | Consumers migrated |
|---|---|---|---|---|
| EFIS (suEfis, suVN300) | `SnapshotPublisher<T>` member | ✓ master (PR #656 → #660) | EfisReadTask | LogSensor, AHRS, DataServer |
| Boom (suBoomData) | `SnapshotPublisher<T>` member | ✓ master (PR #656 → #660) | BoomReadTask | LogSensor |
| AHRS output (18 fields) | `SnapshotPublisher<T>` global | ✓ master (PR #662) | ImuReadTask::Process(), Init(), LogReplay (exclusive) | DataServer, DisplaySerial, LogSensor, Housekeeping, ApiHandlers, ConsoleSerial |
| Flap state (15 fields) | `SnapshotPublisher<T>` global | ⧗ worktree `feat/imu-snapshot` | Flaps::Update on SensorReadTask + HandleConfigSave flap swap | DisplaySerial, DataServer, calwiz API, SnapshotActiveFlap audio path |
| Sensor state (13 fields) | `SnapshotPublisher<T>` global | ⧗ worktree | SensorIO::Read + LogReplay/synth | LogSensor, DataServer, M5 wire builder, /api/sample |
| Raw IMU (ImuSample) | `SnapshotPublisher<T>` global | ⧗ worktree | ImuReadTask + LogReplay | LogSensor (coherent accel+gyro triplet) |

**Six independent flight-state publishers, all on the canonical seqcount primitive, all single-writer-per-stream.** This is exactly the architecture the doc has been describing. The worktree closes the remaining three.

### What the past two days proved

PR #660's migration from PR #656's dedicated-mutex to PR #653's seqcount **collapsed ~85 lines of per-class mutex boilerplate** while keeping the same external API (`Snapshot(out)`). Result: one concurrency primitive, one pattern, two-pattern-merge complete.

PR #662 is the data-integrity payoff: DataServer's pitch/roll/VSI/altitude reads were *unsynchronized* before (relied on aligned-32-bit-load atomicity); now they read an atomically coherent multi-field frame. The PR was honest that it addressed data-integrity, not contention stalls — that's deferred.

PR #665 is the cross-core payoff: moving M5+Housekeeping to Core 0 was *enabled* by PR #662 (the AHRS snapshot makes cross-core reads coherent), and the bench data validates the architecture: display 0→19.9 Hz, heartbeat 0→10.0 Hz, IMU lateMax 4166→784 µs, **zero log drops, zero VN-300 tears across 455k rows.** The "everything is a stream" thesis is now bench-validated production.

The worktree (`feat/imu-snapshot`, 17 commits) closes Flap + Sensor + IMU. Yesterday's coherence-review audit (`local-plans/2026-06-02-snapshot-coherence-review.md`) explicitly verified one consistent pattern across all four flight-state surfaces and named three intentional inconsistencies (publisher-hosting convention, publish-idiom, payload-naming) — each judged "better local choice, document don't change." Three pre-existing leftover raw-global reads (not introduced by this work) named honestly and classified as safe-scalar follow-ups.

### What the past two days revealed about the plan

Two updates to standing positions:

**The composed `LiveDataFrame` view layer is now optional.** The doc has been pushing for it since the very first version. With six per-stream publishers landed/landing, every consumer calls `g_<Stream>Snapshot.read()` for the streams it needs. The composed view layer has no required consumer right now. MAVLink emit will be the first plausible consumer where multi-stream coherence at one tick matters (one MAVLink frame must carry ATTITUDE + AIRSPEED + AOA simultaneously consistent with each other). Defer composed-view work until then.

**`xAhrsMutex` narrowing is complete enough that the mutex's role has changed.** Yesterday's coherence-review confirmed: no flight-data reader takes it. Its remaining responsibilities are (1) AHRS filter state during Process()/Init() — serialized between ImuReadTask and config-save handlers, and (2) flap-state writer-ordering between Flaps::Update and config-save flap-vector swap. The contract has narrowed from "the big mutex that guards everything flight-related" to "the mutex that serializes writers." That contract should be stated in a public header comment so future authors don't reach for the wrong mutex by reflex.

### The actual landed architecture (EFIS + boom)

```
EfisReadTask (Core 0)                BoomReadTask (Core 0)
   |                                    |
   | parses VN-300 / D10 / G5 / etc.   | parses boom frames
   v                                    v
   build SuVN300Data / SuEfisData      build SuBoomData
   on stack                            on stack
   |                                    |
   v                                    v
[xEfisDataMutex_, portMAX_DELAY]   [xBoomDataMutex_, portMAX_DELAY]
   memcpy → published struct           memcpy → published struct
                |                                  |
                v                                  v
Many consumers (LogSensor, AHRS, DisplaySerial, DataServer):
   SuVN300Data vn; g_EfisSerial.SnapshotVn300(vn);
   SuBoomData  bm; g_BoomSerial.Snapshot(bm);
```

Each stream has its own dedicated mutex (`xEfisDataMutex_`, `xBoomDataMutex_`) — not the global `xAhrsMutex`. The publish path holds the mutex for one memcpy (sub-microsecond). Consumers hold it for one memcpy on the read side. **Web handlers reading EFIS state never contend with the IMU task.** The fine-grained per-stream mutex is the architectural win — even though it's mutex-based (not seqcount), the *scope* of the lock is constrained to the producer-consumer pair.

### Bench validation

- 416 Hz IMU + 400 Hz VN-300, 21 minutes, real hardware with USB-TTL stim dongle on EFIS RX
- 525,966 rows captured at exact 416 Hz
- **Zero tears across 528,180 atomic-publish observations** (offline detector: `tools/bench/check-atomic-publish.py`, per-frame counter encoded into Yaw / Pitch / Roll / Lat / Lon / tNs via `efis-stim --epoch-encode`)
- Zero reboots, zero ring drops, zero coredumps
- Compare to Vac's pre-#656 branch on the same stress: 5 reboots in 30 min, 35% Lat/Lon tear rate, 38,625 ring drops

This is a stronger validation than PR #653's 9 unit tests would have provided. **The bench-validated production pattern beats the theoretically-better unit-tested primitive.**

### What we got right

1. **Snapshot pattern at the right seams.** EFIS and boom are upstream stream sources whose consumers want coherent multi-field views. The pattern landed exactly there. Producer-task-owns-publish discipline.
2. **Per-stream mutex, not global.** `xEfisDataMutex_` and `xBoomDataMutex_` are independent of `xAhrsMutex`. EFIS reads no longer block IMU writes.
3. **The AHRS step now consumes via Snapshot pattern.** `AHRS::SnapshotInputs_()` reads `g_pIMU->Snapshot()`, `g_Sensors.Snapshot()`, `g_EfisSerial.SnapshotEfis()`. The four-stage pipeline (PR #590) now feeds from coherent input snapshots — the upstream half of the architecture is genuinely complete.
4. **Bench validation is rigorous.** Offline tear detector + per-frame counter is the right shape for catching this class of bug. PR #648 captured lessons for future cycles.
5. **Pattern is duplicable.** EFIS and boom implementations mirror each other line-for-line. Each new stream that adopts this pattern is mechanical — same shape, ~30 lines.

### What we missed (or chose differently)

1. **PR #653's seqcount primitive is still open and unused.** The merged design used dedicated per-stream mutexes instead. For EFIS/boom this was *probably the right pragmatic call* — fine-grained mutex with sub-microsecond hold is genuinely fine. The interesting question is whether seqcount still belongs on master for the AHRS-output migration (see "Next priority" below).

2. **`portMAX_DELAY` on both publish and snapshot.** This is the same anti-pattern PR #646 fixed for web handlers (was `portMAX_DELAY` → now 100-1000 ms with 503 fallback). In the EFIS/boom case the mutex hold is genuinely sub-microsecond memcpy, so contention is rare — but if a writer is ever preempted between take and give, every reader blocks indefinitely. Best practice is `pdMS_TO_TICKS(X)` with a documented skip-frame fallback. Worth flagging as a hardening item but not blocking the architecture.

3. **AHRS-output side is unchanged.** The 18-20 ms IMU stall measurement (PR #647 bench data) is *not* addressed by #656. `DataServer.cpp:177-258` still reads `g_AHRS.SmoothedPitch`, `g_AHRS.SmoothedRoll`, `g_AHRS.KalmanVSI`, `g_AHRS.fTAS`, `g_AHRS.AccelVertFilter.get()`, `g_AHRS.AccelLatFilter.get()` directly. Any web handler that needs multi-field AHRS state still takes `xAhrsMutex` and can block ImuReadTask. **The pattern landed at EFIS and boom; it hasn't reached AHRS yet.** That's the next step.

4. **No composed `LiveDataFrame` yet.** DataServer still does ~25 reads, now augmented with `SnapshotEfis()` / `SnapshotVn300()` calls plus the existing `xAhrsMutex`-taken flap snapshot. The composition layer doesn't exist. Correct prioritization — per-stream snapshots are building blocks, composition comes later — but worth saying we're at "Stage 4.5" (per-stream snapshots in production), not "Stage 5" (composed view).

5. **The dedicated-mutex pattern doesn't scale cleanly to AHRS.** EFIS-mutex and boom-mutex each protect a single-writer (EfisReadTask, BoomReadTask). AHRS-output has multiple state-mutating sites: `ImuReadTask` writes during `Process()`, `HandleConfigSave` writes during flap vector swap, `ConfigWebServer` writes during init. A dedicated `xAhrsOutputMutex` would still have writer-writer contention against any of those. **The cleanest pattern for AHRS-output is single-publisher seqcount** — which is exactly what PR #653 implements. **PR #653 still belongs on master; the AHRS migration is its motivating use case.**

### Side-wins from PR #656

Several non-architectural wins ride along: bulk-read EFIS (51% → 7% Core 0), gzip on dynamic pages (67 KB → 23 KB on `/aoaconfig`), VN-300 binary protocol at 400 Hz / 921600 baud with per-sample timestamps, double-precision Lat/Lon through CSV. Each one validates that the system absorbs high-rate work cleanly when the architecture is right.

### What this changes in the plan

The previous reconciliation (May 27) framed the next steps as commits 2-7 of `local-plans/PLAN_RATE_DECOUPLED_FILTER.md` building on PR #653's seqcount primitive. **Reality compressed two steps into one:** PR #656 landed the EFIS + boom snapshots directly (skipping the seqcount layer) and demonstrated they work at bench rates. The plan needs to be re-sequenced:

- **Done (PR #656):** EFIS snapshot, boom snapshot, AHRS consuming via Snapshot pattern.
- **Next (this reconciliation calls):** AHRS-output snapshot. Either via PR #653's seqcount primitive or a dedicated `xAhrsOutputMutex` following the EFIS pattern. See **"Next priority"** below for the argument.
- **After that:** consumer migrations off `xAhrsMutex` (DataServer, LogSensor, DisplaySerial, web API handlers).
- **Composition layer (`LiveDataFrame`):** still desirable but lower urgency; the per-stream snapshots are individually testable and individually consumable.

## Where we succeed

### `software/Libraries/onspeed_core/` is a fortress

~100 source files (.h/.cpp), zero Arduino/FreeRTOS includes, 58 native test suites. `scripts/check_core_purity.sh` enforces the no-platform-includes invariant in CI. Module breakdown:

| Layer | Modules | What |
|---|---|---|
| Types | `types/` (8 files) | POD frames passed between layers (`AhrsInputs`, `AhrsOutputs`, `EfisFrame`, `LogRow`, `AudioFrame`, etc.) |
| Processing | `ahrs/` (5), `aoa/` (3), `audio/` (7) | Madgwick, EKF6, Kalman, AOA calc, percent-lift, tone decision, tone synth |
| Filters | `filters/` (5 headers) | EMA, RunningMean, RunningMedian, SavGol, GOnsetFilter — header-only, instance-state |
| Parsers | `efis/` (7) | Per-brand decoders: GarminG3X, GarminG5, DynonD10, DynonSkyView, MglBinary, VN-300 |
| Codecs | `proto/` (4) | DisplaySerial wire format (74-byte M5 frame), LogCsv format/parse, LogCsvHeaderIndex (PR #353) |
| Config | `config/` (4) | `OnSpeedConfig` struct + tinyxml2-based serialization |
| Sensors | `sensors/` (6) | Pure raw-counts → physical-units conversions |

Every module takes inputs, returns outputs, holds state on `this`. No hidden globals.

### The X-Plane plugin proves the model works (and keeps proving it)

`software/OnSpeed-XPlane-Plugin/` is the cleanest possible consumer of the architecture, and the gap between "consumes core algorithms" and "is a complete OnSpeed frontend" closed substantially in v4.22.

What the plugin is, as of c429013:

- A **data-source adapter** reading X-Plane datarefs.
- An **audio sink** that calls `ToneCalc::calculateTone()` and synthesizes PCM via `ToneSynth` — same code the firmware runs (#394 made this spec-conformant: per-pulse stall-volume ramp, directional stereo pan, full audio chain, not buffered samples with sleeps between).
- A **display sink** that **embeds the M5 indexer** in a floating X-Plane window (#395, #415, #416). All five M5 modes — AOA primary, Backup AI, Indexer-only, Energy decel, G-history — render in-sim, click-to-cycle, pop out to a second monitor, position remembered per aircraft (#414).
- The plugin derives `pipPctLift` and `gOnsetRate` from datarefs (#405) — i.e., it produces the same wire fields the firmware does, from a different data source, and feeds them to the same renderer.
- 660 KB binary (#409, was 54 MB), MIT-licensed, standalone CMake, own test suite.

**The architectural significance is large.** The plugin runs the *same indexer code* the M5 sketch runs, against the *same wire format* (`DisplayBuildInputs` from `onspeed_core/proto/`), driven by a *different data source*. This is the doc's data-source-and-sink model fully realized for the X-Plane case. The decoupling isn't aspirational; it's already paid for itself.

The lesson to keep pulling forward: **the wire format (`DisplayBuildInputs`) is the seam**. Anything that produces a `DisplayBuildInputs` frame can drive the indexer. Anything that consumes one can render. Real OnSpeed firmware, X-Plane plugin, future MAVLink-bridged simulator, future log-replay-to-indexer tool — they share the format, they share the renderer.

### Codecs in `proto/` are pure and bidirectional

`DisplaySerial` encodes and decodes the 74-byte M5 frame. `LogCsv` formats and parses log rows. Both are exercised in tests on round-trip data without any I/O. Adding a new sink (HUD frame format) means adding a new module in `proto/` and a test — no other layer changes.

### LogCsvHeaderIndex (PR #353) is the prototype for "schema as first-class artifact"

Before PR #353, log replay was **position-based** — column N of the header had to be `pfwdSmoothed` because that's what the reader hardcoded. Add a column to the writer between releases and old logs broke. PR #353 retrofitted name-keyed parsing: `BuildHeaderIndex` resolves column names to ordinals and surfaces missing required columns and missing optional groups (boom / standard EFIS / VN-300) explicitly. 21 tests cover reorder, missing-required, missing-optional, extra-unknown, sign-flip, garbage rejection, and a four-fixture corpus naming each combination.

This is what the WebSocket JSON schema should look like next. (It also points at what's still missing in the log format itself — `# format=N` in the header — which #353 explicitly defers.)

## Where we fall short

These are concrete. Each one is a place where the next person adding a data source or a sink will trip.

### 1. The WebSocket JSON has no schema *(partial: drift detection landed; codec-from-struct still missing)*

**v4.22 update.** PR #369 (`467174f`) shipped drift detection in v4.22:

- `software/Libraries/onspeed_core/src/api/LiveDataJsonKeys.h` declares `kLiveDataJsonKeys[]` as the single canonical list of 27 field names.
- `test/test_data_server_json/` (5 native tests) asserts the firmware's `snprintf` format emits exactly those keys, that the dev-server replay NDJSON fixture carries the same set, and that the `/api/livedata` mock matches.
- `tools/web/test/api-schema.mjs` adds ~129 JS-side invariants covering every field's type, units, and range.
- PR #345 published a human-readable WebSocket protocol reference at `dev.flyonspeed.org/reference/websocket-protocol/`.

**What this catches:** the next PR #363. Anyone who edits the format string without editing `kLiveDataJsonKeys[]` (or vice versa) fails the test. Anyone who adds a JS read against an unknown field fails the JS schema test.

**What this does not yet do** — and what the original §1 vision called for:

- The JSON is still emitted by hand-written `snprintf`, not generated from a `LiveDataFrame` struct + codec.
- There is no `LiveDataFrame` struct in `onspeed_core/proto/` that other sinks (MAVLink, BLE GATT, future HUD) could share. The drift contract is name-level, not type-level.
- DataServer still reads its 25 globals directly (see §2). The schema pin protects the wire from drift, but doesn't fix the producer.

**Net:** the bug class is foreclosed. The architectural unification (one struct, many encoders) is not. Step 1 of the plan (LiveDataFrame snapshot) is still the right next move; step 2 (the schema layer) is now *partially done*, with the cheaper half delivered.

**2026-05-18 update — the JS side now ships the pattern.** PR #526 ("one renderer family for /indexer and /replay") deleted `tools/web/lib/modes.js` and converged both pages onto `packages/ui-core/components/svg/m5modes/` via a canonical `M5State` struct declared in `packages/ui-core/state-shape.js`. Two adapters — `wsRecordToState.js` (live, WebSocket → M5State) and `m5sim → M5State` (replay) — produce the same shape; one renderer family consumes it. This is exactly the "canonical struct + per-source adapter + one consumer" pattern this doc has been advocating, *already in production on the web side*. The C++ side hasn't caught up — DataServer still emits hand-written JSON read by the WebSocket adapter — but the architectural lesson is now demonstrated by code, not just argued for in a doc.

### 2. `DataServer.cpp` reads 25 unique global fields directly

`DataServer.cpp` (the `UpdateLiveDataJson` body) reads 25 distinct field accesses across `g_AHRS.*`, `g_Sensors.*`, `g_Config.*`, `g_EfisSerial.suVN300.*`, `g_EfisSerial.suEfis.*`, `g_Flaps.*`, `g_fCoeffP`, `g_iDataMark`.

The `xAhrsMutex` snapshot at line 368 covers part of this (the flap vector and lever ADC) but most reads happen without a snapshot, across multiple ticks of the writers. In practice it has not bitten us because aligned 32-bit loads are atomic on the ESP32-S3 and the values are EMA-smoothed at the producer. **In principle, every refactor of AHRS or Sensors has to come edit this file.** That is the tangling: the JSON builder is a load-bearing reader of a global address book.

**What honoring the model would look like:** the JSON builder takes a `LiveDataFrame` struct produced by a single snapshot function called once per 50 ms tick. The snapshot function holds the relevant mutexes, copies into the frame, and returns. The builder formats from the frame. New AHRS fields land by extending the frame, in one place; the JSON builder is a pure function of the frame.

### 3. LogReplay unpacks into globals *(unchanged in firmware; Python tooling is starting to formalize the shape)*

Firmware: `LogReplay.cpp:224-269` reads a `LogRow` and assigns 15+ global fields: `g_Sensors.PfwdSmoothed = row.pfwdSmoothed`, `g_AHRS.SmoothedPitch = row.pitchDeg`, `g_pIMU->Ax = row.imuForwardG`, etc. PR #353 fixed how the row gets parsed from CSV; it did not change how the row gets distributed afterward. A simulator data source (UDP socket from a sim) tomorrow has to duplicate this unpacking exactly, or copy this file and edit it, or refactor it.

**v4.22 update on the tooling side.** PR #380 added `tools/onspeed_py/` — a shared Python module for offline analysis — and its `live_snapshot.py` already declares the per-tick aircraft-state struct the firmware doesn't have yet. The module's docstring is explicit:

> `LiveSnapshot` is the input shape that orchestrators (synth-record scenarios, log-replay adapters, future analysis tools) feed to the display + audio pipeline. … shaped to match the eventual `LiveDataFrame` struct from `docs/ARCHITECTURE_DECOUPLING.md` §1. When that struct lands in `onspeed_core/proto/`, this Python class becomes a thin wrapper around its codec.

So the Python side has the snapshot frame already, and is being designed against this doc directly. The C++ port has not landed.

**2026-05-18 update — replay tooling now demonstrates the multi-adapter pattern.** PRs #543/#544/#545/#548 turned the docs-site `/data-and-logs/replay/` page into a full FlySto-style HUD with MP4 export, clip timelines, and dual inset slots. It consumes `LogRow` data through an `M5Sim → M5State` adapter and renders through the same `m5modes/` family the live `/indexer` page now uses (per §1's 2026-05-18 update). So *one renderer, two adapters, two data sources* is a working pattern on the web/replay side. Firmware-side LogReplay is unchanged; the OAT and boom re-injection gaps still exist. But the replay tool's shape proves the destination is reachable: when firmware LogReplay produces `LiveDataFrame` (step #1 of the plan), it inherits the same downstream renderer for free.

**What honoring rule 1 would look like:** there is a single function `ApplyDataSourceFrame(const SourceFrame&)` that distributes incoming data to wherever it needs to go. Every adapter (real-sensor reader, LogReplay, simulator bridge, X-Plane bridge if we ever embed it) produces a `SourceFrame` and calls this function. New source = new adapter, no edits to the distributor.

The reason this hasn't bitten is that we have one source at a time (compile-time selection: `EnSensors` / `EnReplay` / `EnTestPot` / `EnRangeSweep`). The minute we want a second simultaneously — say, "live sensors with synthetic AOA injection for cal-wizard rehearsal" — we have to invent the abstraction anyway.

### 4. The cal wizard recomputes DerivedAOA client-side and is wrong under EKF6 *(closed: issue #366, PR #373)*

PR #373 (`f077f7d`) ported the cal wizard from the legacy form-and-PROGMEM-JS pattern to a Preact page. The new wizard at `tools/web/lib/pages/CalWizardPage.js:266` reads:

```js
derivedAoaDeg: Number(o.DerivedAOA) || 0,
```

— i.e., it consumes the firmware's authoritative `DerivedAOA` directly off the wire, the seam this doc said it should use. `OnSpeed.DerivedAOA` reflects whichever AHRS is active (Madgwick `SmoothedPitch − FlightPath` or EKF6 `state.alpha_deg()`), so the wizard fits against the same signal the runtime audio path consumes. The CP→AOA polynomial regression and the IAS→AOA fit (lines 518 and 527 respectively) both read this wire-sourced value. The wizard's `pitchDeg`/`flightPathDeg` reads are only used for the auto-stop trigger (`pitchDeg < 0 && |pitchRate| > 5`), not for any regression. The legacy `software/OnSpeed-Gen3-ESP32/Web/javascript_calibration.h` file has been deleted; only the Preact page remains.

PR #373 also added `software/Libraries/onspeed_core/src/api/CalwizSave.{h,cpp}` (pure mutation logic) and `test_calwiz_save_diff` (byte-identical state vs. legacy path), so the save round-trip is differentially tested.

Issue #366 closed 2026-05-17.

The discipline lesson stands: *a downstream consumer must not reinvent an upstream computation* — the seam is the wire frame. The cal wizard has graduated from violating this rule to embodying it, and the differential test pins the contract so it can't regress.

### 5. The audio path reads `g_Config` directly *(still tangled in firmware; partly extracted in v4.22)*

Firmware: `Audio.cpp:623–645` reads `g_Config.iMuteAudioUnderIAS` directly at the IAS-gating layer (mute threshold, +5 kt unmute hysteresis, gating decision). `ToneCalc::calculateTone()` is pure (it takes a thresholds struct), but the wrapper around it still pulls from globals.

**v4.22 progress.** PR #381 extracted the tone-decision tree into `onspeed_core::audio::AudioOrchestrator` (`MakePulseSpec`, `MakeSolidSpec`, `DecideAndArm`) — pure, takes an `OrchestratorConfig` struct. PR #394 made the X-Plane plugin spec-conformant, running the same audio chain (per-pulse stall-volume ramp, directional stereo pan from sim lateral-G) the firmware does. So the *DSP* is in core; what's still in the sketch wrapper is the *config-read at the gating layer*.

The seam to chase next: the IAS-gating logic in `Audio.cpp` should take its mute threshold and hysteresis as parameters from a `ToneInputs` struct, not from `g_Config` directly. With that, the firmware audio task and the X-Plane plugin's audio thread differ only in where their config comes from (XML on flash vs. datarefs in the sim), not in how they consume it.

### 6. Estimators aren't parallel yet

There is one `AOACalculator` instance (`g_Sensors.AoaCalc`), one canonical AOA value (`g_Sensors.AOA`), and every consumer reads it. The runtime cannot today produce, simultaneously:

- Pressure-polynomial AOA (the operational signal)
- EKF6's alpha state
- `PitchAngle − FlightPath` (the Madgwick kinematic estimate)
- Boom probe alpha (when a boom is connected and the data is fresh)
- Live IAS-to-AOA fit residual (would tell us mid-flight that calibration has drifted)

These are five named estimators of the same underlying quantity, computed from overlapping subsets of the same upstream sensor stream. Rule 3 says they should each be a stateful object, with a fusion or selection layer downstream. The auto-calibration goal — "the box knows when its calibration is stale because two estimators disagree by more than their joint uncertainty" — falls out of this naturally.

**What blocks it today:** every consumer reads `g_Sensors.AOA`. There is no struct named `AoaEstimates` carrying multiple values. Adding parallel estimators without that struct would mean either inventing a new global per estimator (bad) or routing them through a side channel (bad). With the struct, ToneCalc takes `const AoaEstimates&` and chooses or blends; the calibration-residual monitor reads the same struct and emits a "calibration drift" signal; the cal wizard could use any chosen estimator without recomputing.

This is the largest pending change, and probably the highest-leverage one for the OnSpeed mission.

## What's already moving in this direction (running scoreboard)

This section tracks landed work that pulls in the model's direction. It's intentionally separate from the audit above — the audit names the gaps, this names the deliveries.

**v4.21 (April 27, 2026)** — ground-truth correctness for AHRS/audio:

- **PR #320 / #336** Honest single-linear PercentLift formula + percent-anchor wire format. Replaced four body-angle setpoints on the wire with five percent anchors (`tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift`, `pipPctLift`). Issue #363 surfaced from the schema-drift fallout; PR #365 fixed the cal wizard side. The "schemas as artifacts" lesson got teeth here.
- **PR #102** Audio engine port to Gen3: DAHDR envelope + per-PPS volume ramp.
- **PR #316/#318/#319** EKF6 correctness: gyro scale, dt scaling for process noise, q_bias bump.
- **PR #312** AHRS test fixtures aligned with production +1g sign convention.

**v4.22 (May 5, 2026)** — the platform shift:

- **PR #353** *LogCsvHeaderIndex name-keyed log parsing.* The doc cites this as the prototype for "schema as first-class artifact." Twenty-one tests pinning the contract; old logs replay correctly across firmware versions.
- **PRs #367, #369, #373, #378, #382, #384, #385, #387** *Web UI rewrite to Preact.* Server-side `/api/*` JSON endpoints, shared bundle, dev server with replay fixtures, every legacy form route preserved.
- **PR #369** *WebSocket schema pin.* Field-set drift detection via `LiveDataJsonKeys.h` + `test_data_server_json` (5 native tests) + `tools/web/test/api-schema.mjs` (~129 JS-side invariants). See §1 for what's still missing (the codec-from-struct).
- **PR #373** *Cal wizard ported to Preact + `CalwizSave.{h,cpp}` + `test_calwiz_save_diff`.* Pure mutation logic in core, byte-identical state vs. legacy path. Closes the §4 EKF6 bug functionally.
- **PR #345** *External-facing WebSocket protocol reference page.* Published at `dev.flyonspeed.org/reference/websocket-protocol/`. Names every wire field, type, units, source. The wire is now a public contract, not a private implementation detail.
- **PR #380** *`tools/onspeed_py/` shared Python module.* `LiveSnapshot`, `Frame`, `FlapSetpoints`, `ComputeLiftFraction`, config parser, log replay. Cites this doc by name; designed against §1's `LiveDataFrame` shape.
- **PRs #386, #407, #413** *Wire v4.23 versioned bump.* `percentLift` becomes tenths-of-a-percent (sub-percent resolution on the indexer); `lateralG` becomes body-frame. Frame size 76 → 77 bytes. **Coordinated flash required (M5 + Gen3 box) — versioned wire discipline working.** Issue #402 captures wire-version-field design for the next breaking change.
- **PR #391** EFIS engine + time-of-day fields routed through `EfisFrame` for D10/G5/G3X/MGL. Narrows the surface DataServer reads from EFIS globals (though doesn't fully fix §2).
- **PRs #394, #395, #404, #405, #409, #410, #411, #414, #415, #416, #420** *X-Plane plugin maturity.* Plugin embeds the M5 indexer, audio is spec-conformant, derives `pipPctLift`/`gOnsetRate` from datarefs, persists state per aircraft, 660 KB binary. The "X-Plane plugin proves the model" claim is now substantially stronger than when this doc was first drafted.
- **PR #357** BootDiagnostics: panic coredumps archived to SD with one-line summary. Field diagnosis is a one-line email instead of bench work.
- **PR #377** Raw flap-pot ADC in the log. Replay tools can reproduce smooth-sliding pip across detents.

**v4.22.1 (May 6, 2026)** — V4B EFIS RX pin fix (#428).

**Post-release work (May 6 → May 18, 2026) — no new release tag yet, but substantial architectural and reliability work:**

- **PR #526** *One renderer family for `/indexer` and `/replay`.* Deleted `tools/web/lib/modes.js`. Both pages now render through `packages/ui-core/components/svg/m5modes/`, each with its own adapter producing a canonical `M5State` (`packages/ui-core/state-shape.js`). Live: `wsRecordToState(record, snapshot, gRing)`. Replay: `M5Sim.read() → M5State`. **This is the canonical-shape-plus-adapters-plus-one-renderer pattern this doc has been advocating, in production on the web side.**
- **PR #530** *Silent data-loss closed in the SD writer.* Three independent paths fixed: short-write data loss (`m_hLogFile.write()` return value honored at all sites via `onspeed::log::ConsumeAlignedWrite`), BYTEBUF drain-loop overflow (switch to `RINGBUF_TYPE_NOSPLIT` + 24-byte carryover), producer-side pause during `/api/logs` (removed `PauseGuard`). Adds `paused_drops` / `overflow_bytes` / `imu_lateMaxUs` instrumentation and microsecond-resolution `timeStampUs` log column. Bench: Vac's 2.07% missing-rows rate → 0%. **The kind of cross-task concurrency bug the LiveDataFrame snapshot discipline is designed to prevent.**
- **PR #501** *`/format` reliable, xWriteMutex fairness, silent config-save failures visible.* 64 GB card format used to leave no `onspeed2.cfg` because of a long-blocking no-yield loop pre-erasing 256 K blocks at 10 MHz, triggering brownout mid-format. Post-format save is now visible via `cardSizeGb` field in `/api/format/status` and an orange warning when save returns false.
- **PR #543/#544/#545/#548** *Replay tooling matures into a full data pipeline.* FlySto-style HUD with pitch ladder / IAS-ALT tapes / slip ball / G readout, burned into MP4 export, clip timeline visualization, hold-last-good m5State during sim re-init, dual inset slots for left/right M5 modes. All consume `LogRow` through the canonical `M5State` adapter and render through the shared `m5modes/` family from PR #526.
- **PR #559** *Bench stress-test protocol formalized.* `tools/bench/stress_web_handlers.py`, plus public docs at `docs/site/docs/contributing/bench-testing.md`. This is the protocol that found PR #530 and #501's bugs. CI-automated half (self-hosted runner with USB V4P) tracked in #553.
- **PR #355** WebSocket double-cleanup heap-poisoning panic on client disconnect — closed.
- **PR #493/#555** Indexer StaleOverlay actually renders when feed goes stale; threshold dropped 3 s → 0.3 s.
- **Issue #366 closed (2026-05-17)** — the cal wizard EKF6 bug from §4 of this doc is closed. PR #373 fixed it in code in v4.22; the ticket bookkeeping caught up after the May 6 reconciliation noted the gap.

**Two more days (May 18 → May 20) — the upstream pipeline lands:**

- **PR #590** *AHRS prep refactor: four-stage pipeline.* See **"The pipeline is now real upstream"** above. This is the architectural news. `onspeed::ahrs::Madgwick` and `EkfqPipeline` algorithm wrappers each own their internal pre-filtering / IAS gating / comp-fade ramp; `Ahrs::Step()` becomes pure stage dispatch. Companion commit: log writer now always emits raw IAS/AOA/DerivedAOA (no validity-blanked cells); validity gating moved to display-consumer side via new `iIasDisplayThresholdKt` config. *This decouples sensed-value from displayed-value at the right layer.* Regression harness extended to score per-algorithm goldens (Madgwick + EKF6 + replay_engine + synth_adc), 200 rows match each.
- **PR #591** *AHRS: replace EKF6 with EKFQ (11-state quaternion EKF).* State vector grew 6 → 11: `[q0..q3, bp,bq,br, z, vz, b_az, β]`. **Alpha is no longer in the state vector** — EKFQ derives AOA via kinematic formula post-step: `α = atan2(sin θ, cos φ cos θ) + asin(corr)` from current (φ, θ, vz, TAS, β). When EKFQ is the active algorithm, the standalone `KalmanFilter` (Stage 3c) is bypassed; EKFQ produces z/vz directly from the state. Adds sideslip-β as a tracked quantity — a new dimension the firmware estimates that wasn't there before.
- **PR #595** *EKFQ: Optuna substrate — tune C++ directly via host_main.* Promotes `EkfqPipeline`'s pre-filter constants to a `PipelineConfig` struct; adds `EkfqConfigKv` parser for per-trial overrides; `host_main ahrs_tone --algorithm ekfq --config X.cfg` consumes real SD logs via `BuildHeaderIndex`. **New `replay::LogRowToAhrsInputs` bridge:** canonical LogRow → AhrsInputs translation that any future replay caller can use. **This means the AHRS pipeline is now an offline-tunable unit-under-test, scored against recorded flight data.** New customer for Stage 5 (`LiveDataFrame`): if the post-AHRS sink path is similarly clean, host_main can score sink-rate behavior the same way it scores AHRS-rate behavior — wire bytes, displayed values, audio decisions, all from a recorded log.
- **PR #569** *M5 Data Source menu (AUTO/UART/USB), remove SerialRead late-binding.* Explicit `g_dataSource` NVS-persisted menu choice locked at boot. The data-source-adapter pattern at the sketch boundary — small but ratifies the model.
- **PR #582** *Per-flight log cards with sidecar rollup + coredump surface.* Sidecar rollup merges `.dbg` and `.meta` into parent CSV entries; coredump enumeration via new `SdFileSys::DirList()`. UX, mostly orthogonal to pipeline shape.
- **PR #580** *Compressible-flow CAS in `PitotPsiToIasKt`.* Lapse-rate-corrected indicated airspeed. Extends the sensor stage's airspeed semantics.
- **PR #585** AOA smoothing default 20 → 10. Config-only.
- **PR #587/#588** Log download gzipped, log cards sorted newest-first with active log pinned. UX.
- **PR #568** VN-300: derive wind columns (speed/direction/vertical) on-board.
- **PR #575** Align display-OAT gating across M5 wire and JSON (closes #361). Removes a JSON-vs-M5 disagreement of the kind §1 of this doc complains about.
- **PR #572** Delete positional `ParseRow`; consolidate onspeed_core unit helpers. Cleanup.
- **PR #561** Post-#501 fixups (serial FORMAT TWDT, StaleOverlay flicker, sidecar guard).

**Three more days (May 20 → May 23) — task isolation and the snapshot pattern sneak into production:**

- **PR #622** *Extract EFIS + boom UART reads to dedicated tasks.* The architectural news of this cluster. EfisReadTask: Core 0, prio 3, IDF UART event queue (wake-on-data, no polling), 2 KB IDF buffer. BoomReadTask: Core 0, prio 3, 5 ms poll cadence, 128 B HW FIFO. **The upstream streams are now genuinely independent tasks with their own scheduling, not just structurally distinct types.** Replaces the previous `loop()`-drains-three-UARTs-serially pattern.
- **PR #639** *Per-task PERF rings, esp_timer synth, task-notify wake.* Each task gets its own SPSC ring; EFIS gets 2048 entries (16 KB), boom gets 512. The default 256-entry ring was overflowing under bursty wake patterns and silently dropping events. Result: PERF instrumentation is now per-task at high fidelity.
- **PR #618** *Synthetic EFIS + boom streams for perf-synth env.* esp_timer callback injects fake VN-300 + boom frames into the actual task code path. **This is the bench infrastructure the rate-bump exploration (208 → 416/832 Hz) needs.** Synth rates configurable.
- **PR #634** *`LogSensor::Write::snapshot g_AHRS reads under xAhrsMutex` (closes #520).* The §2 discipline showing up in production — narrowly scoped to LogSensor, 8 word loads under mutex, but the *pattern* is right. This is the surgical version of LiveDataFrame, applied at the one place that most needed it.
- **PR #625** *Move `FormatRow` off Core 1 onto writer task.* Producer captures `LogRow` struct; consumer (writer task) calls `FormatRow()` and writes. **Producer/consumer split shipped at the SD log seam, with the struct as the carrier.** This is the LiveDataFrame architecture, applied to log-writing, with `LogRow` as the struct. Each new sink built this way reuses this pattern.
- **PR #606** *LogCsv fast formatters in FormatRow hot path (~17% Core 1 win).* Cleaned up hot-path formatting cost.
- **PR #614** *EFIS parsers hot-path speed optimizations (8 stacked wins).* Garmin/Dynon/MGL parsers' fast path.
- **PR #630** *Move static-pressure read off ImuReadTask mutex.* IMU task's xSensorMutex hold shrunk ~270 µs → ~67 µs. Critical for any future IMU rate-bump.
- **PR #635** *Per-CS SPI clocks (HSC 800 kHz absmax, IMU 8 MHz).* IMU SPI bandwidth is no longer a constraint up to ~832 Hz.
- **PR #623** *Bump SD SPI clock 10 → 20 MHz (-4pp Core 0).*
- **PR #605** *PERF telemetry — SPSC rings + histograms over USB serial.* Per-task histograms cross-version-diffable.
- **PR #612** *Standardized PERF report capture* (skill + tooling).
- **PR #626 / #615 / #611** *Per-task ring sizing, event buffers in PSRAM, TLS slot 1 → registry.* The infrastructure underneath #639.
- **PR #598** *DisplaySerial stops broadcasting binary #1 frame on WebSocket.* Removes a since-misnamed broadcast path; small cleanup.
- **PR #599** *Re-init `g_EfisSerial` when EFIS type changes via web UI.* Closes a config-drift bug.
- **PR #601** *Cal wizard numerical regression test for `analyzeDecel` (Phase A of #219).* The cal-wizard pipeline gets a real test fixture.
- **PR #619** *X-Plane: smooth on-ground → in-flight percent-lift transition.* Plugin polish.

**Four more days (May 23 → May 27) — the concurrency primitive lands in code review:**

- **PR #653 (open)** *`SnapshotPublisher<T>` — lock-free single-writer seqcount snapshot.* The concurrency primitive this doc has been pointing at, generalized from #645's design sketch into a header-only template. ~250 lines of code + 9 native race tests. No callsite migrations; pure infrastructure. **This is step 1, commit 1.** See "Step 1 in flight" section above for the merged design (per-stream `SnapshotPublisher<T>` primitive + `LiveDataFrame` composition view on top).
- **PR #647** *416 Hz IMU rate option (experimental opt-in).* Adds the IMU-rate-bump option the May 23 reconciliation discussed. 50 / 208 are bit-identical to production; 416 is the only mode that bumps IMU + AHRS together. Bench-tested at 416 Hz under synth load + aggressive web stress with 100% CSV capture. PR description explicitly notes: *"once 416 validates in flight we expect to flip IMU to always-416 and let `iLogRate` become a pure log-cadence selector — but that flip needs #645 (LogProducerTask with atomic AHRS/sensor snapshots) to land first."* The architecture is gated on #645 (which is gated on #653).
- **PR #646** *Universal writer + web-mutex hardening.* 32 KB PSRAM staging buffer, 8 KB size gate + 100 ms age gate, 1 MB PSRAM ring. Bounded `xAhrsMutex` holds in web handlers (was `portMAX_DELAY` → now 100–1000 ms with 503 fallback). Writer pipeline is now rate-agnostic — works correctly at any iLogRate from 1 Hz to 416 Hz. **This is the writer-side prerequisite for the rate-decoupled producer that #645/#653 enable.**
- **Side-effects from #647** captured several bench-surfaced bugs: `Serial.end()` chip wedge fix; new `imu_lateMaxUsAT` PERF counter that survives per-window resets; ImuReadTask deferred to end of `setup()` so WiFi softAP comes up first.

**The pattern.** v4.21 fixed the math; v4.22 fixed the surfaces around the math. The 12 days through May 18 fixed the reliability around the surfaces and ratified the canonical-shape pattern on the web side. The two days through May 20 ratified the upstream half of the pattern in firmware code (four-stage AHRS) and turned the AHRS step into an offline-tunable unit. The three days through May 23 made the upstream streams genuinely independent tasks. The four days through May 27 landed the writer-side rate-agnosticism (#646), the experimental high-rate IMU option (#647), and — in code review — the lock-free concurrency primitive (#653).

**Four more days (May 27 → May 31) — atomic-publish at EFIS and boom, bench-validated at 400/416 Hz:**

- **PR #656** *VN-300 at 400 Hz + atomic-publish architecture for EFIS / boom.* The architectural milestone of this cluster. `EfisSerialPort` and `BoomSerial` now build decoded data into a stack-resident staging struct, then atomic-publish via single mutex-protected memcpy. Consumers call `SnapshotEfis(out)` / `SnapshotVn300(out)` / `Snapshot(out)`. Per-stream dedicated mutexes (`xEfisDataMutex_`, `xBoomDataMutex_`) — not the global `xAhrsMutex`. **Bench-validated: 416 Hz IMU + 400 Hz VN-300, 21 minutes, zero tears across 528,180 rows, zero drops, zero reboots.** Compare to Vac's pre-#656 branch on the same stress: 5 reboots, 35% Lat/Lon tear rate. Plus: bulk-read EFIS (51% → 7% Core 0), gzip on dynamic pages (67 KB → 23 KB), VN-300 binary protocol with per-sample timestamps, double-precision Lat/Lon through CSV.
- **PR #648** *Bench-stress lessons from 416 Hz / VN-300 cycle.* Skill-level documentation of failure signatures, anti-patterns, and reference tables learned from running the bench-stress protocol on the pre-#656 PR stack. WiFi heap fragmentation, mid-run CSV gap clusters, per-window PERF counter limitations.
- **PR #652** *Follow-ups to #647 (HWCDC comment, upload-path reboot prompt).* Small.
- **PR #653** *(still open).* The seqcount `SnapshotPublisher<T>` primitive. Not used by #656. **Still applicable to the next migration target (AHRS-output).** See §1 below.

**The deeper pattern.** PR #656 demonstrated that **the atomic-publish-via-memcpy pattern works in production at the bench-validation rates we care about.** The user's "everything is a stream" thesis is no longer aspirational; for EFIS and boom, it's bench-validated production. The seqcount primitive (#653) was the *theoretically* right answer; the mutex pattern was the *pragmatically validated* answer that shipped first. Both are valid; the merged design now needs to absorb that EFIS/boom uses one approach and AHRS-output will likely need the other (because AHRS has multi-writer state and the dedicated-mutex scaling story is uglier there).

What still has *not* landed: AHRS-output snapshot (the remaining stall surface), the rest of the consumer migrations off `xAhrsMutex` (DataServer, LogSensor, DisplaySerial), `LiveDataFrame` composition layer, `LogProducerTask`, OAT/boom re-injection in LogReplay (§3), MAVLink emit (planning step 4), parallel estimators (§6 / planning step 7), and BLE sink (planning step 10). **Two upstream streams are on the pattern; AHRS-output is next.**

## Data sources: what's complete, what's missing

| Source | Status | Notes |
|---|---|---|
| Real sensors (IMU + pressures + ADC + OAT) | ✓ production | `EnSensors` mode |
| EFIS serial (Dynon, Garmin, MGL, VN-300) | ✓ production | parsers in `onspeed_core/src/efis/`, dispatcher in `EfisSerial.cpp` |
| Boom probe serial | ✓ production | parser pure; serial reader in sketch |
| LogReplay (SD CSV) | ✓ since PR #353 | name-keyed parsing; covers IMU, pressures, EFIS, VN-300, AHRS state, flaps. **OAT and boom are parsed into `LogRow` but not re-injected into the runtime** (see gaps below). |
| X-Plane plugin | ✓ standalone | runs core directly; not integrated with firmware |
| Test fixtures (TestPot / RangeSweep) | ✓ bench | hardware-checkout sweeps, not for analysis |
| **Simulator (network bridge)** | **missing** | no protocol defined; no adapter |
| **Synthetic data generator** | **missing** | for offline regression / explorer tools |

LogReplay coverage gaps worth knowing:

- **OAT is logged but not re-injected.** `LogCsv` writes `OAT` (and `efisOAT` when EFIS is configured); `LogRow` carries `oatCelsius` and `efisOatCelsius` after parsing. But `LogReplay.cpp` doesn't restore those values to `g_Sensors.OatC` or to the EFIS struct. Result: replay always sees `OAT = 0`, masking any OAT-dependent code path.
- **Boom probe data is the same story but louder.** `LogCsv` writes the six boom columns, `LogRow` carries them, the header-index detector flips `s_bReplayBoom = true` when it sees `boomStatic` — but the row's boom fields are not assigned anywhere in `LogReplay.cpp`. The boom CRC, framing, and freshness logic is bypassed during replay because the boom signal doesn't even reach the parser. Fine for AHRS/AOA verification, not fine for "did the boom protocol survive a firmware change?"
- **EFIS freshness is not preserved.** Replayed EFIS frames appear instantaneously fresh; `IsDataFresh(2000)` always returns true. Not a correctness bug for AHRS replay, but it masks any bug that depends on stale-data fallback paths.

These gaps say: **the SD log is good enough for AHRS/AOA regression testing today, and not yet good enough to be the canonical "flight reproduction" data source.** Closing the gaps is straightforward — add OAT to the log, route boom samples through the parser during replay, optionally annotate frame timestamps for freshness simulation.

## Sinks: what's complete, what's missing

| Sink | Format | Schema enforcement |
|---|---|---|
| Audio I2S (M5 + box) | PCM samples synthesized by `ToneSynth` | N/A (continuous signal) |
| DisplaySerial wire (M5) | 74-byte binary frame | strong: pure encode/decode in `proto/`, round-trip tested |
| WebSocket JSON (browser) | hand-rolled JSON | **none** (#363's root cause; see "Where we fall short §1") |
| SD log CSV | named columns, optional groups | strong since PR #353 |
| EFIS RS-232 emit | not implemented | OnSpeed reads EFIS, does not transmit |
| HUD frame (planned) | not designed | will be a new `proto/` codec when needed |

Two of five are fully schema-controlled (DisplaySerial, SD log). The WebSocket is the obvious next target.

## Prior art: what adjacent industries already settled on

The "AHRS plus air-data plus calibration produces a stream that drives indicators and audio" pattern is not novel. Several adjacent communities have converged on named, versioned wire protocols with codegen and ground-station tooling that already does the things we are reaching for. Naming them is useful because it tells us **what to import rather than reinvent**, and it gives us a measuring stick — *if our wire format can't render in QGroundControl, why not, and what do we lose?*

### MAVLink (UAV autopilots)

The closest match. MAVLink is the lingua franca for ArduPilot, PX4, Mission Planner, QGroundControl, MAVSDK, and the entire small-UAV ecosystem.

- **`ATTITUDE` (msg 30)**: pitch, roll, yaw, gyro rates. Streamed at configurable rate over UDP/TCP/serial.
- **`AOA_SSA` (msg 11020, ArduPilotMega dialect)**: angle of attack and sideslip in degrees, with microsecond timestamp. 16 bytes. Already supports vane-type sensors and pitot-derived estimates.
- **`AIRSPEED`, `VFR_HUD`, `RAW_IMU`, `SCALED_PRESSURE`, `GPS_RAW_INT`**: every signal we ship is already a named MAVLink message.
- **Schema discipline**: messages are versioned, dialected (`common.xml` plus per-vendor extensions), with codegen for C, C++, Python, Rust. Adding a field means a deliberate schema edit, and old clients keep parsing the messages they understand.
- **Rate control**: `SET_MESSAGE_INTERVAL` per message, or `SRx_*` parameter groups.
- **Ground stations**: Mission Planner and QGroundControl already render the pitch ladder, airspeed indicator, and attitude gauge we're building piecemeal in `/indexer`.
- **Sim integration**: PX4 SITL and ArduPilot SITL run autopilot code against JSBSim or Gazebo physics over MAVLink. The autopilot can't tell whether the IMU bytes came from real silicon or simulated. This is the data-source abstraction we're sketching, fully realized.

### X-Plane RREF and FlightGear Generic Protocol (sim → external client)

Both major flight sims publish telemetry over UDP specifically so external hardware and software can subscribe. OnSpeed's X-Plane plugin already lives in the X-Plane half of this world; FlightGear's protocol is interesting as a model.

- **X-Plane RREF**: subscribe to a dataref by name, receive a stream of `(int index, float value)` pairs at your chosen rate.
- **FlightGear Generic Protocol**: write an XML file declaring exactly which `/orientation/pitch-deg`, `/velocities/airspeed-kt`, `/orientation/alpha-deg` properties you want; the sim emits them as UDP at your rate. **The consumer authors the schema, the producer conforms** — the cleanest schema-as-contract example among these protocols.

### GDL90 (avionics → EFB tablets)

A closer cousin to OnSpeed: an on-aircraft device broadcasts AHRS + GPS + ADS-B over WiFi UDP to consumer EFB apps on a tablet. Standard: FAA GDL90 ICD plus ForeFlight's published extensions.

- AHRS message at 5 Hz, framed binary, UDP unicast to port 4000.
- Implementations: Stratux (open source ADS-B + AHRS receiver), SoftRF, Garmin GDL series, uAvionix Sentry.
- Consumers: ForeFlight, Garmin Pilot, FlyQ, AvNav, Naviator, FltPlan Go.
- **Limitation for our purposes**: GDL90 does not carry AOA natively; would require a vendor extension. Lower priority than MAVLink unless we specifically target the tablet-EFB market.

### DCS-BIOS / MSP OSD (sim → DIY cockpit hardware)

The cottage-industry existence proof for "sim telemetry drives a hardware indexer." DCS-BIOS bridges DCS sim state to Arduino over serial; hobbyists build F/A-18 AOA indexers (four LEDs: too-fast / on-speed / on-speed / too-slow) that consume the stream. MSP OSD is the equivalent in the FPV racing-drone world. Same architecture as ours, smaller scale, longer history.

### Transport: WiFi vs. Bluetooth

The wire-format choice (JSON, MAVLink, GDL90) is independent of the transport (WebSocket, UDP, BLE GATT). Worth making the separation explicit because Bluetooth keeps coming up as a "what if the pilot's phone connected over BT instead of WiFi" question, and the answer has real constraints worth knowing up front.

**Hardware floor.** The ESP32-S3-WROOM-2 supports **BLE 5.0 only — no Bluetooth Classic (BR/EDR)**. Classic Bluetooth Serial Port Profile (SPP) and RFCOMM are not available on this silicon, regardless of software effort. Anything that wants to be a "drop a BT dongle on your laptop and run Mission Planner" link is off the table on this hardware. (The original ESP32 had Classic; the S3 dropped it.)

**iOS reality.** iPhone and iPad **do not support generic Bluetooth SPP without MFi certification**. ForeFlight, Garmin Pilot, FlyQ, and the rest of the GDL90-consuming EFB ecosystem connect over **WiFi UDP exclusively**. There's no realistic path to "OnSpeed feeds AOA to ForeFlight over Bluetooth" without becoming an MFi licensee, which is a different scale of project. WiFi is the answer for tablet-EFB integration.

**Where BLE actually fits.** Custom Android or iOS apps written by us (or by anyone willing to build a BLE client) can pair with OnSpeed and consume telemetry over GATT — characteristics for `attitude`, `aoa`, `airspeed`, etc. BLE is not a serial pipe; it's a request-and-notify model with bounded throughput (~100 kbps practical) and a custom chunking layer on top. This is a real engineering exercise — not a free transport swap — but it's the path if we want a phone-paired indexer that doesn't require the pilot to join a WiFi network.

**Implication for the architecture.** Bluetooth doesn't change the schema work. A `LiveDataFrame` snapshot in DataServer (planning step #1) is the same struct whether the next sink encodes it as WebSocket JSON, UDP MAVLink, or a set of BLE GATT characteristics. **Transport is a sink-adapter concern, not a wire-format concern.** When BLE happens, it lands as a new module in `sketch_common/` that subscribes to the snapshot frame and emits notifications, paralleling DataServer.

For the foreseeable near term, **WiFi is the right answer**: it works with iOS EFBs, it works with QGroundControl on a tablet, and the pilot only joins one network anyway. BLE is interesting later, particularly for a phone-paired wearable display (HUD glasses) or a dedicated OnSpeed companion app where a custom GATT contract is acceptable.

### What this implies

Three takeaways that should inform every wire-format decision we make from here:

1. **The contract we're sketching is not novel.** Every system that does AHRS-plus-air-data-plus-displays has converged on "named, versioned messages over UDP, codegen'd into multiple language bindings, with ground-station tooling per use case." OnSpeed reinventing this from scratch is what gave us issue #363 and the PR #353 retrofit.

2. **MAVLink in particular is a credible "import this protocol" candidate.** Not as the only wire format — DisplaySerial and the audio path stay where they are — but as a *second* output that opens a large door:
   - **Emit MAVLink:** OnSpeed firmware producing `AOA_SSA` + `ATTITUDE` + `VFR_HUD` over UDP becomes visible to Mission Planner, QGroundControl, MAVLink Inspector, MAVSDK clients, OpenHD, and any of the dozen tablet ground stations in the UAV ecosystem. The pilot's cockpit tablet running QGC sees OnSpeed's AOA in the HUD with zero custom client code.
   - **Consume MAVLink:** OnSpeed running from a Pixhawk's IMU bytes — or PX4 SITL output, or ArduPilot SITL — validates our AHRS against off-the-shelf flight-stack ground truth. Cross-vendor regression testing for free.
   - **Bridge LogReplay → MAVLink → another OnSpeed:** replay a flight as a MAVLink stream, point a second OnSpeed instance at it, verify identical tone decisions. Same regression idea PR #353 enabled, larger surface.

3. **If the indexer consumed MAVLink, it would work against any MAVLink source.** Today `/indexer` reads OnSpeed's bespoke WebSocket JSON. If the indexer code consumed an `ATTITUDE` + `AOA_SSA` + `VFR_HUD` stream instead — same Preact frontend, different transport — the same indexer would render against:
   - Real OnSpeed firmware (after MAVLink emit lands)
   - A real Pixhawk-based UAV
   - X-Plane via mavlink-router
   - ArduPilot SITL output during regression tests
   - A recorded MAVLink log file replayed via `mavproxy --master=tlog:flight.tlog`
   - Anyone else's MAVLink-speaking system

   The indexer becomes **a generic AOA HUD client**, not an OnSpeed-specific one. That is the kind of leverage that justifies adopting the standard.

**Honest limits.** Adopting MAVLink doesn't solve everything. The audio path — tone selection, pulse rates, the `iMuteAudioUnderIAS` logic — is OnSpeed-specific and has no standard message. We'd either define a vendor-dialect message (`ONSPEED_TONE_STATE`?) or accept that the audio decision stays firmware-internal and only the displayable signals cross the wire. Configuration (per-flap setpoints, calibration curves) is similarly bespoke; MAVLink has parameter messages but the OnSpeed config schema is richer than a flat key-value store. Neither limit kills the value: the displayable signals are most of what an external consumer wants.

## What changes when we add a new ___

These are the discipline questions to ask. If the answer to any of them is "edit a global address book" or "duplicate the unpacker", the model is being violated and the right fix is a small refactor before, not a workaround during.

### When we add a new data source

- What schema does it produce? (name it, version it, test the round-trip)
- What adapter converts it to the internal frame format?
- Does the existing distributor function accept this frame, or do we need to invent the distributor?
- Does the source carry the fields LogReplay carries today? If not, what calibration / sensor state is being mocked?

### When we add a new sink

- What schema does it consume? (codec lives in `onspeed_core/src/proto/`)
- Does it take a snapshot frame as input, or does it read globals? (the answer is the former)
- Round-trip test in the codec module covers the format

### When we add a new estimator

- Stateful object (instance, not global)
- Named in a `…Estimates` struct alongside the existing estimators
- Consumers take `const Estimates&`, not the scalar from a global
- Test exercises agreement-when-coordinated, divergence-when-not against fixture data

### When we change a wire format (JSON, log CSV, display serial)

- Bump a schema version
- Update the codec's round-trip test
- Update every consumer (or fail at compile time / startup, not at runtime months later)
- The two times we have learned this lesson the hard way are PR #353 (logs) and PR #365 (JSON). Both fixes were small. The bug surfaces were months long because nothing complained when the schemas drifted.

## Relationship to other planning

This document defines the *shape*. Detailed sequencing belongs in its own plan. The list below is the rough order of leverage, with explicit notes on **where we should adopt an existing standard rather than invent our own**.

### 1. `SnapshotPublisher` + `LiveDataFrame` + `LiveDataView` — **in flight (PR #653 is commit 1)**

**Status update (2026-05-27):** PR #653 is in code review and implements commit 1 of this step — the `SnapshotPublisher<T>` lock-free seqcount primitive. The full step is now a sequenced series of commits per the merged design (see "Step 1 in flight" section above). The composition layer (`LiveDataFrame`) and view accessor (`LiveDataView`) build on top of #653's per-stream snapshots; each consumer migrates off `xAhrsMutex` in its own follow-up PR.

**Why the design changed from earlier reconciliations:** Previous versions of this doc proposed a mutex-based composed snapshot. That was wrong. PR #645 (`LogProducerTask` architecture) made the case that the right primitive is *lock-free per-stream snapshots* — the writer (e.g., `ImuReadTask`) is never blocked by a slow reader (e.g., a web handler holding `xAhrsMutex` while the SD card is busy). PR #647's bench data showed 18-20 ms IMU stalls under web load with the mutex pattern; the seqcount primitive eliminates that contention entirely. **`LiveDataFrame` is now the composition layer over per-stream lock-free snapshots, not a single mutex-protected composite struct.**

Build the snapshot at the sink boundary as a *composed* type — directly holding the existing per-stream snapshot structs (`ImuSample`, `SensorSample`, `AhrsOutputs`, `EfisFrame`, `FlapState`, `BoomFrame`) — and pair it with a `LiveDataView` accessor layer that exposes selection-policy fusion (`PreferEfis`, `PreferInternal`, `EfisOnly`, `InternalOnly`) and freshness gating to consumers. Fixes §2 (the 25-global address-book reader) by making the snapshot the one place fusion logic lives, instead of the four ad-hoc per-consumer copies that exist today (see "Why the view layer matters" below).

**Shape:**

```cpp
struct LiveDataFrame {
    uint32_t snapshotTickUs;     // when BuildLiveDataFrame() ran
    ImuSample      imu;          // 208 Hz; existing type, has timestampUs
    SensorSample   sensors;      // 50 Hz;  existing type, has timestampUs + iasAlive
    AhrsOutputs    ahrs;         // 208 Hz; existing type, has timestampUs
    EfisFrame      efis;         // ~50 Hz; existing type, needs timestampUs added
    FlapState      flaps;        // 1 Hz;   existing type, needs timestampUs added
    BoomFrame      boom;         // ~10 Hz; existing type, needs timestampUs + present added
    DisplayAnchors displayAnchors;  // derived at snapshot time from flaps + config
    DataMarkState  dataMark;        // event-driven counter + last-increment timestamp
};
```

Roughly 320 bytes total. Five of the eight fields are existing types reused unchanged; three need a `timestampUs` (and `present` flag for boom) added for symmetry. `DisplayAnchors` and `DataMarkState` are new but small (~30 lines combined).

**Accessor layer:**

```cpp
class LiveDataView {
    const LiveDataFrame& f_;
public:
    std::optional<float> pitchDeg(Source policy = Source::PreferInternal) const;
    std::optional<float> aoaDeg() const;
    std::optional<float> boomAlphaDeg(int maxAgeMs = 500) const;
    // ... etc.
};
```

The view collapses the four existing ad-hoc fusion sites (DataServer's `bCalSourceEfis` if/else, DisplaySerial's always-internal read, the audio path's IAS-mute gate, the cal wizard's wire-driven inheritance) into one named, testable, policy-parameterized layer. Today's "the JSON broadcaster picks per-field for everyone" silently disagrees with "the M5 always shows internal AHRS"; under the view, each consumer states its own policy explicitly.

**Why the rate-aware composition matters:**
Today's implicit cross-rate merge is "every sink at 20 Hz reads the most recent value of each upstream signal regardless of that signal's own rate." Three sinks implement this independently. The composed `LiveDataFrame` makes the merge structural (each sub-stream carries its own timestamp; the snapshot tick is separate) and named (one `BuildLiveDataFrame()` function, mutex policy localized). Consumers that don't care about freshness ignore the timestamps; consumers that do care (cal wizard's sample-pairing, future replay correctness, parallel-estimator agreement checks) reach into the relevant sub-struct.

**Starting state as of 2026-05-18 — the pattern is ratified everywhere except C++:**
- **Python side** (`tools/onspeed_py/live_snapshot.py`, PR #380): declares `LiveSnapshot` explicitly, citing this doc by name. Shape is settled — names, units, comments.
- **JS side** (`packages/ui-core/state-shape.js`, PR #526, shipped post-v4.22.1): declares `M5State` as the canonical struct produced by every adapter (live `wsRecordToState`, replay `M5Sim → state`) and consumed by the unified `m5modes/` renderer family. Includes the cadence convention (`display*` fields = 500 ms snapshot, others = 20 Hz wire frame) and validity gating (`IasIsValid`). **This is the design pattern the doc has been advocating, shipped on the web side.**
- **Wire schema** (`software/Libraries/onspeed_core/src/api/LiveDataJsonKeys.h`, PR #369): name-level field-set pin, exercised in 5 native tests + ~129 JS invariants.
- **Sibling shape**: `DisplayBuildInputs` in `proto/DisplaySerial.h` is the precursor flat shape; the M5 wire encoder migrates to take `LiveDataFrame` (or a flattened projection of it). The X-Plane plugin already exercises the projection pattern through `DataRefAdapter::BuildInputsFromDatarefs()`.
- **Snapshot methods**: `Snapshot()` on `g_pIMU` and `g_Sensors` already exist and return POD copies. Extending the pattern to `g_AHRS`, `g_Flaps`, `g_EfisSerial`, `g_BoomSerial` is mechanical (~10 lines each).
- **Existence proof for the snapshot-discipline payoff**: PR #530 (post-v4.22.1) closed three silent-data-loss paths in the SD writer — exactly the kind of cross-task concurrency bug the snapshot pattern prevents. The discipline isn't speculative; it's already paying for itself in adjacent reliability work.

The C++ side is now the *only* remaining place where the shape isn't ratified. The Python and JS sides both committed; the firmware is the lagging piece.

**Performance check (verified, not hand-waved):**
- Memory: ~320 bytes per frame, 0.06% of SRAM. Holding a ring of 4 frames for derivative work is 1.3 KB.
- CPU per snapshot: ~200 ns memcpy + mutex take/give (~1–2 µs uncontended). At 20 Hz that's ~200 µs/sec total. **0.02% CPU.**
- JSON wire growth from adding 5 timestamp fields: ~50 bytes on a ~330-byte payload; 1 KB/s extra over WiFi at 20 Hz.
- M5 wire: unchanged (encoder reads from sub-structs but emits the same flat 77-byte v4.23 format).
- SD log: unchanged (`LogRow` stays its own shape; `LogRowFromLiveDataFrame()` becomes the bridge if we want unified replay).

The "this is too expensive" reflex doesn't survive the numbers. Memory and CPU are rounding errors; wire-size growth is sub-percent; build time is neutral.

**What this *does not* try to be:**
- Not a Reactive-Extensions-style stream framework. There's no observer pattern, no backpressure, no windowing. The snapshot is a *pull* model at the sink rate.
- Not a Kalman fusion engine across heterogeneous sensors. The view's selection policies are the same model real avionics ships with (ArduPilot's `EK3_SRC*` parameter family, MAVLink's per-component source selection) — pick the source per consumer, with freshness gates and fallbacks. Sophisticated Kalman blending across boom + EFIS + pressure would be an `EKF8`/`EKF10` redesign, separable from this work and not precluded by this design.
- Not a substitute for audio's internal state, configuration, or AHRS-internal covariances. Those have their own homes.

**EFIS sub-struct shape — open design question, deferred:**

`EfisFrame` today is a flat product type with NaN sentinels for "this protocol doesn't carry this field." VN-300 (inertial nav with GNSS + 25-field superset) and the ADAHRS-style protocols (Dynon, Garmin, MGL, ~8 common fields) get stuffed into the same shape. The honest model would be a sum type — `std::variant<DynonFrame, GarminFrame, MglFrame, Vn300Frame>` — where the field set the type system enforces matches what the protocol actually carries.

Considered and **deferred**, with reasoning recorded so we don't relitigate:

- The dominant access pattern is "give me pitch / IAS / palt from whichever EFIS is connected" — common-field reads probably outnumber vendor-specific reads ~20:1. A sum type pushes visit-or-switch boilerplate into every common-field consumer; the wide product collapses it.
- Common fields appear in every variant. The natural de-duplication (factor `EfisCommonFields` out, have each variant *contain* it) converges back toward today's shape. The genuine divergence is one outlier (VN-300 inertial nav) and a cluster of ADAHRS protocols with subset variations — not N truly-different beasts.
- `std::variant` carries a discriminator and sizes to the largest member (~120 bytes for the Vn300 case). `std::visit` introduces toolchain interactions worth verifying on ESP32-arduino's `-fno-rtti` build. Workable, but non-zero ongoing complexity.
- The pattern that probably *does* fit: a **grouped product** — `EfisFrame { EfisCommonFields common; EngineFields engine; InertialNavFields inertial; }` with `present` flags on the optional sub-products. Type-level grouping clarifies what comes from where without forcing visit boilerplate on common-field consumers. Mirrors the `BoomFrame { ...; bool present; }` pattern that already works elsewhere in the codebase.

When to revisit: if we add a sixth or seventh EFIS protocol and the common-field redundancy starts hurting; or when an engine-fields-only consumer (HUD glasses showing fuel and RPM) makes the type-level grouping load-bearing. For step 1 of this plan, `EfisFrame` enters `LiveDataFrame` in its current shape.

**Standard to hew to:** **none externally** — internal scaffolding. But it's the unblocking step for steps 4 (MAVLink emit; the emitter takes a `LiveDataFrame`), 5 (indexer-as-generic-HUD; the indexer's data layer migrates to `LiveDataFrame`), 7 (parallel estimators land as additive accessors on `LiveDataView`, no struct restructure), and 10 (BLE GATT producer takes a `LiveDataFrame`). Cheaper to build with the right shape now than to migrate three sinks later.

### 2. WebSocket JSON schema (small) — **partially done in v4.22**

**Status:** PR #369 landed name-level drift detection. `LiveDataJsonKeys.h` + `test_data_server_json` + `tools/web/test/api-schema.mjs`. The next #363 fails at test time.

**Still missing:** the JSON builder is hand-written `snprintf`, not generated from `LiveDataFrame`. Once step 1 lands, `UpdateLiveDataJson()` becomes a one-line call into a codec; right now it's 25 hand-coded global reads with `SafeJsonFloat` wrappers.

Standard to hew to: **JSON Schema** for the field-set declaration. PR #345's external protocol reference page is the human-facing version of this; a machine-readable `livedata.schema.json` checked into the repo would let tooling (the dev server, future analysis scripts, third-party consumers) validate against the same artifact.

### 3. OAT and boom re-injection in LogReplay (small)

Wire `LogRow.oatCelsius` into `g_Sensors.OatC` and `LogRow.boom*` through the boom serial parser during replay. Closes the two named gaps that prevent SD logs from being a true flight-reproduction data source.

Standard to hew to: **none externally** — this is fixing the contract with our own log format.

### 4. **MAVLink emit (medium-large, unlocks the most external compatibility)**

Add a MAVLink emitter that produces `ATTITUDE`, `AOA_SSA`, `VFR_HUD`, `AIRSPEED`, `RAW_IMU`, `SCALED_PRESSURE` over UDP from the `LiveDataFrame`. Use the official C library (`c_library_v2`) rather than rolling our own framing — the codegen is the schema discipline we're trying to import.

What this unlocks:

- **Mission Planner / QGroundControl as ground stations.** Pilot's tablet running QGC connects to OnSpeed over WiFi UDP and sees the full HUD: pitch ladder, airspeed indicator, AOA gauge, attitude. Zero custom client code, written by a community much larger than ours.
- **MAVLink Inspector for debugging.** Drop `mavlink-inspector` or QGC's MAVLink console on the network and watch every message in real time, with names, units, and field documentation.
- **MAVSDK / pymavlink clients.** Anyone who wants to build analysis tooling, recording, or custom visualization can do it against the same protocol that powers tens of thousands of UAVs.
- **OpenHD compatibility.** OpenHD's digital-FPV system is MAVLink-based; an OnSpeed unit could theoretically feed an OpenHD ground station with no glue code.

### 5. **Indexer as generic MAVLink HUD client (medium, follows from #4)**

Rewrite `/indexer`'s data acquisition to subscribe to MAVLink messages instead of (or alongside) the bespoke WebSocket JSON. The Preact components that draw the indexer don't change; only the input adapter does.

What this unlocks:

- **The indexer becomes a generic AOA HUD.** Same code renders against real OnSpeed firmware, a Pixhawk drone, X-Plane via `mavlink-router`, ArduPilot SITL, or a recorded MAVLink telemetry log replayed with `mavproxy --master=tlog:flight.tlog`.
- **Cross-vendor validation.** Run the indexer against ArduPilot SITL output and visually verify our pitch/roll/AOA rendering matches the reference flight stack.
- **No-firmware demo.** Show OnSpeed's indexer at a fly-in with just a laptop running PX4 SITL — no aircraft, no ESP32, no setup. Recruiting and education tool.

### 6. MAVLink consume (medium-large, unlocks SITL-style validation)

A data-source adapter that reads MAVLink `RAW_IMU` + `SCALED_PRESSURE` over UDP and feeds the AHRS. The autopilot world's SITL pattern, applied to OnSpeed.

What this unlocks:

- **OnSpeed firmware running against ArduPilot SITL or PX4 SITL.** Validates AHRS, AOA calc, and tone decisions against an off-the-shelf flight stack's ground truth, on every commit.
- **Bench testing without an aircraft.** A laptop running JSBSim / PX4 SITL → MAVLink → OnSpeed firmware on a dev board → real audio out the speakers. Stalls, AOA pulls, calibration runs — all reproducible at a desk.
- **Real Pixhawk passthrough.** An OnSpeed box could in principle take its IMU bytes from a Pixhawk's MAVLink stream rather than its own ICM-42688, blurring the line between "OnSpeed unit" and "OnSpeed software riding on someone else's hardware."

### 7. `AoaEstimates` struct + parallel estimators (largest)

Multiple AOA estimators running in parallel: pressure-polynomial (today's operational signal), EKF6 alpha state, Madgwick θ−γ, boom probe alpha, live IAS-to-AOA fit residual. Consumers take `const AoaEstimates&` instead of the scalar from `g_Sensors.AOA`. Foundation for live calibration monitoring and eventual auto-calibration.

Standard to hew to: **none externally** — this is novel territory. But step #4 makes it observable: each estimator can be its own MAVLink message (or extended fields in `AOA_SSA`-derived dialect messages), so the agreement-or-divergence between estimators is visible to any ground station.

### 8. Simulator network bridge (medium)

Once steps #4 and #6 land, the "simulator data source" is just MAVLink consume pointed at PX4 SITL or X-Plane. No bespoke protocol needed. Falls out of the standard, doesn't need its own design.

### 9. (Maybe) GDL90 emit for tablet-EFB compatibility

Lower priority unless we specifically target ForeFlight / Garmin Pilot users. GDL90 doesn't carry AOA natively, so the value is mostly attitude and groundspeed bridging. Defer until there's a concrete user ask.

### 10. (Later) BLE GATT sink for phone-paired display

Independent of all wire-format work. When we want a phone-paired indexer that doesn't require the pilot to join a WiFi network, add a BLE module that subscribes to the same `LiveDataFrame` snapshot and exposes attitude / AOA / airspeed as GATT characteristics. Constrained by ESP32-S3-WROOM-2's BLE-only radio (no Classic SPP) and iOS's MFi gate (no SPP without certification), so this is a custom Android-or-companion-app path, not a "ForeFlight over Bluetooth" path. See the Transport section in Prior Art for the constraints.

---

Each step is independently reviewable and shippable. None require a top-down rewrite. The pattern they share is **import the schema discipline that adjacent communities have already worked out**, and reserve our own design effort for the parts that actually are OnSpeed-specific (audio decisions, percent-lift math, calibration logic).

The point of this document is to make the discipline explicit so that the *next* PR — whoever writes it — pulls in the direction of the model rather than against it, and reaches for an existing protocol before inventing one.

## Next priority (as of 2026-06-02)

**The worktree is structurally complete; the missing piece is bench validation, not more code.** Six flight-state surfaces now have publishers (four on master, three in worktree — one overlap). The architecture the doc has been describing for two months is **substantially done**. The next moves are about validation, cleanup, and the deferred work that becomes mechanically reachable once the worktree merges.

### Immediate (this week) — the worktree merge sequence

**1. Bench-validate the worktree.** The 17 commits land Flap + Sensor + IMU snapshots but no perf capture exists for the stack. The existing reports in `docs/perf-reports/` are pre-this-work. Run the standard PERF capture at 416 Hz IMU + 400 Hz VN-300 + `stress_web_handlers.py --aggressive` for 15+ minutes against the worktree tip. **Two specific numbers to compare against PR #665's bench:**
- IMU `lateMaxUs` — must not regress from 784 µs (data-integrity goal: snapshot reads shouldn't slow IMU)
- `/api/calwiz/state` p95 latency — should improve materially (contention goal: calwiz used to take `xAhrsMutex` for the flap read; now lock-free)

That measurement is the load-bearing check. Without it, the merge is structural-only and we don't know if PR #647's 18-20 ms IMU stalls actually closed.

**2. Add `tasMps` to the AHRS snapshot payload.** Closes inconsistency A from the coherence-review. EfisSerialPort's wind-triangle currently reads `g_AHRS.fTAS` raw because the snapshot doesn't carry it. Three lines of code; eliminates the one AHRS-output consumer that bypasses the snapshot. Free win.

**3. Stack the worktree as three PRs, not one.** Flap → Sensor → IMU. The 17 commits already decompose this way. Three stacked PRs gives three independent bench validations and distributes review load. Each PR is an architectural increment with its own success criterion.

**4. Commit the narrowed `xAhrsMutex` contract to a header comment.** Yesterday's coherence-review documented the contract in `local-plans/` (local-only). The narrowed responsibility — writer-side AHRS filter state + flap writer-ordering, NO flight-data readers — needs to be stated where future code-modifiers will see it (top of `SensorIO.cpp` or a public design note). Otherwise the next refactor will reach for the wrong mutex by reflex.

### Near-term (next two weeks) — what becomes mechanically reachable after the worktree lands

**5. PR #647's deferred IMU-stall measurement re-run.** With flap reads off `xAhrsMutex` (the worktree's flap-state migration), calwiz API stress should no longer block ImuReadTask. The 18-20 ms IMU stall measurement from PR #647's bench data either closes (and we should mark it closed with a fresh perf report) or reveals a residual contention source we haven't named yet.

**6. The architecture's §3 gap (replay-as-true-flight-reproduction) closes naturally.** The worktree commits include `LogReplay::PublishReplayResult` which publishes all four snapshots together. Once it lands, **a replayed log produces the exact same snapshot stream a live flight produces**. Every downstream consumer is unchanged — the system can't tell the difference. The doc's §3 "OAT and boom not re-injected" complaint becomes obsolete; the worktree already addresses it.

**7. `AoaEstimates` parallel estimators (the doc's planning step #7) becomes mechanically reachable.** With six publishers in production, adding parallel AOA estimators is a matter of new publishers, not a structural change. EKFQ kinematic AOA, pressure-polynomial AOA, IAS-to-AOA fit residual: three new snapshots, each ~40 lines of producer code, consumer-side is whichever monitor wants to flag drift. The "estimators aren't parallel yet" complaint from §6 of the audit converts from "needs structural work" to "needs three small publishers."

### Deferred (acknowledge but don't rush) — what no longer needs immediate attention

**8. Composed `LiveDataFrame` view layer.** The doc has been pushing this for months. With six per-stream publishers in production, every consumer reads its own snapshots. The composed view layer has no required consumer right now. **Defer until the first multi-stream-coherent consumer (likely MAVLink emit) needs it.** When that consumer arrives, the composed view is one helper function (`BuildLiveDataFrame()` reads N snapshots and assembles a single struct). Not blocking anything.

**9. EKFQ predict/correct schedule split.** Still the right path for the high-rate IMU win. Still gated on bench validation of the snapshot stack working at 416 Hz under load — which is step 1 above. Sequence unchanged from prior reconciliation.

**10. `LogProducerTask` at configurable rate.** Still the architectural payoff for PR #647's "always-on 416 Hz" promise. Still requires the snapshot stack landed first. Sequence unchanged.

**11. MAVLink emit / BLE GATT / GDL90.** All consume per-stream snapshots; each is a new writer task that reads N snapshots and emits. **The infrastructure for all three is now in place.** Each is a deliberate product decision, not a prerequisite.

### Recommendation — what the implementing agent needs from this reconciliation

**(1) Bench before merge.** The work is structurally complete; the validation is what's missing. Use the `perf-testing` skill's standardized capture procedure. Compare against `master-2026-05-20.md` and `refactor-with-stim-400hz.md`. The worktree merge should ship with a fresh perf report demonstrating that the snapshot stack does what we hoped: zero data-integrity tears, IMU jitter unchanged or improved, web handler latency improved.

**(2) Stack the merge as three PRs.** Flap first, sensor second, IMU third. Each independently bench-validated. The decomposition is already in the commits.

**(3) Address inconsistency A inline.** `tasMps` to AHRS payload is free. Inconsistency B (ApiHandlers PitchAC under xSensorMutex) and the leftover read C (AHRS::Init seed) can be deferred to follow-up issues.

**(4) Surface the `xAhrsMutex` narrowed contract in firmware code.** Header comment in `SensorIO.cpp` or equivalent. Otherwise the next refactor will reach for it incorrectly.

**Honest framing:** the architectural arc the doc has been tracking is **substantially complete**. Six flight-state surfaces on a single concurrency primitive, all single-writer-per-stream, all lock-free reads, all consumers migrated off `xAhrsMutex` for the flight-data path. The worktree closes the remaining three (Flap, Sensor, IMU). What's left is empirical: prove on bench that the architecture actually delivers the contention reduction we predicted, and the major decoupling work is done. After the worktree lands, every remaining doc-planning item (composed view, MAVLink, parallel estimators, replay-as-source) is an additive new-consumer pattern, not a structural change.
