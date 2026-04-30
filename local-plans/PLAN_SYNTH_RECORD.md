# Synthetic-replay video tool: consolidation plan

**Date:** 2026-04-30
**Status:** Spike landed in `onspeed-worktrees/synth-record`. Three demo videos produced (spin recovery, L/Dmax pip movement, tone sweep). Plan covers (1) what landed, (2) what's duplicated and needs to retire, (3) what onspeed_core needs to grow, (4) how this becomes the canonical "watch what your plane is doing" tool — synthetic AND real-log.

## What we built

A host-side toolchain that turns a Python "scenario" into an MP4 with synced audio:

```
scenarios/*.py  →  record.py orchestrator
                       │
                       ├─ display_anchors_harness  (calls onspeed_core::aoa)
                       ├─ spin_detector_harness    (calls onspeed_core::sensors)
                       ├─ audio_harness            (calls onspeed_core::audio)
                       └─ M5 sim native build      (calls firmware renderer)
                       │
                       ▼
                   audio.wav + frames.rgb  →  ffmpeg mux  →  out.mp4
```

Three scenarios produced ten-second clips on demand:

| Scenario | What it shows | Audio path |
|---|---|---|
| `spin_recovery.py` | Cruise → hesitation at stall_warn → wing drops → developed right spin → cue fires (`-1`, press LEFT rudder) → un-stall recovery → dive pull-out with IAS ramp | Tone progression, then quiet during cue, then 3D-pan rolls right as lateral-G builds |
| `ldmax_pip.py` | Pilot holding 52% lift; lever sweeps clean → flap 33; pip slides up the indexer; chevron stays put; tones walk fast-pulse → ONSPEED solid | Audio shifts as flap detents snap |
| `tone_sweep.py` | Slow AOA ramp through every region (silent → low-pulse → ONSPEED → high-pulse with ease-out hesitation through stall_warn → 99% saturation) | Audit clip for every tone region + amplitude ramp |

