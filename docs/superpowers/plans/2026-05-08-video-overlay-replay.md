# PLAN_VIDEO_OVERLAY.md — OnSpeed Video Replay Tool

**Branch:** `sam/video-overlay` (pushed to `flyonspeed/OnSpeed-Gen3`).
**Status:** Phase 1–4 + DataMark/Clip system shipped; HUD-style multi-widget overlay is the next chunk.
**Date:** 2026-05-08.
**Owner:** Sam.

This plan documents the OnSpeed video-replay/overlay tool that lives at the
dev-server-only path `/replay`. It is the source of truth for what's
done, what's load-bearing, and what's next.

---

## What this tool does (one-paragraph)

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
- Browse every DataMark the pilot dropped during flight (Jump / Clip 30s / Clip 60s)
- Define arbitrary clip ranges from playhead and Export-all in one click
- Export full-flight or per-clip WebM (1080p / 12 Mbps, audio passthrough)

The tool is **not** built into firmware. It's served only via
`tools/web/dev-server/server.mjs --mock`. The bundler (`scripts/build_web_bundle.py`)
explicitly excludes the `lib/replay/` tree and `pages/ReplayPage.js` so the
firmware bundle stays slim.

---

## File inventory

Everything lives under `tools/web/`. New files are marked **NEW**.

### Pages (one entry-point file)

| File | Purpose |
|---|---|
| `lib/pages/ReplayPage.js` **NEW** | The whole page. Contains the file uploaders, sync state machine, mode switcher, overlay positioning, DataMarkPanel, ClipBuilderPanel, LogTimeline. ~870 lines. |

### Replay pipeline (the JS port of the Python tools)

All under `lib/replay/`. **NEW** in this branch.

