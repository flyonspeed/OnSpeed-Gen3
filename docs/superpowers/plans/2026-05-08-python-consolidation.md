# PLAN_PYTHON_CONSOLIDATION.md — Move Python Algorithms to C++ Wrappers

**Date:** 2026-05-08
**Owner:** Sam
**Status:** Proposed. **Sequenced before Layer 1+ of the video-overlay roadmap.**
**Breaking-changes-OK?** Yes — Sam has confirmed only he is using these
tools today; we can break interfaces freely during this consolidation.

## The thesis

> Python in OnSpeed should be I/O glue. Anything that computes a
> firmware-relevant value (percent-lift, anchors, EMA, lever sweep,
> frame bytes, config parsing) should call `host_main` instead of
> re-implementing the algorithm.

Algorithm code lives **once**: in `software/Libraries/onspeed_core/`.
Every consumer — the firmware, the M5 WASM simulator, the X-Plane
plugin, Python tools, the streaming-goldens CI harness — invokes
that code rather than carrying its own copy.

The JS replay tool is the **single sanctioned hand-port** because
browsers can't shell out to a binary. CI gates JS against the
C++-generated streaming goldens.

## Inventory — what's algorithm code vs I/O glue

| File | Lines | Verdict | Notes |
|---|---|---|---|
| `tools/onspeed_py/__init__.py` | 30 | KEEP | Just module exports |
| `tools/onspeed_py/config.py` | 130 | **MIGRATE** | XML parsing duplicates `Config.cpp::Parse` |
| `tools/onspeed_py/percent_lift.py` | 73 | **MIGRATE** | Direct port of `PercentLift.cpp` |
| `tools/onspeed_py/frame.py` | 166 | **MIGRATE** | Wire-frame builder, mirrors `BuildDisplayFrame` |
| `tools/onspeed_py/live_snapshot.py` | 61 | KEEP | Pure dataclass, no algorithm |
| `tools/onspeed_py/log_replay.py` | 358 | **MIGRATE** | The big one — EMA, lever sweep, full pipeline |
| `tools/m5-replay/replay.py` | 467 | **MOSTLY MIGRATE** | Algorithm sneaks in; serial I/O stays Python |
| `tools/regression/run_snapshot.py` | 252 | KEEP | Already the pattern we want — runs host_main subprocess, diffs CSV |

## The desired endpoint

```
                ┌────────────────────────────┐
                │  C++ onspeed_core (spec)   │
                └────────────┬───────────────┘
                             │
                ┌────────────┴────────────┐
                │                         │
                ▼                         ▼
        ┌────────────┐          ┌──────────────────┐
        │ host_main  │          │ Emscripten WASM  │ ← M5 simulator,
        │  CLI       │          │ (browser)        │   X-Plane plugin
        └─────┬──────┘          └──────────────────┘
              │
              ├─► subprocess (Python wrappers — synth-record, m5-replay)
              ├─► subprocess (CI regression harness)
              ├─► subprocess (streaming-goldens generator)
              └─► JS replay tool gates on its outputs (CI)
```

## Sequenced migration

Each step is independently shippable. **Land them in this order.**

### Step 0 — Extend host_main into an algorithm CLI (1-2 days)

Today `host_main` is single-purpose. Extend it to a multi-subcommand
CLI:

```bash
# One-shot algorithm queries
$ host_main percent_lift --aoa 3.24 --alpha-0 -3.72 --alpha-stall 10.31 --stallwarn 8.24
49.6

$ host_main display_anchors --config X.cfg --flap 16 --raw-adc 897
{"tonesOn":50,"fast":55,"slow":64,"stallWarn":86,"pip":50}

# Config parsing (replaces config.py)
$ host_main parse_config --in X.cfg
{"flapsByDeg": {...}, "muteUnderIas": 35, ...}

# Frame building (replaces frame.py)
$ host_main build_frame --record record.json
<76-byte frame as base64 or hex>

# Streaming pipeline (replaces log_replay.py)
$ host_main replay --input flight.csv --config X.cfg --output-format jsonl > stream.jsonl
```

Implementation: thin `argparse` shell over existing `onspeed_core`
functions. Output formats: JSON for structured returns, CSV for
streaming, base64 for binary. Same `host_main` binary; multiple
subcommands.

