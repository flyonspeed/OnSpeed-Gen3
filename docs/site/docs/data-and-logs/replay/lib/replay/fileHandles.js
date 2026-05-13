// fileHandles.js — File System Access API integration for the replay
// tool. Persists FileSystemFileHandle objects in IndexedDB so a page
// reload can offer "Resume last session?" with a single permission
// re-grant covering all three files.
//
// Background: the replay page takes three file inputs (video, log,
// config). Sync state and clip lists already persist across reloads
// via persistence.js (content-keyed by log SHA-256). But the file
// inputs themselves cannot be auto-restored — browsers do not give
// pages programmatic access to previously selected <input type="file">
// results, for security reasons.
//
// The File System Access API gives the page a FileSystemFileHandle
// that can be stored in IndexedDB and re-grant'd in a single user-
// gesture click. Chrome and Edge desktop ship this API; Firefox and
// Safari do not. On unsupported browsers, callers should fall through
// to the existing <input type="file"> flow.
//
// Permissions: handle.requestPermission({mode:'read'}) MUST be called
// inside a user-gesture handler (button click). Browsers batch the
// permission prompts when multiple handles are requested in the same
// gesture, so one click can grant read access to all three files.
//
// Storage schema (IndexedDB):
//   database  'replay-fsa'
//   store     'handles-v1'
//   key       'current'
//   value     { signature: string, handles: { video, log, cfg } }
//
// The signature is the JSON-stringified recent-files metadata
// ({video, log, cfg} from persistence.js's RECENT_FILES_KEY). On
// reload, we compare the stored signature against the current
// recent-files signature; if it matches, the handles are still
// relevant. The store name carries the schema version (`handles-v1`)
// so a future bump can ignore old data cleanly.

import { html, useState, useEffect, useCallback }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';

const DB_NAME = 'replay-fsa';
const STORE_NAME = 'handles-v1';
const DB_VERSION = 1;
const RECORD_KEY = 'current';

// ---------------------------------------------------------------------
// Pure helpers (testable in Node)
// ---------------------------------------------------------------------

// Returns true iff the browser supports File System Access showOpenFilePicker.
// Chrome/Edge desktop ship it. Firefox/Safari do not.
export function isFileHandleApiSupported() {
  return typeof globalThis !== 'undefined' &&
         typeof globalThis.window !== 'undefined' &&
         typeof globalThis.window.showOpenFilePicker === 'function';
}

// Accept-list per slot. Mirrors the existing <input> accept attrs:
//   video: video/*,.mp4,.mov,.webm
//   log:   .csv,text/csv
//   cfg:   .cfg,.xml,text/xml
//
// showOpenFilePicker's types[] takes a {description, accept: {mime: [exts]}}
// shape; the OS dialog filters on this.
export function pickerOptionsForSlot(slot) {
  if (slot === 'video') {
    return {
      multiple: false,
      types: [{
        description: 'Flight video',
        accept: { 'video/*': ['.mp4', '.mov', '.webm'] },
      }],
    };
  }
  if (slot === 'log') {
    return {
      multiple: false,
      types: [{
        description: 'OnSpeed SD log',
        accept: { 'text/csv': ['.csv'] },
      }],
    };
  }
  if (slot === 'cfg') {
    return {
      multiple: false,
      types: [{
        description: 'OnSpeed config XML',
        accept: { 'text/xml': ['.cfg', '.xml'] },
      }],
    };
  }
  throw new Error(`fileHandles: unknown slot ${slot}`);
}

// Compute the signature for a recent-files info object. Same shape as
// persistence.js's RECENT_FILES_KEY value, so the signature matches
// what that module writes.
export function recentFilesSignature(info) {
  if (!info || !info.video || !info.log) return '';
  return JSON.stringify({
    video: info.video,
    log: info.log,
    cfg: info.cfg || null,
  });
}

// Pull {name, size, lastModified} from a File-like object. Mirrors
// persistence.js's fileMetadata so the same metadata shape feeds both
// the recent-files banner and the signature used to key IDB.
function metaOf(file) {
  if (!file || typeof file !== 'object') return null;
  const { name, size, lastModified } = file;
  if (typeof name !== 'string') return null;
  return { name, size: Number(size), lastModified: Number(lastModified) };
}

// Build a signature from raw File objects (whatever live state
// ReplayPage holds for the three slots). Returns '' if video+log are
// not both present.
export function signatureFromFiles({ video, log, cfg }) {
  const v = metaOf(video);
  const l = metaOf(log);
  const c = metaOf(cfg);
  if (!v || !l) return '';
  return JSON.stringify({ video: v, log: l, cfg: c });
}

