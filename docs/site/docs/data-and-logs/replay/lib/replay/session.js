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
    // Pick most-recent cfg and video silently from the discovery bundle.
    const cfgPicked = pickMostRecent(discovery.configs);
    const videoPicked = pickMostRecent(discovery.videos);
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
        const videoPicked = pickMostRecent(discovery.videos);
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

// Re-export sidecar filename helpers so callers don't need a second
// import line.
export { sidecarFileNameFor, logNameForSidecar };
