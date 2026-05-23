// sidecar.js — File System Access integration for `.replay.json`
// sidecars. Sister to fileHandles.js, but in readwrite mode and keyed
// off the flight directory rather than per-file signatures.
//
// One sidecar per log file, named `<logname>.<ext>.replay.json` and
// stored next to the log. The directory handle obtained via
// `showDirectoryPicker()` covers the log, the sidecar, optional cfg
// and video — one user-gesture pick grants readwrite over everything
// in the folder.
//
// What lives in this file:
//
//   - `pickFlightDir` / `storeFlightDirHandle` / `loadFlightDirHandle`
//     — directory-handle persistence in a separate IDB store
//     (`flight-dir-handles-v1`) so it doesn't entangle with the
//     existing per-log handle resume flow.
//
//   - `discoverFlightContents(dirHandle)` — enumerates immediate
//     children of a flight folder, classifies them as log / config /
//     video / sidecar handles. Tolerates Vac's wild folder naming.
//     Phase 1: no recursion.
//
//   - `readSidecarForLog(dirHandle, logFileName)` — looks for the
//     matching `.replay.json` next to a log and returns
//     `{ok, value, error} | null`.
//
//   - `writeSidecar(handle, doc)` — atomic write. Bumps `revision` and
//     `updatedAt` before serializing. Never keeps the writable stream
//     open across awaits.
//
//   - `useSidecar({...})` — Preact hook wiring up the lifecycle: when
//     a log loads, look for a sidecar; when edits roll in, debounce-
//     save. Surfaces save state for the toolbar pill.

import { useState, useEffect, useCallback, useRef }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';
import {
  emptySession, parseSidecar, sidecarFileNameFor, nowISO,
  SIDECAR_EXT,
} from './sidecar-schema.js';

// ---------------------------------------------------------------------
// IndexedDB — separate store from fileHandles.js so the resume flows
// don't entangle. Keyed by a single record `'current'` for Phase 1;
// future multi-flight support can switch to per-flight keys.
// ---------------------------------------------------------------------

const FLIGHT_DIR_DB_NAME    = 'replay-flight-dir';
const FLIGHT_DIR_STORE_NAME = 'flight-dir-handles-v1';
const FLIGHT_DIR_DB_VERSION = 1;
const FLIGHT_DIR_RECORD_KEY = 'current';

function _openFlightDirDb() {
  return new Promise((resolve, reject) => {
    if (typeof indexedDB === 'undefined') {
      reject(new Error('IndexedDB not available'));
      return;
    }
    const req = indexedDB.open(FLIGHT_DIR_DB_NAME, FLIGHT_DIR_DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(FLIGHT_DIR_STORE_NAME)) {
        db.createObjectStore(FLIGHT_DIR_STORE_NAME);
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('IDB open failed'));
  });
}

function _withFlightDirStore(mode, fn) {
  return _openFlightDirDb().then(db => new Promise((resolve, reject) => {
    let result;
    const tx = db.transaction(FLIGHT_DIR_STORE_NAME, mode);
    const store = tx.objectStore(FLIGHT_DIR_STORE_NAME);
    Promise.resolve(fn(store)).then(r => { result = r; }, reject);
    tx.oncomplete = () => { db.close(); resolve(result); };
    tx.onabort = () => { db.close(); reject(tx.error || new Error('IDB tx aborted')); };
    tx.onerror = () => { db.close(); reject(tx.error || new Error('IDB tx error')); };
  }));
}

function _reqToPromise(req) {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('IDB request failed'));
  });
}

// ---------------------------------------------------------------------
// Browser feature detection
// ---------------------------------------------------------------------

export function isDirectoryPickerSupported() {
  return typeof globalThis !== 'undefined' &&
         typeof globalThis.window !== 'undefined' &&
         typeof globalThis.window.showDirectoryPicker === 'function';
}

// ---------------------------------------------------------------------
// Directory-handle persistence
// ---------------------------------------------------------------------

export async function storeFlightDirHandle(handle) {
  if (!handle) return;
  try {
    await _withFlightDirStore('readwrite', store =>
      _reqToPromise(store.put({ handle, storedAt: Date.now() }, FLIGHT_DIR_RECORD_KEY)));
  } catch (err) {
    console.warn('sidecar: storeFlightDirHandle failed', err);
  }
}

