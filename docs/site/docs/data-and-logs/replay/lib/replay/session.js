// session.js — single mutation point for the replay tool's session
// lifecycle.
//
// A session is the combination of: a source (fresh, a flight folder,
// or individual file picks), the log/video/config slots, the sidecar
// document attached to the log, and the derived UI state (marks,
// clips, sync, HUD) rehydrated from the sidecar.
//
// Every change to the session — initial load, picking a folder,
// picking an individual file, restoring via FSA — goes through
// `switchSession(intent)`. That call tears down the prior session
// (M5 sim, IDB hooks, parsed-log memo, video element), then mounts
// the new one. Nothing else touches `source`, `dirHandle`, the file
// slots, or the sidecar handle directly.
//
// Pure helpers (testable in Node):
//
//   - `discoverFlightContents(dirHandle)` — re-exported from sidecar.js
//     to keep the plan's API stable. Records depth-0 + depth-1 files
//     with POSIX-style `relativePath`.
//   - `matchSidecarsToLogs(logs, sidecars)` — decision tree (D3):
//     when one sidecar matches one log, auto-load; when several do,
//     return a picker descriptor; when a sidecar's log is missing,
//     return an orphan descriptor.
//
// Mutation entry point:
//
//   - `switchSession(intent, ops)` — orchestrator. The caller (the
//     React layer) supplies `ops` — a bundle of callbacks that
//     actually perform the side effects (tearDown, mountFiles,
//     loadSidecar, setSource). Keeping the orchestrator independent
//     of React lets us test the call sequencing without rendering.
//
//   - `useSession()` — Preact hook. Wraps the React-side bookkeeping
//     (state slots, refs, debounced save) so ReplayPage can consume a
//     single object. Today's ReplayPage uses the lower-level helpers
//     directly; future refactors can move more logic in here.

import {
  discoverFlightContents,
  discoverFlightContentsRecursive,
} from './sidecar.js';
import {
  sidecarFileNameFor, logNameForSidecar,
} from './sidecar-schema.js';
import { groupChapterSiblings } from './chapters.js';

// Re-export the depth-1 enumerator from sidecar.js so callers that
// follow the plan can `import { discoverFlightContents } from
// './session.js'`. The recursive (depth-3) variant stays in sidecar.js
// for the existing readSidecarForLog plumbing.
export { discoverFlightContents, discoverFlightContentsRecursive };

// ---------------------------------------------------------------------
// matchSidecarsToLogs — D3 decision tree.
//
// Inputs:
//   logs:     [{ name, relativePath, handle, file }]
//   sidecars: [{ name, relativePath, handle }]
//
// Output:
//   { kind: 'auto-load', log, sidecar }     // exactly one log+sidecar pair
//   { kind: 'auto-load', log, sidecar: null } // one log, no sidecar
//   { kind: 'picker', logs, sidecarMap }    // multiple logs (with or without sidecars)
//   { kind: 'orphan', sidecar, missingLogName }  // sidecar present, its log isn't
//   { kind: 'empty' }                       // no logs found
//
// `sidecarMap` is `{ <logName-lowercase>: sidecarRow }` so the picker
// UI can show "sidecar present (last updated...)" badges per row.
// ---------------------------------------------------------------------

// Lowercase-keyed lookup of sidecars by the log name they describe.
function indexSidecars(sidecars) {
  const map = new Map();
  for (const s of sidecars || []) {
    const logName = logNameForSidecar(s.name || '');
    if (!logName) continue;
    map.set(logName.toLowerCase(), s);
  }
  return map;
}