// ---------------------------------------------------------------------
// IndexedDB plumbing
// ---------------------------------------------------------------------

function openDb() {
  return new Promise((resolve, reject) => {
    if (typeof indexedDB === 'undefined') {
      reject(new Error('IndexedDB not available'));
      return;
    }
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE_NAME)) {
        db.createObjectStore(STORE_NAME);
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('IDB open failed'));
  });
}

function withStore(mode, fn) {
  return openDb().then(db => new Promise((resolve, reject) => {
    let result;
    const tx = db.transaction(STORE_NAME, mode);
    const store = tx.objectStore(STORE_NAME);
    Promise.resolve(fn(store)).then(r => { result = r; }, reject);
    tx.oncomplete = () => { db.close(); resolve(result); };
    tx.onabort = () => { db.close(); reject(tx.error || new Error('IDB tx aborted')); };
    tx.onerror = () => { db.close(); reject(tx.error || new Error('IDB tx error')); };
  }));
}

function reqToPromise(req) {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('IDB request failed'));
  });
}

// ---------------------------------------------------------------------
// Public IDB API
// ---------------------------------------------------------------------

// Store the full bundle keyed by signature. Overwrites any prior record.
// signature: string (recent-files signature, may be empty if not all
// three files have metadata yet — caller decides whether to call).
// handles: { video?, log?, cfg? } — slots present in this bundle.
export async function storeHandles(signature, handles) {
  if (typeof signature !== 'string') return;
  await withStore('readwrite', store => reqToPromise(
    store.put({ signature, handles }, RECORD_KEY)
  ));
}

// Read the current record. Returns null if missing or mismatched.
// If expectedSignature is provided and non-empty, returns null when the
// stored signature differs (the bundle belongs to a different session).
export async function loadHandles(expectedSignature) {
  let record;
  try {
    record = await withStore('readonly', store => reqToPromise(store.get(RECORD_KEY)));
  } catch {
    return null;
  }
  if (!record || !record.handles) return null;
  if (typeof expectedSignature === 'string' && expectedSignature.length > 0 &&
      record.signature !== expectedSignature) {
    return null;
  }
  return record.handles;
}

export async function clearHandles() {
  try {
    await withStore('readwrite', store => reqToPromise(store.delete(RECORD_KEY)));
  } catch {
    // best-effort; missing is fine
  }
}

// ---------------------------------------------------------------------
// Permission helper. Must be called from a user-gesture handler.
// ---------------------------------------------------------------------

// Extract the FileSystemHandle that needs read permission from a slot
// value. For the `video` slot the value is either a FileSystemFileHandle
// (single-chapter session) or a `{kind:'multi-chapter', directoryHandle,
// chapterNames}` envelope — in the latter case the directory handle is
// the permission target (granting it implicitly grants every file inside).
//
// Discriminator note: the FSA spec defines `.kind` on FileSystemHandle
// as `'file' | 'directory'`. Our envelope adds the synthetic value
// `'multi-chapter'` to the same field — the three values are disjoint
// today, but if the spec ever adds a new handle kind that string-equals
// our envelope marker, this branch would misclassify. Cheap to defend:
// rename the envelope discriminator (e.g. `envelopeKind`) only if/when
// that becomes a concrete risk.
function permissionTarget(slotValue) {
  if (!slotValue) return null;
  if (slotValue.kind === 'multi-chapter') return slotValue.directoryHandle || null;
  return slotValue;
}

export async function requestPermissionForHandles(handles) {
  if (!handles) return false;
  const slots = ['video', 'log', 'cfg'];
  // The synchronous `.map()` dispatches all three `requestPermission()`
  // calls in the same JS turn, BEFORE any `await` runs. Chrome's
  // user-activation token survives synchronous dispatch and the
  // resulting microtask chain, so all three prompts read as
  // "user-triggered" and the browser batches them into one combined
  // dialog. `Promise.all` here only collects the results — it does
  // NOT guarantee the batching; that comes from the sync dispatch in
  // the `.map()` body. If a future Chrome change tightens this
  // (e.g. user-activation only survives one async hop), the prompts
  // would degrade to "first ok, rest denied" — observable in dev
  // before it bites a pilot.
  const results = await Promise.all(slots.map(slot => {
    const target = permissionTarget(handles[slot]);
    if (!target) return 'granted'; // missing slot (e.g. cfg) is fine
    if (typeof target.requestPermission !== 'function') return 'denied';
    return target.requestPermission({ mode: 'read' }).catch(() => 'denied');
  }));
  return results.every(r => r === 'granted');
}

