# Snapshot Regression Harness

Catches behavior regressions in `onspeed_core` that unit tests miss. Used as
a gate during the [core extraction refactor](../../docs/superpowers/specs/2026-04-18-onspeed-core-extraction-design.md).

## What it does

- Builds `host_main.cpp` against the current `onspeed_core` (via PlatformIO `[env:native]`).
- Feeds it a recorded flight-log excerpt (`fixtures/short_replay.csv`).
- Diffs the output against the committed golden (`fixtures/golden.csv`).
- Exits non-zero if they disagree (outside float tolerance).

## Usage

```bash
# Check for regression against master golden
./run_snapshot.py

# Regenerate the golden after an intentional behavior change
./run_snapshot.py --update-golden

# Use a different input
./run_snapshot.py --input fixtures/other_log.csv
```

## When to run

- Before flashing a core-extraction PR to the bench.
- In CI on every PR that touches `software/Libraries/onspeed_core/` or the
  sketch-side consumers of core modules.

## How the pipeline grows

At PR 0.3 the pipeline is minimal (pass-through with a placeholder tone
formula) because `onspeed_core` doesn't yet contain AHRS or audio
synthesis. Each subsequent PR that moves a new module into core extends
`host_main.cpp` and regenerates the golden as part of its commit history.
