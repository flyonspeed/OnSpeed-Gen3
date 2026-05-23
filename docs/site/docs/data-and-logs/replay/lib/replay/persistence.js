// persistence.js — localStorage cache for per-log sync + clip state.
//
// Keyed by a 16-char hex SHA-256 prefix of the first 10 KB of the log
// file. The same log picked on reload yields the same key; a
// different log never restores stale state.
//
// Two localStorage namespaces:
//
//   replay-sync-<digest>-v1
//     { videoTakeoffSec, logTakeoffMs }
//
//   replay-clips-<digest>-v1
//     [{ startMs, endMs, label }, ...]
//
// This is a CACHE on top of the sidecar (`.replay.json` next to the
// log). The sidecar is the authoritative source; localStorage just
// keeps the sync + clip recall instant on Chrome/Edge before the
// folder pick re-grants permission. The legacy "recent files banner"
// and the FSA per-file resume have been replaced by the folder-handle
// resume in `session.js` — this module no longer touches that surface.
//
// Pure exports (testable in Node without Preact or browser APIs):
//   fileMetadata(file)           → { name, size, lastModified } | null
//   filesMatch(stored, picked)   → boolean
//   computeLogDigest(file)       → Promise<string | null>
//
// Preact hook: useReplayPersistence({ logFile })

import { useState, useEffect, useCallback, useRef }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';

function safeLsGet(key) { try { return localStorage.getItem(key); } catch { return null; } }
function safeLsSet(key, value) { try { localStorage.setItem(key, value); } catch {} }

function syncKey(digest)  { return `replay-sync-${digest}-v1`;  }
function clipsKey(digest) { return `replay-clips-${digest}-v1`; }

export function fileMetadata(file) {
  if (!file || typeof file !== 'object') return null;
  const { name, size, lastModified } = file;
  if (typeof name !== 'string') return null;
  return { name, size: Number(size), lastModified: Number(lastModified) };
}

export function filesMatch(stored, picked) {
  if (!stored || !picked) return false;
  return stored.name === picked.name &&
         stored.size === picked.size &&
         stored.lastModified === picked.lastModified;
}

export async function computeLogDigest(file) {
  if (!file) return null;
  try {
    const buf = await file.slice(0, 10_240).arrayBuffer();
    const hash = await crypto.subtle.digest('SHA-256', buf);
    const bytes = new Uint8Array(hash);
    let hex = '';
    for (let i = 0; i < 8; i++) {
      hex += bytes[i].toString(16).padStart(2, '0');
    }
    return hex;
  } catch {
    return null;
  }
}

function loadStoredSync(digest) {
  if (!digest) return null;
  const raw = safeLsGet(syncKey(digest));
  if (!raw) return null;
  try {
    const p = JSON.parse(raw);
    if (Number.isFinite(p.logTakeoffMs) && Number.isFinite(p.videoTakeoffSec)) return p;
  } catch {}
  return null;
}

function loadStoredClips(digest) {
  if (!digest) return null;
  const raw = safeLsGet(clipsKey(digest));
  if (!raw) return null;
  try {
    const p = JSON.parse(raw);
    if (Array.isArray(p)) return p;
  } catch {}
  return null;
}

export function useReplayPersistence({ logFile }) {
  const [logDigest, setLogDigest]           = useState(null);
  const [digestReady, setDigestReady]       = useState(false);
  const [storedSync, setStoredSync]         = useState(null);
  const [storedClips, setStoredClips]       = useState(null);
  // Synchronous mirror of logDigest. storeSync / storeClips read this
  // ref instead of the React state, so they see the digest go to null
  // the instant a new logFile is provided — not on the next render
  // after the persistence hook's logFile-effect commits state.
  const logDigestRef = useRef(null);

  useEffect(() => {
    logDigestRef.current = null;
    setLogDigest(null);
    setDigestReady(false);
    setStoredSync(null);
    setStoredClips(null);
    if (!logFile) return;
    let cancelled = false;
    computeLogDigest(logFile).then(digest => {
      if (cancelled) return;
      logDigestRef.current = digest;
      setLogDigest(digest);
      setDigestReady(true);
      setStoredSync(loadStoredSync(digest));
      setStoredClips(loadStoredClips(digest));
    });
    return () => { cancelled = true; };
  }, [logFile]);

  // Synchronous hook a page-level handler calls just before swapping
  // logFile. Clears logDigestRef so any persist-on-change effects
  // that fire during the same React render — with the OLD logDigest
  // state still bound — see ref=null and refuse to write. Without
  // this, the setClips([]) + setLogFile(f) batch causes the clips
  // persist-effect to write [] to the PREVIOUS log's localStorage
  // key before the digest recomputes.
  const beginLogSwap = useCallback(() => {
    logDigestRef.current = null;
  }, []);

  // Both writers read the synchronous ref so they reflect the most
  // recent logFile prop — not whichever logDigest state-value the
  // useCallback closure last captured. storeSync stayed bound to
  // logDigest as the React-state dep so consumers still get a stable
  // identity per-log, but the actual digest used for the write comes
  // from the ref.
  const storeSync = useCallback((sync) => {
    const digest = logDigestRef.current;
    if (!digest) return;
    if (!sync ||
        !Number.isFinite(sync.videoTakeoffSec) ||
        !Number.isFinite(sync.logTakeoffMs)) return;
    safeLsSet(syncKey(digest), JSON.stringify(sync));
  }, [logDigest]);

  const storeClips = useCallback((clips) => {
    const digest = logDigestRef.current;
    if (!digest) return;
    if (!Array.isArray(clips)) return;
    safeLsSet(clipsKey(digest), JSON.stringify(clips));
  }, [logDigest]);

  return {
    digestReady,
    logDigest,
    beginLogSwap,
    storedSync,
    storedClips,
    storeSync,
    storeClips,
  };
}
