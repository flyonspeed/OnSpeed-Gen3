# OnSpeed offline replay & analysis toolkit

**Status:** Active. Update when adding/removing tools or changing data flows.

This directory contains everything OnSpeed needs to run offline against
recorded data — from "render a 10-second demo of feature X" today, to
"a pilot uploads their SD log in a browser and verifies their calibration"
on the way. The unifying idea: **firmware logic runs the same on a desktop
or browser as it does on the box.**

A future contributor or agent landing here should be able to:

1. Read this doc.
2. Pick the right tool for what they want to do.
3. Run it without re-deriving the architecture.
4. Know which surfaces are firmware-faithful and which take liberties.

If you're touching anything in this directory, **read this first.**

---

## What's here

```
tools/
├── onspeed_py/             # Pure-Python flight-data library (PR #380)
│   ├── __init__.py         # Frame, LiveSnapshot, FlapSetpoints
│   ├── log_replay.py       # CSV → LiveSnapshot iterator
│   ├── config.py           # XML config parser (V1 + V2 dialects)
│   ├── filters.py          # EMA filter (per-rate α)
│   ├── percent_lift.py     # Body-angle → fractional lift (mirrors C++)
│   └── tests/              # Unit tests + fixture CSV/CFG files
│
├── synth-record/           # CLI demo renderer (PR 7)
│   ├── record.py           # Entry point: scenario → MP4
│   ├── live_snapshot.py    # The data shape every scenario emits
│   ├── wire_frame_builder.py  # LiveSnapshot → 76-byte #1 wire bytes
│   ├── audio_harness.cpp   # C++ harness around onspeed_core::audio
│   ├── spin_detector_harness.cpp  # Standalone spin-detector for diag
│   ├── display_anchors_harness.cpp  # Standalone pip-anchor for diag
│   ├── build_harnesses.sh  # Compiles the three C++ harnesses
│   ├── scenarios/
│   │   ├── _envelopes.py   # DSL: chain, hold, smooth_to, aoa_ramp, jitter
│   │   ├── from_log.py     # Real-log → LiveSnapshot adapter
│   │   ├── spin_recovery.py
│   │   ├── ldmax_pip.py
│   │   ├── tone_sweep.py
│   │   ├── vac_decel_run.py
│   │   ├── sam_approach_flaps.py
│   │   └── vac_spin_hybrid.py  # Real log → synthetic continuation
│   └── out/                # Render outputs (gitignored)
│
├── m5-replay/              # Stream CSV/scenario over USB-TTL to real M5
│   ├── replay.py           # 20 Hz frame emitter
│   ├── parse_frame.cpp     # Layer-2 wire round-trip test harness
│   └── test_replay.py
│
├── web/                    # Browser-side OnSpeed pipeline (in flight)
│   ├── lib/components/     # Pure-prop Preact components (indexer, slip-ball)
│   ├── lib/core/           # Pure-JS algorithms (percent-lift, spin-detect)
│   ├── lib/io/             # JS log + config parser (planned PR W1)
│   ├── lib/audio/          # WebAudio engine (planned PR W2)
│   ├── pages/              # /indexer (live), /log-scrub (planned)
│   ├── dev-server/         # Local server + mock data + NDJSON replay
│   └── scripts/            # Bundler for firmware PROGMEM
│
└── regression/             # Golden snapshot harness for onspeed_core
    ├── host_main.cpp       # Headless pipeline driver
    ├── run_snapshot.py
    └── fixtures/golden.csv
```

`tools/synth-record/` and `tools/m5-replay/` produce the same firmware-faithful
behavior (audio + visuals identical to the box). `tools/web/` will, once the
JS engine lands. `tools/onspeed_py/` is the algorithmic substrate the others
share for log/config parsing.

---

## How firmware fidelity works

**Audio is byte-identical to the firmware** (C++ side). The harnesses link
against `software/Libraries/onspeed_core/src/audio/` directly:
- `ToneCalc::calculateTone(aoa, flap, ias, palt)` — tone-region selection
- `Envelope::DAHDR(...)` — amplitude shaping
- `AudioMixer::Mix(...)` — pulse modulation
- `Apply3DPan(sample, lateralG)` — stereo pan

PR #381 (Apr 2026) extracted the orchestration into
`onspeed_core::audio::AudioOrchestrator`, so the synth-record harness now
drives the *exact* code path the firmware does — no parallel re-implementation
to drift.

**Visuals are real M5 firmware running headless.** SDL2 native sim builds
the M5 codebase (`OnSpeed-M5-Display/` with `pio run -e native`) and pushes
each rendered frame to `--frames-out`. The renderer is the same one a pilot
sees on the panel.

