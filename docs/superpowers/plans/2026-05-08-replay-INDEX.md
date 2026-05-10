# INDEX — Replay Tool Plan Set

**Date:** 2026-05-08 (retro added 2026-05-09)
**Owner:** Sam.

> **🛑 READ THE RETRO FIRST**: `2026-05-09-replay-retro.md`.
> The post-PR-#512 trial run exposed real bugs that survived the foundation
> PRs (#507, #511, #512). The retro documents what failed and the
> architectural correction needed (lift `LogReplayTask` into onspeed_core,
> kill JS-side hand-derivations of engine/wire input shapes, add real-flight
> fixture tests). Skipping it means repeating the same mistakes.

If you're a fresh agent (or fresh-session Sam) picking up this work,
**read these documents in this order**:

| # | Doc | Purpose |
|---|---|---|
| 1 | `2026-05-08-video-overlay-AGENT-CONTEXT.md` | Orientation. Gotchas, anti-patterns, build commands, the unified architecture diagram. **Read in full before touching code.** |
| 2 | `2026-05-08-python-consolidation.md` | Project A: migrate Python algorithm code to `host_main` subprocess wrappers. **Step 0 (host_main multi-subcommand CLI) is a prerequisite for the WASM project.** ~5-7 days, 7 small PRs. |
| 3 | `2026-05-08-firmware-log-replay-parity.md` | Project B-prereq: replay shows what the M5 saw. Build a rate-adjusted accel EMA so 50 Hz log data produces wire-equivalent output (firmware filter τ at the right rate); LogReplayEngine uses it; synth `flapsRawADC` for old logs. **No log schema change. No "presentation" smoothing.** ~2 days, 3 small PRs. Must land before WASM Step 2. Plan is v3 — see "What we got wrong, twice" for v1/v2 history (PR #475 closed without merge). |
| 4 | `2026-05-08-wasm-core.md` | Project B1: compile `onspeed_core` (algorithm layer) to WebAssembly. Python tools and CI regression also consume it via host_main; the Replay tool consumes it transitively through the M5 firmware (B2). ~5-7 days, 5 small PRs. Lands after Python consolidation Step 0; then runs in parallel with Python Steps 1-7. |
| 5 | `2026-05-09-replay-m5-wasm.md` | Project B2: compile the **M5-Display firmware** (state-machine layer over the wire) to WebAssembly. Same source that flashes to M5/huVVer hardware. Replay tool feeds wire bytes in, reads display-state vars out, paints SVG from those values. **Two invariants:** (1) M5 firmware owns rendering (PR 1: WASM build target). (2) `LogReplayEngine` produces complete wire frames (PR 1.5: field-completeness audit + golden test). ~4-6 days, 4 PRs (PR 1 → PR 1.5 → PR 2 → PR 3). Lands after B1; required by C. |
| 6 | `2026-05-08-video-overlay-replay.md` | Project C: the Replay tool roadmap. Layered architecture (Layer 0 engine → Layer 5 export). Lands AFTER projects A + B1 + B2. |
| 7 | `2026-05-08-cross-impl-drift-prevention.md` | What's left of drift-prevention after WASM lands: a tiny "WASM-vs-native parity check" CI step. Most of this doc is historical — explaining why we no longer need a streaming-goldens CI gate. |

## The one-sentence story (post-foundation)

> **OnSpeed has two sources of truth, layered: `onspeed_core` for
> pure algorithms, and the **M5-Display firmware** for the
> state-machine over the wire. Every "shows what the M5 saw"
> consumer (Replay tool, X-Plane plugin, M5 hardware, huVVer-AVI,
> tools/m5-replay) compiles or loads the M5 firmware itself. Every
> "needs the algorithms" consumer (Python tools, CI regression)
> shells out to host_main, which compiles `onspeed_core`. NO
> hand-ports of either layer. Drift impossible by construction.**

This is the elevator pitch. Everything in the seven plans is in
service of this story. The architecture **collapses beautifully**
after Projects A + B1 + B2 land.

## The unified architecture (two layers)

```
                       ┌───────────────────────────────────┐
                       │  software/Libraries/onspeed_core  │
                       │  Layer 1: pure algorithms          │
                       │  smoothing, AOA calc, %lift,       │
                       │  wire encode/decode, etc.          │
                       │  platform-free C++                 │
                       └────────┬───────────────┬───────────┘
                                │               │
                                │               │ Python / CI
                                │               ▼
                                │       ┌───────────────────┐
                                │       │ host_main CLI     │
                                │       │ (native binary)   │
                                │       └────────┬──────────┘
                                │                │ subprocess
                                │                ▼
                                │       ┌───────────────────┐
                                │       │ Python tools      │
                                │       │ (synth-record,    │
                                │       │  m5-replay,       │
                                │       │  CI regression,   │
                                │       │  calibration      │
                                │       │  explorer)        │
                                │       └───────────────────┘
                                │
                                ▼
                       ┌──────────────────────────────────┐
                       │  software/OnSpeed-M5-Display     │
                       │  Layer 2: state machine over wire│
                       │  20 Hz graphics, 2 Hz numbers,    │
                       │  Slip / DecelRate / gHistory,     │
                       │  mode dispatch (5 modes),         │
                       │  IasIsValid edge handling, etc.   │
                       │  Consumes Layer 1; adds time +    │
                       │  state machine; OWNS the          │
                       │  "what the pilot saw" contract.   │
                       └─────┬───────┬────────┬────────┬───┘
                             │       │        │        │
              ┌──────────────┘       │        │        └──────────────┐
              │ native build         │        │ Emscripten WASM       │ native build
              ▼                      │        ▼                       ▼
       ┌─────────────┐               │  ┌─────────────────┐    ┌──────────────┐
       │ M5 hardware │               │  │  Replay tool    │    │ X-Plane      │
       │ ESP32 +     │               │  │  (browser)      │    │ plugin       │
       │ ILI9342C    │               │  │                 │    │ (Mac/Win/    │
       │             │               │  │  Preact UI +    │    │  Linux)      │
       │ Basic /     │               │  │  M5 firmware    │    │              │
       │ Core2 /     │               │  │  WASM +         │    │ Direct C++   │
       │ huVVer-AVI  │               │  │  SVG render     │    │ link to      │
       │             │               │  │  per mode       │    │ M5 source    │
       └─────────────┘               │  └─────────────────┘    └──────────────┘
                                     │
                                     │ native build (SDL/desktop)
                                     ▼
                              ┌──────────────────┐
                              │ tools/m5-replay  │
                              │ (Python harness  │
                              │  drives M5       │
                              │  firmware via    │
                              │  USB-TTL)        │
                              └──────────────────┘
```

**No hand-ports anywhere.** Every "shows what the M5 saw" consumer
either compiles M5 firmware natively (M5 hardware, huVVer-AVI,
X-Plane plugin) or loads its WASM build (Replay tool). Every
"needs algorithms" consumer compiles `onspeed_core` directly
(firmware, X-Plane plugin) or shells out to host_main (Python
tools). Drift is impossible by construction.

**Why two layers, not one.** `onspeed_core` is platform-free
algorithm code (no time, no UI cadence, no state machine — just
"given inputs, return outputs"). The M5 firmware is a **state
machine over time**: 20 Hz graphics tick, 2 Hz numbers snapshot,
local SavGol-on-IAS for decel rate, ring buffers, mode dispatch.
That state machine is what makes "show what the pilot saw" mean
something specific and reproducible. Cramming it into
`onspeed_core` would muddy the algorithm/state-machine boundary.
Asking each consumer to reimplement it is the hand-port trap. The
two-layer split is the cleanest cut: algorithms below, state
machine in the middle, presentation on top.

## What's NOT in any of these plans (out of scope here)

- **The live `/indexer` page.** It works today over WebSocket JSON
  (not the wire format) and is the live debug viewer. Conceptually
  it's a sibling consumer of Layer 2 (could migrate to feed M5
  firmware WASM via WebSocket bytes once that exists), but the
  migration is **explicitly out of scope** — `/indexer` ships as is,
  no commitment to migrate. If/when we do, it's a fresh plan.
- Firmware development on the airplane (see `CLAUDE.md` at repo root).
- Audio synthesis (separate `onspeed_core/audio/` subsystem).
- Tone-decision logic (`ToneCalc`, governed by Vac's spec docs).

## Project ordering — what to ship in what sequence

```
  ┌─────────────────────────────────────┐
  │  Project A.0: host_main CLI         │  ← FIRST (~1-2 days)
  │  (PLAN_PYTHON_CONSOLIDATION Step 0) │     prerequisite for A + B1
  └─────────────┬───────────────────────┘
                │
        ┌───────┴────────┐
        │                │   (parallel after A.0 lands)
        ▼                ▼
  ┌──────────────┐ ┌──────────────────────────────┐
  │ Project A:   │ │ Project B1-prereq:           │
  │ Python       │ │ Firmware LogReplay parity    │
  │ consolidation│ │ (PLAN_FIRMWARE_LOG_REPLAY_   │
  │ (Steps 1-7)  │ │  PARITY v3) — rate-adjusted  │
  │ ~4-6 days    │ │  accel EMA + synth ADC ~2d   │
  │              │ │ STATUS: merged (PRs #487/    │
  │              │ │  #490/#491)                  │
  │              │ └─────────┬────────────────────┘
  │              │           │
  │              │           ▼
  │              │ ┌──────────────────────────────┐
  │              │ │ Project B1:                  │
  │              │ │ WASM compile of onspeed_core │
  │              │ │ (PLAN_WASM_CORE)             │
  │              │ │ ~5-7 days                    │
  │              │ │ STATUS: Steps 0/1/2 merged   │
  │              │ │  (PRs #496 + earlier)        │
  │              │ └─────────┬────────────────────┘
  │              │           │
  │              │           ▼
  │              │ ┌──────────────────────────────┐
  │              │ │ Project B2:                  │
  │              │ │ WASM compile of M5-Display   │
  │              │ │ firmware (state-machine      │
  │              │ │ layer)                       │
  │              │ │ (PLAN_REPLAY_M5_WASM)        │
  │              │ │ ~3-5 days, 3 PRs             │
  │              │ │ STATUS: this plan, design    │
  │              │ └─────────┬────────────────────┘
  └──────┬───────┘           │
         │                   │
         └────────┬──────────┘
                  │
                  ▼
  ┌─────────────────────────────────────┐
  │  Project C: Replay Tool             │  ← AFTER A + B1 + B2
  │  (PLAN_VIDEO_OVERLAY.md, Layer 1+)  │     Layer 1 → Layer 5,
  │                                     │     parallelizable
  └─────────────────────────────────────┘

  Plus a tiny CI step (PLAN_DRIFT_PREVENTION.md): WASM-vs-native
  parity check, ~half-day, lands once Project B1 Step 1 has bound
  one function.
```

**Why A and B1 are siblings**: A.0 (multi-subcommand host_main)
exposes the C++ API surface. A.1-7 wrap that surface from Python.
B1 exposes the same surface to JS via WASM. Both share host_main
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

**Why B2 follows B1**: B2 (M5 firmware WASM) compiles M5 source
that already imports `onspeed_core` headers. The M5's display
state machine is the *consumer* of the algorithm layer, not a
parallel to it. B1 lands the algorithm WASM artifact; B2 lands
the state-machine WASM artifact (which links B1's code).
Symmetrically, B2 isn't strictly required for the algorithm
artifact to be useful (Python tools and host_main don't need it);
it IS required for the Replay tool to render "what the pilot
saw" correctly.

**Why none blocks Project C strictly**: Replay layer work
(file-handle persistence, HUD widgets, drag-clip, export) doesn't
depend on the WASM drivers being in place. But landing Replay
features without WASM drivers leaves JS hand-ports in place
forever, so we sequence A + B1 + B2 → C to consolidate the
foundation first.

## How to dispatch

Every plan doc has a "Dispatch prompts" section near the bottom.
Pick the one matching the work you're doing, fill in the
brackets, hand to an agent. Each dispatch is self-contained and
references the relevant plan from a fresh checkout.

## When in doubt

Ask Sam. "I'm not sure whether this is Project A's job or
Project C's" beats picking wrong. Same for any architecture call
not covered above.