export async function loadFlightDirHandle() {
  try {
    const record = await _withFlightDirStore('readonly', store =>
      _reqToPromise(store.get(FLIGHT_DIR_RECORD_KEY)));
    if (!record || !record.handle) return null;
    return record.handle;
  } catch {
    return null;
  }
}

export async function clearFlightDirHandle() {
  try {
    await _withFlightDirStore('readwrite', store =>
      _reqToPromise(store.delete(FLIGHT_DIR_RECORD_KEY)));
  } catch {
    // best-effort
  }
}

// ---------------------------------------------------------------------
// pickFlightDir — wraps showDirectoryPicker in readwrite mode.
// Must be called inside a user-gesture handler. Returns the handle on
// success, null on user cancel.
// ---------------------------------------------------------------------

export async function pickFlightDir() {
  if (!isDirectoryPickerSupported()) return null;
  try {
    // mode: 'readwrite' so the same gesture grants write permission to
    // every sidecar inside the folder. Chrome batches the prompt into
    // one dialog when the picker is mode:'readwrite' (no extra prompt
    // round-trip when we go to write).
    const handle = await window.showDirectoryPicker({ mode: 'readwrite' });
    return handle;
  } catch (err) {
    if (err && err.name === 'AbortError') return null;
    // Chrome's one-picker-at-a-time guard fires InvalidStateError when a
    // reload arrives while a prior picker is still resolving. Treat as
    // a non-event so the user doesn't see a toast for a race.
    if (err && err.name === 'InvalidStateError') return null;
    throw err;
  }
}

// Non-prompting permission probe for a directory handle. Mirrors
// queryPermissionForHandles from fileHandles.js so the auto-resume
// path can detect "Allow on every visit" without surfacing a prompt.
export async function queryDirPermission(handle, mode = 'readwrite') {
  if (!handle || typeof handle.queryPermission !== 'function') return 'prompt';
  try {
    return await handle.queryPermission({ mode });
  } catch {
    return 'prompt';
  }
}

// Re-request permission on a previously-stored handle. Must be called
// inside a user-gesture. Returns true on success.
export async function requestDirPermission(handle, mode = 'readwrite') {
  if (!handle || typeof handle.requestPermission !== 'function') return false;
  try {
    const r = await handle.requestPermission({ mode });
    return r === 'granted';
  } catch {
    return false;
  }
}

// ---------------------------------------------------------------------
// Directory enumeration
// ---------------------------------------------------------------------

// Classify a filename into one of {log, config, video, sidecar, other}.
// Phase 1 wants to be generous on what counts as a log/cfg/video and
// conservative on what's surfaced — we never throw away files, we just
// don't offer them in the picker.
function classifyEntry(name) {
  if (!name || typeof name !== 'string') return 'other';
  if (name.startsWith('.')) return 'other';
  const lower = name.toLowerCase();
  if (lower.endsWith(SIDECAR_EXT.toLowerCase())) return 'sidecar';
  if (lower.endsWith('.csv')) return 'log';
  // .cfg.bak / similar — skip. Only true .cfg / .xml count.
  if (lower.endsWith('.cfg') || lower.endsWith('.xml')) return 'config';
  if (lower.endsWith('.mp4') || lower.endsWith('.mov') ||
      lower.endsWith('.webm') || lower.endsWith('.m4v')) return 'video';
  return 'other';
}