Each scenario yields a stream of `LiveSnapshot` ticks at 50 Hz (matching the firmware's `kDisplaySerialPeriodMs`); the orchestrator runs the firmware's actual computation paths via thin C++ harnesses, builds the `#1` wire frame, and tees it to (a) the M5 sim for visuals and (b) the audio harness for stereo PCM.

**Key architectural success:** the tool consumes `onspeed_core` directly. The audio rendered is byte-identical to what the box would emit for the same scenario — same `ToneCalc::calculateTone`, same `Envelope` DAHDR shape, same `AudioMixer::Mix` per-sample composition. The visuals come from running the actual M5 firmware code through SDL2 in headless mode (`SimRecord.cpp`), driven by real `#1` wire frames the firmware would assemble. Display anchors come from `ComputeDisplayPctAnchors`. SpinDetector lives in `onspeed_core::sensors` and we call it.

## Lessons learned during the spike (real logs were the test)

The spike's `from_log.py` adapter exposed five gaps between the design and the data we'll actually be replaying. All are worth folding into the longer-term tool, not just "spike duct tape":

1. **Gen2 logs are pre-smoothing.** Gen3 logs the post-EMA-filtered accel values; Gen2 logs raw IMU. Without α=0.0609 EMA smoothing in the adapter, the ball / AI / G-readout jitter unrealistically when replaying Gen2 data. The Gen3 firmware's `kAccSmoothing` constant becomes the source-of-truth value. Done.

2. **The lever-ADC is not in the log.** PR #336's `pipPctLift` interpolates lever-end-to-end across the pot range, but logs only carry the *detected* detent (integer `flapsPos`). Without a workaround the pip jumps at every detent crossing. Issue [#372](https://github.com/flyonspeed/OnSpeed-Gen3/issues/372) filed for the proper fix (capture `flapsRawADC`); meanwhile `_fake_lever_sweep` synthesizes a 4-second smooth ramp **centered on the detent-detection tick** (the firmware flips detents at the midpoint between adjacent pot values, so the lever was already at midpoint when the log first showed the new detent — sweeping centered, not just-before, models that timing correctly).

3. **V1 configs are missing alpha_0 / alpha_stall / k_fit.** Those fields weren't extracted by the V1 calibration wizard. Default to `α_0 = 0` (matches what Gen2's piecewise display showed) and `α_stall = stallwarn + 1.5°`. Honest values can be recovered for any log via a one-shot fit utility (demonstrated on Vac's data; R²=0.976) but we don't run it automatically since most users won't have the source log.

4. **Old logs use different column names.** `AngleofAttack` instead of `DerivedAOA`, no `efisPercentLift`, no per-flap percent-lift columns. PR #353's name-keyed parsing pattern is the right shape — the adapter accepts either name and falls back to defaults for fields the old format didn't carry.

5. **V1 XML uses tag names that aren't valid XML** (`<3DAUDIO>` etc.). tinyxml2 accepts these; Python's stdlib doesn't. The Python parser preprocesses the raw text to rename `<3...>` → `<_3...>` before parsing. Worth noting because the same trick would be needed in any other Python-side V1 consumer.

These lessons are checked-in:
- `tools/synth-record/scenarios/from_log.py` — the adapter with smoothing, fake lever sweep, name-aliasing, and graceful degradation
- `tools/synth-record/wire_frame_builder.py::_load_flap_setpoints_v1` — V1 config parser
- `test/fixtures/v1-config-and-log/` — Vac's cfg + a 30s log slice as a regression fixture so this never bit-rots

The bigger architectural lesson: **the gap between "log → replay" and "synthetic → render" is much smaller than it looked.** Both produce LiveSnapshot ticks; both go through the same orchestrator → harnesses → ffmpeg pipeline. The forking happens *only* at the front (parsing the source) and the convergence is at the LiveSnapshot itself. That's the architecture-decoupling doc working as intended: the processing core doesn't know its source.

## Where we cheated, and why it has to retire

The spike intentionally cut three corners. Each one is a duplication of firmware-or-spec logic that should live in `onspeed_core` and doesn't yet:

### 1. Audio orchestration constants and state machine

The piece that sits between `ToneCalc::calculateTone` (which gives us `(EnToneType, fPulseFreq, fVolumeMult)`) and `Envelope::NoteOn(spec)` (which takes a fully-built `EnvelopeSpec`) lives in `software/sketch_common/src/audio_io/Audio.cpp` as static helpers:

- `MakePulseSpec(pps, isStall, fromSolid)` — Audio.cpp:160
- `MakeSolidSpec()` — Audio.cpp:198
- The `SetTone` decision tree that picks solid-vs-pulsed and detects `fromSolid` — Audio.cpp:400

Constants the helpers read (`TONE_RAMP_TIME = 15ms`, `STALL_RAMP_TIME = 5ms`, `SOLID_TRANSITION_DELAY_MS = 1000/8.2/2`) are sketch-local `#define`s.

**Tracked in [#368](https://github.com/flyonspeed/OnSpeed-Gen3/issues/368).** The harness has copy-pasted versions of all of this. Until #368 lands, every change to those constants needs a parallel change here. After #368, the harness drops the duplicate and calls `onspeed::audio::DecideEnvelopeStep` (or whatever the extracted API ends up named).

### 2. 3D-audio pan curve and EMA

`software/sketch_common/src/tasks/Housekeeping.cpp:121-141` computes the 3D-pan channel gains from `g_AHRS.AccelLatCorr`:

```c
#define AUDIO_3D_CURVE(x)  (-92.822f*(x)*(x) + 20.025f*(x))
// plus α=0.1 EMA, plus left = |-1+gain|, right = |1+gain|
```

Tracked in [#370](https://github.com/flyonspeed/OnSpeed-Gen3/issues/370). The harness has its own copy (`audio_harness.cpp:130-160`). Same retire path: extraction lands → harness drops the duplicate.

**There's also a real bug in this curve, [#371](https://github.com/flyonspeed/OnSpeed-Gen3/issues/371):** it goes negative above 0.216g lateral, gets clamped to zero, and produces *no pan* during spins (real lateral G during a developed GA spin: 0.3–0.5g). The harness currently uses a local saturating-linear curve (`min(1.0, 8·|x|)`) so the demo videos audibly do what the spec §7 *intends*. Once #371 lands a real fix, the harness drops the local override and calls into core like everything else.

### 3. Display-side hotfixes (synth-record-only patches to firmware code)

For Vac's demo we made two changes in our worktree to the firmware itself:

- **`mapPct2Display` in `OnSpeed-M5-Display/src/main.cpp`** — top segment now spans `OnSpeedSlowPctLift → 99` instead of `OnSpeedSlowPctLift → StallWarnPctLift`. The chevron now reaches the top of the indexer at percent_lift = 99 instead of saturating at stall_warn (typically ~80–85%). The flash-red color logic above stall_warn is preserved.
- **`FullFlapPipTarget` in `onspeed_core::aoa::DisplayPctAnchors`** — pip's full-flap endpoint moved from band geometric center `(fast+slow)/2` to bottom-half-of-donut `(3·fast+slow)/4`. Vac's preference for the demo videos.

Both are small, both are bench-test-grade — but they're firmware behavior changes that need to land as their own properly-reviewed PRs (or be reverted) before the spike merges. Two options for the proper PRs:

- **Option A** — both changes are correct, file two clean PRs with proper specs. The 99% mapping in particular is a clear improvement: the chevron's screen position should track lift envelope, not warning-flash threshold.
- **Option B** — the changes only matter for the demo, keep them as local overrides and revert before merge. Less clean but ships faster.

## What onspeed_core needs to grow (and the extraction is the test)

Three extractions, ordered by leverage:

### 1. `onspeed_core/audio/AudioOrchestrator.{h,cpp}`  (issue #368)

```cpp
namespace onspeed::audio {
struct OrchestratorConfig { /* sample rate, ramp times */ };
EnvelopeSpec MakePulseSpec(float pps, bool isStall, bool fromSolid,
                           const OrchestratorConfig&);
EnvelopeSpec MakeSolidSpec(const OrchestratorConfig&);

struct StepDecision { enum Action { NoteOff, NoteOn }; Action action; EnvelopeSpec spec; };
StepDecision DecideEnvelopeStep(const ToneResult&, bool curEnvIsSolid,
                                const OrchestratorConfig&);
}
```

Sketch wrapper at `Audio.cpp::SetTone` becomes a 2-line call. Harness drops 75 lines of copy-paste. The "go" condition for retiring synth-record's audio duplication is this PR landing.

### 2. `onspeed_core/audio/Pan3D.{h,cpp}`  (issue #370 + bug fix in #371)

```cpp
namespace onspeed::audio {
struct Pan3DState  { float channelGain = 0.0f; };
struct Pan3DOutput { float leftGain; float rightGain; };

constexpr float kPan3DAlpha = 0.1f;
Pan3DOutput UpdatePan3D(float lateralG, Pan3DState&,
                        float alpha = kPan3DAlpha);
}
```

`Housekeeping.cpp` 3D-pan block becomes a 3-line call. Harness drops the curve override and the EMA. **Land #371's curve fix in the same PR** (`min(1.0, 8·|x|)` is the recommended replacement) — there's no point shipping a buggy curve into a freshly-extracted module.

### 3. `tools/synth-record/` itself becomes the canonical demo / log-replay tool

Currently the spike lives in a worktree. Path to mainline:

- After PR #336 (already merged) and the two extractions above are in, rebase the spike branch onto master.
- Drop the `MakePulseSpec`, `MakeSolidSpec`, pan-curve, and detector-call duplications in favor of `onspeed_core` calls.
- Extract the orchestrator's wire-frame and percent-lift Python into a small `tools/onspeed_py/` module so `m5-replay`, `synth-record`, and any future tool import the same code (today m5-replay and synth-record have parallel `Frame` builders — m5-replay's is the one that ships, synth-record's was a fork to skip the pyserial dependency).
- Wire scenarios + log-replay (next section) under one CLI:
  ```
  record.py scenarios/spin_recovery.py --out spin.mp4
  record.py --log-csv flight.csv --window 122-132 --out replay.mp4
  ```
- Tests:
  - Round-trip a scenario through the wire-frame builder and assert byte-identical to a known fixture (m5-replay already has Layer 2 firmware-parser tests; reuse the binary).
  - Run scenarios through the audio harness and assert PCM bytes match a fixture (golden file approach — like `tools/regression/run_snapshot.py`).
  - Render a fixture scenario, run `ffprobe`, assert duration / streams.
- One PR for the tool itself, after the two extractions land. That PR's diff should be much smaller because most of the code-duplication will already be gone.

## Real-log replay — first-class, not deferred (Zach's logs incoming)

Architecturally the tool already wants this — the orchestrator takes a stream of `LiveSnapshot` ticks regardless of source. A `scenarios/from_log.py` adapter is the natural extension:

```python
# scenarios/from_log.py
def scenario_from_log(log_csv: Path, t_start: float, t_end: float,
                      flap_pot_value: int = None):
    """Yield LiveSnapshot ticks from a window of an OnSpeed SD-log CSV.

    Reads the column set documented in LogSensor.cpp.  Resamples to 50 Hz
    if the log was written at 208 Hz (logRate setting).  Optionally
    overrides flap-pot reading (logs may not capture the raw ADC).
    """
```

What's needed beyond what we have today:

1. **Log parser.** OnSpeed SD logs use the format defined in `LogSensor.cpp:303`. There's already `tools/m5-replay/replay.py::csv_frame_stream` that does exactly this — parses the CSV at the 50 Hz tick rate, builds `Frame` objects. Lift it into the shared `tools/onspeed_py/` module (extraction #3 above).

2. **Lever ADC handling.** Logs may capture `flapsPos` (degrees) but not the raw lever ADC the firmware uses for pip-interpolation. Two strategies:
   - For logs without ADC: synthesize lever ADC from flap-degrees by linearly interpolating between configured detents' `iPotPosition` values. The pip will be quantized to detent boundaries, but the chevron and audio (which gate on the snapped active detent) will be exactly right. Acceptable for replay.
   - Once we add `flapsRawADC` to the log format (small follow-up; `LogSensor.cpp` writes whatever globals we expose), real logs replay with the same pip-slide behavior the box would have produced.

3. **Window selection.** Log files are large (hours of flight). The orchestrator needs `--window <t_start>-<t_end>` so we can record a 10s clip from a known interesting moment without rendering hours of frames.

4. **Time-stamping.** Logs carry `timeStamp` rather than tick number. The orchestrator should consume a stream of `(t, LiveSnapshot)` pairs and resample to its 50 Hz output rate. This also handles 208 Hz logs (firmware can be configured to log at the higher rate for AHRS analysis) — the audio + display sim still want 50 Hz.

5. **Testing against Zach's logs.** Once they arrive:
   - Pick a stall or spin event in the log (he'll point at the timestamp).
   - Render a 10s window around that event.
   - Verify the rendered video matches the audio Zach heard in flight (he was there; he can ear-check).
   - Verify the indexer picture matches what the M5 was showing in flight (if there's video).
   - The "do these match" question is the test; if yes, the tool is canonical for "what was the box doing at moment X".

The seam this preserves: **a `LiveSnapshot` doesn't know whether it came from a synthetic envelope or a real log row.** Architecturally identical, just different adapters at the front. That's exactly the "Synthetic data generator" + "Log replay" duality `docs/ARCHITECTURE_DECOUPLING.md` calls for under "Data sources."

## What this gives us, longer-term

Three real uses, all enabled by getting the tool committed and the duplications retired:

1. **Demo videos.** Vac wants to show off the spin cue. Lenny wants tone-region clips. Marketing wants L/Dmax-pip-during-flap-deployment. All three rendered from one scenario each, repeatable, byte-faithful.

2. **Bug reproduction from real logs.** A pilot reports "the audio sounded weird at 12:34 in this flight." Render the 10s window around that timestamp; if the audio sounds the same on the bench, the bug reproduces deterministically. Zach's logs will be the first real test.

3. **Calibration verification.** Apply a candidate calibration to a recorded flight (replay the IMU + pressures through `onspeed_core` with the new flap setpoints) and listen — does the audio fire at the right speeds for that pilot's actual approach pattern?

The dependency chain to get there:

- **#368** (audio orchestration extraction) lets the audio harness drop ~75 lines of duplication
- **#370** + **#371** (pan extraction + curve fix) lets the audio harness drop ~30 lines + a known-bug local override
- **PR for `mapPct2Display` 99% top + pip bottom-half-of-donut** (or revert) — small, independent, can land any time
- **`tools/onspeed_py/` shared module** — pulls Frame builder + percent-lift + IAS-from-AOA out of m5-replay and synth-record into one place
- **PR landing `tools/synth-record/` proper** — once the duplications above are gone, this PR's diff is mostly the orchestrator + scenarios + the SimRecord.cpp pio env addition
- **PR adding `scenarios/from_log.py` + log-window CLI** — natural follow-up; brings Zach's logs into the loop

## Loose ends to address before tool ships

- **PR #336's `pipPctLift` interpolation** assumes the lever-ADC reading is always available. The synthetic side passes `lever_raw` from scenario. Real logs may not carry raw ADC. The "synthesize from flap degrees" fallback above handles it; document the limitation.
- **The "program quit unexpectedly" macOS dialog** when the M5 sim exits is cosmetic — `_Exit(0)` bypasses Cocoa shutdown. Should fix by signalling Panel_sdl to tear down properly. Not blocking for the spike.
- **`SimRecord.cpp` opens an SDL window** during recording (Cocoa requires the SDL pump on the main thread). Window appears briefly, doesn't affect output. Could go full-headless with offscreen rendering but it's not free; defer.
- **The harness binaries are platform-clang-built**, separate from PlatformIO. `build_harnesses.sh` works on macOS and Linux; would need a Windows path if any contributor's on Windows. Probably not a concern for the immediate use case.
- **No tests for the orchestrator itself** in the spike. The C++ harnesses link real-firmware code, so behavior tests on those are inherited from the existing `test_display_pct_anchors`, `test_spin_detect`, etc. The orchestrator-side tests are the "render a fixture and ffprobe it" kind, lower-priority.

## Old-format support — first-class, not deferred

The first real-log test (Vac's `vac_log.csv` + `vac_config.cfg` from late 2025) immediately exposed two old formats the tool must handle:

### Old config (V1 XML, comma-separated lists)

```xml
<FLAPDEGREES>0,20,40</FLAPDEGREES>
<FLAPPOTPOSITIONS>129,158,206</FLAPPOTPOSITIONS>
<SETPOINT_LDMAXAOA>8.03,5.73,4.78</SETPOINT_LDMAXAOA>
<SETPOINT_ONSPEEDFASTAOA>11.25,9.74,8.62</SETPOINT_ONSPEEDFASTAOA>
<SETPOINT_ONSPEEDSLOWAOA>13.84,12.64,12.44</SETPOINT_ONSPEEDSLOWAOA>
<SETPOINT_STALLWARNAOA>16.48,16.29,14.51</SETPOINT_STALLWARNAOA>
<AOA_CURVE_FLAPS0>0.0,8.4845,24.0804,4.6157,1</AOA_CURVE_FLAPS0>
<3DAUDIO>1</3DAUDIO>
```

vs current per-flap-block format. **Python parser must accept both.** The new format has `<FLAP_POSITION>` blocks; old format has top-level `<SETPOINT_*>` lists.

**Three V1-specific quirks the parser must handle:**

1. **Digit-prefix XML tags.** V1 has `<3DAUDIO>` (and possibly other digit-prefix tags), which is not valid XML. Python's stdlib `xml.etree.ElementTree` rejects this; tinyxml2 accepts it. The Python parser preprocesses the raw text to rename `<3...>` → `<_3...>` before calling `ET.fromstring`.

2. **No `ALPHA0` / `ALPHASTALL` / `KFIT`.** These fields didn't exist in V1; the modern calibration wizard derives them by fitting `AOA = K/IAS² + α_0` against the original calibration log. **Default behavior in the parser:** `α_0 = 0` (matches Gen2's piecewise+0-floor display), `α_stall = stallwarn + 1.5°` (typical calibration margin), `k_fit = 0` (the `ias_from_aoa` helper returns 0 when k_fit is unset, gracefully degrading any IAS-from-AOA calculations downstream).

3. **The polynomial intercept (`x0` term of `AOA_CURVE_FLAPS{i}`) is NOT alpha_0.** Vac's intercept of +4.62° looks like it could be the zero-lift body angle but isn't — it's the body angle reading at zero pressure ratio (i.e. when the boom is co-aligned with the relative wind), which is sensitive to installation alignment. Vac's was high because his box was mounted nose-down without programmed bias correction. **Don't use it as an alpha_0 substitute** — gives wildly wrong percent-lift values. (Verified: Vac's flap-0 L/Dmax with the polynomial-intercept heuristic landed at 26% on the indexer; honest fit-derived alpha_0 puts it at 46%.) The proper recovery path is a per-log fit; we don't try that automatically.

Onspeed_core already handles both formats via `ConfigV1Parse.cpp` + `ConfigXmlParse.cpp`. **The shared `tools/onspeed_py/` module needs the same dual-path** (or a tiny C++ harness that runs the firmware's V1 parser and emits a normalized JSON). See `test/fixtures/v1-config-and-log/` for the fixture-based regression tests covering all three quirks.

### Optional: post-fit correction utility

For users replaying old logs who care about visually-correct percent-lift positioning (not just "what Gen2 displayed"), a separate utility:

```bash
python3 tools/onspeed_py/fit_alpha0.py --log vac_log.csv --cfg vac_config.cfg --flap 0
```

Reads clean unaccelerated 1G points from the log, fits `AOA = K/IAS² + α_0`, prints values the user can manually paste into a per-scenario override. Demonstrated working on Vac's data (R²=0.976 across 22,610 points). Not part of the default V1 path because most users won't have the source log; useful as an opt-in for analysis tasks.

### Old log columns (pre-PR #353)

Vac's log columns:
```
timeStamp,Pfwd,PfwdSmoothed,P45,P45Smoothed,PStatic,Palt,IAS,
AngleofAttack,flapsPos,DataMark,OAT,TAS,imuTemp,VerticalG,
LateralG,ForwardG,RollRate,PitchRate,YawRate,Pitch,Roll,
boomStatic,boomDynamic,boomAlpha,boomBeta,boomIAS,boomAge,
vnAngularRate*,vnAccel*,vnYaw,vnPitch,vnRoll,...
EarthVerticalG, FlightPath, VSI, Altitude
```

vs current format that has `DerivedAOA`, `efisPercentLift`, `tonesOnPctLift` band edges, etc. The old log has **`AngleofAttack`** as the body-angle field. The new format renamed to `DerivedAOA`. PR #353 (already merged) adds name-keyed parsing with optional fields, which is exactly the discipline this needs. **The Python adapter must:**
1. Accept old or new column names (`AngleofAttack` ↔ `DerivedAOA`).
2. Emit zeros / sensible defaults for fields the old log didn't carry (band-edge percents — those get computed at scenario time from the cfg, not read from log).
3. Not crash on unknown extra columns.

This dovetails with PR #353's discipline: name-keyed parsing, optional groups, explicit error on missing-required.

## Sequence of PRs (re-ordered: log-replay first-class)

Each PR has explicit acceptance criteria so an agent can self-verify completion.

### PR 1: 3D-pan curve fix  (issue #371)

**Scope:** `software/sketch_common/src/tasks/Housekeeping.cpp:8` — replace the parabola with `min(1.0, 8.0f * fabsf(x))`.

**Acceptance:**
- Build clean (esp32s3-v4p, all M5 envs).
- Audible test: pilot in coordinated 30° banked turn (lateral G ~0.3) hears clear right-channel emphasis on right turn.
- Existing tests pass.
- Spec doc `docs/site/docs/software/audio-tone-spec.md` §7 updated with the new curve formula.

### PR 2: Audio orchestration extraction  (issue #368)

**Scope:** Move `MakePulseSpec` / `MakeSolidSpec` / `SetTone`-state-machine from `Audio.cpp` into `onspeed_core/audio/AudioOrchestrator.{h,cpp}`. Sketch wrapper becomes a 2-line call.

**Acceptance:**
- All `pio test -e native` pass.
- Audio path on the box behaves identically (regression test against a recorded WAV from a known scenario).
- `tools/synth-record/audio_harness.cpp` deletes the copy-pasted `MakePulseSpec` / `MakeSolidSpec` and calls into `onspeed_core::audio::DecideEnvelopeStep`.
- Diff: `Audio.cpp` shrinks by ~50 lines, `audio_harness.cpp` shrinks by ~75 lines, `onspeed_core/audio/AudioOrchestrator.cpp` is the new ~75-line file.

### PR 3: Pan3D extraction  (issue #370)

**Scope:** Extract the 3D-pan EMA + curve into `onspeed_core/audio/Pan3D.{h,cpp}`. Land alongside or after PR 1.

**Acceptance:**
- `Housekeeping.cpp` 3D-pan block becomes ~3 lines.
- `audio_harness.cpp` deletes its local pan implementation.
- New unit tests for `UpdatePan3D` covering: zero G → unity gain, 0.5g step → EMA convergence, sign flip, full-range monotonicity.

### PR 4: M5 indexer hotfixes (`mapPct2Display` 99% top + pip bottom-half-of-donut)

**Scope:** Two firmware behavior tweaks, two commits in one PR. Independent of all other PRs.

**Acceptance:**
- `mapPct2Display`: chevron at percent_lift=99 lands at y=1; at percent_lift=stall_warn lands proportionally below y=1.
- `FullFlapPipTarget`: pip at most-deployed flap sits between OnSpeed-fast edge and band center, not at center.
- Bench-replay a synthetic scenario through the M5 sim; verify visually + against fixtures.
- Vac sign-off on the new visual semantics before merge.

### PR 5: Shared `tools/onspeed_py/` module

**Scope:** Lift `Frame` builder, `FlapSetpoints`, `compute_percent_lift`, `ias_from_aoa`, **the V1+new config parser**, **the old+new log column parser**, into a single Python module. `tools/m5-replay` and `tools/synth-record` both import it. **Independent of all other PRs** but a dependency for PR 7+.

**Acceptance:**
- `tools/m5-replay/replay.py` re-exports / imports from `tools/onspeed_py/` and its existing Layer 2 firmware-parser tests still pass.
- New tests:
  - V1 config (Vac's `vac_config.cfg`) parses without error and yields per-flap setpoints with `alpha_0`/`alpha_stall` derived from the AOA-curve polynomial.
  - New-format config (current N720AK config) parses without error.
  - V1 log (Vac's `vac_log.csv`) parses 100 rows without error; key fields (timeStamp, AngleofAttack, IAS, flapsPos, vG/lG/pitch/roll/yawRate) populate `LiveSnapshot` correctly.
  - New-format log parses without error.

### PR 6: `tools/synth-record/` proper

**Scope:** The orchestrator + scenarios + C++ harnesses (post-extractions) + `SimRecord.cpp` + `[env:native_record]` PIO env. Depends on PRs 1–5.

**Acceptance:**
- `python3 tools/synth-record/record.py scenarios/spin_recovery.py --out spin.mp4` produces a valid 10s MP4.
- Same for `tone_sweep.py` and `ldmax_pip.py`.
- `tools/synth-record/build_harnesses.sh` builds clean against current `onspeed_core`.
- `pio run -e native_record` builds clean.
- Tests: round-trip a fixture scenario through the wire-frame builder and assert byte-identical output. Ffprobe the MP4 and assert (1280×960, 50 fps, h264 video + aac stereo audio, ~10s).
- Worktree's three demo videos render identically (golden-bytes regression test on audio.pcm and frames.rgb).

### PR 7: `scenarios/from_log.py` + `--log-csv` CLI

**Scope:** Adapter that yields `LiveSnapshot` ticks from a window of an OnSpeed SD log CSV. Depends on PRs 5 (config + log parsers) and 6 (orchestrator).

**Acceptance:**
- `record.py --log-csv vac_log.csv --window 2865-2885 --cfg vac_config.cfg --out vac-decel.mp4` produces a 20s MP4 of Vac's deceleration run with correct audio.
- The audio matches what the box would have emitted (verified against the log's `tonesOnPctLift` field if present, or against a recomputed value from the cfg if not).
- Resamples 50 Hz logs at 1:1; resamples 208 Hz logs to 50 Hz cleanly.
- Handles missing fields per the V1 column set (sets defaults, doesn't crash).
- Lever-ADC handling: if `flapsRawADC` not in log, synthesize from `flapsPos` against detent table.

### PR 8: Hybrid `from_log_extended` + spin demo against Vac's flight

**Scope:** Compose a log-window scenario with a synthetic continuation. The interesting use: take Vac's natural decel-to-stall-break run from `vac_log.csv` and *extend* it with synthetic spin development + recovery. Shows Vac the cue firing on his own flying without him having to actually spin the airplane.

**Acceptance:**
- `scenarios/from_log_extended.py` accepts a log window AND a synthetic continuation function.
- The transition between log-derived and synthetic states is smooth (interpolated for ~0.5s if the synthetic continuation begins from a different state than the log window ended at).
- Renders a video that combines Vac's real stall break (from `vac_log.csv` ~t=2877s) with synthetic spin onset → cue → recovery.
- Vac sign-off on the video before declaring this PR complete.

### PR 9: Skill registration

**Scope:** `.claude/skills/synth-record.md` with the terse trigger phrases (synthetic + log + hybrid). Audience is Claude-future, not contributors. Lands after PR 6 minimum, ideally PRs 6+7+8.

**Acceptance:**
- Trigger phrases match real demand patterns: "record a video of X", "render a clip from [log]", "make a demo for [Vac/Lenny]", "show what the box did at [time] in [log]".
- Body documents the scenario DSL, the log-replay CLI, the hybrid composition pattern, and where outputs land.
- Cross-references the README in `tools/synth-record/` for contributor-facing docs.

---

Each PR above is independently reviewable and shippable. None require a top-down rewrite. Order respects the architecture-decoupling doc: pull platform-free logic into onspeed_core *first*, then build host-side tools that consume it, never the reverse.

**Critical path: 1 → 2 → 3 → 5 → 6 → 7 → 8 → 9.** PR 4 (indexer hotfixes) is genuinely independent and can land any time.

## What goes into the skill (after the tool ships)

A `.claude/skills/synth-record.md` with terse trigger phrases:

- "record a video of [feature/scenario]"
- "make a demo for Vac"
- "show the [spin cue / L/Dmax pips / tone X]"
- "render a clip from [log file] at [time]"
- "10-second clip from [synthetic / log]"

Body: the scenario DSL, the log-replay CLI, where outputs land, what to do when audio sounds wrong (check #368/#371 didn't regress), what to do when colors look swapped (firmware/render bug, not pipeline). Audience is Claude-future, not contributors — the README in `tools/synth-record/` is the contributor doc.

## Bottom line

The spike works and produces real, useful videos. The architectural debt is honest and small (~100 lines of duplication across two well-understood code paths) and tracked in three issues. **The path to "this tool evolves with onspeed_core" is exactly the path to retiring those issues.** When #368 and #370/#371 land, the harness shrinks by ~100 lines and gains automatic correctness as those paths get further refined. That's the property we want — and the architecture-decoupling doc is what makes it true.