| File | Mirrors | Purpose |
|---|---|---|
| `parseLog.js` | n/a | CSV parser → typed-array per-column store. `findRowAt(log, tMs)` is the bsearch we hit on every video frame. |
| `config.js` | `tools/onspeed_py/config.py` | Parses OnSpeed `.cfg` XML. Auto-detects new `<FLAP_POSITION>`-block format and V1 `<FLAPDEGREES>` list format. Returns `{ muteUnderIas, flapsByDeg, flapsArray }`. |
| `percentLift.js` | `tools/onspeed_py/percent_lift.py` + `software/Libraries/onspeed_core/src/aoa/{PercentLift,DisplayPctAnchors}.cpp` | `computePercentLift(aoa, flapCfg)` — honest single-linear envelope, alpha_0 floor, alpha_stall fallback. `computeAnchors(activeFs, allFlaps, flapsRawAdc)` — band-edge anchors + pip lerp. |
| `logReplay.js` | `tools/onspeed_py/log_replay.py` | The big one. `buildReplay(log, cfg)` → `{ recordAt(rowIdx) }`. Pre-computes per-row variable-dt EMA on `LateralG`/`VerticalG`/`ForwardG` (τ=0.50s, longer than the firmware's 0.0741s to compensate for the 50 Hz log capturing pre-EMA values + the wire's 20 Hz quantization). Pre-computes a synthetic flap-pot ADC sweep across detent transitions (smoothstep window, centered on the snap tick — same physics as the firmware's `FlapsDetector` flip behavior; mirrors `_fake_lever_sweep` in Python). |
| `syncDetect.js` | n/a (new logic) | Auto-detects the first crosswind turn (sustained \|roll\| ≥ 20° at IAS ≥ 30 kt for 0.5s). Falls back to rotation (IAS+VSI heuristic). Returns `{ row, kind }`. |
| `dataMarks.js` | n/a | Walks the log's `DataMark` column, returns transitions only (with non-zero new value). Each entry is `{ rowIdx, logTimeMs, value, label }`. Plus `logMsToVideoSec(logMs, sync)` for video↔log conversions. |
| `exportRecord.js` | n/a | The `MediaRecorder` pipeline. `exportOverlayedVideo({ videoEl, getOverlaySvg, startSec, endSec, outputWidth, bitrate, onProgress })` → `{ stop, finished }`. Uses `canvas.captureStream(0)` for manual frame advance, `track.requestFrame()` per video frame, audio merged from `videoEl.captureStream()`. Writes WebM/VP9. |

### Components touched

| File | Change |
|---|---|
| `lib/entry.js` | Registers `replay` page id → `ReplayPage`. |
| `lib/shell/PageShell.css` | New styles: `.replay-page`, `.replay-toolbar`, `.replay-stage`, `.replay-overlay`, `.replay-overlay-frame`, `.replay-controls`, `.replay-timeline`, `.replay-marks`, `.replay-clips`, `.replay-progress`. |
| `lib/components/svg/index.js` | UNTOUCHED. The mode SVGs are reused as-is from `/indexer`. |
| `lib/modes.js` | UNTOUCHED. `Mode0`, `Mode1`, `Mode3` are imported and rendered with synthetic records from `recordAt(rowIdx)`. |

### Dev-server / bundler

| File | Change |
|---|---|
| `tools/web/dev-server/server.mjs` | Adds `{ id: 'replay', path: '/replay', title: 'Video Replay' }` to PAGES. |
| `scripts/build_web_bundle.py` | Skips `lib/replay/` and `pages/ReplayPage.js` from the firmware bundle. |

### Test data (gitignored, lives at `~/code/onspeed/onspeed-worktrees/video-overlay/test-data/`)

| File | Notes |
|---|---|
| `sam_onspeed_aoa_4_11_2026.csv` | Real flight log, 76 MB, 286k rows, ~95 min @ 50 Hz. No DataMark column entries (DataMark always 0). |
| `cleaned_4_11_2026_sam_aoa.mp4` | The matching cockpit video, 17 GB, 4K, 16:9. Symlinked from `~/Downloads`. |
| `onspeed2_latest.cfg` | Newer config WITH `<ALPHA0>`, `<ALPHASTALL>`, `<KFIT>` populated. THIS is the right config for percent-lift math; the older `2_11_26_config.cfg` defaults alpha_0 to 0 and gets the lower band wrong. |
| `2_11_26_config.cfg` | Older config (no ALPHA0). Don't use for replay — anchors will be wrong. |
| `test_with_marks.csv` | Synthetic CSV (subset of the real log) with 3 DataMark transitions injected, used for verifying the mark detector. |

---

## What's done (tested end-to-end, screenshots verified)

### Phase 1 — Sync UI (commit `59d1b5ba`)

- Three-file uploader (Video, Log, Config). File-picker click → blob URL or text-content.
- Auto-detect runs on log load:
  - **First-crosswind-turn** (preferred — sustained \|roll\| ≥ 20° @ IAS ≥ 30 kt for 0.5s)
  - **Rotation** fallback (IAS ≥ 30 kt then sustained VSI > 200 fpm for 1s, walk back to rotation IAS)
  - Status text + timeline label show which kind was found ("crosswind turn" / "rotation").
- "Mark video anchor" sets `videoTakeoffSec` from current playhead. The pair `(videoTakeoffSec, logTakeoffMs)` defines the linear sync.
- "Re-detect log anchor" — re-run auto-detect.
- "Clear sync" — drop both anchors.
- IAS timeline at the bottom: downsampled stripchart (~1500 points), yellow vertical at the log anchor, red dashed vertical at the current video-mapped log time.
- **Click on timeline** → seek video to that log moment.
- **Shift-click** → override the log anchor.
- Sync state persists to localStorage keyed by `hash(videoFilename + logFilename)`.

### Phase 2 — Indexer overlay (commit `59d1b5ba`)

- Mode SVG layered over `<video>`, bottom-right corner.
- 22% width, 4:3 aspect-ratio (locked via div wrapper because `<svg>` ignores `aspect-ratio` in many browsers).
- Lifted 56 px from bottom so it clears the native `<video>` controls bar.
- `requestVideoFrameCallback` drives re-renders; falls back to `requestAnimationFrame` in browsers without rVFC.
- Mode switcher buttons: Energy / Attitude / Decel (Mode 0 / 1 / 3).
- Show overlay checkbox.

### Phase 3 — Faithful Python pipeline (commit `59d1b5ba`)

- `config.js` parses `.cfg` XML (V1 and V2). Loads alpha_0 / alpha_stall / kFit when present, defaults to safe values when not.
- `percentLift.js::computePercentLift` mirrors the C++ exactly: honest single-linear envelope, [0, 99.9] clamp, alpha_stall fallback to `stallwarn × 100/90` when uncalibrated.
- `percentLift.js::computeAnchors` returns the four operational anchors (tonesOn/fast/slow/stallWarn) snapped to the active detent + the pip target. When `flapsRawAdc` is provided + ≥ 2 detents configured, pip lerps from clean L/Dmax to full-flap `(3*fast + slow)/4`.
- `logReplay.js::buildReplay` allocates parallel Float32Arrays for smoothed accels, computes once over the whole log, returns a `recordAt(rowIdx)` closure. Active-flap config is cached so consecutive same-detent rows skip the lookup.
- `logReplay.js::synthLeverSweep` fills a per-row Float32Array with the *current* detent's pot value across long stretches, then overlays a smoothstep ramp across each detent transition (sweep window = 4 s, centered on the snap tick). **Two-pass implementation** — single-pass got the lever stuck at the initial value when no transition window touched a row. (Bug surfaced + fixed during Phase 3 verification.)

### Pause/Attach re-sync (commit `59d1b5ba`)

- "Pause indexer for re-sync" freezes the overlay at the current video-mapped log time. Yellow outline appears around the indexer.
- Pilot scrubs video freely; overlay stays frozen.
- "Attach here" recomputes `(videoTakeoffSec, logTakeoffMs)` so the frozen log time lines up with current video time. Any takeoff-anchor drift gets corrected.
- "Cancel" exits paused mode without changing sync.

### Phase 4 — WebM export (commit `d0f03d44`)

- "Export WebM" button records from current playhead to end-of-video.
- 1920×1080 output (preserves source aspect; downscales 4K to 1080p).
- VP9 @ 12 Mbps (YouTube's recommended bitrate for 1080p).
- Audio track from `videoEl.captureStream()` merged into the recording.
- Real `<progress>` bar during export.
- Stop button cancels mid-stream and downloads what was captured so far.
- Output filename: `{video_basename}_with_overlay.webm`.

### DataMark + Clip system (commit `e773041d`)

- Every transition in the log's `DataMark` column → labelled row in DataMarkPanel.
- Each row: log time + mapped video time + Jump / Clip 30s / Clip 60s buttons.
- Tick-marks on the IAS timeline at each mark, labeled with the ordinal.
- Manual ClipBuilder: stack of `{startMs, endMs, label}` rows. Add 30s / 60s from playhead, per-row Export and Delete, "Export all clips" iterates the list.
- All export paths share `exportRange({ startSec, endSec, label })`.
- Out-of-range guards: a mark whose mapped video time is < 0 or > duration shows "outside video" and disables its action buttons. `exportRange` refuses out-of-range windows with a clear error message rather than producing 0-byte WebMs.

### Polish (commit `86de61de`)

- `KACC_TAU_S` bumped from 0.0741 → 0.30 → 0.50. The slip ball still twitched at 0.30 on calm cruise; 0.50 settles to roughly what the M5 displayed in flight.
- Out-of-range data-mark guards (above).
- Real `<progress>` bar.
- Stop button works for any export.

---

## Architecture — load-bearing decisions

These are the calls that, if reverted, break the tool. Document them so future iteration doesn't accidentally regress.

### A. Sync is one linear mapping, not a piecewise spline

```
log_t_ms = sync.logTakeoffMs + (video_t_sec - sync.videoTakeoffSec) × 1000
```

Single anchor. The 30 ppm drift between the ESP32 millis() clock and the camera's clock is negligible over a 30-min flight (≤ 1 frame at 30 fps). If the user uploads an edited reel with cuts, **each segment needs its own anchor** — that's the use-case for the Pause/Attach re-sync UI.

If a future maintainer wants multi-anchor splines, the place to add it is `videoToLogMs` (and its inverse `logMsToVideoSec`). The rest of the page reads from those two helpers.

### B. The overlay is a *reused* Preact component, not a new rendering

The mode SVGs (`Mode0`, `Mode1`, `Mode3`) are pure functions of a `record` prop. The replay page synthesizes that record from a CSV row + parsed config, but the **rendering** is the exact same code that drives `/indexer` live. This means:

- The body-angle convention (alpha_0 floor, etc.) doesn't drift between live and replay — they're literally the same SVG.
- A future change to chevron color logic flows automatically.
- A new mode added to `lib/modes.js` shows up in `/replay` with one line of registration.

**Don't add overlay-specific rendering** unless it's truly replay-only (e.g., the timeline strip is replay-only, and that's why it lives in `ReplayPage.js`).

### C. The replay code is OUTSIDE the firmware bundle

`scripts/build_web_bundle.py` skips `lib/replay/*` and `pages/ReplayPage.js`. This is enforced by a regex match in `_all_js_files()`. The dev-server serves the files as ES modules; the firmware build skips them.

If you ever want to ship the replay tool as a standalone static site (e.g., on `flyonspeed.org/tools/replay`), you'd need a small bundler config that includes those files. **Don't add them back to the firmware bundle.**

### D. Variable-dt EMA, not fixed-α

The firmware applies `α = 0.060899` at 208 Hz IMU rate. The SD log writes the raw pre-EMA value at 50 Hz. We can't replicate the firmware exactly because we lack the 208 Hz signal — instead, we apply a **continuous-time τ-equivalent EMA** at the log's actual sample rate. Tunable via `KACC_TAU_S` in `lib/replay/logReplay.js`.

Currently 0.50s (4× the firmware's nominal continuous-time τ of 0.074). The factor compensates for two missing wire-stage filters:
1. The 50 Hz log signal has aliased high-frequency noise the 208 Hz firmware filter would have rejected.
2. The wire-format 1/100 g quantization + 20 Hz downsample adds hysteresis the log doesn't have.

**Don't lower this without testing on a calm-cruise segment of a real flight video.**

### E. Synthetic flap-pot sweep for old logs

Logs from before PR #221-ish only carry `flapsPos` (snapped detent integer), not `flapsRawADC`. Without a raw ADC value, the L/Dmax pip would jump at every flap-deg change. The synth sweep (`logReplay.js::synthLeverSweep`) fills the gap with a smoothstep ramp between detent pot values, centered on each transition tick.

**Two-pass implementation is required** (first pass: fill each row with the current detent's pot value; second pass: paint smoothstep windows over the transitions). The single-pass version leaves long mid-flight stretches stuck at the initial detent.

### F. Out-of-range guards on mark-driven exports

A pilot might drop a DataMark before the camera starts recording, OR the camera might run for 5 minutes longer than the log. Either way, the mark's mapped video time falls outside `[0, video.duration]`. Catch this in two places:

1. **DataMarkPanel UI**: render "outside video" hint, disable action buttons.
2. **exportRange**: refuse out-of-range windows with a `setParseErr` message rather than starting MediaRecorder against a video time it can't seek to (which produces 0-byte WebMs).

---

## What's next

Three groups, in the order I'd ship them.

### IMMEDIATE (drag-on-timeline, draggable indexer)

These are pure UX upgrades to existing surfaces. No new render paths, no new data models.

#### IM-1. Drag-to-define-clip on the IAS timeline

**File:** `lib/pages/ReplayPage.js` (`LogTimeline` component).

**Today:** click on the timeline = seek video. Shift-click = set log anchor.
**Want:** click-and-drag from time A to time B = define a clip ranging A→B in the ClipBuilder list.

**Design:**
- Use `pointerdown` / `pointermove` / `pointerup` (not mouse events — touch + Mac trackpad need pointer events).
- Track `dragStartMs` and `dragNowMs`. While dragging, render a translucent yellow rect from `xOf(min)` to `xOf(max)`.
- On `pointerup`, if `|dragEndMs - dragStartMs| ≥ 1000` (1s minimum to avoid micro-drag from a click), push a new clip into `clips` state. Otherwise treat as a seek (existing behavior).
- Modifier keys: shift-drag = override log anchor (set anchor to dragStartMs and ignore endMs); plain drag = clip-define.
- Cursor: `crosshair` while idle, `ew-resize` while dragging.

**State to add:**
```js
const [drag, setDrag] = useState(null);  // { startMs, currentMs } or null
```

**API to add:**
```js
const onClipDefined = (startMs, endMs) => setClips(prev => [
  ...prev,
  { startMs, endMs, label: `clip ${String(prev.length + 1).padStart(2, '0')}` },
]);
```

**Dispatch agent prompt** (template at the bottom of this doc).

#### IM-2. Drag the indexer around (and resize)

**Files:**
- `lib/pages/ReplayPage.js` (state for indexer position/size, drag handlers)
- `lib/shell/PageShell.css` (`.replay-overlay-frame.draggable` cursor + outline)

**Today:** indexer is anchored bottom-right at fixed 22% width.
**Want:** pilot drags the indexer to any corner / position. Optional: pinch-corners to resize.

**Design:**
- New state: `overlayPos = { right, bottom, widthPct }` defaulting to `{ 12, 56, 0.22 }`.
- Persist to localStorage same way `sync` does.
- Add a "Lock overlay position" toggle so dragging doesn't fight a precise placement.
- Drag implementation: pointerdown on the frame → record offset → pointermove updates `overlayPos.right` and `overlayPos.bottom` (clamped to keep indexer fully on-screen). Pointerup commits.
- For resize: small handle in the top-left corner of the frame; drag = adjust `widthPct` (clamped 0.10..0.45).
- Critical: **the export pipeline must read from this state too.** `exportRecord.js` currently reads inset values from a hardcoded prop. Pass the live `overlayPos` to `exportOverlayedVideo` so the exported video matches what the pilot sees.

**State to add:**
```js
const [overlayPos, setOverlayPos] = useState({ right: 12, bottom: 56, widthPct: 0.22 });
const [overlayLocked, setOverlayLocked] = useState(true);  // default locked so accidental clicks don't move it
```

**Dispatch agent prompt** below.

### MEDIUM (HUD widget set + toggle UI)

This is the big one. The reference image at `~/Desktop/Screenshot 2026-05-08 at 8.00.54 AM.png` shows the Garmin G3X-style full HUD. We don't need all of it — focus on **airspeed, heading, VSI, slip ball, percent-lift indexer** (the elements you called out as "really good and would help orient folks").

#### MED-1. New widget set

Each is a pure-function React/Preact SVG component, same shape as `Mode0`/`Mode1`. Lives in a new file `lib/replay/widgets/` so they don't clutter the live `lib/components/svg/` (which is shared with the firmware indexer).

| Widget | Source data | Reference layout |
|---|---|---|
| `<HudAirspeedTape r=… style=… />` | `r.iasKt` | Left side. Vertical scrolling tape. Major ticks every 10 kt, labeled every 20. Color zones (white, green, yellow, red) come from config Vno / Vne / Vfe. Pointer = current IAS. |
| `<HudAltitudeTape r=… style=… />` | `r.paltFt` | Right side. Major ticks every 100 ft. |
| `<HudHeadingTape r=… style=… />` | `r.magHeading` (need to check if log carries this — fallback to derived) | Top. Horizontal scrolling tape. Major ticks every 30°, cardinal letters (N/E/S/W). |
| `<HudVsi r=… style=… />` | `r.vsiFpm` | Right of altitude tape. Vertical strip with carrot pointer. ±2000 fpm range. |
| `<HudSlipBall r=… style=… />` | `r.lateralG` | Bottom-center. Same SVG as the existing slip ball (extract it). |
| `<HudGGauge r=… style=… />` | `r.verticalG` | Top-right. Vertical strip with G-load + percent-lift indexer side-by-side. |

The existing `Mode0` indexer becomes one widget in this set: `<HudIndexer r=… style=… />`. **Don't break the live `/indexer` page** — extract the slip ball / chevron rendering into reusable subcomponents that both Mode0 (live) and HudIndexer (replay) compose.

#### MED-2. Layout system

The HUD is **multi-widget** — six separate boxes positioned around the video frame. Each has its own anchor (corner + offset) and size. Two layout choices:

- **A. Hand-positioned per widget** (like the reference image — each widget has fixed corner anchors):
  - Each widget config: `{ id, anchor: 'tl|tr|bl|br|t|b|l|r', offset: {x,y}, size: 'sm|md|lg' }`
  - Stored as an array in component state, persisted to localStorage.
  - Drag-to-reposition any widget (extends IM-2 to apply to all widgets).
- **B. Preset layouts** (user picks "Full HUD" / "Minimal" / "Indexer-only"):
  - Hand-tuned layouts shipped as JSON.
  - User toggles individual widgets on/off (the actual ask in your message).
  - Faster to ship.

**Recommendation: ship B first.** Three presets — "Full HUD" / "Minimal" / "Indexer only" — plus a checkbox per widget. After we live with that, add A if pilots actually want fine-grained positioning.

#### MED-3. Toggle UI

Below the mode buttons, a row of toggles:

```
[Indexer ✓] [Airspeed ✓] [Altitude ✓] [Heading ✓] [VSI ✓] [Slip ✓] [G ✓]
```

Each is a `<label class="replay-toggle"><input type="checkbox" ... /></label>` (already styled in the page CSS). State: `widgets: { indexer: true, airspeed: true, ... }`.

**Critical:** export pipeline must respect the toggle state. The simplest way: render the toggled-off widgets with `display: none` so they don't appear in the SVG that `getOverlaySvg()` rasterizes.

### LATER (Phase 5 + drag-resize + more)

| Item | Why later |
|---|---|
| Drag-to-resize each widget | Useful but presets cover 90% of the case. |
| Per-widget styling (colors, font sizes) | Pilot probably never wants to override default colors; risks looking like a 90s flight sim. |
| Concatenate clips into one output | Requires WebCodecs or ffmpeg-wasm. Sequential separate files is enough for Pack's workflow. |
| MP4 export (Remotion) | Browser MediaRecorder can't write MP4/H.264 in Chrome. Frame-by-frame Remotion render is the path. ~1 day of work. |
| Multi-anchor sync (handle edited reels) | Pause/Attach re-sync covers most cases. |
| Audio cross-correlation auto-sync | Cool but overkill. |

---

## Open questions for Sam

These need your call before an agent can dispatch the next chunk:

1. **HUD widget aesthetic**: do you want OnSpeed-original styling (current dark indexer + simple SVG) OR mimicking the Garmin G3X look (white tapes on dark backdrop, color zones, V-speed bugs)? If G3X-style, where do V-speeds (Vno, Vne, Vfe) come from? They're not in the SD log; either we read from the config XML (it has none today) or you hardcode per-airframe.

2. **Heading data source**: SD log column? `Pitch`/`Roll` are there, but heading isn't on a quick scan. If absent, do we (a) skip the heading widget, (b) integrate yaw rate (drifts badly), or (c) use the EFIS column `efisMagHeading` (only present when EFIS is enabled, which depends on the install)?

3. **Layout**: 3 presets + per-widget toggle (recommendation B), OR drag-each-widget-anywhere (A)?

4. **Default widget set on first load**: just the indexer (current behavior), OR airspeed + indexer + slip + altitude (a useful default HUD)?

5. **Should "Export WebM" button move into a "File → Export…" menu** with sub-options (full / from playhead / current clip set / each clip separately)? Right now it's a single button labeled "Export WebM" which is ambiguous as the feature set grows.

6. **Drag-on-timeline conflict**: today plain-click = seek video, shift-click = override anchor. After IM-1 lands, plain-click = ambiguous (could be seek or could be start of a drag). Resolution: treat any pointer movement > 4 px during pointerdown as drag-start, otherwise treat as click. OK with that?

---

## How to dispatch an agent for any next feature

Use this prompt template. Fill in the brackets.

```
You're extending the OnSpeed Video Replay tool on branch `sam/video-overlay`.

WORKTREE: /Users/sritchie/code/onspeed/onspeed-worktrees/video-overlay/
PLAN DOC: /Users/sritchie/code/onspeed/local-plans/PLAN_VIDEO_OVERLAY.md
TEST DATA: $WORKTREE/test-data/{sam_onspeed_aoa_4_11_2026.csv,
                                  cleaned_4_11_2026_sam_aoa.mp4,
                                  onspeed2_latest.cfg,
                                  test_with_marks.csv}
DEV SERVER: cd $WORKTREE && node tools/web/dev-server/server.mjs --mock --port 9001
            then http://localhost:9001/replay

READ FIRST: PLAN_VIDEO_OVERLAY.md (the whole thing). Especially the
"Architecture — load-bearing decisions" and "File inventory" sections.

DO NOT:
  - Add the replay code back to the firmware bundle.
  - Touch lib/modes.js or lib/components/svg/index.js without
    extracting reusable subcomponents (those files ARE the live
    /indexer's renderers; replay reuses them).
  - Lower KACC_TAU_S below 0.30 without testing on a real flight.

TASK: <e.g. "Implement IM-1: drag-to-define-clip on the IAS timeline.
       See the IM-1 section of PLAN_VIDEO_OVERLAY.md for design.">

VERIFY BY:
  1. Loading the test data in the browser (use Playwright MCP).
  2. Driving the new feature with the pilot's typical workflow.
  3. Screenshotting the result and reading it back.
  4. Confirming existing features (sync, indexer overlay, export)
     still work — the replay tool is an integrated state machine,
     and one regression breaks the whole flow.

COMMIT: one focused commit on sam/video-overlay with a clear message
following the existing pattern (see `git log --oneline origin/master..HEAD`).
```

---

## Where everything lives

| Thing | Path |
|---|---|
| Branch | `sam/video-overlay` (origin) |
| Worktree | `~/code/onspeed/onspeed-worktrees/video-overlay/` |
| The page | `tools/web/lib/pages/ReplayPage.js` |
| Replay pipeline | `tools/web/lib/replay/*.js` |
| Test data | `tools/web/../../test-data/` (gitignored) |
| Plan (this doc) | `~/code/onspeed/local-plans/PLAN_VIDEO_OVERLAY.md` |
| Reference HUD screenshot | `~/Desktop/Screenshot 2026-05-08 at 8.00.54 AM.png` |
| Test export from last session | `~/Desktop/replay-export-test.webm` |

---

## Commit history on this branch

```
86de61de  replay: polish — progress bar, out-of-range guards, more ball smoothing
e773041d  replay: data-mark navigation + clip builder + clip export
d0f03d44  replay: WebM export of video + indexer overlay (phase 4)
59d1b5ba  replay: video-overlay tool with synced indexer (phase 1+2+3)
```
