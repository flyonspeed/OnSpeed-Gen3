# OnSpeed-Gen3 Architecture: Decoupling Model

This document is a **contract** for how OnSpeed firmware should be structured as we deploy to more environments (real planes, simulators, log replay, synthetic data) and as we add more outputs (M5, browser, HUD glasses, X-Plane plugin). It is also an **honest audit** of where the current code honors the contract and where it does not.

It is not a reorganization plan. Migration sequencing belongs in its own document; this one defines what we are reorganizing toward.

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

### The X-Plane plugin proves the model works

`software/OnSpeed-XPlane-Plugin/` is the cleanest possible consumer of the architecture. It:

- Reads X-Plane datarefs (a data-source adapter)
- Calls `ToneCalc::calculateTone()` from `onspeed_core` directly
- Synthesizes PCM with `ToneSynth` from `onspeed_core` directly
- Plays via OpenAL (a sink adapter)
- Has its own MIT license, builds standalone with CMake, has its own test suite

**Zero coupling to ESP32 sketch state.** This is what every external consumer should look like.

### Codecs in `proto/` are pure and bidirectional

`DisplaySerial` encodes and decodes the 74-byte M5 frame. `LogCsv` formats and parses log rows. Both are exercised in tests on round-trip data without any I/O. Adding a new sink (HUD frame format) means adding a new module in `proto/` and a test — no other layer changes.

### LogCsvHeaderIndex (PR #353) is the prototype for "schema as first-class artifact"

Before PR #353, log replay was **position-based** — column N of the header had to be `pfwdSmoothed` because that's what the reader hardcoded. Add a column to the writer between releases and old logs broke. PR #353 retrofitted name-keyed parsing: `BuildHeaderIndex` resolves column names to ordinals and surfaces missing required columns and missing optional groups (boom / standard EFIS / VN-300) explicitly. 21 tests cover reorder, missing-required, missing-optional, extra-unknown, sign-flip, garbage rejection, and a four-fixture corpus naming each combination.

