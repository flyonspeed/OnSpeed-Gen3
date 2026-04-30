# Synthetic-replay video recording (audio + M5 sim)

**Date:** 2026-04-30
**Status:** Spec — first pass is a spike (worktree-only) to validate end-to-end before pulling seams clean.
**Worktree:** `../onspeed-worktrees/synth-record`
**Branch:** `synth-record-spike`, off `sam/spin-recovery-cue`.

## Problem

We need to produce two ten-second MP4 demos from synthetic flight data:

1. **Spin-recovery-cue demo** — flaps up, on speed, pull pitch back, wing drops at stall, autorotation develops, the `spinRecoveryCue` wire field fires and the M5 overlay points at the rudder. PR #329 is the feature being demonstrated.
2. **L/Dmax pip-movement demo** — clean, then deploy flaps in stages; AOA tracks the L/Dmax setpoint for each flap so the chevron stays at the pip while the pip moves with flap deployment.

Audio must be **byte-identical** to what the OnSpeed firmware would produce — same tone-calc and mixer code path, not a JS reimplementation. Video and audio must be tick-locked at 50 Hz so the pulse rate, pip position, and chevron position are visually and audibly coherent.

This work also has a long-term frame: the missing **"Synthetic data generator"** entry under "Data sources" in `docs/ARCHITECTURE_DECOUPLING.md`. The tool we build here is the prototype for that adapter.

## Non-goals (this spike)

- Production-quality CI integration. (After the seam-pull pass, yes.)
- CSV log replay as a scenario source. (Explicit follow-up; the design accommodates it.)
- Stereo audio with 3D-pan. The C++ tone engine produces mono today; for demo videos that's fine.
- A new runtime estimator or model. We're consuming existing `onspeed_core` code unchanged.
- Headless WASM build of the M5 sim. The native SDL build is sufficient.

## Architecture

```
                ┌──────────────────────────────────────────┐
                │  scenarios/*.py  (output-driven, 50 Hz)  │
                │   yields LiveSnapshot per tick           │
                └─────────────────┬────────────────────────┘
                                  │
                                  ▼
                ┌──────────────────────────────────────────┐
                │  record.py  (orchestrator)               │
                │   1. load scenario module                │
                │   2. parse user OnSpeed config           │
                │   3. run SpinDetector via C++ harness    │
                │   4. build #1 wire frames                │
                │   5. tee frames to audio + M5 sim        │
                │   6. ffmpeg-mux PNG seq + WAV → MP4      │
                └────────┬───────────────┬─────────────────┘
                         │ stdin         │ stdin
                         ▼               ▼
        ┌──────────────────────┐  ┌──────────────────────────┐
        │ audio harness (C++)  │  │ M5 sim (SDL, headless)   │
        │ tools/audio-sweep/   │  │ software/.../sim/        │
        │   engines/master/    │  │   SimMain.cpp            │
        │ extended:            │  │ extended:                │
        │   --frames-stdin     │  │   --frames-stdin         │
        │ reads 74-byte #1     │  │   --frames-out <dir>     │
        │   frames; emits PCM  │  │ reads same 74-byte #1    │
        │   on stdout          │  │   frames; renders PNG    │
        └────────────┬─────────┘  └────────────┬─────────────┘
                     │ int16 mono PCM          │ PNG sequence
                     ▼                         ▼
                  audio.wav               frames/00001.png ...
                                  │
                                  └── ffmpeg mux ──► out.mp4
```

### Three guiding rules from `ARCHITECTURE_DECOUPLING.md`

The design is shaped to honor these:

- **Rule 1 (core doesn't name its source).** Both `onspeed_core::audio::*` and the M5 firmware renderer are consumed *unchanged*. The orchestrator is a new data-source adapter; the cores don't know they're being driven by a Python scenario.
- **Rule 2 (every adapter has a schema).** The cross-process schema is the existing `#1` DisplaySerial wire frame (74 bytes), with codec already pure and round-trip-tested in `onspeed_core/proto/`. The audio harness and the M5 sim consume the *same* byte stream. One serialization, two consumers.
- **Rule 3 (estimators are parallel and named).** `SpinDetector` runs once, in a C++ harness that links the real `onspeed_core::SpinDetector`. Result writes back to the orchestrator and lands in the wire frame's `spinRecoveryCue` field. We never reimplement the detector in Python.

## Components

| Component | Path (in worktree) | Status | Purpose |
|---|---|---|---|
| Audio harness | `tools/synth-record/audio_harness.cpp` | NEW | Reads per-tick lines `(aoa_deg, ldmax_aoa, os_fast_aoa, os_slow_aoa, stall_warn_aoa)` from stdin, drives `ToneCalc → AudioMixer` pipeline. Mono int16 PCM @ 16 kHz on stdout. **Why text lines, not wire frames:** the wire frame carries *percent-lift* band edges (post-formula), but `ToneCalc::calculateTone` takes *body-angle* thresholds. Rather than reverse-mapping percents back to body angles inside the harness, the orchestrator writes a parallel stream with the body-angle inputs the tone engine needs. (The pre-existing `tools/audio-sweep/engines/master/harness.cpp` lives in the workspace's primary checkout, not the spin-cue branch, so we cleanly start fresh inside `tools/synth-record/`.) |
| Spin-detector harness | `tools/synth-record/spin_detector_harness.cpp` | NEW | Reads `(t, aoa, yaw_dps, flap_idx)` lines on stdin; calls `onspeed_core::SpinDetector::Update`; writes back `cue` per line. |
| M5 sim recording mode | `software/OnSpeed-M5-Display/sim/SimMain.cpp` | EDIT | Add `--frames-stdin` and `--frames-out <dir>` flags. In recording mode: skip SDL window, feed parsed frames into the existing `SerialRead.cpp` parser, render to off-screen `M5Canvas`, write PNG. |
| M5 sim build glue | `software/OnSpeed-M5-Display/platformio.ini` | EDIT (small) | Reuse `[env:native]` if possible; flag selects record mode at runtime. |
| Orchestrator | `tools/synth-record/record.py` | NEW | Loads scenario, ticks at 50 Hz, runs spin detector, builds wire frames, tees to audio + M5 sim subprocesses, runs ffmpeg mux. |
| Wire frame builder | `tools/synth-record/wire_frame_builder.py` | NEW | Imports/reuses `tools/m5-replay/replay.py`'s frame builder so the same Layer-2-tested encoder is the one source of truth. |
| Scenario library | `tools/synth-record/scenarios/_envelopes.py` | NEW | Helpers: `hold`, `ramp`, `step_at`, `smooth_step`, `chain`. |
| Spin scenario | `tools/synth-record/scenarios/spin_recovery.py` | NEW | The Vac brief, ten seconds. |
| L/Dmax scenario | `tools/synth-record/scenarios/ldmax_pip.py` | NEW | The Vac brief, ten seconds. |
| Output dir | `tools/synth-record/out/` | NEW (gitignored) | MP4s and intermediate frames/WAVs. |

## Data flow

The orchestrator runs in two phases. Phase 1 generates the frame stream into a temp file. Phase 2 runs the audio harness and the M5 sim independently against that file. This avoids any pipe-coordination concerns.

```
# Phase 1: generate frames (Python, single-threaded, fast)
record.py:
  with open(out/frames.bin, 'wb') as f:
      for state in scenario.iter_ticks():            # 500 ticks
          cue = spin_detector_harness.tick(state)    # via pipe to C++
          state.spin_recovery_cue = cue
          frame = wire_frame_builder.build(state, config)   # 74 bytes
          f.write(frame)

# Phase 2: render outputs (each consumer reads its own copy of frames.bin)
  run(audio_harness, '--frames-stdin', stdin=open(frames.bin), stdout=audio.pcm)
  ffmpeg(audio.pcm → audio.wav)
  run(m5_sim, '--frames-stdin', '--frames-out', frames_dir, stdin=open(frames.bin))
  ffmpeg(frames_dir + audio.wav → out.mp4)
```

PNG writes happen inside the M5 sim subprocess. No real-time constraint anywhere — the orchestrator is not "playing" the scenario, it's *describing* it; both consumers run as fast as bytes arrive.

The intermediate `frames.bin` is also useful as a debugging artifact: if a video looks wrong, hexdump the frames or pipe them through `tools/m5-replay/replay.py`'s parser to verify the encoded state matches what the scenario described.

## Scenario format

Output-driven Python — the scenario writes the *aircraft state*, not pilot inputs. No physics model.

```python
@dataclass
class LiveSnapshot:
    t: float            # seconds since scenario start
    aoa: float          # degrees, body-angle convention
    ias: float          # knots
    pitch: float        # degrees
    roll: float         # degrees
    yaw_rate: float     # deg/s
    vertical_g: float
    lateral_g: float
    flaps_pos: int      # raw 0..1000 in wire format
    palt: float         # ft
    vsi: float          # fpm
    oat: float          # °C
    flight_path: float  # deg
    data_mark: int = 0
    spin_recovery_cue: int = 0   # filled by orchestrator post-detector
```

`LiveSnapshot` is a Python POD shaped to align with the eventual `LiveDataFrame` struct from §1 of the architecture decoupling planning list. When that struct lands in `onspeed_core/proto/`, this Python class becomes a thin wrapper around its codec.

Envelope helpers compose timelines:

```python
def scenario():
    return chain(
        cruise_clean(2.0),       # 0–2.0s
        stall_pull(2.0),         # 2.0–4.0s
        wing_drop(0.5),          # 4.0–4.5s
        developed_spin(3.5),     # 4.5–8.0s
        recovery(2.0),           # 8.0–10s
    )
```

Each segment is a generator yielding `LiveSnapshot` values.

## Output

Two files in `tools/synth-record/out/`:
- `spin-recovery-2026-04-30.mp4`
- `ldmax-pips-2026-04-30.mp4`

Spec:
- 10 seconds each
- 50 fps locked to scenario tick rate
- 320×240 native (M5 panel resolution); ffmpeg upscales to 1280×960 with nearest-neighbor for crispness
- 16 kHz mono PCM → AAC in MP4 container
- ~2 MB each; ephemeral — not committed.

## Risks

1. **Headless SDL.** `Panel_sdl` may require a window for pixel readback. If yes, fallback is "open window briefly during render, capture surface, discard" — visible during render, but the PNG sequence is still clean. Validate first thing in the spike.
2. **Stdin deadlock — mitigated by writing frames to a temp file first.** Rather than tee live pipes (which would deadlock if either consumer's buffer fills before the orchestrator gets to it), the orchestrator writes all 500 frames into `out/frames.bin` first, then runs the audio harness and the M5 sim in series, each `< frames.bin`. Loses parallelism we don't need; eliminates the failure mode entirely.
3. **`SpinDetector` first-tick filter seeding.** The detector's filtered-yaw-rate state needs `Reset()` at scenario start, or the first tick or two may glitch. Orchestrator calls `Reset()` once before the loop.
4. **Audio ↔ video sync drift.** Both consumers are tick-locked by construction (frames-in, output-per-frame); sync is enforced by ffmpeg using `-framerate 50` for video and the precise-length WAV for audio, with `-shortest` to clip ragged ends.
5. **Spin scenario realism.** Vac is the source of truth. If the synthesized yaw / AOA profile looks wrong on screen, scenario-only edit, no code changes required.

## Testing

For the spike: **none**. We're validating end-to-end by watching the videos.

After the spike validates and we pull seams clean (separate PR):
- Scenario emits deterministic tick stream (golden-bytes test against fixture).
- Wire-frame builder round-trips through `onspeed_core::ParseDisplayFrame` (already covered by `tools/m5-replay/test_replay.py`'s Layer 2 tests; reuse).
- Audio harness `--frames-stdin` mode produces same PCM as reference WAV for fixture scenario.
- Spin-detector harness produces same `cue` sequence as `test_spin_detect/` fixtures.
- ffmpeg mux produces a valid MP4 (run `ffprobe` and assert duration / streams).

## Implementation order (the spike)

1. Worktree exists, submodules initialized. ✓ (done)
2. Build the spin-detector C++ harness; smoke-test with a hand-crafted yaw spike.
3. Extend audio harness with `--frames-stdin`; pipe the existing 30s sweep through it as a regression test (output should match the existing `master-sweep.wav`).
4. Extend `SimMain.cpp` with `--frames-stdin --frames-out`. Validate against `tools/m5-replay/replay.py`'s `--synthetic` byte stream.
5. Write `record.py` orchestrator + `_envelopes.py` + `wire_frame_builder.py`.
6. Write `scenarios/spin_recovery.py` and `scenarios/ldmax_pip.py`.
7. Run; iterate scenario timing until videos look right.
8. Show the user the videos.

After the user signs off:
9. Pull seams: write tests, write the README in `tools/synth-record/`, write a Skill (`tools/synth-record/SKILL.md` + `.claude/skills/` registration).
10. Decide PR strategy. Most natural: one PR adding `tools/synth-record/` + the harness extensions, branched off master once `sam/spin-recovery-cue` (PR #329) merges.

## Skill (deferred)

After the spike works, register a Skill at `.claude/skills/synth-record.md`:

- **Trigger phrases:** "record a video of [feature]", "make a demo for Vac", "show the spin cue", "show the L/Dmax pips", "demo video", "synthetic scenario", "10-second clip from synthetic data".
- **Body:** the DSL (envelopes + chain), the orchestrator command, output paths, and the placeholder for the eventual `scenarios/from_log.py` that converts a CSV window into a `LiveSnapshot` stream.
- **Audience:** Claude (me-future). The README in `tools/synth-record/` is the contributor doc.

## Pointer to architecture decoupling

This tool is the prototype for the **"Synthetic data generator"** entry under "Data sources" in `docs/ARCHITECTURE_DECOUPLING.md` (currently flagged as missing). The seam between scenario and consumer is intentionally the same wire-frame schema that the LogReplay path uses, so the same orchestrator structure later supports `scenarios/from_log.py` (CSV-window-to-snapshot) without architectural change.
