# PLAN — FileSystemFileHandle persistence for the Replay tool

**Date:** 2026-05-12.
**Branch:** new feature branch off `sam/video-overlay` tip
(post-#532 merge).
**Owner:** Sam.
**Tracking issue:** #72.

This plan supersedes the "File-handle persistence via File System
Access API" bullet under Step 2 of
`2026-05-08-video-overlay-replay.md` with actionable detail. The
prior bullet was placeholder; this is the concrete spec.

---

## Problem

Today the replay page (`/data-and-logs/replay/` on the docs site)
takes three file inputs every session:

- Flight video (`.mp4`) — up to 18 GB GoPro footage in practice
- SD card log (`.csv`) — typically 5–30 MB
- OnSpeed XML config (`.cfg`) — ~3 KB

Sync state and clip list **do** persist across reloads, content-keyed
by the SHA-256 prefix of the log file (shipped 2026-05-12 in PR #527).
But the file inputs themselves cannot be auto-restored: browsers
deliberately do not give pages programmatic access to previously
selected `<input type="file">` results across reloads, for security
reasons.

Sam's feedback 2026-05-12: re-picking three files on every reload is
"bananas." The friction kills the daily-driver use case.

## Solution

Use the File System Access API to capture **persistable file handles**
on first pick. The API gives the page a `FileSystemFileHandle` that
can be stored in IndexedDB, then retrieved on the next reload. The
handle by itself does not grant read access — but `handle.requestPermission({ mode: 'read' })`
re-grants access with a single user click. Browsers batch the
permission prompts when multiple handles are requested in the same
gesture, so one click covers all three files.

### Flow

1. **First visit**: pilot clicks "Pick video / log / config" buttons
   (one per slot). Each button calls `showOpenFilePicker({ types: [...] })`
   and the returned `FileSystemFileHandle` is stored in IndexedDB
   keyed by slot name (`video`, `log`, `config`).
2. **Reload**: on page mount, read the three handles from IndexedDB.
   If all three present, render a "Resume last session?" banner with
   a single button.
3. **Resume click**: the banner button calls
   `handle.requestPermission({ mode: 'read' })` for each of the three
   handles in the same click handler (must be a user gesture).
   Browsers show one consolidated permission dialog covering all three
   files. Pilot clicks Allow once.
4. **Files load**: page proceeds as if the pilot had just picked the
   files. The persistence layer (PR #527) picks up sync + clips by
   log SHA-256 automatically.
5. **Re-pick**: each slot's button is still available; clicking it
   replaces the stored handle.

### Browser support gate

File System Access API ships in Chrome and Edge desktop today. It is
not in Firefox or Safari. The replay tool is already Chrome/Edge-only
because of WebCodecs, so the gate is **not new ground**.

On unsupported browsers, the page falls back to the existing
`<input type="file">` flow with no banner. Document the fallback in
the user-facing replay docs.

---

## API contract

```
tools/web/lib/replay/fileHandles.js                  ← new module

  // IndexedDB schema: db 'onspeed-replay', store 'fileHandles',
  // key = slot name ('video' | 'log' | 'config'), value = handle.
  // Plus a sibling key 'recentFilesSignature' = { videoName,
  // videoSize, logName, logSize, configName, configSize, savedAt }
  // for the banner text and for stale-detection (size mismatch on
  // file system reload).

  saveHandle(slot, handle): Promise<void>
  loadHandle(slot): Promise<FileSystemFileHandle | null>
  loadAllHandles(): Promise<{ video?, log?, config? }>
  clearHandle(slot): Promise<void>
  clearAllHandles(): Promise<void>

  // Permission helper. Browsers batch the prompts when called in the
  // same user-gesture handler.
  requestRead(handles): Promise<{ granted: boolean, denied: string[] }>

  loadRecentFilesSignature(): Promise<RecentFilesInfo | null>
  saveRecentFilesSignature(info: RecentFilesInfo): Promise<void>

tools/web/lib/components/ReplayResumeBanner.js       ← new component

  Props:
    info:   RecentFilesInfo | null        // already-resolved from IDB
    onResume(): void                      // calls requestRead +
                                          // loads files into state
    onDismiss(): void                     // clearAllHandles +
                                          // hides banner
```

In `ReplayPage.js`:

- Replace each `<input type="file">` with a button that calls
  `showOpenFilePicker()` on click. On supported browsers, the
  returned handle is persisted via `saveHandle()`. Read access via
  `handle.getFile()` produces a `File` exactly like the legacy input
  flow, so downstream pipeline is unchanged.
- On unsupported browsers (`window.showOpenFilePicker` undefined),
  render the existing `<input type="file">` as before.
- On mount, if all three handles are present in IndexedDB **and** the
  browser supports the API, render `<ReplayResumeBanner>` above the
  pickers. On dismiss, banner hides. On resume, click handler does:

  ```js
  const grants = await Promise.all([
    handles.video.requestPermission({ mode: 'read' }),
    handles.log.requestPermission({ mode: 'read' }),
    handles.config.requestPermission({ mode: 'read' }),
  ]);
  if (grants.every(g => g === 'granted')) {
    const [vFile, lFile, cFile] = await Promise.all([
      handles.video.getFile(),
      handles.log.getFile(),
      handles.config.getFile(),
    ]);
    // hand off to the existing slot-setter pipeline
    setVideo(vFile); setLog(lFile); setConfig(cFile);
  }
  ```

### Permission gesture

`requestPermission()` must be called from a user gesture handler.
Calling it from `useEffect` or any post-mount async path will fail
silently or be rejected by the browser. The banner button click is
the gesture; do the work in the click handler directly, not after an
`await` chain that loses the gesture token.

### Persistence schema versioning

Store keys live under a versioned namespace
(`replay-file-handles-v1`). Version-bump the namespace when changing
the shape so old data isn't accidentally misread. On first load with
a new version, treat any old version's entries as not present.

---

## Optional follow-on: directory picker

A natural extension that unblocks #64 (multi-GoPro chapter ingest):
`showDirectoryPicker()` returns a `FileSystemDirectoryHandle` that
enumerates files. One picker gesture covers an entire folder.

Flow:

1. Pilot clicks "Pick session folder" (e.g.,
   `~/Dropbox/N720AK/replay-sessions/2026-05-11/`).
2. Page enumerates files via the directory handle.
3. Auto-detect by extension: `.mp4` → video, `.csv` → log, `.cfg` →
   config. If a directory contains multiple chapter files
   (`GH01*.MP4`, `GH02*.MP4`), defer to #64's chapter-concat logic.
4. Persist the directory handle (one handle, not three) in IndexedDB.
   On reload, single permission dialog re-grants the whole directory.

If this rides in the same PR as the basic file-handle persistence,
budget +0.5 day. If deferred, file it as a follow-on to #72 (the
basic three-slot persistence is the value-now feature; the directory
picker is the cherry on top).

---

## Scope boundary

- **This PR (#72)**: per-slot file-handle persistence for the three
  existing input slots; resume banner; permission re-grant via single
  user click.
- **Future PR (#64)**: directory picker + multi-chapter ingest. May
  ride along here if the time fits — flag it in the PR description.

Out of scope:

- Auto-load on page mount without a user gesture. The browser security
  model requires the click. Don't fight it.
- Cloud-backed handles (Drive / iCloud). Out of scope for the
  static-build SPA we ship today.
- Cross-device sync. Pilots use one machine per session.

---

## Verification plan

Playwright test (`tools/web/test/replay-file-handles.spec.mjs`):

1. Launch the dev server + open the replay page in a Chromium
   context configured to allow File System Access without prompting
   (Chromium flag `--enable-features=FileSystemAccessAPI` plus
   `permissions: ['cross-origin-isolated', 'storage-access']` on the
   browser context).
2. Click each of the three slot buttons; in the picker dialog, select
   the fixture files from `test-data/`.
3. Confirm sync + clips appear (carried over by the existing #527
   persistence).
4. Reload the page.
5. Assert the "Resume last session?" banner is visible and shows the
   three filenames + sizes.
6. Click Resume.
7. Assert the three slots populate, the indexer renders against the
   first log row, and sync + clips restore identically to step 3.

Manual verification: on a real Chrome session (not Playwright),
confirm the consolidated permission dialog appears with all three
files listed. Take a screenshot for the PR description.

Negative cases:

- Reload after deleting one of the fixture files outside the browser.
  Expect: `getFile()` rejects on that slot, banner shows an error,
  pilot falls back to per-slot pick.
- Reload in Firefox / Safari. Expect: banner hidden, regular `<input>`
  flow visible, no console errors.

---

## Effort

- **Basic file-handle persistence** (three slots, banner, resume):
  ~1 day including Playwright test.
- **+ directory picker rideshare** (single-folder session pick):
  +0.5 day.

Total: ~1–1.5 days depending on rideshare.

---

## Reference for the implementer

- The plan this supersedes: `2026-05-08-video-overlay-replay.md`,
  Step 2a "File-handle persistence via File System Access API". The
  parent plan only said "do this"; this doc says how.
- The persistence module that already exists:
  `tools/web/lib/replay/persistence.js` (shipped in PR #527) handles
  sync + clip persistence by log SHA-256. Pattern to follow:
  - Same IndexedDB connection (`onspeed-replay` db)
  - Same `idb-keyval`-like wrapper if used; otherwise raw IDB calls
  - Same async/await conventions
- MDN reference: `FileSystemFileHandle.requestPermission()` —
  https://developer.mozilla.org/en-US/docs/Web/API/FileSystemHandle/requestPermission
- WICG spec: https://wicg.github.io/file-system-access/

---

## Dispatch prompt

```
Implement FileSystemFileHandle persistence for the OnSpeed Replay tool.

WORKTREE: ~/code/onspeed/onspeed-worktrees/video-overlay/  (off sam/video-overlay tip)
PLAN: docs/superpowers/plans/2026-05-12-filesystem-handle-persistence.md
TEST DATA: $WORKTREE/test-data/{video.mp4, log.csv, config.cfg}
DEV SERVER:
  cd $WORKTREE && node tools/web/dev-server/server.mjs --port 9001
PAGE URL: http://127.0.0.1:8000/data-and-logs/replay/ (mkdocs serve)

WHAT TO BUILD:
  1. tools/web/lib/replay/fileHandles.js — IDB save/load/clear for
     FileSystemFileHandle per slot, plus a recent-files signature for
     the banner text.
  2. tools/web/lib/components/ReplayResumeBanner.js — the
     "Resume last session?" UI.
  3. tools/web/lib/pages/ReplayPage.js — replace <input type="file">
     calls with showOpenFilePicker() on supported browsers, fall back
     to <input> on others, render the banner when all three handles
     are stored, do the permission request inside the resume click
     handler (must be a user gesture).
  4. tools/web/test/replay-file-handles.spec.mjs — Playwright test
     per the verification plan in the spec.

CONSTRAINTS:
  - Don't break the legacy <input type="file"> flow on Firefox /
    Safari. Feature-detect window.showOpenFilePicker.
  - Don't call requestPermission outside a user gesture.
  - Persist under versioned IDB key namespace (replay-file-handles-v1)
    so future schema bumps are clean.
  - No directory picker in this PR unless the rideshare time fits.
    Flag in the PR description either way.

VERIFY:
  - npm test (existing tests pass, new Playwright spec passes)
  - Manual: open the page in Chrome, pick three files, reload, click
    Resume. Confirm one consolidated permission dialog covering all
    three. Confirm sync + clips restore.

COMMIT: one focused commit, "replay: persist file handles across reload".
```