// Enumerate the immediate children of a directory handle and return a
// classification bundle. No recursion. Tolerates entries whose
// `getFile()` rejects (cloud-only files): we surface the handle and
// name but leave `file: null`.
//
// Each row carries `relativePath` — the POSIX-style path from the
// directory handle's root. For immediate children this is the
// filename; for recursive discovery (discoverFlightContentsRecursive)
// it's `subfolder/.../file.csv`. Sidecar resume reads this back when
// resolving subject.<slot>.relativePath against a freshly-opened
// flight folder.
//
// Returned shape:
//   {
//     logs:     [{ name, relativePath, handle, file }],
//     configs:  [{ name, relativePath, handle, file }],
//     videos:   [{ name, relativePath, handle, file }],
//     sidecars: [{ name, relativePath, handle }],
//   }
export async function discoverFlightContents(dirHandle) {
  const out = { logs: [], configs: [], videos: [], sidecars: [] };
  if (!dirHandle || typeof dirHandle.entries !== 'function') return out;
  for await (const [name, entry] of dirHandle.entries()) {
    if (!entry || entry.kind !== 'file') continue;
    const kind = classifyEntry(name);
    if (kind === 'other') continue;
    if (kind === 'sidecar') {
      out.sidecars.push({ name, relativePath: name, handle: entry });
      continue;
    }
    // For logs/configs/videos, also try to get the File body so the
    // picker UI can sort by lastModified. Tolerate failure — a
    // cloud-only file still belongs in the list, the engineer just
    // can't open it until it's local.
    let file = null;
    try { file = await entry.getFile(); }
    catch { /* leave as null */ }
    const row = { name, relativePath: name, handle: entry, file };
    if (kind === 'log') out.logs.push(row);
    else if (kind === 'config') out.configs.push(row);
    else if (kind === 'video') out.videos.push(row);
  }
  return out;
}

// Recursive variant: walks immediate children AND immediate-child
// subdirectories up to `maxDepth` deep. Vac's real flight folders
// split into `11 May 26 Cockpit/`, `11 May 26 Raw Video/`, etc., with
// the sidecar at the day-folder root — the per-level walk surfaces
// log_007.csv and GOPR0314.MP4 even though they're one level down.
//
// Bounded for safety: maxDepth defaults to 3. A user-pointed folder
// is usually a flight-day with one nested level; the cap stops us
// from accidentally walking an entire Dropbox tree.
export async function discoverFlightContentsRecursive(dirHandle, maxDepth = 3) {
  const out = { logs: [], configs: [], videos: [], sidecars: [] };
  if (!dirHandle || typeof dirHandle.entries !== 'function') return out;
  async function walk(handle, prefix, depth) {
    for await (const [name, entry] of handle.entries()) {
      if (!entry) continue;
      const relPath = prefix ? `${prefix}/${name}` : name;
      if (entry.kind === 'directory') {
        if (depth >= maxDepth) continue;
        if (name.startsWith('.')) continue;
        try { await walk(entry, relPath, depth + 1); }
        catch { /* unreadable subdir: skip */ }
        continue;
      }
      if (entry.kind !== 'file') continue;
      const kind = classifyEntry(name);
      if (kind === 'other') continue;
      if (kind === 'sidecar') {
        out.sidecars.push({ name, relativePath: relPath, handle: entry });
        continue;
      }
      let file = null;
      try { file = await entry.getFile(); }
      catch { /* leave as null */ }
      const row = { name, relativePath: relPath, handle: entry, file };
      if (kind === 'log') out.logs.push(row);
      else if (kind === 'config') out.configs.push(row);
      else if (kind === 'video') out.videos.push(row);
    }
  }
  await walk(dirHandle, '', 0);
  return out;
}

// Walk a POSIX-style relative path inside a directory handle and
// return the leaf file handle. Returns null when any segment is
// missing — the caller decides whether to fall back to basename
// search. Tolerates leading "./" and collapses repeated slashes.
export async function resolveRelativePath(dirHandle, relativePath) {
  if (!dirHandle || typeof relativePath !== 'string' || !relativePath) {
    return null;
  }
  const parts = relativePath.split('/').filter(p => p && p !== '.');
  if (parts.length === 0) return null;
  let cur = dirHandle;
  for (let i = 0; i < parts.length - 1; i++) {
    try {
      cur = await cur.getDirectoryHandle(parts[i]);
    } catch {
      return null;
    }
  }
  try {
    return await cur.getFileHandle(parts[parts.length - 1]);
  } catch {
    return null;
  }
}

// Compute the POSIX-style relative path from a directory handle to a
// file handle. Uses the FSA `dirHandle.resolve(fileHandle)` API which
// returns an array of segments (or null if the file isn't a descendant).
// On older browsers without `resolve()`, returns the filename alone so
// callers still get a non-empty hint.
export async function computeRelativePath(dirHandle, fileHandle) {
  if (!dirHandle || !fileHandle) return '';
  if (typeof dirHandle.resolve === 'function') {
    try {
      const segments = await dirHandle.resolve(fileHandle);
      if (Array.isArray(segments) && segments.length > 0) {
        return segments.join('/');
      }
    } catch {
      // Fall through to filename-only.
    }
  }
  return typeof fileHandle.name === 'string' ? fileHandle.name : '';
}

