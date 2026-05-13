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

function openDb() {
  return new Promise((resolve, reject) => {
    if (typeof indexedDB === 'undefined') {
      reject(new Error('IndexedDB not available'));
      return;
    }
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

function withStore(mode, fn) {
  return openDb().then(db => new Promise((resolve, reject) => {
    let result;
    const tx = db.transaction(JOURNAL_STORE_NAME, mode);
    const store = tx.objectStore(JOURNAL_STORE_NAME);
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
    const record = await withStore('readonly', store =>
      reqToPromise(store.get(logHash)));
    return record || null;
  } catch (err) {
    console.warn('journal: loadJournal failed', err);
    return null;
  }
}

// Upsert a mark by (value, logTimeMs). Patch is { name?, notes? };
// fields not present in patch are left unchanged. Creates the record
// + the mark row if either is missing.
export async function upsertMark(logHash, key, patch) {
  if (!logHash || !key || typeof key !== 'object') return;
  const { value, logTimeMs } = key;
  if (!Number.isFinite(value) || !Number.isFinite(logTimeMs)) return;
  const now = Date.now();
  try {
    await withStore('readwrite', async store => {
      const existing = await reqToPromise(store.get(logHash));
      const record = existing || emptyRecord(logHash);
      record.lastUsed = now;
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
      await reqToPromise(store.put(record));
    });
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
    await withStore('readwrite', async store => {
      const existing = await reqToPromise(store.get(logHash));
      const record = existing || emptyRecord(logHash);
      record.lastUsed = now;
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
      await reqToPromise(store.put(record));
    });
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
