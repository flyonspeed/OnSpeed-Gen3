# PLAN_VIDEO_OVERLAY.md — OnSpeed Video Replay Tool

**Branch:** `sam/video-overlay` (pushed to `flyonspeed/OnSpeed-Gen3`).
**Status:** Phase 1–4 + DataMark/Clip system shipped. Next chunk
follows the layered architecture below.
**Form factor:** static-build SPA (deploy to `flyonspeed.org/replay`
or similar). Electron deferred until SPA reveals a concrete need.
**Date:** 2026-05-08.
**Owner:** Sam.

This plan documents the OnSpeed video-replay/overlay tool and the
sequenced roadmap to a complete pilot-facing analysis app. Read this
first; everything below is the source of truth.

---

## What this tool does (one paragraph)

A pilot uploads a flight video (`.mp4`), the matching SD-card log
(`.csv`), and the OnSpeed XML config (`.cfg`). The page time-syncs them
via a single anchor (auto-detected at the first crosswind turn,
overridable by the pilot). With sync established, the same Preact mode
renderers from `/indexer` overlay the video at every frame, fed by a
faithful in-browser port of `tools/onspeed_py/{config,percent_lift,
log_replay}.py`. The pilot can:

- Watch the flight with the OnSpeed indexer rendered over the video
- Switch between Energy / Attitude / Decel modes
- Click anywhere on the IAS timeline to seek the video
- Pause-and-re-attach when sync drifts
- Browse every DataMark dropped during flight (Jump / Clip 30s / Clip 60s)
- Define arbitrary clip ranges from playhead and Export-all in one click
- Export full-flight or per-clip MP4 (faster-than-realtime via WebCodecs)

The tool is **not** built into firmware. The bundler explicitly
excludes the replay code so the firmware bundle stays slim.

---

## The architectural invariant

> **For any given `(log_row, config)`, all four implementations
> (firmware C++, Python tools, JavaScript replay, M5 WASM simulator)
> compute the same `record`** — the bag of fields the indexer
> renders against (IAS, AOA, percentLift, band-edge anchors,
> smoothed accels, etc.).

This is bedrock. **No pilot-facing layer above can be trusted unless
this invariant holds.** A drift between firmware and replay means
"what the airplane showed in flight" disagrees with "what the replay
shows after landing," which would silently corrupt every analysis
session.

The strategy for enforcing it: **JSON spec fixtures, run by all four
implementations, gated by CI.** See `PLAN_DRIFT_PREVENTION.md` for
the full design. Short version:

```
test/spec_fixtures/
  percent_lift/basic_clean.json     ← inputs + expected outputs
  display_anchors/v4p_clean.json    ← same shape
  ema_smoothing/50hz_taps.json
  ...
```

Each language has a small runner that walks the directory, calls its
production implementation, asserts within tolerance. CI runs all
four. Drift is impossible to merge.

This is **Layer 0** in the architecture below. Everything else builds
on top.

---