// Convenience: find the sidecar handle for a given log filename inside
// a discovery bundle. Returns null when the log has no sidecar yet.
// Case-insensitive — APFS treats `LOG_007.csv` and `log_007.csv` as
// the same file, so the sidecar may have either casing.
export function findSidecarHandle(discovery, logFileName) {
  if (!discovery || !logFileName) return null;
  const expected = sidecarFileNameFor(logFileName).toLowerCase();
  for (const s of discovery.sidecars) {
    if (s.name.toLowerCase() === expected) return s.handle;
  }
  return null;
}

// Resolve a subject slot (log/video/config) to a row inside a
// discovery bundle. Tries `subjectEntry.relativePath` first
// (exact match), then falls back to filename. Returns the discovery
// row ({name, relativePath, handle, file}) or null when neither match.
//
// `slotKey` is 'logs' | 'videos' | 'configs' (the discovery bundle's
// list name).
export function findDiscoveryRowFor(discovery, slotKey, subjectEntry) {
  if (!discovery || !subjectEntry) return null;
  const rows = discovery[slotKey];
  if (!Array.isArray(rows) || rows.length === 0) return null;
  const wantPath = typeof subjectEntry.relativePath === 'string'
    ? subjectEntry.relativePath : '';
  const wantName = typeof subjectEntry.name === 'string'
    ? subjectEntry.name.toLowerCase() : '';
  if (wantPath) {
    const wantPathLc = wantPath.toLowerCase();
    for (const row of rows) {
      if ((row.relativePath || '').toLowerCase() === wantPathLc) return row;
    }
  }
  if (wantName) {
    for (const row of rows) {
      if ((row.name || '').toLowerCase() === wantName) return row;
    }
  }
  return null;
}

// ---------------------------------------------------------------------
// Sidecar read / write
// ---------------------------------------------------------------------

// Read a sidecar from a file handle. Returns one of:
//   { ok: true, value: SidecarDoc, file }
//   { ok: false, error: string }
// Caller may pass either a FileSystemFileHandle or an entry from
// discoverFlightContents. The `file` field is included for the case
// where the caller wants to capture lastModified/size for diagnostics.
export async function readSidecarFromHandle(handle) {
  if (!handle || typeof handle.getFile !== 'function') {
    return { ok: false, error: 'readSidecar: invalid handle' };
  }
  let file;
  try {
    file = await handle.getFile();
  } catch (err) {
    return { ok: false, error: `readSidecar: getFile failed: ${err.message}` };
  }
  let text;
  try {
    text = await file.text();
  } catch (err) {
    return { ok: false, error: `readSidecar: read failed: ${err.message}` };
  }
  const parsed = parseSidecar(text);
  if (!parsed.ok) return parsed;
  return { ok: true, value: parsed.value, file };
}

// Look for a sidecar matching `logFileName` inside the directory.
// Walks subdirectories (up to default depth) so a flight folder split
// into `Cockpit/`, `Raw Video/`, etc. still resolves.
//
// Returns one of:
//   null                                          — no sidecar present
//   { ok: true, value, file, handle, discovery }  — present and valid
//   { ok: false, error, handle, discovery }       — present but malformed
//
// `discovery` is the recursive enumeration the caller can hand back to
// resolve subject.<slot>.relativePath / basename fallbacks without
// re-walking the directory tree.
export async function readSidecarForLog(dirHandle, logFileName) {
  if (!dirHandle || !logFileName) return null;
  const discovery = await discoverFlightContentsRecursive(dirHandle);
  const handle = findSidecarHandle(discovery, logFileName);
  if (!handle) return null;
  const r = await readSidecarFromHandle(handle);
  if (!r.ok) return { ...r, handle, discovery };
  return { ...r, handle, discovery };
}

