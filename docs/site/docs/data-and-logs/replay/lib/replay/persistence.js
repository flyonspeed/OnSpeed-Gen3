// persistence.js — localStorage persistence for the replay tool.
//
// Keyed by a 16-char hex SHA-256 prefix of the first 10 KB of the log
// file. Because the key derives from file content, the same log picked
// on reload yields the same key; a different log never restores stale state.
//
// Three localStorage namespaces:
//
//   replay-sync-<digest>-v1
//     { videoTakeoffSec, logTakeoffMs, anchorKind }
//
//   replay-clips-<digest>-v1
//     [{ startMs, endMs, label }, ...]
//
//   replay-recent-files-v1
//     { video: { name, size, lastModified },
//       log:   { name, size, lastModified },
//       cfg:   { name, size, lastModified } | null }
//
// Pure exports (testable in Node without Preact or browser APIs):
//   fileMetadata(file)           → { name, size, lastModified } | null
//   filesMatch(stored, picked)   → boolean
//   computeLogDigest(file)       → Promise<string | null>
//
// Preact hook: useReplayPersistence({ logFile })
// Preact component: RecentFilesBanner({ info, onDismiss })

import { html, useState, useEffect, useCallback, useRef }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';

function safeLsGet(key) { try { return localStorage.getItem(key); } catch { return null; } }
function safeLsSet(key, value) { try { localStorage.setItem(key, value); } catch {} }

const RECENT_FILES_KEY = 'replay-recent-files-v1';

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
  const [bannerInfo, setBannerInfo]         = useState(null);
  const [bannerDismissed, setBannerDismissed] = useState(false);
  const pickedFilesRef = useRef({ video: null, log: null, cfg: null });

  useEffect(() => {
    const raw = safeLsGet(RECENT_FILES_KEY);
    if (!raw) return;
    try {
      const info = JSON.parse(raw);
      if (info && info.video && info.log) setBannerInfo(info);
    } catch {}
  }, []);

  useEffect(() => {
    setLogDigest(null);
    setDigestReady(false);
    setStoredSync(null);
    setStoredClips(null);
    if (!logFile) return;
    let cancelled = false;
    computeLogDigest(logFile).then(digest => {
      if (cancelled) return;
      setLogDigest(digest);
      setDigestReady(true);
      setStoredSync(loadStoredSync(digest));
      setStoredClips(loadStoredClips(digest));
    });
    return () => { cancelled = true; };
  }, [logFile]);

  const dismissBanner = useCallback(() => { setBannerDismissed(true); }, []);

  const notifyFilePicked = useCallback((slot, file) => {
    pickedFilesRef.current = {
      ...pickedFilesRef.current,
      [slot]: fileMetadata(file),
    };
    const { video, log, cfg } = pickedFilesRef.current;
    if (video && log) {
      safeLsSet(RECENT_FILES_KEY, JSON.stringify({ video, log, cfg: cfg || null }));
    }
  }, []);

  const storeSync = useCallback((sync) => {
    if (!logDigest) return;
    if (!sync ||
        !Number.isFinite(sync.videoTakeoffSec) ||
        !Number.isFinite(sync.logTakeoffMs)) return;
    safeLsSet(syncKey(logDigest), JSON.stringify(sync));
  }, [logDigest]);

  const storeClips = useCallback((clips) => {
    if (!logDigest) return;
    if (!Array.isArray(clips)) return;
    safeLsSet(clipsKey(logDigest), JSON.stringify(clips));
  }, [logDigest]);

  return {
    digestReady,
    logDigest,
    bannerInfo: bannerDismissed ? null : bannerInfo,
    dismissBanner,
    notifyFilePicked,
    storedSync,
    storedClips,
    storeSync,
    storeClips,
  };
}

export function RecentFilesBanner({ info, onDismiss }) {
  if (!info) return null;
  const parts = [];
  if (info.video && info.video.name) parts.push(info.video.name);
  if (info.log   && info.log.name)   parts.push(info.log.name);
  if (info.cfg   && info.cfg.name)   parts.push(info.cfg.name);
  const label = parts.join(' + ') || 'previous session files';
  return html`
    <div class="replay-recent-banner">
      <span class="replay-recent-text">
        Last session: ${label}.
        Re-pick these files to restore sync and clips.
      </span>
      <button class="replay-recent-dismiss"
              type="button"
              onClick=${onDismiss}>×</button>
    </div>
  `;
}
