// fileHandles.js — File System Access pickers for individual files.
//
// Today's role: the small, low-level surface for the toolbar's
// legacy per-file pickers (Open video / Open log / Open config) and
// for multi-chapter GoPro directory walks. The full per-file resume
// flow that used to live here — `useFileHandleResume`,
// `ReplayResumeBanner`, the `'handles-v1'` IDB store — has been
// subsumed by the folder-handle resume in `session.js` /
// `sidecar.js` and removed.
//
// Public surface still in use:
//   - `isFileHandleApiSupported()`        — feature probe
//   - `pickerOptionsForSlot(slot)`        — showOpenFilePicker types
//   - `pickFile(slot)`                    — single-file open picker
//   - `requestPermissionForHandles(h)`    — batched permission grant
//                                            used by the multi-chapter
//                                            directory-handle re-grant
//   - `queryPermissionForHandles(h)`      — non-prompting permission
//                                            probe; auto-resume path
//   - `queryPermissionPerSlot(h)`         — per-slot variant for the
//                                            ?debug=1 diagnostic
//   - `expandMultiChapterHandle(env)`     — walks a stored directory
//                                            handle, returns the ordered
//                                            chapter files
//
// Removed (subsumed by session.js):
//   - `useFileHandleResume`
//   - `ReplayResumeBanner`
//   - The `'handles-v1'` IDB store + `storeHandles` / `loadHandles` /
//     `clearHandles` API
//   - `recentFilesSignature` / `signatureFromFiles` (the IDB key
//     derivation; replaced by per-folder + per-log keying)

// ---------------------------------------------------------------------
// Pure helpers
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

// ---------------------------------------------------------------------
// Permission helper. Must be called from a user-gesture handler when
// transitioning from 'prompt' to 'granted'.
// ---------------------------------------------------------------------

// Extract the FileSystemHandle that needs permission from a slot
// value. For the `video` slot the value is either a FileSystemFileHandle
// (single-chapter session) or a `{kind:'multi-chapter', directoryHandle,
// chapterNames}` envelope — in the latter case the directory handle is
// the permission target (granting it implicitly grants every file inside).
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
  // the `.map()` body.
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
// every present slot's queryPermission resolves to 'granted'.
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
// is blocking resume.
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
