# INDEX — Replay Tool Plan Set

**Date:** 2026-05-08
**Owner:** Sam.

If you're a fresh agent (or fresh-session Sam) picking up this work,
**read these documents in this order**:

| # | Doc | Purpose |
|---|---|---|
| 1 | `2026-05-08-video-overlay-AGENT-CONTEXT.md` | Orientation. Gotchas, anti-patterns, build commands, the unified architecture diagram. **Read in full before touching code.** |
| 2 | `2026-05-08-python-consolidation.md` | Project A: migrate Python algorithm code to `host_main` subprocess wrappers. **Step 0 (host_main multi-subcommand CLI) is a prerequisite for the WASM project.** ~5-7 days, 7 small PRs. |
| 3 | `2026-05-08-firmware-log-replay-parity.md` | Project B-prereq: extract LogReplay's per-row pipeline into a platform-free `onspeed_core/replay/LogReplayEngine` (mirrors the AHRS task/engine split), then fix two ingest bugs inside it (rate-correct EMA, synth ADC when missing). ~3-4 days, 3 small PRs. **Must land before WASM Step 2.** |
| 4 | `2026-05-08-wasm-core.md` | Project B: compile `onspeed_core` to WebAssembly. Replay tool's JS UI loads the WASM module and calls into it for every algorithm — **zero hand-ports**. ~5-7 days, 5 small PRs. Lands after Python consolidation Step 0; then runs in parallel with Python Steps 1-7. |
| 5 | `2026-05-08-video-overlay-replay.md` | Project C: the Replay tool roadmap. Layered architecture (Layer 0 engine → Layer 5 export). Lands AFTER projects A+B. |
| 6 | `2026-05-08-cross-impl-drift-prevention.md` | What's left of drift-prevention after WASM lands: a tiny "WASM-vs-native parity check" CI step. Most of this doc is historical — explaining why we no longer need a streaming-goldens CI gate. |

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
        ┌────────────────┬───────────────────┼─────────────────────┬──────────────────┐
        │                │                   │                     │                  │
        ▼                ▼                   ▼                     ▼                  ▼
  ┌──────────┐   ┌─────────────┐   ┌──────────────────┐   ┌──────────────┐   ┌────────────────┐
  │ Firmware │   │ X-Plane     │   │ M5 simulator     │   │ host_main    │   │ onspeed_core   │
  │ (V4P/V4B)│   │ plugin      │   │ (full-featured   │   │ CLI binary   │   │ .wasm          │
  │          │   │ (Mac/Win/   │   │ WASM, browser)   │   │ (native)     │   │ (algorithm-only│
  │ ESP32-S3 │   │  Linux)     │   │                  │   │              │   │  WASM, browser)│
  │ native   │   │ native      │   │ Emscripten +     │   │ Python tools │   │                │
  │ C++,     │   │ C++         │   │ M5GFX rendering  │   │ shell out    │   │                │
  │ real     │   │             │   │                  │   │ to this      │   │                │
  │ sensors  │   │             │   │                  │   │              │   │                │
  └────┬─────┘   └─────────────┘   └──────────────────┘   └──────┬───────┘   └────────┬───────┘
       │                                                          │                    │
       │ wire format                                              │ subprocess         │ JS calls
       │ (display-serial / WebSocket JSON)                        ▼                    ▼
       ▼                                                  ┌──────────────┐    ┌────────────────┐
  ┌──────────────┐                                        │ Python tools │    │  Replay tool   │
  │ M5 hardware  │  multiple variants (Basic / Core2 /    │ (synth-      │    │  (browser)     │
  │              │  huVVer-AVI). Same firmware, different │  record,     │    │                │
  └──────────────┘  boxes.                                │  m5-replay,  │    │  Preact UI +   │
                                                          │  etc.)       │    │  WASM-bound    │
                                                          │ I/O glue     │    │  algorithms    │
                                                          │ only         │    └────────────────┘
                                                          └──────────────┘
```

**No hand-ports anywhere.** Every consumer either compiles
`onspeed_core` directly (firmware, X-Plane plugin, M5 simulator,
host_main), shells out to a binary that does (Python tools), or
loads a WASM build of it (Replay tool). Drift is impossible by
construction.

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
  ┌──────────────┐ ┌──────────────────────────────┐
  │ Project A:   │ │ Project B-prereq:            │
  │ Python       │ │ Firmware LogReplay parity    │
  │ consolidation│ │ (PLAN_FIRMWARE_LOG_REPLAY_   │
  │ (Steps 1-7)  │ │  PARITY) — engine extraction │
  │ ~4-6 days    │ │  + 2 ingest fixes ~3-4 days  │
  │              │ └─────────┬────────────────────┘
  │              │           │
  │              │           ▼
  │              │ ┌──────────────────────────────┐
  │              │ │ Project B:                   │
  │              │ │ WASM compile of onspeed_core │
  │              │ │ (PLAN_WASM_CORE)             │
  │              │ │ ~5-7 days                    │
  │              │ └─────────┬────────────────────┘
  └──────┬───────┘           │
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

**Why firmware LogReplay parity precedes WASM Step 2**: today the
JS Replay tool patches over two firmware-side gaps (rate-coupling
of the IMU EMA when log rate ≠ 208 Hz, and synthesizing a flap-pot
ADC sweep when the log lacks the column). Both belong in firmware
LogReplay. Once they land there, WASM Step 2's "delete the JS
variable-dt EMA + synth sweep" is a clean delete; the WASM build
inherits correct behavior. If we skipped the firmware fix, we'd be
moving the JS hack into a WASM-bound replay-mode wrapper that
diverges from what firmware LogReplay does on the same SD card —
the opposite of "drift impossible by construction".

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