export function matchSidecarsToLogs(logs, sidecars) {
  const logList = Array.isArray(logs) ? logs : [];
  const sidecarList = Array.isArray(sidecars) ? sidecars : [];

  if (logList.length === 0) {
    if (sidecarList.length > 0) {
      // Orphan: a sidecar exists but no logs in the folder.
      const s = sidecarList[0];
      return {
        kind: 'orphan',
        sidecar: s,
        missingLogName: logNameForSidecar(s.name || ''),
      };
    }
    return { kind: 'empty' };
  }

  const sidecarByLog = indexSidecars(sidecarList);

  // Single log: auto-load (with sidecar if matching, else without).
  if (logList.length === 1) {
    const log = logList[0];
    const sidecar = sidecarByLog.get(log.name.toLowerCase()) || null;
    return { kind: 'auto-load', log, sidecar };
  }

  // Multiple logs: picker. The sidecarMap lets the picker UI badge each
  // row with the matching sidecar (if any). Engineer chooses.
  const sidecarMap = {};
  for (const log of logList) {
    const s = sidecarByLog.get(log.name.toLowerCase());
    if (s) sidecarMap[log.name.toLowerCase()] = s;
  }

  // Edge case: exactly one log has a sidecar AND it's the only sidecar.
  // The plan calls this "one-sidecar-one-log" → auto-load. The picker
  // would feel like busywork — the sidecar's existence pre-selects.
  if (sidecarList.length === 1 && Object.keys(sidecarMap).length === 1) {
    const logName = Object.keys(sidecarMap)[0];
    const log = logList.find(l => l.name.toLowerCase() === logName);
    if (log) {
      return { kind: 'auto-load', log, sidecar: sidecarMap[logName] };
    }
  }

  return { kind: 'picker', logs: logList, sidecarMap };
}

// ---------------------------------------------------------------------
// switchSession — the single mutation entry point.
//
// `intent` describes what's changing:
//   { kind: 'fresh' }
//   { kind: 'folder', dirHandle: FileSystemDirectoryHandle }
//   { kind: 'file-pick', slot: 'log'|'video'|'config', file: File, handle? }
//   { kind: 'restore', dirHandle: FileSystemDirectoryHandle }
//
// `ops` is the bundle of side-effect callbacks the React layer wires
// up:
//   - tearDown()                  — kill M5 sim, clear slots, reset UI
//   - setSource(source, dirHandle)
//   - mountLog(file, handle, relativePath)
//   - mountVideo(file, handle, relativePath)
//   - mountConfig(file, handle, relativePath)
//   - loadSidecar(sidecarHandle, dirHandle) — read + apply
//   - showPicker(logs, sidecarMap, onChosen, dirHandle)
//   - reportError(message)
//
// Returns the resolved match result for diagnostics (or null on error).
// ---------------------------------------------------------------------

export async function switchSession(intent, ops) {
  if (!intent || typeof intent !== 'object') return null;
  if (!ops || typeof ops.tearDown !== 'function') return null;

  // 1. Tear down current session unconditionally.
  try { ops.tearDown(); }
  catch (e) {
    if (typeof ops.reportError === 'function') {
      ops.reportError('Session teardown failed: ' + (e?.message || e));
    }
  }

  if (intent.kind === 'fresh') {
    if (typeof ops.setSource === 'function') ops.setSource('fresh', null);
    return { kind: 'empty' };
  }

  if (intent.kind === 'file-pick') {
    if (typeof ops.setSource === 'function') ops.setSource('files', null);
    const slot = intent.slot;
    const file = intent.file;
    const handle = intent.handle || null;
    if (!file) return null;
    try {
      if (slot === 'log' && typeof ops.mountLog === 'function') {
        await ops.mountLog(file, handle, file.name);
      } else if (slot === 'video' && typeof ops.mountVideo === 'function') {
        await ops.mountVideo(file, handle, file.name);
      } else if (slot === 'config' && typeof ops.mountConfig === 'function') {
        await ops.mountConfig(file, handle, file.name);
      }
    } catch (e) {
      if (typeof ops.reportError === 'function') {
        ops.reportError(`Mount ${slot} failed: ` + (e?.message || e));
      }
    }
    return { kind: 'file-pick', slot };
  }

  if (intent.kind === 'folder' || intent.kind === 'restore') {
    const dirHandle = intent.dirHandle;
    if (!dirHandle) return null;
    if (typeof ops.setSource === 'function') ops.setSource(intent.kind, dirHandle);
    let discovery;
    try {
      discovery = await discoverFlightContentsRecursive(dirHandle);
    } catch (e) {
      if (typeof ops.reportError === 'function') {
        ops.reportError('Folder discovery failed: ' + (e?.message || e));
      }
      return null;
    }
    const match = matchSidecarsToLogs(discovery.logs, discovery.sidecars);
    return _applyMatch(match, discovery, dirHandle, ops);
  }

  return null;
}

