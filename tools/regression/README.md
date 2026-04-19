# Snapshot Regression Harness

Catches behavior regressions in `onspeed_core` that unit tests miss. Used as
a gate during the core extraction refactor established in PRs #178 and #179 —
platform code belongs in the sketch, pure logic in `onspeed_core/`.

## What it does

- Builds `host_main.cpp` against the current `onspeed_core` (via PlatformIO `[env:native]`).
- Feeds it a recorded flight-log excerpt (`fixtures/short_replay.csv`).
- Diffs the output against the committed golden (`fixtures/golden.csv`).
- Exits non-zero if they disagree (outside float tolerance).

## Tolerance model

Uses `math.isclose(a, b, rel_tol=rtol, abs_tol=atol)` — a match if EITHER the
relative or absolute difference is within tolerance.

Default tolerances:

- `atol = 1e-4` — matches the `%.4f` CSV wire precision. Two values that
  round to the same 4-decimal string stay within atol.
- `rtol = 1e-5` — catches per-value drift ≥ 0.001% on large-magnitude fields
  (altitude, TAS) where absolute tolerance would be silly-tight.

An absolute-only tolerance breaks at both extremes: too loose on small
values, too tight on large ones. `math.isclose` correctly handles both.

## Usage

```bash
# Check for regression against master golden
./run_snapshot.py

# Regenerate the golden after an intentional behavior change
./run_snapshot.py --update-golden

# Use a different input
./run_snapshot.py --input fixtures/other_log.csv

# Override tolerances (useful for debugging a near-miss diff)
./run_snapshot.py --rtol 1e-4 --atol 1e-3
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
