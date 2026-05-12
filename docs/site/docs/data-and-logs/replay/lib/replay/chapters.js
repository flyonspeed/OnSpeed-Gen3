// chapters.js — GoPro multi-chapter ingest for the replay tool.
//
// GoPro cameras split long recordings into chapter files. The first
// chapter is named GOPR####.MP4; each continuation is GP0N####.MP4
// where N is the chapter index (1-9) and #### is a 4-digit sequence
// number that stays constant across siblings.
//
// Example: a 50-minute recording named #0314 produces
//   GOPR0314.MP4   (chapter 0)
//   GP010314.MP4   (chapter 1)
//   GP020314.MP4   (chapter 2)
//   GP030314.MP4   (chapter 3)
//
// This module:
//   1. Parses GoPro chapter filenames (detectGoProChapterPattern).
//   2. Groups a flat list of files into the largest chapter cluster
//      sharing the same sequence number (groupChapterSiblings).
//   3. Builds a virtual single-timeline from a chapter array
//      (buildChapterTimeline) by probing each file's duration with a
//      temporary <video> element.
//   4. Maps a global timeline second to a (chapter, local-sec) pair
//      and back (globalToLocal / localToGlobal).
//   5. Selects the chapter segments overlapping a global time window,
//      with local-time bounds per segment, for the cross-chapter clip
//      export (selectChaptersForClip).
//
// Pure functions only — no React imports, no DOM coupling outside the
// duration probe (which is gated behind a Promise so Node-side smoke
// tests can stub it). The chapter-swap playback hook lives in
// ReplayPage.js itself, where it can wire into the existing rVFC chain
// and videoRef.

// GoPro chapter filename pattern. Optional leading prefix (e.g. the
// `chapter01-` rename Playwright/manual tests use) is stripped before
// matching. The core token is:
//   - GOPR####.MP4   (first chapter, chapterIndex 0)
//   - GP0N####.MP4   (continuation, chapterIndex = N, 1..9)
//
// Case-sensitive on the prefix and extension to match GoPro's actual
// filenames; if a future GoPro tool ships lowercase variants, loosen
// then (and add a test case).
const GOPRO_FIRST_RE = /(?:^|[^A-Za-z0-9])(GOPR)(\d{4})\.MP4$/;
const GOPRO_CONT_RE  = /(?:^|[^A-Za-z0-9])(GP)0([1-9])(\d{4})\.MP4$/;

// Parse a GoPro chapter filename. Returns
//   { prefix: 'GOPR' | 'GP', seq: '0314', chapterIndex: 0..9 } | null
// for non-matches. The leading-folder/garbage tolerance is intentional
// — Playwright mirror-symlinks chapters as `chapter01-GOPR0314.MP4` so
// the test path needs to work too.
export function detectGoProChapterPattern(filename) {
  if (typeof filename !== 'string' || filename.length === 0) return null;
  // GP0N first: the continuation pattern is more specific (GP0 prefix
  // + non-zero digit). GOPR after, since GP01...09 followed by 4 digits
  // shouldn't be mis-detected as GOPR.
  const cont = filename.match(GOPRO_CONT_RE);
  if (cont) {
    return {
      prefix: 'GP',
      seq: cont[3],
      chapterIndex: parseInt(cont[2], 10),
    };
  }
  const first = filename.match(GOPRO_FIRST_RE);
  if (first) {
    return {
      prefix: 'GOPR',
      seq: first[2],
      chapterIndex: 0,
    };
  }
  return null;
}