**The synth-record CLI's pan curve is intentionally punchier than firmware.**
Demo audio benefits from quicker stereo separation than flight audio (firmware
uses ~quadratic; demo uses `min(1, 12·|x|)`). Pass `--firmware-pan` to opt
in to firmware-faithful pan for a literal byte-identical render. Documented
in `audio_harness.cpp`.

---

## What's supported today

### Render a synthetic scenario to MP4

```bash
cd tools/synth-record
./build_harnesses.sh    # one-time, after touching audio_harness.cpp etc.
python3 record.py scenarios/spin_recovery.py \
    --cfg ~/Downloads/onspeed2_latest.cfg \
    --out out/spin.mp4
```

### Render a real-log window to MP4

```bash
python3 record.py \
    --log-csv ~/Downloads/vac_log.csv \
    --cfg ~/Downloads/vac_config.cfg \
    --window 2857-2885 \
    --out out/vac_decel.mp4
```

`--window` takes seconds. Format `<t_start>-<t_end>` from the log's `Millis`
column / 1000.

### Render a hybrid (real then synthetic)

```bash
python3 record.py scenarios/vac_spin_hybrid.py \
    --cfg ~/Downloads/vac_config.cfg \
    --out out/vac_hybrid.mp4
```

The scenario file calls `chain_log_then_synthetic(log_states, synthetic)`.
The synthetic phase's first state is anchored to the log's last state so the
handoff is continuous (no smoothstep needed in practice).

### Stream a log to a real M5 over USB

```bash
cd tools/m5-replay
python3 replay.py \
    --device /dev/cu.usbserial-XXXX \
    --log-csv ~/Downloads/vac_log.csv \
    --window 2857-2885
```

Used for bench-testing M5 changes without an OnSpeed box. The frame-emit
path is byte-identical to what `synth-record` writes to the C++ harness's
stdin, so a scenario that renders correctly with `synth-record` will look
identical on a real M5.

### Tone-region sweep (audio sanity check)

```bash
python3 record.py scenarios/tone_sweep.py \
    --cfg ~/Downloads/onspeed2_latest.cfg \
    --out out/tone_sweep.mp4
```

Walks AOA from 0 → past stall, hesitating in each tone region, with
calibrated dwell. Useful for diagnosing tone-boundary tuning.

---

## What's NOT supported (and where to find the seams)

| Want | Status | Where to start |
|---|---|---|
| Browser-based log scrubber | In design (PRs W1-W7) | `local-plans/PLAN_WEB_LOG_SCRUBBER.md` (forthcoming) |
| Browser-based clip exporter | Future | After W6 |
| Offline calibration verification | Future | Builds on W4; reuses `onspeed_py.percent_lift` ↔ `tools/web/lib/core/` |
| Auto-calibration tool | Future | `docs/site/docs/calibration/` references this; not started |
| Multi-aircraft regression fixtures | Long-tail | Need logs from non-RV-10 airframes |
| Golden audio hashes | Gap | Needs a `--golden-check` PR; see `tools/synth-record/CLAUDE.md` *Open gaps* |
| Wire-frame round-trip test | Partial | `tools/m5-replay/test_replay.py` covers parser; producer side untested |

---

## Sign conventions (READ BEFORE TOUCHING `lateral_g`)

