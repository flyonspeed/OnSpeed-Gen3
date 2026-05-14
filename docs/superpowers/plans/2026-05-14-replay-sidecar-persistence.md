# PLAN — Replay Tool Sidecar Persistence

**Date:** 2026-05-14
**Owner:** Sam
**Status:** Plan — research locked, ready to execute
**Anchor branch:** `feature/bundler-esbuild` (don't-merge, like PR #504 was for the overlay plan)
**Predecessor work:** Project C video overlay tool, post-overlay consolidation (`PLAN_POST_OVERLAY_CONSOLIDATION.md`)

## TL;DR for an agent picking this up cold

The replay tool currently stores flight-test engineer notes (clips, DataMark annotations, sync points, debrief text, HUD pitch offset) in the browser's IndexedDB. A flight test engineer doing real work on this — Vac, on his RV-4 — risks losing his debrief if his browser cache is cleared. That's an anxiety this plan eliminates.

**The fix:** persist all engineer-authored state into a JSON sidecar file that lives on disk next to the flight log. The sidecar is a sibling of the log file (`log_007.csv` → `log_007.csv.replay.json`), opened/saved via the File System Access API. IndexedDB stays as a write-through cache, not the source of truth.

**Triple-keyed debriefs:** the *workspace* unit being debriefed is the tuple `(video, log, config)`. One sidecar per log file, containing an array of debrief records each keyed by `(videoHash, configHash)`. Same log + different video = different debrief record in the same file. Modeled on Lightroom's multi-version XMP convention.

This plan is research-backed, scoped, and broken into ship-able PRs. Read straight through; section 0 is "the story," section 1 is "the data shape," section 2 is "the UX flow," sections 3-6 are implementation, section 7 is acceptance, and the appendix is the research that justifies the design choices.

## 0. The story

Vac is a flight test engineer. He opens the replay tool in Chrome on his Mac, loads `log_007.csv` + a GoPro video + `onspeed2.cfg`. He marks DataMark annotations on stall breaks, defines four clips of interesting maneuvers, types a multi-paragraph debrief in markdown, calibrates the camera-pitch offset slider, and sets the sync anchor.

**Today**, all of that lives in IndexedDB under his Chrome profile. If he clears site data, switches profiles, or his Mac's auto-update wipes a Chrome cache, it's gone. Backup is impossible. Sharing is impossible. Reviewing on a second machine is impossible.

**After this plan**, his work lives in `log_007.csv.replay.json` next to the log. He emails the whole flight folder to his colleague and the debrief travels with it. He opens it in any text editor — it's plain JSON with markdown text fields. He puts the flight folder in Dropbox, his colleague opens the same folder in Chrome on his own machine, and the debrief loads.

A future phase adds a left-rail flight library picker that enumerates many flight folders. That phase reuses the same sidecar format and the same FSA mechanism — `showDirectoryPicker` instead of `showOpenFilePicker`. Even further out, an S3-hosted "shared flight library" replaces the FSA layer with a network adapter, same data shape.

## 1. Data shape

### 1.1 Sidecar JSON v1

One file per **log**, sibling of the log file, named `<logBasename>.<logExt>.replay.json` (so `log_007.csv` → `log_007.csv.replay.json`). The doubled extension follows the XMP convention used by Lightroom, darktable, ExifTool, and Adobe Bridge — it binds the sidecar unambiguously to one log file even when the same directory contains a config file with a similar basename.

```jsonc
{
  "$schema": "https://flyonspeed.org/schemas/replay/v1.json",
  "schemaVersion": 1,

  // The log this sidecar belongs to.
  "log": {
    "hash": "a3f8b9c1ee019aa0",          // 16 hex chars; SHA-256 prefix of first 10 KB
    "name": "log_007.csv",
    "sizeBytes": 360123456,
    "rowCount": 968427,
    "durationSec": 4751
  },

  "createdAt": "2026-05-14T15:32:00Z",   // when the sidecar was first written
  "updatedAt": "2026-05-14T17:48:12Z",   // last write
  "createdBy": "OnSpeed Replay v4.23+abc1234",

  // One entry per (video, config) combination debriefed against this log.
  // Engineer can have multiple — e.g., same log against two camera angles,
  // or before/after a calibration tweak.
  "debriefs": [
    {
      "id": "deb-2026-05-14-001",        // UUID or human-readable, unique within file
      "video": {
        "hash": "ee019aa0a3f8b9c1",
        "name": "GH010123.MP4",
        "durationSec": 893,
        "framerate": 59.94
      },
      "config": {
        "hash": "0c7d4e21aa017c33",
        "name": "onspeed2.cfg"
      },

      "createdAt": "2026-05-14T15:32:00Z",
      "updatedAt": "2026-05-14T17:48:12Z",
      "revision": 12,                     // optimistic-lock counter, see §4

      "author": "Vac",                   // single-line free text, prompts on first save

      // Camera/installation calibration. Per-debrief because it's per-video.
      "hud": {
        "pitchOffsetDeg": 1.4,
        "leftInsetMode": "off",
        "rightInsetMode": "energy"
      },

      // Sync anchor between video time and log time.
      "sync": {
        "logAnchorTimestampMs": 142853,
        "videoAnchorSec": 12.4,
        "method": "manual-datamark",      // "manual-datamark" | "auto-detected"
        "confidence": "high"              // "low" | "medium" | "high"
      },

      // User-marked DataMarks (overlaying the firmware's CSV DataMark column
      // with engineer annotations). Keyed by (value, logTimeMs).
      "marks": [
        {
          "value": 3,
          "logTimeMs": 78900,
          "name": "Power-off stall #1",
          "notes": "Body angle held at 18° for 1.2 s before break. Wing drop right.",
          "createdAt": "2026-05-14T16:12:00Z",
          "updatedAt": "2026-05-14T16:14:00Z"
        }
      ],

      // Engineer-defined clips with annotations.
      "clips": [
        {
          "id": "clip-2026-05-14-001",
          "startLogMs": 78000,
          "endLogMs":   84000,
          "label": "Stall #1",
          "notes": "Note pitch-up tendency right before break. Investigate flap-0 alpha_stall.",
          "createdAt": "2026-05-14T16:01:00Z",
          "updatedAt": "2026-05-14T16:05:00Z"
        }
      ],

      // Full-debrief markdown text. Renders via marked + DOMPurify.
      "summary": "## 11 May 26 — Steep turn investigation\n\nThree coordinated 60° turns at...",

      // Optional structured fields for triage workflow.
      "openQuestions": [
        "Is the Madgwick pitch bias an installation issue or fundamental algorithm?"
      ],
      "nextActions": [
        "Re-fly with EKF6 enabled, compare VSI peak-to-peak."
      ]
    }
  ]
}
```

### 1.2 Field decisions worth explaining

- **`hash` is a 16-hex-char SHA-256 prefix of the file's first 10 KB.** The existing `journal.js::computeLogDigest` already uses this for the IDB key. Cheap to compute, no streaming needed, collision-resistant enough for this use case. `(hash, sizeBytes)` together is the identity tuple — two files with the same first-10-KB content AND same byte count is vanishingly unlikely outside intentional duplication. Don't use `lastModified` for identity — cloud sync clients rewrite it.
- **Per-record `updatedAt`** at the mark/clip level enables newer-wins reconciliation when both IDB and sidecar have changes (see §3).
- **`revision: int`** at the debrief level is the optimistic-lock counter. Incremented on every write to that debrief. If a reader sees a revision they didn't expect, they re-read before clobbering.
- **`summary` is markdown source, not HTML.** Rendered at display time with marked + DOMPurify. Stays human-readable in any text editor.
- **`createdBy` includes the firmware build SHA.** When schema v2 ships, knowing what version wrote a v1 file lets us migrate intelligently.
- **No `videoFile` / `videoPath`** — only `name` and `hash`. The file might not be in the same folder as the log; we don't need to track its path, we just need to verify the right video is loaded.

### 1.3 What does NOT go in the sidecar

- Scrub position, last-played-time, window size — session state, stays in localStorage.
- Recent-files list — that's a global app concern, not per-flight.
- File handles — those go in IndexedDB (FSA handles can't be serialized to JSON).
- Generated MP4 clips — those are outputs, not inputs.

## 2. UX flow

### 2.1 First-time use (no sidecar exists yet)

1. User picks log + (later) video + config via existing flow. Tool computes `logHash`.
2. Tool checks for sibling `<logname>.<ext>.replay.json`. None found.
3. Page mounts in current "in-memory state" mode. Engineer edits freely.
4. After first non-trivial edit (first clip, first mark, first character of debrief text), a sticky banner appears at the top:
   > 💾 **Your notes will be lost if you close this tab.** Save them to a file next to your log → \[**Save sidecar**]
5. Click → `showSaveFilePicker({ startIn: logFileHandle, suggestedName: 'log_007.csv.replay.json', types: [{ accept: { 'application/json': ['.json'] } }] })`.
6. After save, the sidecar handle is persisted in IndexedDB under `replay-sidecar-handle/<logHash>`. Subsequent edits debounce-write to it. The banner disappears, replaced by a small "💾 Saved 2 s ago" indicator in the toolbar.

### 2.2 Returning user (sidecar exists and was attached last session)

1. User reopens the replay tool. Page reads IndexedDB, finds the sidecar handle for the last-loaded log.
2. `queryPermission({mode:'readwrite'})` returns `'granted'` (because user clicked "Allow on every visit" last session, Chrome 122+ behavior).
3. Sidecar auto-loads. Engineer sees their work exactly as they left it.
4. If `queryPermission` returns `'prompt'` (default, or "Allow this time" was picked, or backgrounded too long), show the existing Resume banner — its click is the user-gesture site for `requestPermission`.

### 2.3 Returning user (sidecar was moved or deleted)

1. Handle deserializes. `getFile()` throws.
2. Fall back to IndexedDB-only mode. Show banner: "⚠️ Couldn't open `log_007.csv.replay.json` — file may have moved. \[Re-pick file] \[Continue without sidecar]".

### 2.4 Multiple debriefs in one file

Common case: engineer debriefs the same log against two different camera angles, or re-runs the debrief after a calibration tweak.

1. User opens log + video + config. Tool computes the triple `(logHash, videoHash, configHash)`.
2. Reads sidecar, looks for a debrief record where all three hashes match.
3. **Match found:** load that debrief.
4. **No match:** show "New debrief for this video+config" indicator. First save appends a new entry to `debriefs[]`.
5. **Log loaded but no video yet:** show "Recent debriefs" picker — list each debrief's `(videoName, configName, updatedAt, summary first line)`. Engineer can pick one to load (and the tool will offer to load that video file too) or start fresh.

### 2.5 Browser incompatibility

Firefox / Safari users have no FSA API. The tool:

1. Detects support at startup (`'showOpenFilePicker' in window`).
2. If unsupported, hides the "Save sidecar" affordance entirely. IndexedDB is the only store.
3. Adds an "Export debrief" button that downloads the current state as a `.replay.json` via `<a download>`. They can email/manually-place the file.
4. Adds a "Load debrief" button that accepts a `.replay.json` upload to import (one-time, no resume — the imported state merges into IDB).

This is the same progressive-enhancement shape Excalidraw uses.

## 3. Reconciliation between IndexedDB and sidecar

**Rule: per-record `updatedAt`-wins.**

- IndexedDB stays as a fast cache and as the sole store for browsers without FSA.
- Every edit writes IDB synchronously (no UX latency change from today).
- A debounced (~500 ms) writer flushes the full debrief to the sidecar.
- On load, both sources are read. For each `(value, logTimeMs)` mark and each `id` clip, the record with the higher `updatedAt` wins. Top-level fields (summary, hud, sync) also reconcile by `updatedAt`.

This handles the common cases cleanly:

| Scenario | Result |
|---|---|
| First-ever use, no sidecar | IDB-only; banner offers to create sidecar |
| Sidecar exists, IDB empty (new browser profile, second machine) | Sidecar wins; IDB populated from it |
| IDB exists, no sidecar (Firefox) | IDB-only; export-on-demand |
| Both exist, IDB newer (offline-edit-then-reattach) | IDB wins per record, sidecar updated on next write |
| Both exist, sidecar newer (other-tab wrote, then we resumed) | Sidecar wins per record, IDB synced from it |
| Both exist, divergent edits (rare — needs concurrent writes) | Newer-wins per record; user warned if `revision` jumped |

## 4. Two-tab safety

Two mechanisms together cover the realistic two-tab case (engineer accidentally opens the same flight in a second tab):

### 4.1 `navigator.locks` for the file-level lock

When a tab attaches a sidecar handle, it requests `navigator.locks.request('replay-sidecar:' + logHash, {mode:'exclusive'}, ...)` for the duration the handle is open. Second tab attempting the same:

- With `{ifAvailable: true}`: returns `null`. Show banner: "Already editing this flight in another tab. \[Open read-only] \[Steal lock]". Engineer chooses.

Locks die with the page (tab crash, navigate away, browser close). No stale-lock problem.

### 4.2 `revision` counter for race detection

Even with the lock, two tabs that loaded the file before either acquired the lock can race. So:

- Every debrief carries `revision: int`. Incremented on each write.
- Before write: read current file, find this debrief's current `revision`. If it's higher than the one we last loaded, conflict. Show "this debrief was modified by another tab — \[keep yours] \[take theirs] \[merge]".
- Three-button dialog, no CRDT. Won't happen on the happy path.

Together: lock prevents the race in 99% of cases; revision counter catches the 1% (sleep/wake, lock not yet acquired, etc.).

### 4.3 `BroadcastChannel` for live UX updates

When tab A saves, broadcast `{channel: 'replay-sidecar', event: 'saved', logHash, debriefId, revision}`. Tab B (read-only) sees the broadcast, reloads its view automatically.

Same-origin only. Doesn't affect correctness — just nicer UX.

## 5. Implementation

### 5.1 New files

```
docs/site/docs/data-and-logs/replay/lib/replay/
  sidecar.js              ← read/write/discover/reconcile
  sidecar-schema.js       ← shape validators (hand-rolled type guards, see appendix on Ajv)
  sidecar-migration.js    ← IDB→sidecar one-time migration on first attach
```

### 5.2 Files to modify

```
docs/site/docs/data-and-logs/replay/lib/replay/
  journal.js              ← reconciliation logic, mark/clip writes also queue sidecar flush
  fileHandles.js          ← extend to mode:'readwrite' for sidecar; sidecar handle stored under separate IDB key
  persistence.js          ← move sync + clip-geometry + recent-files awareness to use sidecar when available
docs/site/docs/data-and-logs/replay/lib/pages/
  ReplayPage.js           ← banner UI, "Save sidecar" button, "Recent debriefs" picker, save-indicator
```

### 5.3 Hashing

Keep the existing `computeLogDigest` (10-KB-prefix via SubtleCrypto). It's good enough for content-key identity. **Don't** introduce hash-wasm for full-file hashing yet — current architecture doesn't need cryptographic strength, and a 360 MB hash-on-load stall (even in a Worker) is UX-worse than the prefix approach.

If we later need tamper-detection or cross-origin file identity, wrap behind a `hashFile(file, {kind: 'prefix' | 'full'})` helper. Future swap to hash-wasm WASM SHA-256 (~426 MB/s) is local.

### 5.4 Markdown

Render `summary` and per-record `notes` with **marked** + **DOMPurify**. Both small (~30 KB combined), both well-maintained, both safe-by-default when chained correctly:

```js
import { marked } from 'marked';
import DOMPurify from 'dompurify';

const renderMarkdown = (src) => DOMPurify.sanitize(marked.parse(src));
```

Add both as vendored ESM under `lib/vendor/` matching the existing Preact pattern. Don't pull in npm.

### 5.5 Validation

Hand-rolled type guards in `sidecar-schema.js`, mirroring the style in `journal.js`:

```js
const isFiniteNumber = (x) => typeof x === 'number' && Number.isFinite(x);
const isNonEmptyString = (x) => typeof x === 'string' && x.length > 0;
const isMark = (x) => x && typeof x === 'object'
  && isFiniteNumber(x.value) && isFiniteNumber(x.logTimeMs)
  && (x.name === undefined || typeof x.name === 'string')
  // ...
```

Returns a `{ok: true, value} | {ok: false, error}` discriminated union. Logs `console.warn` on shape mismatch and falls back to IDB-only mode. No Ajv (CSP issue with `Function` constructor, see appendix).

## 6. PR sequence

Four PRs, each landable independently:

| # | Title | Effort | Branches off |
|---|---|---|---|
| 1 | Sidecar schema + reader + IDB-only no-op writer | 1 day | master |
| 2 | Sidecar writer + FSA integration + Save banner | 1.5 days | PR 1 |
| 3 | Multi-debrief support + Recent debriefs picker | 1 day | PR 2 |
| 4 | Two-tab safety: navigator.locks + revision check + BroadcastChannel | 0.5 day | PR 3 |

PRs 1 and 2 can be one PR if there's appetite; splitting helps review. PR 3 is the bulkiest because it adds UI surfaces. PR 4 is small and safety-net.

Total: ~4 engineering days for the full migration, including tests.

## 7. Acceptance

### 7.1 Functional

- [ ] On a fresh log + fresh browser, edits create a sidecar via Save banner; sidecar lives at `<logname>.<ext>.replay.json` next to the log.
- [ ] On reload, Chrome 122+ "Allow on every visit" path auto-resumes the sidecar without a prompt.
- [ ] On reload with permission `'prompt'`, the Resume banner click loads the sidecar.
- [ ] Same log + different video + same config → second debrief record appends to existing sidecar; first debrief preserved.
- [ ] Sidecar moved out from under the tool → graceful fall-back to IDB-only mode with clear banner.
- [ ] Firefox: no Save banner; Export/Import buttons work for manual sidecar transfer.

### 7.2 Data integrity

- [ ] IndexedDB → sidecar reconciliation preserves all marks and clips when both sources have data.
- [ ] `updatedAt` per record correctly resolves conflicts (newer wins).
- [ ] Schema validation rejects malformed sidecars without crashing; logs `console.warn` and falls back to IDB.
- [ ] Atomic write: page kill during `createWritable` → old sidecar intact.

### 7.3 Two-tab

- [ ] Second tab gets read-only banner when first tab holds lock.
- [ ] Both tabs editing → `revision` mismatch surfaces three-button dialog.
- [ ] BroadcastChannel updates second tab's view when first tab saves.

### 7.4 Tests

- [ ] Schema validator unit tests: valid sidecar, missing required field, wrong type, unknown field (lenient).
- [ ] Reconciliation unit tests for the six scenarios in §3 table.
- [ ] Playwright e2e: open log → mark → save → reload → verify mark loaded from sidecar.
- [ ] Playwright e2e: Firefox path → mark → export → load in new session → verify mark restored.

## 8. Out of scope (deferred)

- **Flight library picker** (`showDirectoryPicker`, left-rail enumeration). This is the natural Phase 2. Same sidecar format; just different discovery mechanism.
- **S3-hosted shared library**. Phase 3, when multi-engineer cloud sync is needed. Same sidecar format; network adapter behind the FSA layer.
- **CRDT / Yjs / Automerge multi-user editing.** Not needed for single-engineer-mostly-offline use case. Documented off-ramp in appendix if the threshold is ever crossed.
- **Full-file SHA-256 tamper-detection.** Wrap hash behind `hashFile(file, {kind})` helper so the swap is local later.
- **Sidecar version migration.** When schema v2 ships, readers must handle v1 files; deferred until v2 exists.

## Appendix A: Research findings (locked design constraints)

Each finding ranked by design-impact. **Decisions that, if reversed, force a rewrite a year later.**

### A.1 — Browser support is the load-bearing constraint

- **Chrome 105+, Edge 105+, Opera 91+** ship full FSA API. Mozilla position: harmful. Safari: not supported.
- Source: [caniuse: native-filesystem-api](https://caniuse.com/native-filesystem-api).
- **Implication:** progressive enhancement, not requirement. Today's IDB-only path becomes the Firefox/Safari path forever. Don't gate notes on FSA.

### A.2 — `queryPermission` after reload returns `'prompt'` by default

- Chrome 122+ adds "Allow on every visit" → subsequent `queryPermission` returns `'granted'`.
- Without it (or before 122, or after long backgrounded-tab idle): `'prompt'`. Need user gesture.
- Source: [Chrome blog: Persistent Permissions for File System Access API](https://developer.chrome.com/blog/persistent-permissions-for-the-file-system-access-api).
- **Implication:** Resume banner click is the user-gesture site for `requestPermission`. Existing `useFileHandleResume` pattern is the template.

### A.3 — `createWritable` is atomic (temp + rename on close)

- Page crash mid-write: original file intact.
- Source: [MDN: createWritable](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemFileHandle/createWritable).
- **Implication:** Trivial safe-write recipe: `const w = await handle.createWritable(); await w.write(JSON.stringify(doc)); await w.close()`.

### A.4 — `startIn: fileHandle` works for save dialog default location

- `showSaveFilePicker({startIn: logFileHandle})` opens dialog in log's folder.
- Source: [MDN: showSaveFilePicker](https://developer.mozilla.org/en-US/docs/Web/API/Window/showSaveFilePicker).
- **Implication:** First-save UX is one-click — dialog appears in the right folder with the right suggested name.

### A.5 — Cloud-storage folders: no guarantees, document caveats

- macOS uses FileProvider API for on-demand hydration; FSA's `getFile()` will block.
- `lastModified` is sometimes rewritten by sync clients.
- **Implication:** content-hash for identity, not metadata. Show spinner during first hash. Document "if resume banner missing your last session, suspect cloud-sync changed lastModified" as a runbook item.

### A.6 — OPFS is NOT what we want

- Origin Private File System is sandboxed; files invisible to user's file manager.
- Source: [MDN: File System API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API).
- **Implication:** Confirms we want the regular FSA path. Document explicitly so reviewers don't ask.

### A.7 — Hashing: SubtleCrypto for 10 KB prefix, hash-wasm for full file later

- SubtleCrypto has no streaming API.
- hash-wasm: SHA-256 ~426 MB/s, xxHash3 ~16 GB/s.
- Source: [hash-wasm benchmarks](https://github.com/Daninet/hash-wasm).
- **Implication:** Keep current 10-KB-prefix SubtleCrypto for content-key purposes. Wrap behind `hashFile(file, {kind})` helper for future swap.

### A.8 — Schema versioning: integer + lenient readers + per-record `updatedAt`

- XMP uses RDF/XML namespace URIs (heavyweight); modern JSON tools use integer or semver.
- Forward-compatibility convention: readers ignore unknown keys.
- **Implication:** Top-level `schemaVersion: 1`. Readers ignore unknown top-level fields. Writers refuse to overwrite a higher-version file (refuse → fall back to IDB-only mode with clear banner).

### A.9 — Ajv is the wrong validation choice

- Ajv compiles with `Function` constructor; requires `unsafe-eval` in CSP.
- Source: [Ajv: Security](https://ajv.js.org/security.html).
- **Implication:** Hand-rolled type guards in `sidecar-schema.js`. Same pattern `journal.js` already uses.

### A.10 — `navigator.locks` + `revision` counter cover two-tab race

- Web Locks API is Baseline-widely-available (Chrome, Firefox, Safari since March 2022).
- Locks die with page, no stale-lock problem.
- Source: [MDN: Web Locks API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Locks_API).
- **Implication:** No lockfile pattern, no CRDT. Lock for write duration; `revision` catches the race the lock missed.

### A.11 — Marked + DOMPurify for markdown

- Marked alone is XSS-unsafe; pair with DOMPurify.
- Source: [marked README](https://github.com/markedjs/marked).
- **Implication:** Vendor both as ESM. ~30 KB combined.

### A.12 — Sidecar filename convention: `<basename>.<ext>.replay.json` (XMP-style)

- Lightroom: `IMG_1234.CR2.xmp`. Darktable: same. ExifTool: same.
- **Implication:** Doubled extension binds sidecar unambiguously to one log. Decision locked: `log_007.csv.replay.json`, NOT `log_007.replay.json`.

## Appendix B: Existing journal — what we're migrating from

Active file: `docs/site/docs/data-and-logs/replay/lib/replay/journal.js`.

**IDB database `replay-journal`, object store `journal-v1`, keyPath `logHash`:**

```js
{
  logHash:    string,       // 16-char hex SHA-256 prefix of first 10KB of log
  logName:    string,
  videoName?: string,
  lastUsed:   number,       // Date.now()
  marks: [{ value, logTimeMs, name?, notes?, createdAt, updatedAt }, ...],
  clips: [{ id, startLogMs, endLogMs, label?, notes?, createdAt, updatedAt }, ...],
}
```

**Adjacent localStorage keys (in `persistence.js`):**
- `replay-sync-<digest>-v1` → `{videoTakeoffSec, logTakeoffMs, anchorKind}`
- `replay-clips-<digest>-v1` → `[{startMs, endMs, label}, ...]` (geometry, **not** annotation)
- `replay-recent-files-v1` → `{video, log, cfg}` metadata
- `replay-banner-dismissed-v1` → JSON signature of last-dismissed banner

**Adjacent IDB database `replay-fsa`, object store `handles-v1`** (in `fileHandles.js`):
- FileSystemFileHandle objects for video/log/cfg, keyed by `'current'`
- `signature` field built from `(name, size, lastModified)` of each
- **Read-mode only today; the readwrite migration is in scope for this plan**

**Important migration notes:**
- Clips have **two** persistence layers: geometry in localStorage, annotation in IDB. Migration reconciles both into one sidecar `clips[]` array.
- Marks parser is source of truth for `(value, logTimeMs)` tuples (extracted from CSV's `DataMark` column). Journal stores the overlay (`name`, `notes`). Migration preserves the same shape.

## Appendix C: Risks I couldn't fully resolve

1. **Chrome 122 "Allow on every visit" auto-revoke timer for backgrounded tabs.** Documented to exist; not quantified. Plan as runbook item ("if Vac reports lost permission, suspect long-idle tab").

2. **`getFile()` behavior on Dropbox/Drive placeholder files.** Should hydrate via OS API; not browser-documented. Empirical test on Sam's actual Dropbox setup before PR 2 ships.

3. **`lastModified` survival through Dropbox round-trip on macOS.** Likely unreliable; mitigated by using content-hash for identity, not metadata. Confirm in PR 1 testing.

These are runbook items, not blockers.

## Appendix D: Why not Yjs / Automerge / RxDB?

- Yjs core ~65 KB, Automerge ~300 KB. Both excellent for **multi-user real-time editing**.
- Our case: single engineer, mostly offline, occasional two-tab race. Cost too high for the payoff.
- **Off-ramp:** if we ever add multi-engineer cloud sync, **Yjs** is the recommended migration path (smaller, better fit for our document shape). Sidecar JSON becomes the Yjs serialization format; existing files migrate via "load JSON → wrap in Yjs doc → save Yjs binary."

## Appendix E: References

- [caniuse: native-filesystem-api](https://caniuse.com/native-filesystem-api)
- [MDN: File System Access API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_Access_API)
- [MDN: queryPermission](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemHandle/queryPermission)
- [MDN: createWritable](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemFileHandle/createWritable)
- [MDN: showSaveFilePicker](https://developer.mozilla.org/en-US/docs/Web/API/Window/showSaveFilePicker)
- [MDN: showDirectoryPicker](https://developer.mozilla.org/en-US/docs/Web/API/Window/showDirectoryPicker)
- [MDN: Web Locks API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Locks_API)
- [MDN: BroadcastChannel](https://developer.mozilla.org/en-US/docs/Web/API/BroadcastChannel)
- [Chrome: File System Access](https://developer.chrome.com/docs/capabilities/web-apis/file-system-access)
- [Chrome blog: Persistent Permissions](https://developer.chrome.com/blog/persistent-permissions-for-the-file-system-access-api)
- [hash-wasm benchmarks](https://github.com/Daninet/hash-wasm)
- [darktable: sidecar files](https://docs.darktable.org/usermanual/development/en/overview/sidecar-files/sidecar/)
- [marked README](https://github.com/markedjs/marked)
- [Ajv security docs](https://ajv.js.org/security.html)
- [Excalidraw: browser-fs-access blog](https://plus.excalidraw.com/blog/browser-fs-access)
- [Yjs vs Automerge HN discussion](https://news.ycombinator.com/item?id=41012895)