// Write a sidecar doc to a writable file handle. Atomic via
// createWritable()+close(); never keeps the stream open across awaits.
// Mutates the input doc: bumps `revision`, sets `updatedAt`. Returns
// the doc after the bumps so the caller's local state can pick up the
// new revision.
export async function writeSidecar(handle, doc) {
  if (!handle || typeof handle.createWritable !== 'function') {
    throw new Error('writeSidecar: handle has no createWritable');
  }
  if (!doc || !doc.session) {
    throw new Error('writeSidecar: doc missing session');
  }
  // Bump revision + timestamp. The local state object is mutated in
  // place so the caller can store it as-is.
  doc.session.revision = (Number(doc.session.revision) || 0) + 1;
  doc.session.updatedAt = nowISO();
  const text = JSON.stringify(doc, null, 2);
  const writable = await handle.createWritable();
  try {
    await writable.write(text);
  } finally {
    // Always close the writable, even on a write throw — leaving it
    // open holds the file lock on Windows.
    await writable.close();
  }
  return doc;
}

// ---------------------------------------------------------------------
// File-picker fallback for "I want to save a new sidecar"
//
// Used by the Save banner click: when no sidecar handle is attached
// yet, this calls showSaveFilePicker with a suggested filename
// matching the log. Returns the handle on success, null on cancel.
// ---------------------------------------------------------------------

export async function showSidecarSavePicker({ dirHandle, logFileName }) {
  if (typeof window === 'undefined' ||
      typeof window.showSaveFilePicker !== 'function') {
    return null;
  }
  const suggestedName = sidecarFileNameFor(logFileName) || 'session.replay.json';
  const opts = {
    suggestedName,
    types: [{
      description: 'OnSpeed replay sidecar',
      accept: { 'application/json': [SIDECAR_EXT] },
    }],
  };
  // The startIn hint is honored by Chrome 122+; older Chromes ignore
  // it gracefully. With a dirHandle, the dialog opens in the flight
  // folder so the engineer doesn't have to navigate.
  if (dirHandle) opts.startIn = dirHandle;
  try {
    return await window.showSaveFilePicker(opts);
  } catch (err) {
    if (err && err.name === 'AbortError') return null;
    // Chrome's one-picker-at-a-time guard fires InvalidStateError when a
    // reload arrives while a prior picker is still resolving. Treat as
    // a non-event so the user doesn't see a toast for a race.
    if (err && err.name === 'InvalidStateError') return null;
    throw err;
  }
}

// ---------------------------------------------------------------------
// mutateSidecar / flushSidecar — pure helpers (no React).
//
// `mutateSidecar(doc, patch)` returns a new doc with `patch` merged in,
// `revision` bumped, and `updatedAt` set. Callers hand the result to
// `writeSidecar` (which bumps revision a second time on its own,
// producing the on-disk row's revision). The helper is here so test
// code can exercise the bump logic without spinning up a Preact tree.
//
// `flushSidecar(handle, doc)` is a thin async wrapper that wraps
// writeSidecar with the standard "createWritable, write, close on
// finally" contract.
// ---------------------------------------------------------------------

export function mutateSidecar(doc, patch) {
  if (!doc || !doc.session) {
    throw new Error('mutateSidecar: doc missing session');
  }
  const next = { ...doc };
  if (patch && typeof patch === 'object') {
    if (patch.marks)   next.marks = patch.marks;
    if (patch.clips)   next.clips = patch.clips;
    if (patch.sync !== undefined) next.sync = patch.sync;
    if (patch.hud)     next.hud = { ...next.hud, ...patch.hud };
    if (patch.summary !== undefined) next.summary = patch.summary;
    if (patch.subject) {
      next.subject = { ...next.subject };
      for (const k of ['log', 'config', 'video']) {
        if (patch.subject[k]) {
          next.subject[k] = { ...(next.subject[k] || {}), ...patch.subject[k] };
        }
      }
    }
  }
  next.session = {
    ...next.session,
    revision: (Number(next.session.revision) || 0) + 1,
    updatedAt: nowISO(),
  };
  return next;
}

export async function flushSidecar(handle, doc) {
  return writeSidecar(handle, doc);
}

