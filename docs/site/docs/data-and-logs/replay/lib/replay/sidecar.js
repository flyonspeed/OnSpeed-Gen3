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

import { html, useState, useEffect, useCallback, useRef }
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
// Returned shape:
//   {
//     logs:     [{ name, handle, file }],
//     configs:  [{ name, handle, file }],
//     videos:   [{ name, handle, file }],
//     sidecars: [{ name, handle }],   // no file body until we actually read it
//   }
export async function discoverFlightContents(dirHandle) {
  const out = { logs: [], configs: [], videos: [], sidecars: [] };
  if (!dirHandle || typeof dirHandle.entries !== 'function') return out;
  for await (const [name, entry] of dirHandle.entries()) {
    if (!entry || entry.kind !== 'file') continue;
    const kind = classifyEntry(name);
    if (kind === 'other') continue;
    if (kind === 'sidecar') {
      out.sidecars.push({ name, handle: entry });
      continue;
    }
    // For logs/configs/videos, also try to get the File body so the
    // picker UI can sort by lastModified. Tolerate failure — a
    // cloud-only file still belongs in the list, the engineer just
    // can't open it until it's local.
    let file = null;
    try { file = await entry.getFile(); }
    catch { /* leave as null */ }
    const row = { name, handle: entry, file };
    if (kind === 'log') out.logs.push(row);
    else if (kind === 'config') out.configs.push(row);
    else if (kind === 'video') out.videos.push(row);
  }
  return out;
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
// Returns one of:
//   null                                — no sidecar present
//   { ok: true, value, file, handle }   — present and valid
//   { ok: false, error, handle }        — present but malformed
export async function readSidecarForLog(dirHandle, logFileName) {
  if (!dirHandle || !logFileName) return null;
  const discovery = await discoverFlightContents(dirHandle);
  const handle = findSidecarHandle(discovery, logFileName);
  if (!handle) return null;
  const r = await readSidecarFromHandle(handle);
  if (!r.ok) return { ...r, handle };
  return { ...r, handle };
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
    throw err;
  }
}

