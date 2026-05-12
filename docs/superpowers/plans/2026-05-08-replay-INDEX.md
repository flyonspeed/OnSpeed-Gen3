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
| 0 | `2026-05-09-replay-retro.md` | **READ FIRST** | Trial-run retro after PR #512. Bugs that survived foundation, why tests missed them, architectural correction (lift `LogReplayTask` into `onspeed_core`). Now mostly historical — PR #512 shipped the redirected design. Still required reading for shape-of-mistake context. |
| 0a | `2026-05-09-replay-continue-prompt.md` | Historical | Hand-off prompt that bridged the redirect. PR #512 merged 2026-05-10 with the LogReplayTask lift applied. |
| 1 | `2026-05-08-video-overlay-AGENT-CONTEXT.md` | Active reference | Orientation, gotchas, two-layer architecture diagram. Note: "no hand-ports" is now read more strictly per retro (data-shape transformations also count). |
| 2 | `2026-05-08-python-consolidation.md` | Steps 0-2 ✅; 3-7 deferred | Project A: migrate Python algorithm code to `host_main` subprocess wrappers. Steps 3-7 deferred for the `LogReplayTask` lift. |
| 3 | `2026-05-08-firmware-log-replay-parity.md` | ✅ MERGED (#487/#490/#491) | Rate-adjusted accel EMA + synth flapsRawADC for old logs. Historical reference. |
| 4 | `2026-05-08-wasm-core.md` | Steps 0-2 ✅ (#496); 3-5 deferred | Project B1: `onspeed_core` → WASM. Steps 3-5 deferred for the `LogReplayTask` lift. |
| 5 | `2026-05-09-replay-m5-wasm.md` | ✅ MERGED — PR 1 (#507), PR 1.5 (#511), PR 2 (#512, 2026-05-10) | Project B2: M5-Display firmware → WASM, used end-to-end by the docs-site replay page at `/data-and-logs/replay/`. Foundation complete. |
| 6 | `2026-05-08-video-overlay-replay.md` | Layer 1+ ideas active; engine framing dead | Project C: Replay tool roadmap. Layer-1+ feature ideas (DataMark, ClipBuilder, export) still apply. The "in-browser engine port" framing is superseded by B1+B2. |
| 7 | `2026-05-08-cross-impl-drift-prevention.md` | Historical | One-paragraph summary of the streaming-goldens CI step. Mostly resolved by B1+B2. |

**Status snapshot (2026-05-12):** Foundation projects (A, B1, B2) all
shipped. Project C Layers 0/1/4/5 shipped 2026-05-12 via a merge cascade
of PRs #527 / #529 / #532. Next phase: see "Project ordering — current
state" and "Status snapshot (2026-05-12)" at the bottom of this doc.
Open follow-ups from PR #512: **#523** (umbrella for shared `ui-core/`
+ adapter pattern; PR-A first), **#521** (SD log captures wire-quantized
fields; firmware-side companion).

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

## Design principle: lift, don't copy

The C++ no-hand-ports rule has a JS-side counterpart that deserves
the same discipline. Web pages (`/indexer`, `/replay`, future log
analyzer, future calibration explorer) share rendering, helpers,
and the canonical state shape. They differ **only** in their input
source.

The shape we're building toward:

```
  ui-core/        ← shared UI: components, modes, geometry,
                    helpers, presentation filter. Pure functions
                    of M5State.
  adapters/       ← one adapter file per input source. Each
                    produces an M5State from its source format
                    (WebSocket record, SD log + cfg, mock fixture).
  pages/          ← compose `data source → adapter → ui-core`.
                    Nothing else.
```

The rules that hold this together:

- **One canonical state shape (`M5State`)** matches the M5
  firmware's native vocabulary. All adapters produce this shape;
  all renderers consume it.
- **No duplicate helpers.** If two pages need the same formatter,
  geometry constant, or smoothing filter, it lives in `ui-core/`
  exactly once. The next person who needs it imports it.
- **No parallel renderer families.** If a new page needs a
  slightly different field, extend `M5State` (or its adapters) —
  never fork a renderer.
- **Adapters are small and pure.** One function per input source.
  No UI code in adapters; no input-format knowledge in `ui-core/`.
- **"Library" is conceptual, not packaging.** Shared files in the
  repo imported by relative path. No npm package, no version pin,
  no separate test suite from consumers.

Why this matters: drift between `/indexer` and `/replay` is the
JS-side version of the same hand-port problem the C++ side
already solved. Two near-duplicate `mapPct2Display` functions,
two parallel mode-renderer families, two helpers files with
slightly different signatures — these grow by accident and rot
in place.

PR #512 surfaced exactly this drift mid-development. Issue **#523**
is the umbrella for consolidating; PR-A (mechanical `ui-core/`
extraction) is the first concrete step. Sibling issue **#521**
tracks the parallel firmware-side concern: SD logs capture
wire-quantized display values for fields like `Slip`, so offline
consumers can't recover the source signal. PR #221 (`flapsRawADC`)
is the proven template for fixing that family of fields.

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

## Project ordering — current state (2026-05-11)

**Foundation complete.** Projects A, B1, and B2 all shipped between
late-April and 2026-05-10. The replay tool consumes the C++ pipeline
end-to-end via WASM, no hand-ports.

```
  ┌─────────────────────────────────────┐
  │  Project A.0: host_main CLI         │  ✅ MERGED
  ├─────────────────────────────────────┤
  │  Project A: Python consolidation    │  ✅ Steps 0-2 (#498)
  │                                     │     Steps 3-7 deferred
  ├─────────────────────────────────────┤
  │  B1-prereq: LogReplay parity        │  ✅ MERGED (#487/#490/#491)
  │  (rate-adjusted accel EMA +         │
  │  synth flapsRawADC)                 │
  ├─────────────────────────────────────┤
  │  Project B1: onspeed_core → WASM    │  ✅ Steps 0-2 (#496);
  │                                     │     LogReplayTask lift (#512)
  ├─────────────────────────────────────┤
  │  Project B2: M5-Display fw → WASM   │  ✅ PR 1 (#507), PR 1.5 (#511),
  │  /data-and-logs/replay/ on docs     │     PR 2 (#512, 2026-05-10)
  └─────────────────────────────────────┘
```

### What PR #512 surfaced — the next phase of work

PR #512 was the first project to consume the foundation end-to-end.
Trial against real flight data exposed two follow-up tracks, both
filed and ready to schedule:

```
  ┌────────────────────────────────────────────────────┐
  │  Issue #523 — Shared JS UI library + adapters     │
  │  (the "lift, don't copy" principle in code)       │
  ├────────────────────────────────────────────────────┤
  │  PR-A  ▶ NEXT: extract ui-core/ (mechanical)      │
  │  PR-B  formalize canonical M5State                │
  │  PR-C  IndexerPage adopts M5State via adapter;    │
  │        delete tools/web/lib/modes.js              │
  │  PR-D  pattern documented for future pages        │
  └────────────────────────────────────────────────────┘

  ┌────────────────────────────────────────────────────┐
  │  Issue #521 — SD log captures wire-quantized      │
  │  values; offline smoothing can't recover signal.  │
  │  Firmware-side companion track.                    │
  │  PR #221 (flapsRawADC) is the proven template.    │
  └────────────────────────────────────────────────────┘
```

### What's next

**Sequence #523 PR-A before resuming Project C feature work.** PR-A
is mechanical (~1 day), zero behavior change, prevents the next
Project C step from forking another helper. PR-B and PR-C can run
in parallel with Project C.

**Schedule #521 against firmware-side rhythm.** Doesn't block
Project C. Lands when the firmware team is ready for a log-schema
bump (proven shape per PR #221).

**Project C** (PLAN_VIDEO_OVERLAY Layers 1-5: sync persistence,
HUD widgets, drag-clip, MP4 export, docs) resumes after PR-A.
Each Project C PR consumes `ui-core/` rather than forking helpers.

## How to dispatch

Every plan doc has a "Dispatch prompts" section near the bottom.
Pick the one matching the work you're doing, fill in the
brackets, hand to an agent. Each dispatch is self-contained and
references the relevant plan from a fresh checkout.

## When in doubt

Ask Sam. "I'm not sure whether this is Project A's job or
Project C's" beats picking wrong. Same for any architecture call
not covered above.

---

## Status snapshot (2026-05-12)

**Project A (Python consolidation)**: Steps 0-2 shipped. Steps 3-7
remain deferred for the LogReplayTask lift; no change since the
2026-05-09 retro.

**Project B (WASM core + LogReplayEngine)**: SHIPPED. WASM core
builds; LogReplayTask is the canonical replay engine; replay tool
consumes it. Engine-vs-task parity native test guards drift.

**Project C (Video Overlay Replay tool)**: Layers 0/1/4/5 SHIPPED.
Layer 2 (HUD widgets gallery) and Layer 3 (HUD chrome) still pending —
not blocked on anything; lower priority than Round 4 because the
indexer-corner overlay covers Vac's primary use case.

PRs that landed Layers 0/1/4/5:

- **PR #512** — Layer 0 engine + WASM pipeline (LogReplayTask lift)
- **PR #524** — PR-A: shared `ui-core/` extraction
- **PR #526** — PR-C: live indexer adopts `M5State` via adapter
- **PR #527** (merged 2026-05-12) — sync + clip persistence across
  reload, content-keyed by SHA-256 prefix of the log file
- **PR #529** (merged 2026-05-12) — `--target replay` mode in
  `scripts/build_web_bundle.py`; single `replay-bundle.js` served by
  the docs page instead of fan-out of relative-path imports
- **PR #532** (merged 2026-05-12) — composite MP4 export
  (source-faithful 4K HEVC + AAC passthrough + rotation handling),
  overlay-only export (5 modes + size selector: native 320×240 /
  20% / 30% / 50% of source), Mediabunny migration

### Round 4a (Mediabunny + worker)

- **Mediabunny migration**: SHIPPED in PR #532. Deprecated
  `mp4-muxer` and memory-hungry `mp4box.js` both removed; single
  MPL-2.0 dep with streaming reads handles demux + mux.
- **Worker-thread the export loop**: RETREATED. The worker branch
  (`feature/replay-worker`, tagged `replay-pre-worker-retreat` at
  the mediabunny tip) got 17 s vs 40 s wall clock plus a responsive
  UI, but every SVG-to-bitmap call failed with `InvalidStateError`.
  Root cause: Chrome's `createImageBitmap(svgBlob)` does not accept
  `image/svg+xml` despite the spec. Tracked as issue **#62**.
  Revisit after the SVG-rasterization architecture is redesigned
  (likely render SVG to `ImageBitmap` on the main thread, transfer
  to worker).

### Round 4b (ergonomics) — queued

In rough sequencing order:

1. **#72 — FileSystemFileHandle persistence** (next ship after this
   doc update). Three-pick-on-every-reload behavior is "bananas" per
   Sam 2026-05-12. Plan doc:
   `2026-05-12-filesystem-handle-persistence.md`.
2. **#63 — Frame-step keyboard scrub** (←/→ source-frame, NLE keymap)
3. **#64 — Multi-GoPro chapter ingest** (folder picker, auto-concat
   virtual file; builds on the directory picker from #72)
4. **#65 — Multi-anchor sync** (piecewise-linear for clock drift over
   long flights; uses frame-step for fine alignment)
5. **#69 — Clip timeline visualization + nudge UX** (render clip
   spans on the timeline, drag handles on endpoints)
6. **#71 — Bulldog deferred follow-ups** (rotation 90°/270° pixel
   swap; comment sweep across `mp4Export.js` + `ReplayPage.js`;
   helper dedup)

See `2026-05-08-video-overlay-replay.md` "Round 4 — what shipped
2026-05-12" for full detail on what landed and what's next.
