// journal.js — per-log annotation store for the replay tool.
//
// IDB store `journal-v1` keyed by the log's content hash (the same
// SHA-256-prefix-of-first-10KB digest persistence.js uses for sync +
// clip persistence). One record per log:
//
//   {
//     logHash:    string,
//     logName:    string,
//     videoName?: string,
//     lastUsed:   number,
//     marks: [{ value, logTimeMs, name?, notes?, createdAt, updatedAt }, ...],
//     clips: [{ id, startLogMs, endLogMs, label?, notes?, createdAt, updatedAt }, ...],
//   }
//
// Marks are addressed by the (value, logTimeMs) tuple via
// `markKey(value, logTimeMs) === value + ':' + logTimeMs`. The marks
// the parser surfaces are the source of truth for which keys exist;
// the journal layer only carries pilot-authored overlay fields.
//
// All operations are best-effort: caller errors are caught and logged
// to console, never propagated. Mirrors the storeHandles / loadHandles
// pattern in fileHandles.js.

import { useState, useEffect, useCallback, useRef }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';

const JOURNAL_DB_NAME = 'replay-journal';
const JOURNAL_STORE_NAME = 'journal-v1';
const JOURNAL_DB_VERSION = 1;

// ---------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------

export function markKey(value, logTimeMs) {
  return String(value) + ':' + String(logTimeMs);
}

// ---------------------------------------------------------------------
// IndexedDB plumbing — mirrors fileHandles.js's pattern.
// ---------------------------------------------------------------------

function _openJournalDbFresh() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(JOURNAL_DB_NAME, JOURNAL_DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(JOURNAL_STORE_NAME)) {
        db.createObjectStore(JOURNAL_STORE_NAME, { keyPath: 'logHash' });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('IDB open failed'));
  });
}

function _openJournalDb() {
  return new Promise((resolve, reject) => {
    if (typeof indexedDB === 'undefined') {
      reject(new Error('IndexedDB not available'));
      return;
    }
    _openJournalDbFresh().then(db => {
      // Defensive recovery: if the DB exists but the store doesn't
      // (a stale dev-build state from before this fix), nuke the DB
      // and recreate. The data we'd be wiping was never reachable
      // anyway — no upsert ever succeeded if the store was missing.
      if (db.objectStoreNames.contains(JOURNAL_STORE_NAME)) {
        resolve(db);
        return;
      }
      db.close();
      const del = indexedDB.deleteDatabase(JOURNAL_DB_NAME);
      del.onsuccess = () => _openJournalDbFresh().then(resolve, reject);
      del.onerror = () => reject(del.error || new Error('IDB delete failed'));
      del.onblocked = () => reject(new Error('IDB delete blocked — close other tabs'));
    }, reject);
  });
}

function _withJournalStore(mode, fn) {
  return _openJournalDb().then(db => new Promise((resolve, reject) => {
    let result;
    const tx = db.transaction(JOURNAL_STORE_NAME, mode);
    const store = tx.objectStore(JOURNAL_STORE_NAME);
    Promise.resolve(fn(store)).then(r => { result = r; }, reject);
    tx.oncomplete = () => { db.close(); resolve(result); };
    tx.onabort = () => { db.close(); reject(tx.error || new Error('IDB tx aborted')); };
    tx.onerror = () => { db.close(); reject(tx.error || new Error('IDB tx error')); };
  }));
}

// Variant of _withJournalStore for read-modify-write callbacks that must chain
// multiple IDB requests inside one transaction. The callback returns a
// Promise that resolves when ALL requests have settled — but it must
// chain those requests via IDB event callbacks (NOT `await`), because
// `await` allows a microtask to drain, which auto-commits the IDB
// transaction. See upsertMark / upsertClipAnnotation for the pattern.
function _withJournalStoreCallback(mode, fn) {
  return _openJournalDb().then(db => new Promise((resolve, reject) => {
    const tx = db.transaction(JOURNAL_STORE_NAME, mode);
    const store = tx.objectStore(JOURNAL_STORE_NAME);
    let callbackError = null;
    fn(store).then(() => {}, (err) => { callbackError = err; });
    tx.oncomplete = () => {
      db.close();
      if (callbackError) reject(callbackError);
      else resolve();
    };
    tx.onabort = () => { db.close(); reject(callbackError || tx.error || new Error('IDB tx aborted')); };
    tx.onerror = () => { db.close(); reject(callbackError || tx.error || new Error('IDB tx error')); };
  }));
}