// ---------------------------------------------------------------------
// useSidecar — Preact hook tying together discovery, load, debounced
// save. Owned by ReplayPage; consumed via props by the banner / pill.
//
// Inputs:
//   logFileName  — the active log's filename (drives sidecar match)
//   dirHandle    — directory handle from pickFlightDir (or null)
//   logHash      — content digest of the active log (for diagnostics)
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
//
// Actions:
//   markDirty()      — call when an annotation changes; schedules a
//                      debounced save (~500ms) IF a handle is attached
//   attachHandle(h, doc) — install a fresh handle + doc from the Save-
//                          banner click; immediately writes once
//   detachLog()      — call when swapping logs; clears state
//
// PR 1 reconciliation rule: when a sidecar loads, its data is the
// canonical state. The page is expected to overwrite its IDB-derived
// state from the loaded sidecar. PR 2 layers conflict UI on top.
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

  // Refs so the debounced timer can read the latest snapshot at fire
  // time without recreating the timer on every state change.
  const handleRef = useRef(null);
  const docRef    = useRef(null);
  const snapshotRef = useRef(buildAuthorSnapshot);
  const subjectRef  = useRef(buildSubject);
  const timerRef    = useRef(null);
  const dirtyRef    = useRef(false);
  const loadingRef  = useRef(false);
  // True for the brief window between "sidecar loaded" and the next
  // edit. The save banner suppresses itself during this window so a
  // freshly-loaded sidecar doesn't immediately show "save your
  // notes" — the notes are already saved, just not yet edited.
  const justLoadedRef = useRef(false);

  // Keep the callback refs in sync without forcing re-renders on the
  // page's frame loop.
  useEffect(() => { snapshotRef.current = buildAuthorSnapshot; }, [buildAuthorSnapshot]);
  useEffect(() => { subjectRef.current  = buildSubject;        }, [buildSubject]);
  useEffect(() => { handleRef.current   = sidecarHandle;       }, [sidecarHandle]);
  useEffect(() => { docRef.current      = sidecarDoc;          }, [sidecarDoc]);

  // Discover-and-load whenever the log filename or dirHandle changes.
  useEffect(() => {
    if (!logFileName || !dirHandle) {
      setSidecarHandle(null);
      setSidecarDoc(null);
      setSaveState('idle');
      return;
    }
    let cancelled = false;
    loadingRef.current = true;
    readSidecarForLog(dirHandle, logFileName).then(r => {
      if (cancelled) return;
      loadingRef.current = false;
      if (!r) {
        // No existing sidecar; the page starts blank-of-engineer
        // annotations. The Save banner appears on first edit.
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
      justLoadedRef.current = true;
      dirtyRef.current = false;
      if (typeof onSidecarLoaded === 'function') {
        try { onSidecarLoaded(r.value); }
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

  const scheduleSave = useCallback(() => {
    if (!handleRef.current) return;
    if (timerRef.current) clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => {
      timerRef.current = null;
      flush();
    }, SIDECAR_SAVE_DEBOUNCE_MS);
  }, [flush]);

  const markDirty = useCallback(() => {
    // First edit after a fresh load: drop the just-loaded suppression.
    justLoadedRef.current = false;
    dirtyRef.current = true;
    if (handleRef.current) {
      setSaveState('dirty');
      scheduleSave();
    } else {
      // No handle yet — the save banner will offer to create one. The
      // 'dirty' state surfaces the banner via the page.
      setSaveState('dirty');
    }
  }, [scheduleSave]);

  // Attach a fresh handle + write the current snapshot to it. Used by
  // the Save banner click after `showSaveFilePicker`. Returns the
  // saved doc.
  const attachHandle = useCallback(async (handle) => {
    if (!handle) return null;
    handleRef.current = handle;
    setSidecarHandle(handle);
    // Cancel any pending debounce; we'll do the write inline.
    if (timerRef.current) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    // Seed the doc if we don't have one yet.
    if (!docRef.current) {
      const subj = typeof subjectRef.current === 'function'
        ? subjectRef.current() : {};
      const seeded = emptySession({
        log: subj.log, config: subj.config, video: subj.video,
        createdBy: 'OnSpeed Replay',
        author: '',
      });
      // emptySession returns revision: 1; writeSidecar will bump to 2.
      // Knock it down to 0 first so the freshly-written file lands at 1.
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
    setSidecarHandle(null);
    setSidecarDoc(null);
    setSaveState('idle');
  }, []);

  // Banner visibility: dirty AND no handle AND not currently loading.
  const showSaveBanner = !sidecarHandle && saveState === 'dirty' && !loadingRef.current;

  return {
    sidecarHandle,
    sidecarDoc,
    saveState,
    lastSavedAt,
    lastError,
    markDirty,
    attachHandle,
    detachLog,
    showSaveBanner,
  };
}

// ---------------------------------------------------------------------
// Save banner UI — a sticky strip that appears when the engineer has
// made edits but no sidecar handle is attached yet.
// ---------------------------------------------------------------------

export function SidecarSaveBanner({ logFileName, onSave, onDismiss }) {
  if (!logFileName) return null;
  const sidecarName = sidecarFileNameFor(logFileName);
  return html`
    <div class="replay-sidecar-banner">
      <span class="replay-sidecar-banner-text">
        Save your notes to <code>${sidecarName}</code> next to your log?
      </span>
      <button class="replay-sidecar-banner-save"
              type="button"
              onClick=${onSave}>Save</button>
      ${onDismiss ? html`
        <button class="replay-sidecar-banner-dismiss"
                type="button"
                onClick=${onDismiss}
                title="Dismiss">×</button>
      ` : null}
    </div>
  `;
}

// Toolbar status pill showing save state when a handle is attached.
export function SidecarSaveIndicator({ saveState, lastSavedAt }) {
  if (!saveState || saveState === 'idle') return null;
  let label = '';
  let cls = 'replay-sidecar-status';
  if (saveState === 'saving') {
    label = 'Saving...';
    cls += ' is-saving';
  } else if (saveState === 'saved') {
    const ago = lastSavedAt ? Math.max(0, Math.round((Date.now() - lastSavedAt) / 1000)) : 0;
    label = ago < 2 ? 'Saved' : `Saved ${ago}s ago`;
    cls += ' is-saved';
  } else if (saveState === 'dirty') {
    label = 'Unsaved changes';
    cls += ' is-dirty';
  } else if (saveState === 'error') {
    label = 'Save failed';
    cls += ' is-error';
  }
  return html`<span class=${cls}>${label}</span>`;
}