// ---------------------------------------------------------------------
// useSidecar — Preact hook tying together discovery, load, debounced
// save. Owned by ReplayPage; consumed via props by the toolbar pill.
//
// D4: auto-save by default. First call to markDirty() creates the
// sidecar at `<dirHandle>/<logname>.replay.json` and writes; subsequent
// calls debounce-write. No save banner, no save-consent dialog.
//
// Inputs:
//   logFileName  — the active log's filename (drives sidecar match)
//   dirHandle    — directory handle from pickFlightDir (or null)
//   buildAuthorSnapshot — () => snapshot to merge into the sidecar doc
//                          (current marks/clips/sync/hud/summary)
//   buildSubject — () => { log, config, video } for emptySession()
//
// State:
//   sidecarHandle    — current writable handle for the active log
//   sidecarDoc       — last-saved (or just-loaded) doc; null when none
//   saveState        — 'idle' | 'dirty' | 'saving' | 'saved' | 'error'
//   lastSavedAt      — Date.now() of last successful write
//   lastError        — last error string
//   autoSaveDisabled — per-session toggle (defaults false)
//
// Actions:
//   markDirty()        — call when an annotation changes; schedules a
//                        debounced save. Auto-creates the sidecar if it
//                        doesn't exist yet and the engineer hasn't
//                        opted out of auto-save.
//   flushNow()         — force-flush the pending debounce + write
//                        immediately. Bound to the SaveStatePill's click.
//   setAutoSaveDisabled(bool) — flip the per-session opt-out.
//   attachHandle(h)    — install a handle from an external file picker
//                        (e.g. the file-pick path). Writes once.
//   detachLog()        — call when swapping logs; clears state.
//
// PR 1 reconciliation rule: when a sidecar loads, its data is the
// canonical state. The page is expected to overwrite its IDB-derived
// state from the loaded sidecar. PR 1c layers per-record merge.
// ---------------------------------------------------------------------

const SIDECAR_SAVE_DEBOUNCE_MS = 500;