## Layered architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 5: EXPORT PIPELINE                                         │
│  WebCodecs demux → render → encode. Faster than realtime.        │
│  Independent of layers 1-4 except the canvas it consumes.        │
│  → can be designed by a separate agent in isolation              │
├─────────────────────────────────────────────────────────────────┤
│ Layer 4: TIMELINE INTERACTIONS                                   │
│  Drag-to-define-clip on IAS strip. Click-to-seek (existing).     │
│  Shift-click to override anchor (existing). DataMark navigation. │
├─────────────────────────────────────────────────────────────────┤
│ Layer 3: HUD CHROME                                              │
│  Layout state, presets, per-widget on/off toggles, drag-to-      │
│  position with snap-to-anchor, "Edit layout" mode.               │
├─────────────────────────────────────────────────────────────────┤
│ Layer 2: HUD WIDGETS                                             │
│  Pure (record) → SVG components: airspeed tape, altitude+VSI,    │
│  attitude indicator, slip ball, indexer (already done).          │
│  → designed in a Storybook-style side window (NOT against video) │
├─────────────────────────────────────────────────────────────────┤
│ Layer 1: SYNC                                                    │
│  Establish + persist + nudge the linear (log_t ↔ video_t) map.   │
│  File-handle persistence via File System Access API.             │
│  ± 0.1 s / ± 1 s nudge buttons. Pause/Attach (existing).         │
├─────────────────────────────────────────────────────────────────┤
│ Layer 0: REPLAY ENGINE     (BEDROCK — must match firmware)       │
│  (log_row, cfg) → record. Body-angle / alpha_0 / EMA / lever     │
│  sweep / data marks. Governed by spec fixtures + CI.             │
└─────────────────────────────────────────────────────────────────┘
```

**Each layer:**

1. Has its own test gate (spec fixtures, render-smoke, click-handler tests, etc.).
2. Can be developed in isolation (the agent prompt for layer N doesn't need to load video / log / config; it needs the previous layer's API + a mock).
3. Has a single source of truth file or directory; future agents extend that, don't fork it.

---

## Sequencing roadmap

In dependency order. Each step is independently shippable. Each step
has a recommended agent dispatch (see "Dispatch templates" at the
bottom).

### Step 1 — Harden Layer 0 (drift prevention scaffolding)

**Why first:** if the engine drifts between languages, every layer
above is moot. Get this right before adding more pilot-facing
features.

**What ships:**
- `test/spec_fixtures/percent_lift/` directory with ~5 fixtures (clean detent, alpha_0 negative, alpha_stall fallback, NaN AOA, IAS-invalid).
- Three runners: C++ Unity, Python pytest, JS node test runner.
- CI gate that fails any PR where they disagree.
- Same shape extended to `display_anchors`, `ema_smoothing`, `flap_detection`, `config_parse`.

**Owner:** new agent dispatch. Spec lives in `PLAN_DRIFT_PREVENTION.md`.
**Effort:** ~3 days.
**Blocks:** nothing today (current engine works), but blocks confidence
in Layers 2+ once we add new behavior.

### Step 2 — Layer 1 polish (sync persistence + nudge)

**Why early:** Vac is going to use this tool repeatedly. If every
session requires re-picking files and re-syncing, friction kills
adoption. Quick wins here pay back daily.

**What ships:**
- **File-handle persistence via File System Access API.** When Vac
  picks files, store the `FileSystemFileHandle` in IndexedDB. Next
  session, prompt "Re-grant access to flight.mp4?" with a single
  click instead of full re-pick.
- **Sync nudge buttons**: ± 0.1 s, ± 1 s next to the status text. Each
  click adjusts `sync.videoTakeoffSec` so the offset shifts by the
  named amount. Useful when auto-detect lands you 3 frames early.
- **Multi-anchor support** (deferred): if Vac uploads an edited reel
  with a cut, he needs more than one anchor. Defer to when this
  actually breaks.

**Owner:** small agent dispatch on existing `/replay` page.
**Effort:** ~half-day.
**Blocks:** nothing.

### Step 3 — Layer 2 widgets (in their own window)

**Why third:** widgets are the most visible new feature, but designing
them against live video makes iteration brutal. Build them in a
storybook page first.

**What ships:**

a) **A widget gallery page.** New file `tools/web/widget-gallery.html`
   served by the dev-server. Lists every widget in the catalog,
   renders each against a fixed mock record, shows the mock in a
   side panel. Hot-reloadable: tweak a widget, refresh, see it
   update without touching video.

b) **The widget catalog.** New directory `tools/web/lib/replay/widgets/`.
   Each file exports one pure component: `(record, style) → SVG`.
   - `HudIndexer.js` — the existing Mode0 indexer extracted into a
     standalone widget (it already is, ~just re-exposed)
   - `HudAirspeedTape.js` — left side, vertical scrolling tape, tick
     marks every 10 kt, label every 20
   - `HudAltitudeTape.js` — right side, vertical scrolling tape, tick
     every 100 ft
   - `HudVsi.js` — VSI carrot, ±2000 fpm range
   - `HudSlipBall.js` — extracted from the existing Mode0 SlipBall
   - `HudAttitude.js` — pitch/roll horizon as a HUD overlay (see-
     through, no solid fill)
   - `HudHeading.js` — horizontal heading tape, top edge. Reads from
     `record.magHeadingDeg` — sourced from `vnYawDeg` (VN-300) or
     `efisMagHeading` (other EFIS); skipped if absent.

c) **Widget-level render-smoke tests.** Each widget gets a fixture
   record and a snapshot test that locks visual output. Same pattern
   as the existing `tools/web/test/render-smoke.mjs`.

**Owner:** widget design is its own thing; aesthetic decisions
involved. **OnSpeed-original styling, not G3X chrome.** No V-speed
bugs, no color-zone arcs, no gradients. Minimal HUD elements.

**Effort:** ~2-3 days for an agent who can iterate visually with you.
The widget gallery page accelerates this dramatically — Sam can
review each widget in isolation in seconds.

### Step 4 — Layer 3 chrome (presets, toggles, drag-position)

**What ships:**
- **Layout JSON files.** `tools/web/lib/replay/layouts/{full_hud,minimal,indexer_only}.json`. Each declares `[{ widgetId, anchor, offset, size }]`.
- **Per-widget toggle UI.** Row of checkboxes under the mode buttons. Stored in component state, persists to localStorage.
- **Drag-to-position.** "Edit layout" button enters a mode where each visible widget gets a drag handle + dashed outline. Drag = move (clamped to viewport, snaps to 8 anchor points within 16 px). Click outside = exit edit mode.
- **Reset to preset** button.
- **Critical hookup:** export pipeline must read live `overlayPos` per widget so exported video matches what the pilot sees.

**Default loadout on first visit:** indexer + airspeed tape + altitude/VSI + slip + attitude. Heading off (depends on EFIS).

**Owner:** UI-heavy agent dispatch; needs Layer 2 widgets shipped first.
**Effort:** ~1-2 days.

### Step 5 — Layer 4 timeline interactions

**What ships:**
- **Drag-to-define-clip on IAS strip.** Pointer-event-based. > 4 px movement during pointerdown = drag start. Drag releases create a new entry in the ClipBuilder list with the dragged range.
- **Plain click** still seeks video (existing). **Shift-click** still overrides log anchor (existing).
- **Visual feedback during drag**: translucent yellow rect from drag-start to current pointer.
- **Drag-to-resize an existing clip** (stretch goal): drag the edges of an existing clip's range on the timeline.

**Owner:** small agent dispatch on `LogTimeline` component.
**Effort:** ~half-day.

### Step 6 — Layer 5 faster-than-realtime export

**Why last:** export is the most complex single change but it's
**independent** of everything above. As long as Layers 0-4 produce a
canvas with the right pixels, an agent can build the export pipeline
in isolation against any canvas source.

**Architecture pivot — drop MediaRecorder. Go WebCodecs.**

The existing pipeline ties export wall-clock to the video's playback
rate. Browser plays at 1×, MediaRecorder records what it sees, output
takes as long as the video. The pivot:

```
File handle (Blob via File System Access API)
   ↓