// Given a flat array of File-like objects (anything with a `.name`),
// find the largest cluster of GoPro chapters that share the same `seq`,
// sort by chapterIndex, and return [{file, chapterIndex}, ...].
//
// If no two files share a seq, returns a single-element array containing
// the first GoPro-matching file in input order — single chapter is a
// degenerate case of multi-chapter, handled the same way downstream.
//
// If NO file matches the GoPro pattern, returns an empty array. The
// caller (ReplayPage) treats the empty result as "this isn't a chapter
// pick — use the file as a standalone video" and the legacy path runs.
export function groupChapterSiblings(files) {
  if (!Array.isArray(files) || files.length === 0) return [];
  const bySeq = new Map();
  for (const file of files) {
    const m = detectGoProChapterPattern(file?.name || '');
    if (!m) continue;
    if (!bySeq.has(m.seq)) bySeq.set(m.seq, []);
    bySeq.get(m.seq).push({ file, chapterIndex: m.chapterIndex });
  }
  if (bySeq.size === 0) return [];

  // Pick the largest cluster. Ties broken by first-seen order in the
  // input — Map iteration preserves insertion order so the first seq
  // that hit max size wins.
  let best = null;
  let bestSize = 0;
  for (const cluster of bySeq.values()) {
    if (cluster.length > bestSize) {
      best = cluster;
      bestSize = cluster.length;
    }
  }
  // Deduplicate chapterIndex within the cluster. If the user picks the
  // same chapter twice (e.g. two filesystem entries pointing at the
  // same recording), keep the first occurrence — sort is stable but
  // the duplicate would make timeline math undefined.
  const seenIdx = new Set();
  const deduped = [];
  for (const c of best) {
    if (seenIdx.has(c.chapterIndex)) continue;
    seenIdx.add(c.chapterIndex);
    deduped.push(c);
  }
  deduped.sort((a, b) => a.chapterIndex - b.chapterIndex);
  return deduped;
}

// Probe one File's duration by mounting it in a temporary off-DOM
// <video> element. Returns Promise<number> (seconds). Rejects if
// metadata fails to load (corrupt file, unsupported codec).
//
// Exported so the chapter-build path can be Node-tested by replacing
// this function with a stub. In the browser, called transparently by
// buildChapterTimeline.
export function probeFileDurationSec(file) {
  if (typeof document === 'undefined' || typeof URL === 'undefined') {
    return Promise.reject(new Error('chapters: probeFileDurationSec requires a DOM environment'));
  }
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const v = document.createElement('video');
    v.preload = 'metadata';
    v.muted = true;
    const cleanup = () => {
      URL.revokeObjectURL(url);
      v.removeAttribute('src');
      try { v.load(); } catch (_) { /* best-effort */ }
    };
    v.onloadedmetadata = () => {
      const dur = v.duration;
      cleanup();
      if (Number.isFinite(dur) && dur > 0) resolve(dur);
      else reject(new Error(`chapters: invalid duration ${dur} for ${file?.name}`));
    };
    v.onerror = () => {
      cleanup();
      reject(new Error(`chapters: failed to probe ${file?.name}`));
    };
    v.src = url;
  });
}

// Build the chapter timeline given a sorted chapter list. Each entry's
// duration is probed via the supplied probeDuration function (defaults
// to probeFileDurationSec; tests pass a stub).
//
// Returned shape:
//   { chapters: [{file, durationSec, startSec, endSec, chapterIndex}],
//     totalDurationSec,
//     signature }
//
// startSec / endSec are global-timeline seconds; signature is a stable
// hash of (name, size) pairs used for persistence keying so a re-pick
// of the same chapter cluster restores sync + clips.
export async function buildChapterTimeline(chapters, { probeDuration = probeFileDurationSec } = {}) {
  if (!Array.isArray(chapters) || chapters.length === 0) {
    throw new Error('chapters: buildChapterTimeline requires at least one chapter');
  }
  const durations = await Promise.all(chapters.map(c => probeDuration(c.file)));
  let cursor = 0;
  const built = chapters.map((c, i) => {
    const durationSec = durations[i];
    const startSec = cursor;
    const endSec   = cursor + durationSec;
    cursor = endSec;
    return {
      file: c.file,
      chapterIndex: c.chapterIndex,
      durationSec,
      startSec,
      endSec,
    };
  });
  const sigParts = built.map(c => `${c.file?.name || '?'}:${c.file?.size || 0}`);
  return {
    chapters: built,
    totalDurationSec: cursor,
    signature: sigParts.join('|'),
  };
}