**Test gate**: every subcommand has a fixture-based integration test.

### Step 1 — Migrate `percent_lift.py` and `config.py` (1 day)

Smallest, simplest. Each becomes a ~30-line subprocess wrapper:

```python
# tools/onspeed_py/percent_lift.py (after migration)
import json, subprocess
from .types import FlapSetpoints

def compute_percent_lift(aoa: float, fs: FlapSetpoints) -> float:
    out = subprocess.run(
        ['host_main', 'percent_lift',
         '--aoa', str(aoa),
         '--alpha-0', str(fs.alpha_0),
         '--alpha-stall', str(fs.alpha_stall),
         '--stallwarn', str(fs.stallwarn_aoa)],
        capture_output=True, text=True, check=True,
    )
    return float(out.stdout)
```

The existing tests pass against the wrapper. If they don't, the
strategy is wrong and we stop here.

**This step is the proof-of-concept.** Don't proceed to step 2 until
step 1 lands cleanly.

### Step 2 — Migrate `log_replay.py` (1-2 days)

The whole pipeline (variable-dt EMA, synth lever sweep, record
shaping). One-shot batch execution: write the input CSV to a temp
file, run `host_main replay --input X --output-format jsonl`, read
the JSON-lines output.

```python
# tools/onspeed_py/log_replay.py (after migration)
def replay_log(log_path: Path, config_path: Path) -> Iterator[LiveSnapshot]:
    proc = subprocess.run(
        ['host_main', 'replay', '--input', str(log_path),
         '--config', str(config_path), '--output-format', 'jsonl'],
        capture_output=True, text=True, check=True,
    )
    for line in proc.stdout.splitlines():
        yield LiveSnapshot(**json.loads(line))
```