// Non-prompting permission probe. `queryPermission` reports the current
// state without surfacing a dialog, so it is safe to call outside a
// user-gesture context (page load, mount-time effect). Returns true iff
// every present slot's queryPermission resolves to 'granted' — the
// signal the auto-resume path needs to skip the banner click. A 'prompt'
// or 'denied' result on any slot falls back to the banner-required path.
export async function queryPermissionForHandles(handles) {
  if (!handles) return false;
  const slots = ['video', 'log', 'cfg'];
  const results = await Promise.all(slots.map(slot => {
    const target = permissionTarget(handles[slot]);
    if (!target) return 'granted';
    if (typeof target.queryPermission !== 'function') return 'prompt';
    return target.queryPermission({ mode: 'read' }).catch(() => 'prompt');
  }));
  return results.every(r => r === 'granted');
}

// Same as queryPermissionForHandles but returns a per-slot breakdown.
// Used by the ?debug=1 diagnostic path so the pilot can see which slot
// is blocking auto-resume (e.g. cfg granted but video stuck on 'prompt'
// because the directory handle came from a different gesture).
export async function queryPermissionPerSlot(handles) {
  if (!handles) return [];
  const slots = ['video', 'log', 'cfg'];
  return Promise.all(slots.map(async slot => {
    const target = permissionTarget(handles[slot]);
    if (!target) return { slot, present: false, result: 'granted' };
    if (typeof target.queryPermission !== 'function') {
      return { slot, present: true, result: 'unsupported' };
    }
    let result = 'prompt';
    try { result = await target.queryPermission({ mode: 'read' }); }
    catch (e) { result = 'error:' + (e && e.name); }
    return { slot, present: true, result };
  }));
}

// Walk a stored multi-chapter envelope: re-open the directory handle's
// entries, match against the persisted chapter-name list, return the
// ordered file + handle arrays. Permission must already be granted on
// `directoryHandle` (caller is the post-Resume click path).
//
// If a chapter's file is missing (renamed, moved, deleted) it's
// dropped from the result; the caller can detect a partial recovery
// by comparing `result.files.length` to `envelope.chapterNames.length`.
export async function expandMultiChapterHandle(envelope) {
  if (!envelope || envelope.kind !== 'multi-chapter') return null;
  const { directoryHandle, chapterNames } = envelope;
  if (!directoryHandle || !Array.isArray(chapterNames)) return null;
  const found = new Map(); // name → { file, handle }
  for await (const [name, entry] of directoryHandle.entries()) {
    if (entry.kind !== 'file') continue;
    if (!chapterNames.includes(name)) continue;
    try {
      const file = await entry.getFile();
      found.set(name, { file, handle: entry });
    } catch (_) { /* unreadable: skip */ }
  }
  const ordered = chapterNames
    .map(n => found.get(n))
    .filter(Boolean);
  return {
    files:    ordered.map(x => x.file),
    handles:  ordered.map(x => x.handle),
    directoryHandle,
  };
}

// ---------------------------------------------------------------------
// showOpenFilePicker wrapper. Returns {file, handle} or null on cancel.
// ---------------------------------------------------------------------

export async function pickFile(slot) {
  if (!isFileHandleApiSupported()) return null;
  let handle;
  try {
    const [h] = await window.showOpenFilePicker(pickerOptionsForSlot(slot));
    handle = h;
  } catch (err) {
    // User cancelled the picker; AbortError is the standard signal.
    if (err && err.name === 'AbortError') return null;
    throw err;
  }
  if (!handle) return null;
  const file = await handle.getFile();
  return { file, handle };
}

// ---------------------------------------------------------------------
// Preact hook: useFileHandleResume
//
// Reads IDB on mount + whenever recentFilesSig changes. Exposes:
//   supported       — boolean, FSA available
//   availableHandles — { video, log, cfg } | null (matched signature)
//   resumeReady     — true iff video + log handles both present
//                     (cfg is optional, mirrors persistence.js)
//   autoResumeReady — true iff resumeReady AND every present handle's
//                     queryPermission returned 'granted'. Lets the page
//                     skip the banner click and load files at mount.
//   dismissed       — pilot dismissed the resume offer this session
//   dismiss()       — hide the resume offer + clear IDB record
//   markUsed()      — call after a successful resume so future
//                     renders don't keep offering the same resume
//
// The hook re-queries IDB whenever the recentFilesSig changes (so a
// fresh session triggers a fresh lookup).
// ---------------------------------------------------------------------

