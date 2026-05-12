# PLAN_VIDEO_OVERLAY.md — OnSpeed Video Replay Tool

> **🛑 READ THE RETRO.** `2026-05-09-replay-retro.md` is the canonical
> guidance. This plan's "faithful in-browser port" framing is dead —
> the trial run identified it as a drift-seam pattern. Foundation
> projects (B1 onspeed_core WASM, B2 M5 firmware WASM) replaced it
> with C++ pipelines, and the upcoming `LogReplayTask` lift completes
> the picture. **Layer 1+ feature ideas** (DataMark, ClipBuilder,
> export, file-handle persistence) **still apply.** The
> rendering-engine framing here doesn't.

**Branch:** `sam/video-overlay` (pushed to `flyonspeed/OnSpeed-Gen3`).
**Status:** Phase 1–4 + DataMark/Clip system shipped. Architecture
since redirected through Projects B1 + B2; see retro.
**Form factor:** static-build SPA shipped via the existing
`dev.flyonspeed.org` MkDocs deploy → bookmark at `dev.flyonspeed.org/replay`.
Electron deferred until SPA reveals a concrete need.
**Date:** 2026-05-08 (retro added 2026-05-09).
**Owner:** Sam.

This plan documents the OnSpeed video-replay/overlay tool and the
sequenced roadmap to a complete pilot-facing analysis app. Read the
retro first; this for layer-1+ feature inspiration only.

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
> (firmware C++, Python tools — wrappers post-consolidation —
> JavaScript replay, M5 WASM simulator)
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
│ Layer 0: REPLAY ENGINE                                           │
│  POST-`PLAN_WASM_CORE.md`: a thin WASM driver. JS loads the      │
│  compiled onspeed_core.wasm and calls into it for every          │
│  algorithm. (log_row, cfg) → record runs in WASM. NO hand-port.  │
│  Drift between firmware C++ and Replay JS is impossible by       │
│  construction — they ARE the same code.                          │
│                                                                   │
│  PRE-WASM (today): JS hand-port at lib/replay/*.js. ~1500 lines  │
│  re-implementing C++ algorithms. Works today; replaced by Step 0.│
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

### Step 0 — Foundation projects (do these FIRST)

Two parallel projects must land before Replay tool layer work:

**Project A: Python Consolidation** (`PLAN_PYTHON_CONSOLIDATION.md`)
- Migrate Python algorithm code to `host_main` subprocess wrappers.
- ~5-7 days, 7 small PRs. Sam confirmed breaking changes are fine.

**Project B: WASM Compile of onspeed_core** (`PLAN_WASM_CORE.md`)
- Compile `software/Libraries/onspeed_core/` to WebAssembly.
- The Replay tool's JS layer loads the WASM module instead of
  hand-porting algorithms. **Zero hand-ports. Drift impossible by
  construction.**
- ~5-7 days, 5 small PRs.

Both projects share host_main as a dependency: Python consolidation
builds the multi-subcommand CLI; WASM consolidation builds the same
C++ API as a browser-loadable module. After Project A's Step 0
(host_main multi-subcommand) lands, Project B can proceed in
parallel.

**Why both before Replay layer work:**
- Project A makes Python wrappers, not algorithm ports — drift
  impossible.
- Project B makes JS load real C++ via WASM — drift impossible.
- After both: the architecture collapses. `onspeed_core` is the
  single source of truth; every consumer compiles or links it.
  The Replay tool's "Layer 0 engine" becomes a thin WASM driver,
  not a 1500-line JS port.

### Step 1 — (former drift-prevention work, now redundant)

The streaming-goldens CI gate from earlier drafts of
`PLAN_DRIFT_PREVENTION.md` is **no longer needed** after Project B
lands — JS calls into WASM-compiled C++, so there's no JS port to
gate. The only residual check is "WASM build matches native build"
which is one tiny CI step (see PLAN_DRIFT_PREVENTION's "WASM-vs-
native parity" dispatch prompt).

**This step replaces the old Layer 0 drift-scaffolding step.**

**What ships:**
- `tools/regression/run_snapshot_wasm.py` — loads the compiled
  `onspeed_core.js` module via node, walks the same input CSV the
  existing native `run_snapshot.py` uses, calls into WASM bindings
  per row, diffs output against the same committed `golden.csv`
  with the same tolerance.
- One CI step in `.github/workflows/ci.yml` that runs the WASM
  regression after the existing native regression. Both must pass.

**Owner:** new agent dispatch. Spec lives in `PLAN_DRIFT_PREVENTION.md`'s
"Dispatch prompt — WASM-vs-native parity check" section.
**Effort:** ~half-day.
**Depends on:** `PLAN_WASM_CORE.md` Step 0+1 having landed (so a
WASM build exists to diff against native).
**Blocks:** nothing day-to-day; this is paranoia infrastructure
guarding against emcc determinism issues.

### Step 2 — Layer 1 polish (sync persistence + nudge + keyboard scrub + sticky config + all modes)

**Why early:** Vac is going to use this tool repeatedly. If every
session requires re-picking files, re-loading the same config,
re-syncing, friction kills adoption. Quick wins here pay back daily.

**What ships:**

a) **File-handle persistence via File System Access API.** When Vac
   picks files, store the `FileSystemFileHandle` in IndexedDB. Next
   session, prompt "Re-grant access to flight.mp4?" with a single
   click instead of full re-pick.

b) **Sticky config across reloads.** The config file is small
   (~3 KB) and Vac uses the same one repeatedly. Beyond just
   storing the file *handle*, **persist the PARSED config object
   itself** in IndexedDB on first load. Next session, the parsed
   config is already in memory by the time the page mounts —
   no file picker needed for config at all. The video and log
   still need file-handle re-grant (they're large; can't store
   contents). User can still pick a different config to override.

c) **Sync nudge buttons**: ± 0.1 s, ± 1 s next to the status text.
   Each click adjusts `sync.videoTakeoffSec` so the offset shifts
   by the named amount. Useful when auto-detect lands you 3 frames
   early.

d) **Keyboard frame-by-frame scrubbing.** Industry-standard NLE
   keymap so Vac's muscle memory from Premiere/FCP/DaVinci works.
   When the video element has focus (or always, if there's no
   conflicting focus):
     - `←` / `→` — scrub one frame back / forward (read video's
       actual frame rate; default 30 fps if unknown)
     - `Shift + ←/→` — scrub ±1 second
     - `,` / `.` — alias for `←` / `→` (NLE convention)
     - `J` / `K` / `L` — play backward / pause / play forward
     - `Space` — toggle play/pause (browser default; just confirm)
   Frame-accurate seek via `videoEl.currentTime = currentTime ± (1/fps)`.

e) **All five indexer modes.** Today the replay page only registers
   Mode0/1/3 in the MODES array. Add Mode2 (Indexer-only, no
   numeric corners) and Mode4 (Historic G). Same components,
   imported from `lib/modes.js`. One-line addition per mode. Worth
   doing because Vac may want each.

f) **Multi-anchor support** (deferred): if Vac uploads an edited
   reel with a cut, he needs more than one anchor. Defer to when
   this actually breaks.

**Owner:** small agent dispatch on existing `/replay` page.
**Effort:** ~1 day (was half-day; keyboard scrub + sticky config add bulk).
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
   - `HudOnSpeedLogo.js` — OnSpeed branding, **default-on, top-left**
     of the video frame. Stateless (pure prop-driven, no record
     dependency). Asset lives at `tools/web/static/branding/onspeed_logo.svg`
     (copied from `~/Downloads/onspeed_logo.svg` — 14 KB SVG, scales
     cleanly). PNG fallback at `FlyOnSpeed_Logo.png` (331 KB) if
     SVG export through MediaRecorder/WebCodecs proves finicky;
     prefer SVG. Configurable height (default ~10% of frame height),
     opacity (default ~85%) via the layout JSON.

c) **Widget-level render-smoke tests.** Each widget gets a fixture
   record and a snapshot test that locks visual output. Same pattern
   as the existing `tools/web/test/render-smoke.mjs`.

**Owner:** widget design is its own thing; aesthetic decisions
involved. **OnSpeed-original styling, not G3X chrome.** No V-speed
bugs, no color-zone arcs, no gradients. Minimal HUD elements.

**Effort:** ~2-3 days for an agent who can iterate visually with you.
The widget gallery page accelerates this dramatically — Sam can
review each widget in isolation in seconds.

### Step 4 — Layer 3 chrome (declarative layout, toggles, drag-edit)

**Inspired by `gopro-dashboard-overlay`'s nested-translate XML
layout system.** Single source of truth: a JSON layout file with
composable groups, source-controllable, shareable across pilots.
Drag-to-edit UI reads/writes the same JSON.

**What ships:**

a) **Declarative layout file** at `lib/replay/layouts/default.json`:

```json
{
  "schemaVersion": 1,
  "name":          "default-hud",
  "groups": [
    {
      "name":      "branding-top-left",
      "translate": { "left": 16, "top": 16 },
      "widgets":  [{ "type": "onspeed-logo", "height": 0.10, "opacity": 0.85 }]
    },
    {
      "name":      "right-cluster",
      "translate": { "right": 12, "bottom": 56 },
      "widgets": [
        { "type": "indexer",        "size": 0.22 },
        { "type": "altitude-tape",  "translate": { "y": -0.05 } }
      ]
    },
    {
      "name":      "airspeed-tape-left",
      "translate": { "left": 12, "bottom": 56 },
      "widgets":  [{ "type": "airspeed-tape", "size": 0.18 }]
    },
    {
      "name":      "slip-ball-bottom",
      "translate": { "centerX": 0.5, "bottom": 80 },
      "widgets":  [{ "type": "slip-ball", "size": 0.10 }]
    }
  ]
}
```

   Composable nested transforms: move a group, move all children
   together. Shareable: Vac sends his preferred layout to another
   pilot via one JSON file.

b) **Two preset layouts shipped**: `default.json` (the one above —
   indexer + airspeed + altitude/VSI + slip + attitude),
   `indexer-only.json` (just the indexer, current behavior).

c) **Per-widget toggle UI**: row of checkboxes under the mode
   buttons. Toggling = visibility flag, NOT layout-edit. Toggle
   off, the widget hides; toggle back on, it's at the same
   position. Persisted to localStorage as a separate visibility
   layer over the layout JSON.

d) **Drag-to-edit mode**: "Edit layout" button enters a mode where
   each widget gets a drag handle + dashed outline. Drag a
   widget = move within its group. Drag a group label = move the
   whole group. Snap to 8 anchor points within 16 px. Click
   outside = exit. **Edit mode reads/writes the JSON layout state**
   (saved to localStorage as an override).

e) **Reset to default** button: drops localStorage override, falls
   back to `default.json`.

f) **Critical hookup**: export pipeline must read the live layout
   state and rasterize each widget at its current position. Same
   widgets, same positions, exported video matches preview.

**Default visibility on first visit:** indexer, airspeed tape,
altitude/VSI, slip ball, attitude indicator. Heading off (depends on
EFIS data presence — only renders when `record.magHeadingDeg` is finite).

**Owner:** UI-heavy agent dispatch; needs Layer 2 widgets shipped first.
**Effort:** ~2 days (was 1-2; layout-as-JSON adds nuance).

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

### Step 6.5 — Static-build deploy via dev.flyonspeed.org

**Why before Step 7 (docs):** docs reference the live URL; ship the
URL first.

The **dev.flyonspeed.org docs site** already builds + deploys
through `.github/workflows/docs.yml` (MkDocs Material + GitHub
Pages). The replay tool ships as part of that same deploy — same
hosting, same SSL, same CI. **Not** the public marketing site
(`flyonspeed.org`) — that's Framer-managed.

What ships:

1. **`tools/web/scripts/build_static.sh`** — non-firmware bundle
   variant. Includes `lib/replay/*` and `lib/pages/ReplayPage.js`
   (which the firmware bundler skips). Inlines CSS, logo, meta
   tags. Writes `dist/replay/{index.html, app.js, app.css, onspeed-logo.png}`.

2. **GitHub Actions hook in `.github/workflows/docs.yml`**: before
   `mkdocs build --strict`, run `tools/web/scripts/build_static.sh`,
   then copy `dist/replay/` → `docs/site/site/replay/` so the
   gh-pages deploy picks it up at the right path. (Alternative: a
   MkDocs hook that does this at build time. Whichever is less
   brittle against MkDocs strict mode.)

3. **Discoverability**: bookmark only. **No nav entry from MkDocs.**
   Vac knows the URL; we don't surface it on the public docs nav
   until it's a more polished feature.

The static deploy doesn't need a backend. All processing stays
client-side; user files never leave the browser. The page does need
File System Access API + WebCodecs, both of which work over HTTPS
(GitHub Pages ✓).

**MkDocs strict-mode gotcha**: `mkdocs build --strict` may error if
the site/ output dir contains files MkDocs doesn't know about. Two
fixes — copy AFTER `mkdocs build`, or add `replay/` to MkDocs's
`extra_files` allowlist. Whichever the agent finds works first.

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

## Form factor — static-build SPA at **dev.flyonspeed.org/replay**

**Decision**: ship as a static-built single-page app, deployed as
part of the existing **dev.flyonspeed.org** docs site (MkDocs +
GitHub Actions). Same hosting, same SSL, same CI. **Not** the
public marketing site (`flyonspeed.org`) — that's Framer-managed.

Vac bookmarks `dev.flyonspeed.org/replay`. No install, no clone,
no terminal.

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
| **Python replay tools** | `tools/onspeed_py/{config,percent_lift,log_replay,frame}.py` | **PRE-CONSOLIDATION**: hand-port of firmware algorithm code, has its own pytest suite. **POST-`PLAN_PYTHON_CONSOLIDATION.md` Step 1+**: thin subprocess wrappers around `host_main`. The wrapper interface is what stays; the algorithm code is C++ in `onspeed_core/`. |
| **M5 WASM simulator** | `software/OnSpeed-M5-Display/sim/` | Builds the M5 firmware to a 320×240 canvas via Emscripten + SDL2. Embedded on the docs site at `installation/external-display.md`. Same renderer; different driver. **A future "JS replay vs M5 sim" parity test runs both against the same fixture and pixel-diffs the output.** |
| **Existing render-smoke tests** | `tools/web/test/render-smoke.mjs` | Renders Mode0/1/3 against fixed records; asserts text content. **Layer 2 widget tests follow the same pattern.** |
| **Live `/indexer` page** | `tools/web/lib/pages/IndexerPage.js` | The existing live indexer. **Shares the SVG components** in `lib/components/svg/index.js` with the replay tool. Don't break this when extracting widgets — Layer 2 work pulls subcomponents into `lib/replay/widgets/` without changing the live page's behavior. |

---

## Dispatch templates

Each step gets its own self-contained prompt.

### Step 0 — Python consolidation (do FIRST; see PLAN_PYTHON_CONSOLIDATION.md)

Sequenced before everything else. Migrates Python algorithm code
to `host_main` subprocess wrappers across 7 small PRs. Each step
ships independently. After Step 1 (proof of concept on percent_lift
+ config), the rest is mechanical.

**Dispatch prompts for each step are inside `PLAN_PYTHON_CONSOLIDATION.md`'s
"Dispatch prompts" section.** Start with the Step 0 prompt
("host_main multi-subcommand algorithm CLI"), then the Step 1
prompt, then proceed in order.

### Layer 0 — foundation work (firmware LogReplay parity, then WASM, then parity check)

Layer 0 is no longer a single CI-gate task. It's three sequenced
foundation pieces, each with its own plan doc:

1. **`PLAN_FIRMWARE_LOG_REPLAY_PARITY.md`** (~2 days) — fix firmware
   LogReplay so SD-log replay produces flight-equivalent wire output.
   Closes the rate-coupling and ADC-synth gaps the JS Replay tool
   patches over today. Must land before WASM Step 2.

2. **`PLAN_WASM_CORE.md`** (~5-7 days, 5 small PRs) — compile
   `onspeed_core` to WebAssembly. Replay tool's JS UI loads the
   WASM module and calls into it for every algorithm. Replaces the
   JS hand-port at `tools/web/lib/replay/*.js` entirely. Step 2's
   "delete the JS variable-dt EMA + synth sweep" depends on the
   firmware-LogReplay-parity work having landed.

3. **`PLAN_DRIFT_PREVENTION.md`** (~half-day) — one CI step that
   runs the existing snapshot regression against BOTH the native
   host_main binary AND the WASM build, asserting they produce the
   same output. The WASM-vs-native parity check is the only drift
   gate we still need; everything else is by-construction.

Dispatch prompts for each step live in their respective plan docs.

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

## Lessons from prior art

[**gopro-dashboard-overlay**](https://github.com/time4tea/gopro-dashboard-overlay)
is the closest analog to what we're building (telemetry overlay on
action-cam video). Reviewed for lessons on 2026-05-08.

**What we adopted:**
- **Composable-nested-translate layout file** (their XML → our JSON).
  Layouts are source-controllable, shareable, diffable. See Layer 3
  redesign above.
- **Hardware-accelerated encode = 17× realtime** (their NVENC → our
  WebCodecs/VideoToolbox). Validates the "faster than realtime"
  promise is real.
- **"Overlay only" mode** — render an indexer animation against a
  black backdrop with no source video. Future feature, not Phase 1.

**What we deliberately didn't copy:**
- **Server-side Python orchestrating FFmpeg.** They're a CLI tool
  that ships a Docker image. We're a browser tool with live
  preview. WebCodecs gives us the same speed without the
  orchestration cost.
- **Vendored map-tile system (Geotiler).** We don't need a route map
  for OnSpeed. Avoiding that whole world of pain (tile licensing,
  attribution, server agreements).
- **Two rendering backends** (Pillow + Cairo). They have legacy
  reasons for both; we're SVG-only and that's enough.
- **XML format.** We use JSON because we're browser-native; XML's
  composability advantage is preserved via nested groups in JSON.

**Privacy note worth documenting**: exported videos contain raw
flight data — times, altitudes, configurations, occasionally GPS.
Add a one-line warning to user-facing replay docs (Step 7) that
pilots should review before public sharing.

## Commit history on this branch

```
adfd6549  docs: resolve open questions + drift-prevention sibling plan
9e8ae4d0  docs: plan for video-overlay replay tool
86de61de  replay: polish — progress bar, out-of-range guards, more ball smoothing
e773041d  replay: data-mark navigation + clip builder + clip export
d0f03d44  replay: WebM export of video + indexer overlay (phase 4)
59d1b5ba  replay: video-overlay tool with synced indexer (phase 1+2+3)
```

---

## Round 4 — what shipped 2026-05-12

Three replay PRs merged in cascade to master on 2026-05-12, retiring
the WebCodecs export work and the deprecated demux/mux infrastructure
in one pass:

### What shipped today

**Composite MP4 export** (the headline feature):
- Source-faithful encoder: matches source resolution, framerate (exact
  rational cts), codec family (HEVC source → HEVC output, H.264 → H.264),
  High profile. No silent down-conversion.
- 4K HEVC sources export at native resolution with audio.
- AAC audio passthrough via Mediabunny's raw-packet path —
  bit-perfect, no re-encode.
- Source rotation matrix honored: GoPro's `tkhd` rotation respected at
  composite time, output pixels upright (180° validated on Vac's
  footage; 90°/270° pixel-dimension swap deferred to #71).
- PresentationFilter applied during export so slip-ball smoothing
  matches live preview.

**Overlay-only MP4 export** (the second output path):
- 5-mode picker (per-mode checkboxes; Energy / Attitude / Indexer /
  Decel / Historic G) — single export pass renders one or many modes.
- Size selector: native M5 dimensions (320×240), or 20% / 30% / 50%
  of source width. Black panel background, no chroma key. Output IS
  the M5 panel as a video; pilots drag-on-top in any NLE.

**Sync + clip persistence across reload** (PR #527):
- Content-keyed by SHA-256 prefix of the log file. Same log file →
  same sync state and clip list across reloads, even across browser
  restarts.
- Stored in IndexedDB.

**Bundled replay tool** (PR #529):
- `scripts/build_web_bundle.py` grew a `--target replay` mode that
  emits a single `replay-bundle.js` for the docs-site replay page.
- Page no longer fans out per-file relative-path imports; one bundle
  drops in, matches how the firmware serves its own bundle.

**Mediabunny migration** (part of PR #532):
- `mp4-muxer` (deprecated by its own author) gone.
- `mp4box.js` (OOMs on >500 MB reads, no streaming reads) gone.
- Single MPL-2.0 dep (same author as `mp4-muxer`) handles both demux
  and mux with streaming reads.
- Unblocks: non-fast-start file correctness; future 90-min full-flight
  exports without the head/tail probe budget.

### Bulldog round outcomes

Bulldog review on 2026-05-12 raised 27 issues across `mp4Export.js` and
`ReplayPage.js`. BLOCKERs plus selected HIGHs fixed in commit
`eee2b807` (cross-log persistence corruption + memory leaks + AAC
t=0 edge case). Deferred items tracked as **#71**:
- 90°/270° rotation pixel-dimension swap (Vac uses GoPro 180° which
  works; 90°/270° currently produces wrong-aspect output)
- Stale-comment sweep across `mp4Export.js` + `ReplayPage.js`
- Helper dedup between the composite and overlay-only paths

### Worker-thread retreat

`feature/replay-worker` (commit `1b383bcd`) moved the
decode/composite/encode loop into a Web Worker. Measured wins: 17 s
vs 40 s wall clock + responsive UI throughout. But every
SVG-to-bitmap call failed with `InvalidStateError` because Chrome's
`createImageBitmap(svgBlob)` does not accept `image/svg+xml` despite
the spec saying it should.

Retreated to mediabunny tip with tag `replay-pre-worker-retreat` at
commit `b842c8ba`. Worker code preserved on its branch as WIP.
Revisit after redesigning SVG rasterization: most likely render SVG
to `ImageBitmap` on the main thread and `transfer()` the bitmap into
the worker. Tracked as issue **#62**.

### What's next (Round 4b — ergonomics)

The user-facing friction surfaced 2026-05-12 by Sam ("re-picking 3
files on every reload is bananas") moves these features to the front
of the queue:

1. **#72 — FileSystemFileHandle persistence** — NEXT. Plan doc:
   `2026-05-12-filesystem-handle-persistence.md`. Stores
   `FileSystemFileHandle` per slot in IndexedDB; one click + single
   permission dialog re-grants all three files on reload.
2. **#63 — Frame-step keyboard scrub** — NLE keymap (←/→ one source
   frame, Shift+←/→ ±1 s, J/K/L transport, `,`/`.` alias). Frame
   rate read from mp4box probe (accuracy matters for sync). Foundation
   for #65.
3. **#64 — Multi-GoPro chapter ingest** — auto-concat
   `GH010001.MP4` + `GH020001.MP4` etc into a virtual file. Builds on
   the directory picker from #72.
4. **#65 — Multi-anchor sync** — piecewise-linear sync for clock drift
   over long flights and across chapter boundaries.
5. **#69 — Clip timeline visualization + nudge UX** — render clip
   spans on the IAS timeline; drag handles on endpoints to nudge.
6. **#71 — Bulldog deferred follow-ups** (above).

This section supersedes the prior "Round 4" planning text — Mediabunny
landed, the worker retreated, and Round 4b sequencing is anchored on
file-handle persistence as the user-felt next step.