For very long logs (Sam's 95-min flight is 286k rows), the one-shot
buffers the whole output in memory (~30 MB). That's fine for
foreseeable use. If it ever isn't, swap to streaming via Popen +
stdout.readline().

### Step 3 — Migrate `frame.py` (half-day)

The wire-frame builder. Pure function: `record → 76 bytes`. Becomes
a subprocess call. m5-replay's frame-emission loop calls the wrapper
per tick.

### Step 4 — Audit `m5-replay/replay.py` (half-day)

After steps 1-3, replay.py should be almost pure I/O. Hunt down any
remaining algorithm code (EMA, derived computation, etc.) and route
through host_main. What stays:
- Reading the input CSV
- Pacing at 20 Hz
- Writing wire bytes to USB-CDC
- Pilot-side button mocks for testing

### Step 5 — Audit Python tests + synth-record (half-day)

The Python tests (`tools/onspeed_py/tests/`) currently test the
algorithm code we just deleted. Decisions per file:
- Tests of pure algorithm behavior (NaN handling, edge cases): **delete**.
  C++ unit tests cover the same.
- Tests of the wrapper integration (subprocess returns the right
  thing for a known input): **keep**. They're the contract that the
  Python interface still works.
- Synth-record orchestrator (`tools/onspeed_py/synth_*`): **audit**.
  It generates fake flight data; if any of it computes "what
  percent-lift would be at this fake AOA," that's algorithm code.
  Either route through host_main or remove.

### Step 6 — Streaming-goldens CI gate (1 day)

With Python now subprocess-based, drift between Python and C++ is
literally impossible. The remaining drift surface:
- JS replay vs C++ (the only hand-port left)

Becomes the simplified version of `PLAN_DRIFT_PREVENTION.md`:

1. CI: build host_main, run against committed input CSVs, compare
   against committed golden CSVs (existing `run_snapshot.py` pattern,
   extended to multiple input files).
2. CI: run JS over the same inputs, diff against the goldens.

Two CI jobs, same inputs, same tolerance model. Drift fails the PR.

### Step 7 — Documentation + removal of the deprecated paths

- Remove `onspeed_py.percent_lift.compute_percent_lift` etc. from
  the public Python interface (the wrapper functions stay, but
  imports against the old API break loudly).
- Update `tools/onspeed_py/README.md` to say "this package is I/O
  glue around `host_main`. New algorithm work goes in C++."
- Update CLAUDE.md if it references Python algorithm files.

## Total effort

~5-7 days of focused work, in 7 small PRs (one per step). Each PR
ships independently. Nothing here blocks the video-replay roadmap —
the cleanup pulls forward, then video-replay Layer 1+ proceeds on
top of cleaner foundation.

## Why this matters

1. **Drift is impossible to introduce in Python after migration.**
   Wrappers can have bugs (subprocess quoting, JSON parsing) but they
   can't have algorithm bugs.
2. **Python becomes maintainable by I/O-focused contributors** rather
   than requiring deep firmware knowledge.
3. **The mental model gets simpler.** "How does OnSpeed compute X?"
   has one answer: read `onspeed_core/X.cpp`. The four other
   implementations either compile that source (M5 simulator) or call
   it (host_main, Python wrappers, regression harness).
4. **JS becomes the only hand-port,** which is the right place to
   spend test budget. The streaming-goldens CI gate is much more
   valuable when only one consumer is gating.

## Dispatch prompts

### Step 0 prompt

```
Implement PLAN_PYTHON_CONSOLIDATION.md Step 0: extend host_main
into a multi-subcommand algorithm CLI.

WORKTREE: a fresh worktree off origin/master
PLAN: docs/superpowers/plans/2026-05-08-python-consolidation.md
WHAT TO BUILD:
  1. Update tools/regression/host_main.cpp (or split into files)
     to use a top-level argparse-style subcommand dispatcher:
       host_main percent_lift --aoa F --alpha-0 F --alpha-stall F --stallwarn F  → float on stdout
       host_main parse_config --in PATH                                            → JSON on stdout
       host_main display_anchors --config PATH --flap I --raw-adc I               → JSON on stdout
       host_main build_frame --record JSON                                         → bytes (hex) on stdout
       host_main replay --input PATH --config PATH --output-format jsonl|csv      → stream on stdout
  2. The existing snapshot regression flow still works: it now
     uses `host_main replay`. Update tools/regression/run_snapshot.py
     to invoke the new form.
  3. Add tests under test/test_host_main_cli/ that exercise each
     subcommand with a known input and assert the output.
  4. Build artifact: a single `host_main` binary in build/native/.

VERIFY: pio test -e native green. Each subcommand callable from
the shell with the documented flags.

COMMIT: one focused PR titled "host_main: multi-subcommand algorithm CLI".
```

### Step 1 prompt

```
Implement PLAN_PYTHON_CONSOLIDATION.md Step 1: migrate percent_lift.py
and config.py to host_main subprocess wrappers.

WORKTREE: a fresh worktree off origin/master with Step 0 merged
PLAN: docs/superpowers/plans/2026-05-08-python-consolidation.md

WHAT TO BUILD:
  1. Rewrite tools/onspeed_py/percent_lift.py as a thin subprocess
     wrapper. Same Python public API; subprocess.run('host_main
     percent_lift ...') under the hood.
  2. Rewrite tools/onspeed_py/config.py the same way.
  3. Run the existing pytest suite under tools/onspeed_py/tests/.
     Should pass against the new wrappers without modification.
     If a test fails, the migration is wrong; debug before
     proceeding to step 2.
  4. Add a brief module-level docstring to each wrapper noting
     "this is a host_main subprocess wrapper. Algorithm lives in
     onspeed_core/aoa/PercentLift.cpp".

VERIFY: cd tools/onspeed_py && uv run --with pytest pytest. Green.

COMMIT: "onspeed_py: migrate percent_lift + config to host_main wrappers".
```

(Steps 2-7 follow the same template — see PLAN body for what each does.)

## What's NOT in scope

- **Removing `tools/regression/run_snapshot.py`**: it's already the
  good pattern. It stays.
- **Removing the C++ host_main snapshot golden test**: it stays as
  the bedrock of the streaming-goldens system.
- **Migrating the JS replay**: JS can't shell out to a binary in
  the browser. JS stays as the sanctioned hand-port; CI gates it
  via streaming goldens.
- **Touching `software/OnSpeed-XPlane-Plugin/`**: it already calls
  `onspeed_core` C++ directly. It's the model we want to replicate.