function _journalReqToPromise(req) {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('IDB request failed'));
  });
}

function emptyRecord(logHash) {
  return {
    logHash,
    logName: '',
    lastUsed: Date.now(),
    marks: [],
    clips: [],
  };
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

// Load the full journal record for a log hash. Returns null if none
// exists or IDB is unavailable.
export async function loadJournal(logHash) {
  if (!logHash || typeof logHash !== 'string') return null;
  try {
    const record = await _withJournalStore('readonly', store =>
      _journalReqToPromise(store.get(logHash)));
    return record || null;
  } catch (err) {
    console.warn('journal: loadJournal failed', err);
    return null;
  }
}

// Upsert a mark by (value, logTimeMs). Patch is { name?, notes? };
// fields not present in patch are left unchanged. Creates the record
// + the mark row if either is missing.
//
// IDB-transaction note: the read-modify-write happens inside a single
// `readwrite` transaction. We must NOT `await` between the get's
// success and the put's request — IDB transactions auto-commit (and
// close) as soon as the microtask queue is empty between operations.
// Chaining via the get's onsuccess callback keeps the same transaction
// alive across both requests.
export async function upsertMark(logHash, key, patch) {
  if (!logHash || !key || typeof key !== 'object') return;
  const { value, logTimeMs } = key;
  if (!Number.isFinite(value) || !Number.isFinite(logTimeMs)) return;
  const now = Date.now();
  try {
    await _withJournalStoreCallback('readwrite', (store) => new Promise((resolve, reject) => {
      const getReq = store.get(logHash);
      getReq.onerror = () => reject(getReq.error);
      getReq.onsuccess = () => {
        // Wrap the body so a synchronous throw (corrupt record shape,
        // unstructured-cloneable value, etc.) doesn't leak as an
        // unhandled exception while the transaction silently auto-
        // commits with no pending requests. The outer try/catch on the
        // await wouldn't see the throw without this — the Promise
        // would just never resolve or reject and tx.oncomplete would
        // fire with success.
        try {
          const existing = getReq.result;
          const record = existing || emptyRecord(logHash);
          record.lastUsed = now;
          if (!Array.isArray(record.marks)) record.marks = [];
          const target = markKey(value, logTimeMs);
          const idx = record.marks.findIndex(
            m => markKey(m.value, m.logTimeMs) === target);
          if (idx >= 0) {
            const prior = record.marks[idx];
            record.marks[idx] = {
              ...prior,
              ...patch,
              value, logTimeMs,
              updatedAt: now,
            };
          } else {
            record.marks.push({
              value, logTimeMs,
              ...patch,
              createdAt: now,
              updatedAt: now,
            });
          }
          const putReq = store.put(record);
          putReq.onerror = () => reject(putReq.error);
          putReq.onsuccess = () => resolve();
        } catch (err) {
          reject(err);
        }
      };
    }));
  } catch (err) {
    console.warn('journal: upsertMark failed', err);
  }
}

// Upsert a clip annotation by id. Patch is { label?, notes?,
// startLogMs?, endLogMs? }; missing fields are left unchanged. Creates
// the record + clip row if either is missing.
export async function upsertClipAnnotation(logHash, id, patch) {
  if (!logHash || !id || typeof id !== 'string') return;
  const now = Date.now();
  try {
    await _withJournalStoreCallback('readwrite', (store) => new Promise((resolve, reject) => {
      const getReq = store.get(logHash);
      getReq.onerror = () => reject(getReq.error);
      getReq.onsuccess = () => {
        // See upsertMark for the rationale on this try/catch.
        try {
          const existing = getReq.result;
          const record = existing || emptyRecord(logHash);
          record.lastUsed = now;
          if (!Array.isArray(record.clips)) record.clips = [];
          const idx = record.clips.findIndex(c => c.id === id);
          if (idx >= 0) {
            record.clips[idx] = {
              ...record.clips[idx],
              ...patch,
              id,
              updatedAt: now,
            };
          } else {
            record.clips.push({
              id,
              ...patch,
              createdAt: now,
              updatedAt: now,
            });
          }
          const putReq = store.put(record);
          putReq.onerror = () => reject(putReq.error);
          putReq.onsuccess = () => resolve();
        } catch (err) {
          reject(err);
        }
      };
    }));
  } catch (err) {
    console.warn('journal: upsertClipAnnotation failed', err);
  }
}

// Return the mark-annotation overlay map keyed by `value:logTimeMs`,
// with payload `{ name, notes, updatedAt }`. Empty object if the
// record doesn't exist or has no marks.
export async function loadMarkAnnotations(logHash) {
  const record = await loadJournal(logHash);
  if (!record || !Array.isArray(record.marks)) return {};
  const out = {};
  for (const m of record.marks) {
    if (!Number.isFinite(m.value) || !Number.isFinite(m.logTimeMs)) continue;
    out[markKey(m.value, m.logTimeMs)] = {
      name: m.name || '',
      notes: m.notes || '',
      updatedAt: m.updatedAt || 0,
    };
  }
  return out;
}

// ---------------------------------------------------------------------
// Preact hook: useReplayJournal
//
// Watches the current log digest (computed externally — pass it in
// from useReplayPersistence). On hash change, loads the mark-annotation
// overlay into state. Returns { markAnnotations, upsertMarkAnnotation }
// for the rest of the page to consume.
//
// The local-state mirror is what the UI renders. IDB writes happen in
// the background; the callback updates local state synchronously so
// keystrokes feel instant.
// ---------------------------------------------------------------------

export function useReplayJournal({ logHash }) {
  const [markAnnotations, setMarkAnnotations] = useState({});
  // Synchronous mirror of logHash so the updater callback writes to
  // the correct record even when called between an IDB-load promise
  // resolving and Preact committing the new state.
  //
  // Known narrow race (deferred): if a pilot edits a mark's name, then
  // swaps logs before the 500ms debounce fires, AND the new log
  // happens to contain a mark with the same (value, logTimeMs) key,
  // the stale debounced write lands under the new log's record. The
  // unmount path on DataMarkPanel cancels the timer but doesn't flush
  // it. Real-world hit rate is vanishingly small (mark keys collide
  // across separate flights only by coincidence); proper fix is to
  // capture the logHash at edit-time into the debounce closure rather
  // than reading the ref at flush-time.
  const logHashRef = useRef(null);

  useEffect(() => {
    logHashRef.current = logHash || null;
    setMarkAnnotations({});
    if (!logHash) return;
    let cancelled = false;
    loadMarkAnnotations(logHash).then(ann => {
      if (cancelled) return;
      setMarkAnnotations(ann);
    });
    return () => { cancelled = true; };
  }, [logHash]);

  const upsertMarkAnnotation = useCallback((mark, patch) => {
    const hash = logHashRef.current;
    if (!hash || !mark) return;
    const key = markKey(mark.value, mark.logTimeMs);
    setMarkAnnotations(prev => ({
      ...prev,
      [key]: {
        name:  patch.name  != null ? patch.name  : (prev[key]?.name  || ''),
        notes: patch.notes != null ? patch.notes : (prev[key]?.notes || ''),
        updatedAt: Date.now(),
      },
    }));
    upsertMark(hash, { value: mark.value, logTimeMs: mark.logTimeMs }, patch);
  }, []);

  return { markAnnotations, upsertMarkAnnotation };
}