export function useSidecar({
  logFileName,
  dirHandle,
  buildAuthorSnapshot,
  buildSubject,
  onSidecarLoaded,
} = {}) {
  const [sidecarHandle, setSidecarHandle] = useState(null);
  const [sidecarDoc, setSidecarDoc]       = useState(null);
  const [saveState, setSaveState]         = useState('idle');
  const [lastSavedAt, setLastSavedAt]     = useState(0);
  const [lastError, setLastError]         = useState('');
  // Per-session opt-out for auto-save. Right-click the SaveStatePill to
  // toggle. Not persisted across reloads — a fresh page lifetime
  // re-enables auto-save unconditionally.
  const [autoSaveDisabled, setAutoSaveDisabled] = useState(false);
  // Flag flips true the first time a sidecar successfully loads
  // during the current page lifetime. Available to callers that need
  // to know whether the page came back to life from a sidecar (e.g.
  // suppressing first-edit prompts that would now read as redundant).
  const [restoredInThisSession, setRestoredInThisSession] = useState(false);

  // Refs so the debounced timer can read the latest snapshot at fire
  // time without recreating the timer on every state change.
  const handleRef = useRef(null);
  const docRef    = useRef(null);
  const snapshotRef = useRef(buildAuthorSnapshot);
  const subjectRef  = useRef(buildSubject);
  const dirHandleRef = useRef(dirHandle);
  const logFileNameRef = useRef(logFileName);
  const autoSaveDisabledRef = useRef(false);
  const timerRef    = useRef(null);
  const dirtyRef    = useRef(false);
  const loadingRef  = useRef(false);
  // Guards re-entry into the auto-create path: a markDirty that fires
  // while the creation is in flight should NOT kick a second create.
  const creatingRef = useRef(false);

  // Keep the callback refs in sync without forcing re-renders on the
  // page's frame loop.
  useEffect(() => { snapshotRef.current = buildAuthorSnapshot; }, [buildAuthorSnapshot]);
  useEffect(() => { subjectRef.current  = buildSubject;        }, [buildSubject]);
  useEffect(() => { handleRef.current   = sidecarHandle;       }, [sidecarHandle]);
  useEffect(() => { docRef.current      = sidecarDoc;          }, [sidecarDoc]);
  useEffect(() => { dirHandleRef.current = dirHandle;          }, [dirHandle]);
  useEffect(() => { logFileNameRef.current = logFileName;      }, [logFileName]);
  useEffect(() => { autoSaveDisabledRef.current = autoSaveDisabled; },
            [autoSaveDisabled]);

  // Discover-and-load whenever the log filename or dirHandle changes.
  useEffect(() => {
    if (!logFileName || !dirHandle) {
      setSidecarHandle(null);
      setSidecarDoc(null);
      setSaveState('idle');
      // Leave restoredInThisSession alone — once a sidecar has loaded
      // during this page lifetime we want the suppression to hold
      // until a hard reload.
      return;
    }
    let cancelled = false;
    loadingRef.current = true;
    readSidecarForLog(dirHandle, logFileName).then(r => {
      if (cancelled) return;
      loadingRef.current = false;
      if (!r) {
        // No existing sidecar; the page starts blank-of-engineer
        // annotations. First edit triggers auto-create.
        setSidecarHandle(null);
        setSidecarDoc(null);
        setSaveState('idle');
        return;
      }
      if (!r.ok) {
        console.warn('sidecar: failed to parse sidecar', r.error);
        setSidecarHandle(r.handle || null);
        setSidecarDoc(null);
        setSaveState('error');
        setLastError(r.error);
        return;
      }
      setSidecarHandle(r.handle);
      setSidecarDoc(r.value);
      setSaveState('saved');
      setLastSavedAt(Date.now());
      dirtyRef.current = false;
      setRestoredInThisSession(true);
      if (typeof onSidecarLoaded === 'function') {
        try { onSidecarLoaded(r.value, r.discovery || null); }
        catch (e) { console.warn('sidecar: onSidecarLoaded threw', e); }
      }
    }).catch(err => {
      if (!cancelled) {
        loadingRef.current = false;
        console.warn('sidecar: discovery failed', err);
      }
    });
    return () => { cancelled = true; };
  // onSidecarLoaded is stored in a ref via the loaded-callback
  // pattern below, so deps stay narrow.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [logFileName, dirHandle]);

  // Internal save. Reads the latest snapshot from refs, merges into
  // the doc, calls writeSidecar.
  const flush = useCallback(async () => {
    const handle = handleRef.current;
    if (!handle) return;
    const snap = typeof snapshotRef.current === 'function'
      ? snapshotRef.current() : null;
    if (!snap) return;
    // Build (or update) the doc. If a doc is already in flight, merge
    // the snapshot fields onto it; otherwise seed a new one.
    let doc = docRef.current;
    if (!doc) {
      const subj = typeof subjectRef.current === 'function'
        ? subjectRef.current() : {};
      doc = emptySession({
        log: subj.log, config: subj.config, video: subj.video,
        createdBy: snap.createdBy || 'OnSpeed Replay',
        author: snap.author || '',
      });
    } else {
      // Re-seed subject from the latest computed values so durationSec /
      // rowCount / hash, which may have arrived after the doc was first
      // built, end up on disk.
      const subj = typeof subjectRef.current === 'function'
        ? subjectRef.current() : {};
      if (subj.log)    doc.subject.log    = { ...(doc.subject.log || {}),    ...subj.log };
      if (subj.config) doc.subject.config = { ...(doc.subject.config || {}), ...subj.config };
      if (subj.video)  doc.subject.video  = { ...(doc.subject.video || {}),  ...subj.video };
    }
    // Merge the snapshot — caller controls the shape exactly.
    if (snap.marks)   doc.marks = snap.marks;
    if (snap.clips)   doc.clips = snap.clips;
    if (snap.sync !== undefined) doc.sync = snap.sync;
    if (snap.hud)     doc.hud = { ...doc.hud, ...snap.hud };
    if (snap.summary !== undefined) doc.summary = snap.summary;
    if (snap.author !== undefined && typeof snap.author === 'string') {
      doc.session.author = snap.author;
    }
    if (snap.title !== undefined && typeof snap.title === 'string') {
      doc.session.title = snap.title;
    }
    setSaveState('saving');
    try {
      const updated = await writeSidecar(handle, doc);
      setSidecarDoc(updated);
      docRef.current = updated;
      setSaveState('saved');
      setLastSavedAt(Date.now());
      setLastError('');
      dirtyRef.current = false;
    } catch (err) {
      console.warn('sidecar: writeSidecar failed', err);
      setSaveState('error');
      setLastError(err.message || String(err));
    }
  }, []);

  // Create the sidecar file on disk and attach the resulting handle.
  // Called by markDirty when no handle exists yet AND auto-save is
  // enabled AND we have a dirHandle. Idempotent via `creatingRef`.
  const autoCreate = useCallback(async () => {
    if (creatingRef.current) return;
    if (handleRef.current) return;
    const dir = dirHandleRef.current;
    const logName = logFileNameRef.current;
    if (!dir || !logName) return;
    creatingRef.current = true;
    try {
      const name = sidecarFileNameFor(logName);
      const handle = await dir.getFileHandle(name, { create: true });
      handleRef.current = handle;
      setSidecarHandle(handle);
      // Seed a fresh doc. emptySession returns revision: 1; writeSidecar
      // will bump to 2 on the first write. Knock it down so the on-disk
      // file lands at revision 1.
      const subj = typeof subjectRef.current === 'function'
        ? subjectRef.current() : {};
      const seeded = emptySession({
        log: subj.log, config: subj.config, video: subj.video,
        createdBy: 'OnSpeed Replay',
        author: '',
      });
      seeded.session.revision = 0;
      docRef.current = seeded;
      setSidecarDoc(seeded);
      await flush();
    } catch (err) {
      console.warn('sidecar: autoCreate failed', err);
      setSaveState('error');
      setLastError(err.message || String(err));
    } finally {
      creatingRef.current = false;
    }
  }, [flush]);

  const scheduleSave = useCallback(() => {
    if (timerRef.current) clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => {
      timerRef.current = null;
      if (autoSaveDisabledRef.current) return;
      if (!handleRef.current) {
        // Sidecar doesn't exist yet — create it now.
        autoCreate();
      } else {
        flush();
      }
    }, SIDECAR_SAVE_DEBOUNCE_MS);
  }, [flush, autoCreate]);

  const markDirty = useCallback(() => {
    dirtyRef.current = true;
    setSaveState('dirty');
    if (autoSaveDisabledRef.current) return;
    scheduleSave();
  }, [scheduleSave]);

  // Force-flush the pending debounce and write immediately. Bound to
  // the SaveStatePill's click — ignores the auto-save-disabled flag
  // (manual save IS the engineer expressing intent to persist).
  const flushNow = useCallback(async () => {
    if (timerRef.current) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    if (!handleRef.current && dirHandleRef.current && logFileNameRef.current) {
      await autoCreate();
    } else if (handleRef.current) {
      await flush();
    }
  }, [flush, autoCreate]);

  // Attach a fresh handle from an external picker. Used by the
  // file-pick path that selects a .replay.json directly. Writes once
  // to seal in the current snapshot.
  const attachHandle = useCallback(async (handle) => {
    if (!handle) return null;
    handleRef.current = handle;
    setSidecarHandle(handle);
    if (timerRef.current) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    if (!docRef.current) {
      const subj = typeof subjectRef.current === 'function'
        ? subjectRef.current() : {};
      const seeded = emptySession({
        log: subj.log, config: subj.config, video: subj.video,
        createdBy: 'OnSpeed Replay',
        author: '',
      });
      seeded.session.revision = 0;
      docRef.current = seeded;
      setSidecarDoc(seeded);
    }
    await flush();
    return docRef.current;
  }, [flush]);

  const detachLog = useCallback(() => {
    if (timerRef.current) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    handleRef.current = null;
    docRef.current = null;
    dirtyRef.current = false;
    creatingRef.current = false;
    setSidecarHandle(null);
    setSidecarDoc(null);
    setSaveState('idle');
    setLastSavedAt(0);
    setLastError('');
    setRestoredInThisSession(false);
  }, []);

  const toggleAutoSave = useCallback(() => {
    setAutoSaveDisabled(d => !d);
  }, []);

  return {
    sidecarHandle,
    sidecarDoc,
    saveState,
    lastSavedAt,
    lastError,
    restoredInThisSession,
    autoSaveDisabled,
    markDirty,
    flushNow,
    attachHandle,
    detachLog,
    toggleAutoSave,
    setAutoSaveDisabled,
  };
}