export function useFileHandleResume({ recentFilesSig }) {
  const [supported] = useState(() => isFileHandleApiSupported());
  const [availableHandles, setAvailableHandles] = useState(null);
  // Set only when queryPermission reports 'granted' on every present
  // slot. Reset to false alongside availableHandles on signature change.
  const [permGranted, setPermGranted] = useState(false);
  const [dismissed, setDismissed] = useState(false);
  const [used, setUsed] = useState(false);

  useEffect(() => {
    const debug = typeof window !== 'undefined' &&
                  new URLSearchParams(window.location.search).get('debug') === '1';
    if (!supported) {
      if (debug) console.log('fileHandles autoResume diag', {
        stage: 'unsupported',
        note: 'File System Access API not available in this browser',
      });
      return;
    }
    if (!recentFilesSig) {
      if (debug) console.log('fileHandles autoResume diag', {
        stage: 'no-sig',
        note: 'no recent-files signature yet — open a log to establish one',
      });
      setAvailableHandles(null);
      setPermGranted(false);
      return;
    }
    let cancelled = false;
    loadHandles(recentFilesSig).then(async h => {
      if (cancelled) return;
      setAvailableHandles(h);
      if (!h) {
        if (debug) console.log('fileHandles autoResume diag', {
          stage: 'no-handles',
          sig: recentFilesSig,
          note: 'no stored handles for this log signature — open files manually first',
        });
        setPermGranted(false);
        return;
      }
      // queryPermission does not require a user gesture, so it is safe
      // to call here from the mount-time effect.
      let ok = false;
      try {
        if (debug) {
          const perSlot = await queryPermissionPerSlot(h);
          ok = perSlot.every(s => s.result === 'granted');
          // eslint-disable-next-line no-console
          console.log('fileHandles autoResume diag', {
            stage: 'queried', sig: recentFilesSig, perSlot, ok,
          });
        } else {
          ok = await queryPermissionForHandles(h);
        }
      } catch (e) {
        if (debug) console.log('fileHandles autoResume diag', {
          stage: 'query-error', err: String(e),
        });
        ok = false;
      }
      if (!cancelled) setPermGranted(ok);
    }).catch((e) => {
      if (debug) console.log('fileHandles autoResume diag', {
        stage: 'load-error', err: String(e),
      });
      if (!cancelled) { setAvailableHandles(null); setPermGranted(false); }
    });
    return () => { cancelled = true; };
  }, [supported, recentFilesSig]);

  // Dismiss hides the resume UI for the rest of this session. We
  // intentionally do NOT clearHandles() here — file handles are the
  // auto-resume backbone, and the recent-files banner that triggers
  // this dismiss is a separate concern (visual decluttering). Wiping
  // handles on dismiss strands users who dismiss the banner once and
  // then never see their files come back automatically.
  const dismiss = useCallback(() => {
    setDismissed(true);
  }, []);

  const markUsed = useCallback(() => {
    setUsed(true);
  }, []);

  const resumeReady = !!(availableHandles &&
                         availableHandles.video &&
                         availableHandles.log) &&
                      !dismissed &&
                      !used;

  const autoResumeReady = resumeReady && permGranted;

  return {
    supported,
    availableHandles,
    resumeReady,
    autoResumeReady,
    dismiss,
    markUsed,
  };
}

// ---------------------------------------------------------------------
// ReplayResumeBanner — renders the "Resume last session?" UI when the
// hook signals resumeReady. The Resume button click is the user-gesture
// site that triggers requestPermissionForHandles.
//
// Props:
//   info       — banner metadata (filenames + sizes, from persistence)
//   onResume() — handler invoked synchronously inside the Resume
//                click. Caller must call requestPermissionForHandles
//                inside the same handler (no awaits before it) to
//                preserve the user-gesture token.
//   onDismiss() — handler for the dismiss (×) button.
// ---------------------------------------------------------------------

export function ReplayResumeBanner({ info, onResume, onDismiss }) {
  if (!info) return null;
  const parts = [];
  if (info.video && info.video.name) parts.push(info.video.name);
  if (info.log   && info.log.name)   parts.push(info.log.name);
  if (info.cfg   && info.cfg.name)   parts.push(info.cfg.name);
  const label = parts.join(' + ') || 'previous session files';
  return html`
    <div class="replay-recent-banner">
      <span class="replay-recent-text">
        Resume last session: ${label}?
      </span>
      <button class="replay-recent-resume"
              type="button"
              onClick=${onResume}>Resume</button>
      <button class="replay-recent-dismiss"
              type="button"
              onClick=${onDismiss}
              title="Dismiss">×</button>
    </div>
  `;
}