MP4Box.js demuxer       — parses MP4 container, yields encoded chunks
   ↓
VideoDecoder            — hardware-accelerated H.264 decode → VideoFrame
   ↓
canvas.drawImage        — paint frame + overlay SVG to canvas
   ↓
new VideoFrame(canvas)  — wrap composed canvas as a new VideoFrame
   ↓
VideoEncoder            — hardware-accelerated H.264 encode
   ↓
mp4-muxer (~30 KB JS)   — write MP4 container as we go
   ↓
Blob → download
```

No browser playback in the loop. Decoder pulls frames as fast as
hardware allows; encoder pushes frames as fast as hardware allows.

**Realistic speedup on a 2024 Mac**: 3-10× realtime for a 4K → 1080p
H.264 transcode. A 30 s clip exports in 3-10 s. A 95 min flight in
~10-30 min. Hardware-accelerated via the GPU video unit
(VideoToolbox on Mac, DXVA2/D3D11 on Windows).

**Browser support**: Chrome 94+, Edge, Safari 16.4+. Firefox: behind
a flag, missing in stable. Vac is on Chrome — fine.

**File metadata**: `mp4-muxer` accepts a `metadata` option for the
`udta` atom. Embed at export time:
```js
{
  title:    "RV-10 Pattern Practice",
  artist:   "FlyOnSpeed Replay Tool",
  comment:  "OnSpeed v4.23 / clip 03 / log 612-1112 / synced at crosswind turn",
  encoder:  "onspeed-replay v0.1.0"
}
```

Plus a sidecar `.onspeed.json` per export (see "Metadata sidecar"
below).

**Audio**: parallel pipeline. `MP4Box.js` yields audio packets,
`AudioDecoder` → `AudioEncoder` (re-encode is required because we
might be transcoding from one codec to another), `mp4-muxer` writes
the audio track interleaved with video.

**Owner:** new agent dispatch. Has to read WebCodecs + MP4Box.js docs;
not paint-by-numbers. Estimate ~2-3 days.

**Fallback**: keep the current MediaRecorder/WebM path as a "live
preview only" feature. If WebCodecs ever flakes for a particular
file, fall back to MediaRecorder for that one. No transcode-to-MP4
fallback (ffmpeg-wasm) — the bundle cost isn't worth it given
WebCodecs covers every browser Vac uses.

### Step 7 — Documentation

**Three docs:**

- **`docs/site/docs/tools/replay.md`** — user-facing. "What is the
  replay tool, how do I use it, sync workflow, data marks, exporting
  clips." Linked from the docs site nav.
- **`docs/superpowers/specs/2026-05-08-replay-architecture.md`** — design.
  The 6-layer architecture, where things live, how to extend, the
  invariant.
- **`docs/site/docs/reference/replay-spec.md`** — reference. The
  JSON fixture format from `PLAN_DRIFT_PREVENTION`. List every
  `record` field and its source.

**Owner:** can be the same agent that ships Layer 5, OR a docs-only
dispatch.

---

## Form factor — static-build SPA at flyonspeed.org/replay

**Decision**: ship as a static-built single-page app, deployable
anywhere. No install, no clone, no terminal. Vac bookmarks a URL.

**Why static SPA before Electron:**
- Validates whether Electron is even needed. Vac will tell us by
  using it.
- Same code we have today + a build entry. ~half-day.
- File System Access API gives us 80% of what Electron's native
  filesystem buys (handle persistence, no re-pick on reload).
- WebCodecs gives us hardware-accelerated MP4 export without an
  ffmpeg binary.

**If Vac hits a wall** (17 GB video stutters during scrub, or some
codec we can't decode in WebCodecs, or he hates re-granting file
permission), we promote to Electron. Same React tree; Electron just
gives us native filesystem + bundled ffmpeg + signed installer +
auto-update. ~3 days of work when the time comes.

The Electron honest-cost list (signing, notarization, IPC plumbing,
slower dev loop, CI matrix complexity) is in the prior-conversation
notes. Defer until forced.

---

## Metadata sidecar (ship from day 1)

Every export writes two files:

```
flight_2026-04-11_clip03.mp4
flight_2026-04-11_clip03.onspeed.json
```

The JSON carries:

```json
{
  "schemaVersion":  1,
  "tool":           { "name": "onspeed-replay", "version": "0.1.0" },
  "exportedAt":     "2026-05-08T12:34:56Z",
  "sourceVideo":    "cleaned_4_11_2026_sam_aoa.mp4",
  "sourceLog":      "sam_onspeed_aoa_4_11_2026.csv",
  "sourceConfig":   "onspeed2_latest.cfg",
  "configHash":     "sha256:...",
  "logHash":        "sha256:...",
  "syncAnchor":     {
    "kind": "crosswind",
    "videoSec": 488.4,
    "logMs":    550690
  },
  "clipRangeMs":    { "startMs": 612400, "endMs": 1112400 },
  "widgets":        ["indexer", "airspeed", "altitude", "slip"],
  "layoutPositions": { ... }
}
```

Future tools can re-derive raw data from any export. And it's a
permanent record of provenance — six months from now, you can answer
"what config was Vac flying when he made this clip?"

---

## Existing infrastructure (re-use, don't rebuild)

Inventory of relevant pieces already in the repo. Future agents
should know about these instead of duplicating effort.

| Thing | Path | What it does |
|---|---|---|
| **Snapshot regression harness** | `tools/regression/{host_main.cpp,run_snapshot.py,fixtures/}` | Runs `host_main.cpp` (linked against current `onspeed_core`) over a recorded flight excerpt, diffs output against committed `golden.csv`. **One-language today (C++)**, but the shape is exactly what we want for Layer 0. The drift-prevention plan extends this to all four languages. |
| **Python replay tools** | `tools/onspeed_py/{config,percent_lift,log_replay,frame}.py` | The Python implementation that the JS replay was ported from. Has its own pytest suite. Treat as authoritative when the JS port has a question. |
| **M5 WASM simulator** | `software/OnSpeed-M5-Display/sim/` | Builds the M5 firmware to a 320×240 canvas via Emscripten + SDL2. Embedded on the docs site at `installation/external-display.md`. Same renderer; different driver. **A future "JS replay vs M5 sim" parity test runs both against the same fixture and pixel-diffs the output.** |
| **Existing render-smoke tests** | `tools/web/test/render-smoke.mjs` | Renders Mode0/1/3 against fixed records; asserts text content. **Layer 2 widget tests follow the same pattern.** |
| **Live `/indexer` page** | `tools/web/lib/pages/IndexerPage.js` | The existing live indexer. **Shares the SVG components** in `lib/components/svg/index.js` with the replay tool. Don't break this when extracting widgets — Layer 2 work pulls subcomponents into `lib/replay/widgets/` without changing the live page's behavior. |

---

## Dispatch templates

Each step gets its own self-contained prompt.

### Layer 0 — drift-prevention scaffolding

```
Implement PLAN_DRIFT_PREVENTION.md PR 1: scaffold the cross-language
fixture-driven test system.