`lateral_g` is body-frame everywhere at v4.23 (PR #386, May 2026).  Slip-skid ball renderers negate locally at the rendering site.  **Always start at the canonical reference:** `local-plans/LATERAL_G_CONVENTION.md`.

Quick summary:

| Surface | Frame | Positive means |
|---|---|---|
| IMU, log column, WebSocket JSON | body | airframe accel rightward |
| `#1` wire field | body | airframe accel rightward |
| `LiveSnapshot.lateral_g` (scenario authors!) | body | airframe accel rightward |
| M5 `Slip` (after `SerialRead::SerialProcess` negates) | screen | ball drawn right of center |

**Authoring rule:** in a LEFT spin the airframe is pulled leftward → `lateral_g = -0.40` (body-frame).  Wire carries `-0.40` → M5 negates → Slip = +340 → ball drawn RIGHT → cue right rudder.  In a RIGHT spin: `lateral_g = +0.40`, ball drawn left, cue left rudder.

The trip-wire test (run when in doubt): in a right spin, what should each
value be? See `LATERAL_G_CONVENTION.md` § "The trip-wire test."

---

## Authoring scenarios

A scenario is a Python module that defines `def scenario() -> Iterator[LiveSnapshot]`.
The DSL in `scenarios/_envelopes.py`:

- `hold(state, duration_s)` — emit `state` for `duration_s` at 50 Hz.
- `smooth_to(state_from, state_to, duration_s, easing="smoothstep")` —
  field-by-field tween. Easings: `linear`, `smoothstep`, `ease_out_pow`.
- `aoa_ramp(state, aoa_from, aoa_to, duration_s)` — convenience for
  the most common AOA-sweep pattern.
- `chain(*phases)` — concatenate phases; t-stamps restamp monotonically.
- `add_realistic_jitter(stream, *, gyro_amp, accel_amp, ias_amp)` —
  add bandlimited noise to mimic flight data.
- `n_ticks(duration_s)` — at the canonical 50 Hz rate.

Sign rules in `live_snapshot.py`'s field docstring; **read it before
authoring a spin or yawing scenario.**

Scenario hygiene:
- Always pass `--cfg <path>` so the renderer knows flap setpoints.
- For real-log scenarios, scope the window tight — every second of render
  is ~30 s of CPU.
- For synthetic scenarios, use `add_realistic_jitter` so the indexer's
  derivatives don't show up as suspiciously smooth.
- Confirm sign correctness with the trip-wire test before considering
  the render done.

---

## Algorithmic invariants (Python ↔ C++ ↔ JS)

These algorithms exist (or will exist) in three implementations. They MUST
agree on shared fixtures or the toolkit's "firmware-faithful" claim breaks.

| Algorithm | Python | C++ | JS |
|---|---|---|---|
| Percent-lift (body-angle → fraction) | `onspeed_py/percent_lift.py` | `onspeed_core/src/aoa/PercentLift.cpp` | `tools/web/lib/core/percentLift.js` |
| EMA filter (per-rate α) | `onspeed_py/filters.py` | `onspeed_core/src/EMAFilter.h` | `tools/web/lib/core/ema.js` (planned) |
| Tone region selection | (calls C++ via harness) | `onspeed_core/src/audio/ToneCalc.cpp` | `tools/web/lib/core/toneCalc.js` (planned) |
| Spin detector | (calls C++ via harness) | `onspeed_core/src/spin/SpinDetector.cpp` | (planned) |
| Wire frame builder | `tools/synth-record/wire_frame_builder.py` | `onspeed_core/src/proto/DisplaySerial.cpp::BuildDisplayFrame` | (planned PR W1) |

Shared fixture set (golden vector):
`software/Libraries/onspeed_core/test/fixtures/` — every implementation
loads these, runs them, and asserts byte-equal output (or RMS-error-bounded
for audio). When you change an algorithm, **update the fixture in lockstep
with all three implementations**, or you've created a silent divergence.

---

## Architecture-decoupling principles (followed throughout)

These come from `docs/ARCHITECTURE_DECOUPLING.md` (the Gen3 design doc) and
apply doubly to anything in `tools/`:

1. **Sources, processors, sinks.** A scenario is a *source* (yields
   `LiveSnapshot`). A renderer is a *sink*. Filters/transformers are
   *processors*. None of these know about each other's internals.
2. **Schema is first-class.** `LiveSnapshot` is the shared shape. Adding
   a field is a coordinated change across `live_snapshot.py`,
   `wire_frame_builder.py`, the C++ harnesses' `parse_frame`, and (eventually)
   the JS log parser. Drift here breaks fidelity silently.
3. **Pure functions where possible.** `_envelopes.py`'s DSL combinators,
   `percent_lift.py`, `wire_frame_builder.build()` — all stateless. State
   lives in iterators (Python), state machines (C++ harness), or hooks (JS).
4. **One implementation per algorithm per language.** If you need percent-lift
   in Python, add to `onspeed_py/percent_lift.py`. Don't reinvent it in
   `synth-record/scenarios/from_log.py`.

---

## Common diagnostic recipes

### "Audio sounds wrong"

1. Did you rebuild the harnesses after touching `audio_harness.cpp` or
   `onspeed_core/`? `./build_harnesses.sh`.
2. Are you on `--firmware-pan` or default? Default is the punchier demo
   curve; firmware uses quadratic. For "what would the box sound like," use
   `--firmware-pan`.
3. Compare against the golden if one exists for that scenario: `sha256sum
   out/scenario.audio.pcm`. (This check doesn't exist yet — see *Open gaps*.)

### "Ball renders on the wrong side"

You probably have a `lateral_g` sign error. Read
`local-plans/LATERAL_G_CONVENTION.md` § "The trip-wire test."
`LiveSnapshot.lateral_g` is body-frame at v4.23 — left spin = negative, right spin = positive.

### "Pip jumps instead of interpolating"

Real logs only carry integer `flapsPos`. The `_fake_lever_sweep` workaround
synthesizes a smooth lever ramp around each detent change; if you're seeing
jumps, that workaround may be off (window-edge clamping bug fixed in PR #380).

After issue #372 + PR #377 land, real logs will carry `flapsRawADC` and the
workaround disappears.

### "Tone races through stall warn"

Ramp rate too fast for tone-region dwell. `tone_sweep.py` shows the canonical
pacing (`hesitate_at_stall_warn` segment with `ease_out_pow` envelope).

### "Render finishes but MP4 is corrupted"

Almost always a frame-pipe stall. Check `record.py`'s ffmpeg subprocess
return code; if non-zero, the C++ harness probably crashed mid-stream
(SDL2 native-sim cleanup is `_Exit(0)` deliberate-skip-of-static-dtors).

---

## Open gaps (catalog of missing tests / coverage)

These are known weaknesses. Don't claim "tool X works" without checking
whether your change hits one of these:

1. **No golden audio hash.** Audio fidelity rests on linking against
   `onspeed_core::audio` directly, but no test pins a render's hash. A
   firmware tone-math change would silently re-tune every demo.
2. **No video frame-content checks.** A pip-position regression could ship
   without us noticing. Lift the `geometry-invariants.mjs` pattern from
   `tools/web/`.
3. **No wire-frame round-trip test.** `tools/m5-replay/parse_frame.cpp` tests
   the parser; the producer side (`wire_frame_builder.py` / the C++ harness's
   frame-write) is untested. With #374 about to bump the wire format, this
   is acutely relevant.
4. **EMA over-smoothing on Gen3 logs (50 Hz).** Hardcoded `α=0.0609` is
   firmware's 208 Hz value; over-smooths 4× at log rate. PR #380 fixes by
   computing α from log dt.
5. **Lever-sweep window-edge clamping.** `_fake_lever_sweep`'s smoothstep
   center isn't on the snap tick when snap is within `half` ticks of log
   start/end. PR #380 fixes.
6. **All test logs are RV-10s.** Lift-fraction math behaves differently on
   significantly negative `alpha_0` aircraft. Need fixtures from other
   airframes.

---

## Onward to offline calibration analysis

The trajectory: today this is a video tool. Tomorrow it's the substrate
for **offline calibration** — a pilot uploads their flight log, marks the
calibration runs, and replays them through the same JS algorithms that
would have run live, then verifies the resulting curves before reflashing
the box.

The pieces fall into place naturally:

- **`onspeed_py.percent_lift`** + **`onspeed_py.config`** are already the
  Python truth for the lift formula and per-flap `alpha_0/alpha_stall`.
  The auto-calibrator's curve fit lives there.
- **`tools/web/lib/io/`** (PR W1) gives the browser the same parser. A
  pilot drags an SD log into the browser; the same iterator-of-LiveSnapshot
  feeds either a scrub-and-export tool (PR W4-W6) or a calibration analyzer
  (future).
- **`tools/web/lib/core/percentLift.js`** is the same lift math, in JS,
  so the browser can re-evaluate "would this calibration have worked?" on
  any historical flight.
- **The fixture set** that all three implementations share doubles as the
  regression test for the calibrator: any time the formula changes, all
  three break or all three pass — never one of three.

The shape we're building toward:

> "Open log. Mark calibration window. See live what the box would have
> shown. Tweak `alpha_0`. Re-render the same audio + indexer. Iterate
> until satisfied. Export the new config to flash."

That's a Tier-3 deliverable; today's PRs are paving the runway. Every
scenario authored, every algorithm shared between languages, every
fixture committed pushes us closer.

---

## Pointers to deeper docs

- `local-plans/LATERAL_G_CONVENTION.md` — sign convention canonical reference.
- `local-plans/PLAN_SYNTH_RECORD.md` — the 9-PR roadmap that built this toolkit.
- `local-plans/PLAN_WEB_LOG_SCRUBBER.md` — browser-version plan (forthcoming).
- `tools/synth-record/README.md` — contributor-facing README for synth-record
  (audience: humans; this CLAUDE.md is the agent-facing doc).
- `tools/web/CLAUDE.md` — orientation for the browser side
  (`tools/web/`'s own conventions; this doc is the umbrella).
- `software/Libraries/onspeed_core/CLAUDE.md` — the C++ algorithm library
  (firmware/tools share this; do not reinvent algorithms outside it).
- `docs/ARCHITECTURE_DECOUPLING.md` — the source/processor/sink design.
