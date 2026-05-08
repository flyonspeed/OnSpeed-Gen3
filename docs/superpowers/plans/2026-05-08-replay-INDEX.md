# INDEX — Replay Tool Plan Set

**Date:** 2026-05-08
**Owner:** Sam.

If you're a fresh agent (or fresh-session Sam) picking up this work,
**read these documents in this order**:

| # | Doc | Purpose |
|---|---|---|
| 1 | `2026-05-08-video-overlay-AGENT-CONTEXT.md` | Orientation. Gotchas, anti-patterns, build commands, the unified architecture diagram. **Read in full before touching code.** |
| 2 | `2026-05-08-python-consolidation.md` | Project A: migrate Python algorithm code to `host_main` subprocess wrappers. **Step 0 (host_main multi-subcommand CLI) is a prerequisite for the WASM project.** ~5-7 days, 7 small PRs. |
| 3 | `2026-05-08-wasm-core.md` | Project B: compile `onspeed_core` to WebAssembly. Replay tool's JS UI loads the WASM module and calls into it for every algorithm — **zero hand-ports**. ~5-7 days, 5 small PRs. Lands after Python consolidation Step 0; then runs in parallel with Python Steps 1-7. |
| 4 | `2026-05-08-video-overlay-replay.md` | Project C: the Replay tool roadmap. Layered architecture (Layer 0 engine → Layer 5 export). Lands AFTER projects A+B. |
| 5 | `2026-05-08-cross-impl-drift-prevention.md` | What's left of drift-prevention after WASM lands: a tiny "WASM-vs-native parity check" CI step. Most of this doc is historical — explaining why we no longer need a streaming-goldens CI gate. |

## The one-sentence story (post-foundation)

> **OnSpeed has one source of truth: `software/Libraries/onspeed_core/`.
> Every consumer either compiles it (firmware, X-Plane plugin),
> compiles it to WASM and loads the artifact (M5 simulator, Replay
> tool), or shells out to a CLI binary that compiles it (Python
> tools, CI regression). NO hand-ports. Drift impossible by
> construction.**

This is the elevator pitch. Everything in the five plans is in
service of this story. The architecture **collapses beautifully**
after Projects A + B land.

## The unified architecture

```
                          ┌──────────────────────────────┐
                          │   software/Libraries/        │
                          │   onspeed_core/              │  ← THE SPEC
                          │   (platform-free C++)        │
                          └──────────────┬───────────────┘
                                         │
                  ┌──────────────────────┼──────────────────────┐
                  │                      │                      │
                  ▼                      ▼                      ▼
          ┌──────────────┐      ┌─────────────────┐    ┌────────────────┐
          │  Firmware    │      │  X-Plane plugin │    │   M5 simulator │
          │  (V4P/V4B)   │      │  (Mac/Win/Linux)│    │  (WASM browser)│
          │              │      │                 │    │                │
          │ ESP32-S3,    │      │ Loads onspeed_  │    │ Emscripten-    │
          │ FreeRTOS,    │      │ core directly,  │    │ compiled       │
          │ real sensors │      │ feeds X-Plane   │    │ onspeed_core,  │
          │              │      │ datarefs in     │    │ same renderer  │
          └──────┬───────┘      └─────────────────┘    └────────────────┘
                 │
                 │ wire format (display-serial / WebSocket JSON)
                 ▼
          ┌──────────────┐
          │ M5 hardware  │ ← multiple variants (Basic / Core2 /
          │              │   huVVer-AVI). Same firmware, different boxes.
          └──────────────┘

          ┌─────────────────────┐
          │  Replay tool (JS)   │ ← THE ONLY HAND-PORT
          │                     │
          │  Hand-port of       │  Browser tool, can't shell out.
          │  onspeed_core algos │  Gated by streaming goldens vs C++.
          │  in JavaScript      │
          └──────────┬──────────┘
                     │
                     │ CI gate (diff against host_main output)
                     ▼
          ┌──────────────────┐
          │  host_main CLI   │  Multi-subcommand binary that exposes
          │  (subprocess)    │  every onspeed_core algorithm. Used by
          │                  │  Python wrappers + JS regression tests.
          └──────────────────┘
                     ▲
                     │ subprocess shells
                     │
          ┌──────────┴──────────┐
          │  Python tools       │  Post-`PLAN_PYTHON_CONSOLIDATION.md`:
          │  (synth-record,     │  thin wrappers, no algorithm code.
          │   m5-replay, etc.)  │
          └─────────────────────┘
```

## What's NOT in any of these plans (out of scope here)

- The live `/indexer` page (separate tool, shares SVG components).
- Firmware development on the airplane (see `CLAUDE.md` at repo root).
- Audio synthesis (separate `onspeed_core/audio/` subsystem).
- Tone-decision logic (`ToneCalc`, governed by Vac's spec docs).

## Project ordering — what to ship in what sequence

```
  ┌─────────────────────────────────────┐
  │  Project A.0: host_main CLI         │  ← FIRST (~1-2 days)
  │  (PLAN_PYTHON_CONSOLIDATION Step 0) │     prerequisite for A + B
  └─────────────┬───────────────────────┘
                │
        ┌───────┴────────┐
        │                │   (parallel after A.0 lands)
        ▼                ▼
  ┌──────────────┐ ┌──────────────────┐
  │ Project A:   │ │ Project B:       │
  │ Python       │ │ WASM compile of  │
  │ consolidation│ │ onspeed_core     │
  │ (Steps 1-7)  │ │ (PLAN_WASM_CORE) │
  │ ~4-6 days    │ │ ~5-7 days        │
  └──────┬───────┘ └─────────┬────────┘
         │                   │
         └────────┬──────────┘
                  │
                  ▼
  ┌─────────────────────────────────────┐
  │  Project C: Replay Tool             │  ← AFTER A + B
  │  (PLAN_VIDEO_OVERLAY.md, Layer 1+)  │     Layer 1 → Layer 5,
  │                                     │     parallelizable
  └─────────────────────────────────────┘

  Plus a tiny CI step (PLAN_DRIFT_PREVENTION.md): WASM-vs-native
  parity check, ~half-day, lands once Project B Step 1 has bound
  one function.
```

**Why A and B are siblings**: A.0 (multi-subcommand host_main)
exposes the C++ API surface. A.1-7 wrap that surface from Python.
B exposes the same surface to JS via WASM. Both share host_main
as the binding target.

**Why neither blocks Project C strictly**: Replay layer work
(file-handle persistence, HUD widgets, drag-clip, export) doesn't
depend on the WASM driver or the Python wrappers being in place.
But landing Replay features without the WASM driver leaves the JS
hand-port in place forever, so we sequence A+B → C to consolidate
the foundation first.

## How to dispatch

Every plan doc has a "Dispatch prompts" section near the bottom.
Pick the one matching the work you're doing, fill in the
brackets, hand to an agent. Each dispatch is self-contained and
references the relevant plan from a fresh checkout.

## When in doubt

Ask Sam. "I'm not sure whether this is Project A's job or
Project C's" beats picking wrong. Same for any architecture call
not covered above.