WORKTREE: a fresh worktree off origin/master
PLAN: ~/code/onspeed/local-plans/PLAN_DRIFT_PREVENTION.md (in repo at
      docs/superpowers/plans/2026-05-08-cross-impl-drift-prevention.md)
WHAT TO BUILD:
  1. test/spec_fixtures/percent_lift/basic_clean.json with one
     test case (alpha_0=-3.72, alpha_stall=10.31, aoa=3.24, expected=49.6).
  2. C++ runner: test/test_spec_fixtures/test_spec_fixtures.cpp.
     Walks test/spec_fixtures/ via a build-time script (Python) that
     converts JSON to embedded const data. Calls
     ::onspeed::aoa::ComputePercentLift(...) and asserts.
  3. Python runner: tools/onspeed_py/tests/test_spec_fixtures.py.
     Walks the directory at runtime via pytest parametrize.
  4. JS runner: tools/web/test/spec_fixtures.mjs. Walks the
     directory via fs, imports the production module, asserts.
     Plumb into npm test.

VERIFY: all three runners pass green on the one fixture; modify
the fixture's `expected` to a wrong value, all three fail.

COMMIT: one focused PR titled "test: cross-language spec fixtures (scaffold)".
```

### Layer 1 — sync persistence + nudge

```
Extend the OnSpeed Video Replay tool with sync persistence and
nudge controls.

