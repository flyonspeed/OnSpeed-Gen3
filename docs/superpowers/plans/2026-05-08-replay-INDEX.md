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

| # | Doc | Status | Purpose |
|---|---|---|---|
| 0 | `2026-05-09-replay-retro.md` | **READ FIRST** | Trial-run retro after PR #512. Bugs that survived foundation, why tests missed them, architectural correction (lift `LogReplayTask` into `onspeed_core`). |
| 0a | `2026-05-09-replay-continue-prompt.md` | **READ SECOND** | Hand-off prompt to next agent. A/B-toggle plan. Cherry-pick instructions for shipping the merge-worthy fixes from PR #512 alone. |
| 1 | `2026-05-08-video-overlay-AGENT-CONTEXT.md` | Active reference | Orientation, gotchas, two-layer architecture diagram. Note: "no hand-ports" is now read more strictly per retro (data-shape transformations also count). |
| 2 | `2026-05-08-python-consolidation.md` | Steps 0-2 ✅; 3-7 deferred | Project A: migrate Python algorithm code to `host_main` subprocess wrappers. Steps 3-7 deferred for the `LogReplayTask` lift. |
| 3 | `2026-05-08-firmware-log-replay-parity.md` | ✅ MERGED (#487/#490/#491) | Rate-adjusted accel EMA + synth flapsRawADC for old logs. Historical reference. |
| 4 | `2026-05-08-wasm-core.md` | Steps 0-2 ✅ (#496); 3-5 deferred | Project B1: `onspeed_core` → WASM. Steps 3-5 deferred for the `LogReplayTask` lift. |
| 5 | `2026-05-09-replay-m5-wasm.md` | PR 1 ✅ (#507), PR 1.5 ✅ (#511); PR 2 ⚠️ unmerged (#512), redirected per retro | Project B2: M5-Display firmware → WASM. The core foundation IS shipped; the UI integration approach in PR 2/3 is what got redirected. |
| 6 | `2026-05-08-video-overlay-replay.md` | Layer 1+ ideas active; engine framing dead | Project C: Replay tool roadmap. Layer-1+ feature ideas (DataMark, ClipBuilder, export) still apply. The "in-browser engine port" framing is superseded by B1+B2. |
| 7 | `2026-05-08-cross-impl-drift-prevention.md` | Historical | One-paragraph summary of the streaming-goldens CI step. Mostly resolved by B1+B2. |

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

## Project ordering — current state (2026-05-09)

```
  ┌─────────────────────────────────────┐
  │  Project A.0: host_main CLI         │  ✅ MERGED
  └─────────────────────────────────────┘
                  │
        ┌─────────┴────────┐
        ▼                  ▼
  ┌──────────────┐  ┌─────────────────────────────────┐
  │ Project A:   │  │ B1-prereq: Firmware LogReplay   │
  │ Python       │  │ parity (rate-adjusted accel EMA │
  │ consolidation│  │ + synth flapsRawADC for old logs)│
  │ Steps 0-2 ✅ │  │ ✅ MERGED (#487/#490/#491)      │
  │ Steps 3-7    │  └─────────────┬───────────────────┘
  │ deferred for │                │
  │ LogReplayTask│                ▼
  │ lift below   │  ┌─────────────────────────────────┐
  │              │  │ Project B1: onspeed_core → WASM │
  │              │  │ Steps 0/1/2 ✅ MERGED (#496)    │
  │              │  │ Steps 3-5 deferred for          │
  │              │  │ LogReplayTask lift below        │
  │              │  └─────────────┬───────────────────┘
  │              │                │
  │              │                ▼
  │              │  ┌─────────────────────────────────┐
  │              │  │ Project B2: M5-Display fw → WASM│
  │              │  │ PR 1 ✅ MERGED (#507)           │
  │              │  │ PR 1.5 ✅ MERGED (#511)         │
  │              │  │ PR 2 ⚠️ open (#512), redirected  │
  │              │  │ per retro                       │
  │              │  └─────────────┬───────────────────┘
  └──────┬───────┘                │
         └────────────┬───────────┘
                      │
                      ▼
  ┌────────────────────────────────────────────────────┐
  │  ▶ NEXT UP: lift LogReplayTask into onspeed_core   │
  │                                                     │
  │  Move sketch_common/src/tasks/LogReplay.cpp        │
  │  Process_() into onspeed_core, expose via embind.  │
  │  Replay tool routes through it instead of JS-side  │
  │  rowObjAt + buildDisplayInputs hand-ports.         │
  │                                                     │
  │  Sam's approach: A/B-toggle in the replay UI so    │
  │  the new C++ path can be visually compared with   │
  │  the existing JS path on real flight data BEFORE   │
  │  deleting the JS path. See retro + continue prompt.│
  └────────────────────────────────────────────────────┘
                      │
                      ▼
  ┌────────────────────────────────────────────────────┐
  │  Project C: Replay Tool layer 1+                   │
  │  (file-handle persistence, ClipBuilder polish,     │
  │   MP4 export, etc.)                                │
  └────────────────────────────────────────────────────┘
```

### What's left before "correct by construction"

1. **Carve out merge-worthy fixes from PR #512 into a small PR**
   (cfg-bindings AOA polynomial round-trip, mode-id refresh on
   pause, `.wasm` MIME, delete-Module workaround). The polynomial
   fix is critical — it's a real bug in `bindings.cpp::parseConfigVal`
   that affects EVERY consumer. Add a cfg round-trip test so it
   can't regress.

2. **`LogReplayTask` lift** (the architectural correction). Lift
   `sketch_common/src/tasks/LogReplay.cpp::Process_()` into
   `onspeed_core/src/replay/LogReplayTask.{h,cpp}`. Expose via
   embind. Add A/B toggle in replay UI. Sam compares JS vs C++
   paths on real flight data.

3. **Three new test types** (per retro): real-flight fixture,
   cfg round-trip, hysteretic state-machine fixture.

4. **Delete JS hand-ports** once C++ path is confirmed equivalent
   or better.

5. **Project C feature work** (file persistence, clip builder, MP4
   export). Mostly independent of the foundation; can run
   anytime.

## How to dispatch

Every plan doc has a "Dispatch prompts" section near the bottom.
Pick the one matching the work you're doing, fill in the
brackets, hand to an agent. Each dispatch is self-contained and
references the relevant plan from a fresh checkout.

## When in doubt

Ask Sam. "I'm not sure whether this is Project A's job or
Project C's" beats picking wrong. Same for any architecture call
not covered above.