// Internal: apply a match result by calling mount/load/picker ops.
// Exposed implicitly via switchSession's return — callers don't call
// this directly.
async function _applyMatch(match, discovery, dirHandle, ops) {
  if (match.kind === 'empty') {
    if (typeof ops.reportError === 'function') {
      ops.reportError(
        `No .csv logs found in "${dirHandle.name}". Pick a folder containing OnSpeed SD logs.`);
    }
    return match;
  }
  if (match.kind === 'orphan') {
    if (typeof ops.reportError === 'function') {
      const want = match.missingLogName || 'the matching log';
      ops.reportError(
        `Found sidecar but its log "${want}" isn't here. ` +
        `Re-pick the folder containing the log.`);
    }
    return match;
  }
  if (match.kind === 'auto-load') {
    // Pick most-recent cfg and most-recent video (or GoPro chapter
    // cluster) silently from the discovery bundle.
    const cfgPicked = pickMostRecent(discovery.configs);
    const videoPicked = await pickVideoSlot(discovery.videos, dirHandle);
    try {
      // Order matters: log first (the digest drives downstream state),
      // then cfg, then video, then sidecar load.
      await _mountRow(ops, 'log', match.log);
      if (cfgPicked) await _mountRow(ops, 'config', cfgPicked);
      if (videoPicked) await _mountRow(ops, 'video', videoPicked);
      if (match.sidecar && typeof ops.loadSidecar === 'function') {
        await ops.loadSidecar(match.sidecar.handle, dirHandle, discovery);
      }
    } catch (e) {
      if (typeof ops.reportError === 'function') {
        ops.reportError('Mount failed: ' + (e?.message || e));
      }
    }
    return match;
  }
  if (match.kind === 'picker') {
    if (typeof ops.showPicker === 'function') {
      ops.showPicker(match.logs, match.sidecarMap, async (chosenLog) => {
        const cfgPicked = pickMostRecent(discovery.configs);
        const videoPicked = await pickVideoSlot(discovery.videos, dirHandle);
        try {
          await _mountRow(ops, 'log', chosenLog);
          if (cfgPicked) await _mountRow(ops, 'config', cfgPicked);
          if (videoPicked) await _mountRow(ops, 'video', videoPicked);
          const sidecarRow = match.sidecarMap[chosenLog.name.toLowerCase()] || null;
          if (sidecarRow && typeof ops.loadSidecar === 'function') {
            await ops.loadSidecar(sidecarRow.handle, dirHandle, discovery);
          }
        } catch (e) {
          if (typeof ops.reportError === 'function') {
            ops.reportError('Mount failed: ' + (e?.message || e));
          }
        }
      }, dirHandle);
    }
    return match;
  }
  return match;
}

async function _mountRow(ops, slot, row) {
  // Multi-chapter video envelope: forward as-is to mountVideo. The
  // React layer's mountVideo handler knows to expand the chapter
  // list and build the timeline via applyVideoFiles. Same shape that
  // expandMultiChapterHandle produces today on the legacy resume path.
  if (slot === 'video' && row && row.kind === 'multi-chapter') {
    if (typeof ops.mountVideo !== 'function') return;
    await ops.mountVideo(row, null, row.relativePath || '');
    return;
  }
  const file = row.file || (row.handle && await row.handle.getFile());
  if (!file) return;
  const handle = row.handle || null;
  const relPath = row.relativePath || file.name || '';
  if (slot === 'log' && typeof ops.mountLog === 'function') {
    await ops.mountLog(file, handle, relPath);
  } else if (slot === 'video' && typeof ops.mountVideo === 'function') {
    await ops.mountVideo(file, handle, relPath);
  } else if (slot === 'config' && typeof ops.mountConfig === 'function') {
    await ops.mountConfig(file, handle, relPath);
  }
}

function pickMostRecent(rows) {
  if (!Array.isArray(rows) || rows.length === 0) return null;
  const sorted = [...rows].sort((a, b) => {
    const am = (a.file && a.file.lastModified) || 0;
    const bm = (b.file && b.file.lastModified) || 0;
    return bm - am;
  });
  return sorted[0];
}

