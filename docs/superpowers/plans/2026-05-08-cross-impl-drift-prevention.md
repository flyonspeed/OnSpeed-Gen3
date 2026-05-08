# PLAN_DRIFT_PREVENTION.md — How OnSpeed Prevents Cross-Implementation Drift

**Date:** 2026-05-08
**Status:** Mostly resolved by `PLAN_WASM_CORE.md` and
`PLAN_PYTHON_CONSOLIDATION.md`. This doc is now a one-paragraph
summary + the small piece of CI gate that remains.

## The end state, in one paragraph

After `PLAN_PYTHON_CONSOLIDATION.md` and `PLAN_WASM_CORE.md` land:

> Algorithm code lives in **`software/Libraries/onspeed_core/`**.
> Four targets consume it: firmware (native C++ on ESP32),
> X-Plane plugin (native C++ on Mac/Win/Linux), M5 simulator (full
> WASM build with rendering), and `host_main` (native CLI).
> Two **wrappers** call into those compiled targets: Python tools
> shell out to host_main; the Replay tool's JS UI loads the
> algorithm-only WASM build. **No hand-ports anywhere.** Drift is
> impossible by construction.

The rest of this doc is historical — it's preserved because future
maintainers might want to know why we don't have a streaming-goldens
CI gate.

## What we no longer need

The earlier proposal: **streaming-goldens CI gate.** Real C++
generates golden CSVs from 208 Hz raw inputs; Python and JS run over
the same inputs and diff against the goldens. Drift fails the PR.

This was the right design *if* we were going to keep a JS hand-port.
We're not — `PLAN_WASM_CORE.md` removes the hand-port by compiling
C++ to WebAssembly. The streaming-goldens gate becomes redundant.

## What we keep

The existing **C++ snapshot regression** at
`tools/regression/{host_main.cpp, run_snapshot.py, fixtures/}`
stays. It compares C++ output against a committed golden CSV. This
catches behavior changes — both the kind a developer intends (and
should regenerate the golden for) and the kind they don't (which
fails CI).

This is a **per-PR sanity check on C++ behavior**, not a
cross-language drift gate. It's the right level of paranoia for a
flight-critical system.

## What replaces the cross-language drift concern

**Compile, don't port.** Three checks, all by-construction:

1. **Firmware → X-Plane plugin parity**: both link the same
   `onspeed_core/src/`. Build system enforces this.
2. **C++ → Python parity** (post-`PLAN_PYTHON_CONSOLIDATION.md`):
   Python wrappers shell out to `host_main`. Same compiled binary.
3. **C++ → JS parity** (post-`PLAN_WASM_CORE.md`): JS loads the
   WASM build of onspeed_core. Same compiled module.

There's no fourth implementation. There's nothing to gate.

## A residual concern: WASM build determinism

The compiled WASM module is the algorithm. If the WASM build is
non-deterministic (different emcc versions producing different
behavior, optimizer differences across CI runners, etc.), drift
could re-emerge.

**Mitigations**:

- Pin the emcc version in `.github/workflows/docs.yml`.
- Ship the WASM artifact with `-O3` (deterministic optimization);
  don't ship debug builds to production.
- Run `tools/regression/run_snapshot.py` against the WASM module
  too (load it from node, run the same fixtures, diff against the
  same golden as the native C++ build). One small CI step ensures
  the WASM-compiled algorithm produces the same output as the
  native-compiled one.

That last bullet is the only "drift gate" we still need:
**WASM-vs-native parity check.** Tiny — it's the existing snapshot
regression run twice (once on native C++, once on WASM-compiled
C++).

## Dispatch prompt — WASM-vs-native parity check

```
After PLAN_WASM_CORE.md Step 0+1 land, add a CI step that runs the
existing snapshot regression against BOTH the native host_main
binary AND the WASM build.

WORKTREE: a fresh worktree off origin/master, with WASM Step 0+1 merged
PLAN: docs/superpowers/plans/2026-05-08-cross-impl-drift-prevention.md

WHAT TO BUILD:
  1. tools/regression/run_snapshot_wasm.py:
     - Load the compiled onspeed_core.js module via node
     - Walk the same input CSV the existing run_snapshot.py uses
     - Call into the WASM bindings for each row
     - Diff the output against the same committed golden.csv
     - Use the same tolerance model (atol=1e-4, rtol=1e-5)
  2. .github/workflows/ci.yml: add a "WASM regression" step that
     runs after the existing native regression. Both must pass.
  3. If WASM and native disagree, the diff log shows which rows /
     fields drifted. Fix the WASM build (likely an emcc flag or
     binding mismatch) until they match.

VERIFY: green CI on a clean PR. Modify a value in onspeed_core,
confirm BOTH the native and WASM regressions catch it (regenerate
the golden, both pass again).

COMMIT: "regression: WASM-vs-native parity check (Drift Prevention)".
```

## What this leaves us with

A single check — "WASM build matches native build" — protecting the
one place that could conceivably go wrong (the Emscripten compile
step). Everything else is by-construction.

That's the right shape: minimal infrastructure, maximum
correctness, zero ongoing maintenance.
