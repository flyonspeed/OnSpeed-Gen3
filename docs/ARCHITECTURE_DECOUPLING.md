# OnSpeed-Gen3 Architecture: Decoupling Model

This document is a **contract** for how OnSpeed firmware should be structured as we deploy to more environments (real planes, simulators, log replay, synthetic data) and as we add more outputs (M5, browser, HUD glasses, X-Plane plugin). It is also an **honest audit** of where the current code honors the contract and where it does not.

It is not a reorganization plan. Migration sequencing belongs in its own document; this one defines what we are reorganizing toward.

> **Last reconciled with master:** 2026-05-06, against tip `c429013` (post-v4.22.1). Major releases v4.21 (audio engine port, percent-lift honesty, EKF6 correctness) and v4.22 (Web UI rewrite, X-Plane indexer window, wire v4.23) have shipped since the original draft. See **"What's already moving in this direction"** below for the running scoreboard, and **"Next priority"** at the bottom for the leverage call.

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

### 2. `DataServer.cpp` reads 25 unique global fields directly

`DataServer.cpp` (the `UpdateLiveDataJson` body) reads 25 distinct field accesses across `g_AHRS.*`, `g_Sensors.*`, `g_Config.*`, `g_EfisSerial.suVN300.*`, `g_EfisSerial.suEfis.*`, `g_Flaps.*`, `g_fCoeffP`, `g_iDataMark`.

The `xAhrsMutex` snapshot at line 368 covers part of this (the flap vector and lever ADC) but most reads happen without a snapshot, across multiple ticks of the writers. In practice it has not bitten us because aligned 32-bit loads are atomic on the ESP32-S3 and the values are EMA-smoothed at the producer. **In principle, every refactor of AHRS or Sensors has to come edit this file.** That is the tangling: the JSON builder is a load-bearing reader of a global address book.

**What honoring the model would look like:** the JSON builder takes a `LiveDataFrame` struct produced by a single snapshot function called once per 50 ms tick. The snapshot function holds the relevant mutexes, copies into the frame, and returns. The builder formats from the frame. New AHRS fields land by extending the frame, in one place; the JSON builder is a pure function of the frame.

### 3. LogReplay unpacks into globals *(unchanged in firmware; Python tooling is starting to formalize the shape)*

Firmware: `LogReplay.cpp:224-269` reads a `LogRow` and assigns 15+ global fields: `g_Sensors.PfwdSmoothed = row.pfwdSmoothed`, `g_AHRS.SmoothedPitch = row.pitchDeg`, `g_pIMU->Ax = row.imuForwardG`, etc. PR #353 fixed how the row gets parsed from CSV; it did not change how the row gets distributed afterward. A simulator data source (UDP socket from a sim) tomorrow has to duplicate this unpacking exactly, or copy this file and edit it, or refactor it.

**v4.22 update on the tooling side.** PR #380 added `tools/onspeed_py/` — a shared Python module for offline analysis — and its `live_snapshot.py` already declares the per-tick aircraft-state struct the firmware doesn't have yet. The module's docstring is explicit:

> `LiveSnapshot` is the input shape that orchestrators (synth-record scenarios, log-replay adapters, future analysis tools) feed to the display + audio pipeline. … shaped to match the eventual `LiveDataFrame` struct from `docs/ARCHITECTURE_DECOUPLING.md` §1. When that struct lands in `onspeed_core/proto/`, this Python class becomes a thin wrapper around its codec.

So the Python side has the snapshot frame already, and is being designed against this doc directly. The C++ port has not landed.

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

**The pattern.** v4.21 fixed the math; v4.22 fixed the surfaces around the math. Schema discipline at the WS layer (#369), wire-version discipline at the M5 layer (#386), and an external-facing protocol reference (#345) all landed together. The X-Plane plugin became a complete frontend (#394–#420). The cal wizard moved off the legacy path (#373). The Python tooling formalized the shape this doc proposed (#380).

What did *not* land in this window: the C++ `LiveDataFrame` snapshot in DataServer (§2), the OAT/boom re-injection in LogReplay (§3), MAVLink emit (planning step 4), parallel estimators (§6 / planning step 7), and BLE sink (planning step 10). Those are still the next moves.

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

### 1. `LiveDataFrame` + `LiveDataView` (medium) — **the single highest-leverage open piece**

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

**v4.22 starting state (helpful prior work):**
- `tools/onspeed_py/live_snapshot.py` already declares this struct on the Python side, designed against this doc by name. The C++ port mirrors the shape; the Python class becomes a thin codec wrapper afterward.
- `software/Libraries/onspeed_core/src/api/LiveDataJsonKeys.h` (PR #369) already pins the wire-side field-name list. Defining `LiveDataFrame` with the same names completes the contract on the producer side.
- `DisplayBuildInputs` in `proto/DisplaySerial.h` is the precursor flat shape; the M5 wire encoder migrates to take `LiveDataFrame` (or a flattened projection of it). The X-Plane plugin already exercises this through `DataRefAdapter::BuildInputsFromDatarefs()` — the projection pattern is proven.
- `Snapshot()` methods on `g_pIMU` and `g_Sensors` already exist and return POD copies. Extending the pattern to `g_AHRS`, `g_Flaps`, `g_EfisSerial`, `g_BoomSerial` is mechanical (~10 lines each).

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

## Next priority (as of 2026-05-06)

If only one of these landed in the next release window, **step 1 — the C++ `LiveDataFrame` snapshot in `onspeed_core/proto/`** — is the highest-leverage move, and the cheapest it has ever been.

Why now:
- The Python side (`tools/onspeed_py/live_snapshot.py`, PR #380) already designed the struct's shape, against this doc by name. The naming, units, and field set are settled.
- The schema-pin half of the contract (PR #369's `LiveDataJsonKeys.h`) is in place. A `LiveDataFrame` whose field names match `kLiveDataJsonKeys[]` finishes the producer-side closure.
- `DisplayBuildInputs` is already a sibling shape and is exercised cross-platform (firmware, X-Plane plugin, M5 sketch). Subsuming or aligning with it costs little.
- Every downstream step (#4 MAVLink emit, #5 indexer-as-generic-HUD, #7 `AoaEstimates` parallel estimators, #10 BLE GATT) takes a `LiveDataFrame` as input. Without it, each of those steps re-invents its own snapshot.

What's *not* the next priority, and why:
- **MAVLink emit (#4)** is tempting because it unlocks the most external compatibility, but doing it before step 1 means the MAVLink emitter has to read 25 globals directly — duplicating DataServer's tangling rather than fixing it.
- **OAT / boom re-injection in LogReplay (#3)** is small and worth doing soon, but it's not the architectural bottleneck.
- **Parallel estimators (#7)** is the largest leverage for the OnSpeed mission long term, but it depends on `LiveDataFrame` (or a sibling `AoaEstimates` struct routed the same way) to land first without re-inventing the snapshot pattern.