This is what the WebSocket JSON schema should look like next. (It also points at what's still missing in the log format itself — `# format=N` in the header — which #353 explicitly defers.)

## Where we fall short

These are concrete. Each one is a place where the next person adding a data source or a sink will trip.

### 1. The WebSocket JSON has no schema

`DataServer.cpp:300-309` builds the JSON with a hand-written `snprintf` format string. There is no test that pins the field set. Issue #363 is the canonical example: PR #320 dropped four body-angle fields from the wire when it added percent anchors, and the cal wizard JS kept reading them for several months without anything firing. The fix (PR #365) was 11 lines, but the *bug* was schema drift between writer and reader, identical in shape to what #353 fixed for log files.

**What honoring rule 2 would look like:** the JSON broadcast is generated from a struct (call it `LiveDataFrame`) declared in `onspeed_core/src/proto/`, with a `FormatLiveData(const LiveDataFrame&, char* out, size_t cap)` codec and a test pinning the field set. Every JS consumer (`/live`, `/indexer`, `/calwiz`) imports a single reference list of expected fields. Changing the schema becomes a deliberate edit; reads of removed fields fail at the schema layer, not silently as `undefined`.

This is a small PR, separable from any other refactor, and it forecloses a class of bug we have already seen twice (#353 for logs, #363 for JSON).

### 2. `DataServer.cpp` reads 25 unique global fields directly

`DataServer.cpp` (the `UpdateLiveDataJson` body) reads 25 distinct field accesses across `g_AHRS.*`, `g_Sensors.*`, `g_Config.*`, `g_EfisSerial.suVN300.*`, `g_EfisSerial.suEfis.*`, `g_Flaps.*`, `g_fCoeffP`, `g_iDataMark`.

The `xAhrsMutex` snapshot at line 368 covers part of this (the flap vector and lever ADC) but most reads happen without a snapshot, across multiple ticks of the writers. In practice it has not bitten us because aligned 32-bit loads are atomic on the ESP32-S3 and the values are EMA-smoothed at the producer. **In principle, every refactor of AHRS or Sensors has to come edit this file.** That is the tangling: the JSON builder is a load-bearing reader of a global address book.

**What honoring the model would look like:** the JSON builder takes a `LiveDataFrame` struct produced by a single snapshot function called once per 50 ms tick. The snapshot function holds the relevant mutexes, copies into the frame, and returns. The builder formats from the frame. New AHRS fields land by extending the frame, in one place; the JSON builder is a pure function of the frame.

### 3. LogReplay unpacks into globals

`LogReplay.cpp:224-269` reads a `LogRow` and assigns 15+ global fields: `g_Sensors.PfwdSmoothed = row.pfwdSmoothed`, `g_AHRS.SmoothedPitch = row.pitchDeg`, `g_pIMU->Ax = row.imuForwardG`, etc. PR #353 fixed how the row gets parsed from CSV; it did not change how the row gets distributed afterward. A simulator data source (UDP socket from a sim) tomorrow has to duplicate this unpacking exactly, or copy this file and edit it, or refactor it.

**What honoring rule 1 would look like:** there is a single function `ApplyDataSourceFrame(const SourceFrame&)` that distributes incoming data to wherever it needs to go. Every adapter (real-sensor reader, LogReplay, simulator bridge, X-Plane bridge if we ever embed it) produces a `SourceFrame` and calls this function. New source = new adapter, no edits to the distributor.

The reason this hasn't bitten is that we have one source at a time (compile-time selection: `EnSensors` / `EnReplay` / `EnTestPot` / `EnRangeSweep`). The minute we want a second simultaneously — say, "live sensors with synthetic AOA injection for cal-wizard rehearsal" — we have to invent the abstraction anyway.

### 4. The cal wizard recomputes DerivedAOA client-side and is wrong under EKF6

Issue #366 (filed during the #365 review). `javascript_calibration.h:125`: `derivedAOA = PitchAngle - flightPath`. This matches `Ahrs.cpp:487`'s Madgwick branch exactly, and silently disagrees with `Ahrs.cpp:408`'s EKF6 branch (`DerivedAOA = state.alpha_deg()`). The wizard fits its calibration curve against the wrong signal when EKF6 is active.

This is rule 3 violated in miniature: the wizard had its own *fourth* AOA estimator (`PitchAngle - flightPath`), not declared as one, parallel to the one the runtime audio path uses. The fix is to consume `OnSpeed.DerivedAOA` from the wire (which is the firmware's authoritative value, whichever AHRS is active). The discipline is: *a downstream consumer must not reinvent an upstream computation* — the seam is the wire frame.

### 5. The audio path reads `g_Config` directly

`Audio::Play()` and `g_AudioPlay.UpdateTones()` read volume curves and pulse parameters from `g_Config.*` at the call site. `ToneCalc::calculateTone()` is pure (it takes a thresholds struct), but the wrapper around it pulls from globals. A second audio path tomorrow — for example, the X-Plane plugin's audio thread, or a "run audio decisions on replayed data and emit a transcript" tool — would need to either read the same globals or duplicate the wrapper.

The plugin gets away with it today because it talks to `ToneCalc` and `ToneSynth` directly, bypassing the sketch wrapper. That works because the plugin doesn't need volume curves. The moment we want consistent volume behavior between firmware and plugin (or between firmware and a future bench-test analysis tool), the wrapper has to take its config as an argument.

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

### 1. `LiveDataFrame` snapshot in DataServer (medium)

Define a `LiveDataFrame` struct, snapshot all 25 globals into it once per 50 ms tick under the relevant mutexes, and have the JSON builder format from the frame. Fixes the global-address-book reader and gives every other sink the same input.

Standard to hew to: **none yet** — this is internal scaffolding. But the frame becomes the source of truth that downstream wire formats (JSON, MAVLink, future HUD) all encode from.

### 2. WebSocket JSON schema (small)

Generate `UpdateLiveDataJson()` from the `LiveDataFrame` declaration via a codec module in `onspeed_core/src/proto/` with a round-trip test. Pin the field set so the next #363 fails at the test, not in a pilot's browser.

Standard to hew to: **JSON Schema** for the field-set declaration, so downstream clients (cal wizard, M5 sim, future tools) can validate inputs against the same schema artifact rather than each shipping its own list of expected keys.

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

---

Each step is independently reviewable and shippable. None require a top-down rewrite. The pattern they share is **import the schema discipline that adjacent communities have already worked out**, and reserve our own design effort for the parts that actually are OnSpeed-specific (audio decisions, percent-lift math, calibration logic).

The point of this document is to make the discipline explicit so that the *next* PR — whoever writes it — pulls in the direction of the model rather than against it, and reaches for an existing protocol before inventing one.
