# INDEX — Replay Tool Plan Set

**Date:** 2026-05-08
**Owner:** Sam.

If you're a fresh agent (or fresh-session Sam) picking up this work,
**read these documents in this order**:

| # | Doc | Purpose |
|---|---|---|
| 1 | `2026-05-08-video-overlay-AGENT-CONTEXT.md` | Orientation. Gotchas, anti-patterns, build commands, the unified architecture diagram. **Read in full before touching code.** |
| 2 | `2026-05-08-python-consolidation.md` | The prerequisite project. Migrate Python algorithm code to `host_main` subprocess wrappers. **Land before Replay Layer 1+ work.** Sam confirmed breaking changes during migration are fine; only he uses these tools today. |
| 3 | `2026-05-08-video-overlay-replay.md` | The Replay tool roadmap. Layered architecture (Layer 0 engine → Layer 5 export). Sequenced step-by-step. Dispatch prompts per layer. |
| 4 | `2026-05-08-cross-impl-drift-prevention.md` | The streaming-goldens CI gate. After Python consolidation, the only remaining drift-vulnerable consumer is JS replay; this doc designs the mechanism that catches drift. |

## The one-sentence story

> **OnSpeed has one source of truth (`software/Libraries/onspeed_core/`)
> and four consumers. Three consumers (firmware, X-Plane plugin, M5
> WASM simulator) compile or link the source. One (JS replay) hand-ports
> it and gates against the source via CI streaming-goldens. Python
> tooling is I/O glue, calling the source via `host_main` subprocess
> wrappers. New algorithms land in C++ first.**

This is the elevator pitch. Everything in the four plans is in
service of this story.

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
  │  Project A: Python Consolidation    │  ← do FIRST
  │  (PLAN_PYTHON_CONSOLIDATION.md)     │     ~5-7 days, 7 PRs
  └─────────────┬───────────────────────┘
                │
                │ Step 0 (host_main multi-subcommand) is the
                │ prerequisite for everything else.
                │
                ▼
  ┌─────────────────────────────────────┐
  │  Project B: Streaming-Goldens Gate  │  ← do SECOND
  │  (PLAN_DRIFT_PREVENTION.md)         │     ~1-2 days
  └─────────────┬───────────────────────┘
                │
                │ Catches drift between C++ and JS replay.
                │
                ▼
  ┌─────────────────────────────────────┐
  │  Project C: Replay Tool             │  ← THIRD
  │  (PLAN_VIDEO_OVERLAY.md, Layer 1+)  │     Layer 1 → Layer 5,
  │                                     │     parallelizable
  └─────────────────────────────────────┘
```

Project A blocks B (B needs `host_main` extended). A doesn't
strictly block C — replay tool layer work can proceed in parallel —
but **landing C without A leaves the JS port unguarded against
drift**, so we sequence A → C anyway.

## How to dispatch

Every plan doc has a "Dispatch prompts" section near the bottom.
Pick the one matching the work you're doing, fill in the
brackets, hand to an agent. Each dispatch is self-contained and
references the relevant plan from a fresh checkout.

## When in doubt

Ask Sam. "I'm not sure whether this is Project A's job or
Project C's" beats picking wrong. Same for any architecture call
not covered above.