// ---------------------------------------------------------------------
// Video slot selection — GoPro chapter clustering.
//
// `pickVideoSlot(videos, rootDirHandle)` examines the discovered video
// rows and decides whether they form a GoPro multi-chapter recording.
// Each row carries { name, relativePath, handle, file }. Rows are
// grouped by parent directory (so chapters in `Raw Video/` cluster
// among themselves, not with an unrelated chapter from a different
// subfolder). For each parent-dir group, `groupChapterSiblings` finds
// the largest cluster sharing a GoPro seq number.
//
// Resolution:
//   - If the largest cluster has >= 2 chapters: return a multi-chapter
//     envelope (`{kind:'multi-chapter', chapters, directoryHandle,
//     relativePath, ...}`). `_mountRow` forwards the envelope to the
//     React mountVideo op, which expands it via `applyVideoFiles`.
//   - Otherwise: return `pickMostRecent(videos)` unchanged.
//
// The envelope's `chapters` array is sorted by chapterIndex with shape
// `{file, handle, chapterIndex, relativePath}` — same downstream
// consumers expect what `applyVideoFiles` already builds when the FSA
// picker walks a directory directly.
// ---------------------------------------------------------------------
export async function pickVideoSlot(videos, rootDirHandle) {
  if (!Array.isArray(videos) || videos.length === 0) return null;
  const cluster = clusterVideoChapters(videos);
  if (!cluster) return pickMostRecent(videos);
  // Walk from the root dirHandle down to the parent of the chapter
  // files. The cluster's `parentRelativePath` is the POSIX subpath; if
  // empty, the chapters live at the root.
  let parentDir = rootDirHandle || null;
  if (parentDir && cluster.parentRelativePath) {
    const segs = cluster.parentRelativePath.split('/').filter(Boolean);
    for (const seg of segs) {
      try { parentDir = await parentDir.getDirectoryHandle(seg); }
      catch { parentDir = null; break; }
    }
  }
  return {
    kind: 'multi-chapter',
    chapters: cluster.chapters,
    directoryHandle: parentDir,
    // Preserve relativePath of the first chapter for sidecar
    // round-tripping: the sidecar's subject.video.relativePath /
    // subject.video.name target the first chapter (option A).
    relativePath: cluster.chapters[0]?.relativePath || '',
    name: cluster.chapters[0]?.file?.name || cluster.chapters[0]?.name || '',
    file: cluster.chapters[0]?.file || null,
    handle: cluster.chapters[0]?.handle || null,
  };
}

// Examine the discovered video rows and return the largest GoPro
// chapter cluster, scoped to a single parent directory. Returns
//   { chapters: [{file, handle, chapterIndex, relativePath, name}],
//     parentRelativePath }
// or null if no cluster of >= 2 chapters was found in any directory.
//
// Per-parent scoping matters: Vac's flight folders can have GoPro
// footage in multiple subfolders (cockpit cam + tail cam), and
// chapters from different recordings shouldn't merge into a single
// cluster even when their seq numbers happen to match. Each parent
// dir's videos cluster among themselves; the largest cluster wins.
export function clusterVideoChapters(videos) {
  if (!Array.isArray(videos) || videos.length === 0) return null;
  // Bucket rows by parent directory (relativePath up to the last '/').
  const byParent = new Map();
  for (const row of videos) {
    const rel = typeof row.relativePath === 'string' ? row.relativePath : '';
    const slash = rel.lastIndexOf('/');
    const parent = slash >= 0 ? rel.slice(0, slash) : '';
    if (!byParent.has(parent)) byParent.set(parent, []);
    byParent.get(parent).push(row);
  }
  let bestChapters = null;
  let bestParent = '';
  let bestSize = 0;
  for (const [parent, rows] of byParent.entries()) {
    // groupChapterSiblings expects file-like objects with `.name`.
    // We feed it the row's `.file` (preferred) or a stub with the row
    // name — chapter detection only reads `.name`. After grouping we
    // re-stitch the row's handle/relativePath back onto each cluster
    // entry so downstream consumers have the full envelope.
    const fileLikes = rows.map(r => r.file || { name: r.name });
    const grouped = groupChapterSiblings(fileLikes);
    if (grouped.length < 2) continue;
    if (grouped.length > bestSize) {
      bestSize = grouped.length;
      bestParent = parent;
      // Re-stitch row metadata onto each cluster entry by matching name.
      const rowByName = new Map(rows.map(r => [r.name, r]));
      bestChapters = grouped.map(g => {
        const row = rowByName.get(g.file.name) || {};
        return {
          file: row.file || g.file,
          handle: row.handle || null,
          chapterIndex: g.chapterIndex,
          relativePath: row.relativePath || g.file.name,
          name: g.file.name,
        };
      });
    }
  }
  if (!bestChapters) return null;
  return { chapters: bestChapters, parentRelativePath: bestParent };
}

// Re-export sidecar filename helpers so callers don't need a second
// import line.
export { sidecarFileNameFor, logNameForSidecar };