WORKTREE: ~/code/onspeed/onspeed-worktrees/video-overlay/  (sam/video-overlay)
PLAN: docs/superpowers/plans/2026-05-08-video-overlay-replay.md (Layer 1 section)
TEST DATA: $WORKTREE/test-data/{sam_onspeed_aoa_4_11_2026.csv,
                                  cleaned_4_11_2026_sam_aoa.mp4,
                                  onspeed2_latest.cfg}
DEV SERVER: cd $WORKTREE && node tools/web/dev-server/server.mjs --mock --port 9001

WHAT TO BUILD:
  1. File-handle persistence via File System Access API.
     Use window.showOpenFilePicker for video and config picks.
     Store FileSystemFileHandle in IndexedDB (one entry per slot:
     'video', 'log', 'config'). On page load, check for stored
     handles; if present, call queryPermission and prompt the user
     for re-grant. Fallback to the existing <input type="file"> if
     File System Access API is unavailable.
  2. Sync nudge buttons. Add four buttons to the existing controls
     row, next to "Mark video anchor": "− 1 s", "− 0.1 s",
     "+ 0.1 s", "+ 1 s". Each click adjusts sync.videoTakeoffSec
     by the named amount. Status text updates immediately.
     Persisted to localStorage like the existing sync state.

VERIFY: drive in Playwright. Pick the test files. Reload. Confirm
re-grant prompt appears (in chrome://flags maybe needs File System
Access enabled, but it's in stable Chrome). Click − 0.1 s twice;
confirm the indexer state shifts in the video.

COMMIT: one focused commit, "replay: file-handle persistence + sync nudge".
```

### Layer 2 — HUD widgets + gallery

```
Build the OnSpeed HUD widget catalog and a gallery page for
designing them in isolation.

WORKTREE: ~/code/onspeed/onspeed-worktrees/video-overlay/  (sam/video-overlay)
PLAN: docs/superpowers/plans/2026-05-08-video-overlay-replay.md (Layer 2 section)

AESTHETIC: OnSpeed-original. Dark backgrounds, white/cyan ticks,
no G3X chrome, no color-zone arcs, no V-speed bugs, no gradients.
Minimal, readable, pilot-focused. Reference: ~/Desktop/Screenshot 2026-05-08 at 8.00.54 AM.png
shows the G3X-style HUD we are NOT copying — we want our own
minimal version.

WHAT TO BUILD:
  1. tools/web/widget-gallery.html — a dev-server-only page that
     mounts every widget against a fixed mock record, side-by-side.
     Hot-reloadable: edit widget code, refresh, see the result.
  2. tools/web/lib/replay/widgets/ directory. One file per widget,
     each exports a pure (record, style) -> SVG component.
     Components: HudIndexer (re-export Mode0 indexer wrapped),
     HudAirspeedTape, HudAltitudeTape, HudVsi, HudSlipBall (extract
     from existing svg/index.js), HudAttitude, HudHeading.
     Heading reads from record.magHeadingDeg; if NaN, render nothing.
  3. tools/web/test/widget-snapshot.mjs — render-smoke tests that
     lock visual output. One test per widget with a fixed mock.

CRITICAL CONSTRAINTS:
  - Don't break the live /indexer page. Mode0/1/3 in lib/modes.js
    must still work. If you extract subcomponents (e.g. SlipBall),
    make sure the live page still renders correctly.
  - Each widget is pure — props in, SVG out, no useState, no
    useEffect, no module-level state.
  - Style each widget for transparency/over-video legibility:
    drop-shadow filters, semi-transparent backgrounds, white
    primary stroke.

VERIFY: run npm test (existing render-smoke + new widget snapshots).
Open http://localhost:9001/widget-gallery and visually walk each widget.
Take screenshots; iterate with Sam.

COMMIT: 2-3 commits — one per logical group ("scaffold gallery + HudIndexer
extract", "+ tape widgets", "+ attitude/heading/slip extracts").
```

### Layer 3 — HUD chrome (presets, toggles, drag-position)

```
[Same template as above. Reference Layer 3 section. Depends on
Layer 2 having shipped.]
```

### Layer 4 — drag-to-define-clip

```
[Same template as above. Reference Layer 4 section. ~half-day.]
```

### Layer 5 — WebCodecs MP4 export

```
[Same template as above. Reference Layer 5 section. Independent of
Layers 1-4 except that the canvas and overlay SVG must be available
to read from. ~2-3 days. Reads WebCodecs spec + MP4Box.js docs.]
```

### Step 7 — documentation

```
[Same template. Three target files. ~1 day. Can pair with any earlier
agent dispatch as a follow-on, or be its own focused dispatch.]
```

---

## Open questions for Sam

These are the only things the layered plan above doesn't already
answer:

1. **For Layer 1 sync persistence**: Are you OK with File System Access
   API permission re-grant prompts on every reload? Some browsers
   don't quite handle this gracefully yet. Alternative: just persist
   the file *names* and re-prompt with a pre-populated picker.

2. **For Layer 2 widget aesthetic**: do you want me to spec specific
   colors / typography in the plan, or trust the agent to iterate
   visually with you? I lean toward "iterate visually" — fewer
   pre-decisions, more good results.

3. **For Layer 5 export**: is MP4 the only output format we want, or
   should the WebM-via-MediaRecorder path stay as an option for
   browsers without WebCodecs? My instinct: drop WebM entirely,
   require WebCodecs. Vac is on Chrome; the audience for this tool
   skews toward modern browsers. Less code to maintain.

---

## Where everything lives

| Thing | Path |
|---|---|
| **Branch** | `sam/video-overlay` (origin) |
| **Worktree** | `~/code/onspeed/onspeed-worktrees/video-overlay/` |
| **The page** | `tools/web/lib/pages/ReplayPage.js` |
| **Replay pipeline** | `tools/web/lib/replay/*.js` |
| **Widget gallery (TBD)** | `tools/web/widget-gallery.html` |
| **Widget catalog (TBD)** | `tools/web/lib/replay/widgets/` |
| **Test data** (gitignored) | `$WORKTREE/test-data/` |
| **Drift-prevention plan** | `~/code/onspeed/local-plans/PLAN_DRIFT_PREVENTION.md` |
| **This plan** (master) | `~/code/onspeed/local-plans/PLAN_VIDEO_OVERLAY.md` |
| **This plan** (in-repo) | `docs/superpowers/plans/2026-05-08-video-overlay-replay.md` |
| **Reference HUD screenshot** | `~/Desktop/Screenshot 2026-05-08 at 8.00.54 AM.png` |
| **Test export from prior session** | `~/Desktop/replay-export-test.webm` |

---

## Commit history on this branch

```
adfd6549  docs: resolve open questions + drift-prevention sibling plan
9e8ae4d0  docs: plan for video-overlay replay tool
86de61de  replay: polish — progress bar, out-of-range guards, more ball smoothing
e773041d  replay: data-mark navigation + clip builder + clip export
d0f03d44  replay: WebM export of video + indexer overlay (phase 4)
59d1b5ba  replay: video-overlay tool with synced indexer (phase 1+2+3)
```