// Map a global second to (chapterIndex, localSec). Past-the-end times
// clamp to the last chapter's end. Negative or pre-start clamp to
// chapter 0 localSec=0. Binary search; O(log N) chapters.
export function globalToLocal(timeline, globalSec) {
  if (!timeline || !Array.isArray(timeline.chapters) || timeline.chapters.length === 0) {
    return { chapterIndex: 0, localSec: 0 };
  }
  const chapters = timeline.chapters;
  if (!Number.isFinite(globalSec) || globalSec <= 0) {
    return { chapterIndex: 0, localSec: 0 };
  }
  if (globalSec >= timeline.totalDurationSec) {
    const last = chapters[chapters.length - 1];
    return { chapterIndex: chapters.length - 1, localSec: last.durationSec };
  }
  // Binary search for the chapter whose [startSec, endSec) covers globalSec.
  let lo = 0;
  let hi = chapters.length - 1;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (chapters[mid].endSec <= globalSec) lo = mid + 1;
    else hi = mid;
  }
  const c = chapters[lo];
  return { chapterIndex: lo, localSec: Math.max(0, globalSec - c.startSec) };
}

// Inverse of globalToLocal. Out-of-range chapterIndex clamps; invalid
// localSec falls back to 0.
export function localToGlobal(timeline, chapterIndex, localSec) {
  if (!timeline || !Array.isArray(timeline.chapters) || timeline.chapters.length === 0) {
    return 0;
  }
  const chapters = timeline.chapters;
  const idx = Math.max(0, Math.min(chapters.length - 1, chapterIndex | 0));
  const c = chapters[idx];
  const local = Number.isFinite(localSec) ? Math.max(0, Math.min(c.durationSec, localSec)) : 0;
  return c.startSec + local;
}

// Pick the chapter segments that overlap a global time window. Used by
// the cross-chapter clip export to iterate "open chapter N from local A
// to local B" pairs.
//
// Returns [{
//   file, chapterIndex,
//   localStartSec,        // sec inside this chapter file
//   localEndSec,
//   globalStartSec,       // sec in the virtual timeline
//   globalEndSec,
// }, ...] — possibly empty if [startGlobalSec, endGlobalSec) doesn't
// overlap the timeline.
export function selectChaptersForClip(timeline, startGlobalSec, endGlobalSec) {
  if (!timeline || !Array.isArray(timeline.chapters)) return [];
  if (!Number.isFinite(startGlobalSec) || !Number.isFinite(endGlobalSec)) return [];
  if (endGlobalSec <= startGlobalSec) return [];
  const segs = [];
  for (const c of timeline.chapters) {
    const segStart = Math.max(c.startSec, startGlobalSec);
    const segEnd   = Math.min(c.endSec,   endGlobalSec);
    if (segEnd <= segStart) continue;
    segs.push({
      file:           c.file,
      chapterIndex:   c.chapterIndex,
      localStartSec:  segStart - c.startSec,
      localEndSec:    segEnd   - c.startSec,
      globalStartSec: segStart,
      globalEndSec:   segEnd,
    });
  }
  return segs;
}

// Friendly status-line text for the toolbar.
//   ("foo.mp4")                 — single chapter (or non-GoPro pick)
//   ("foo.mp4 + 3 chapters")    — multi-chapter cluster, 4 files total
//   (with optional " (51m 23s)" suffix when total duration is known)
export function describeChapterPick(timeline, primaryName) {
  if (!timeline || !Array.isArray(timeline.chapters) || timeline.chapters.length === 0) {
    return primaryName || '';
  }
  const n = timeline.chapters.length;
  const head = primaryName || (timeline.chapters[0].file?.name || '');
  if (n <= 1) return head;
  const extra = n - 1;
  let label = `${head} + ${extra} chapter${extra === 1 ? '' : 's'}`;
  if (Number.isFinite(timeline.totalDurationSec) && timeline.totalDurationSec > 0) {
    label += ` (${formatDurationShort(timeline.totalDurationSec)})`;
  }
  return label;
}

// Compact H:MM:SS / M:SS for the toolbar status line.
function formatDurationShort(sec) {
  const total = Math.floor(sec);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  if (h > 0) return `${h}h ${m}m ${s}s`;
  return `${m}m ${s}s`;
}
