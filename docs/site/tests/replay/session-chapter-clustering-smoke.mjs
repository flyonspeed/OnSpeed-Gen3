// session-chapter-clustering-smoke.mjs — Node smoke test for the
// GoPro chapter clustering layered on top of folder discovery.
//
// Covers:
//   - clusterVideoChapters scopes to a single parent directory
//   - pickVideoSlot returns a multi-chapter envelope when a cluster of
//     >= 2 chapters is detected
//   - pickVideoSlot falls back to most-recent single-file row when no
//     cluster is detected (single video, non-GoPro, etc.)
//   - The largest cluster wins when chapters are split across subdirs
//   - switchSession (folder intent) routes a multi-chapter envelope
//     through ops.mountVideo with kind === 'multi-chapter'
//   - Sidecar resume scenario: a folder with 4 chapters discovered
//     after a sidecar pointed at the first chapter still mounts the
//     full envelope (option A in PR description)
//
// Run:
//   node docs/site/tests/replay/session-chapter-clustering-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

globalThis.indexedDB = globalThis.indexedDB || {
  open: () => ({ onsuccess: null, onerror: null, onupgradeneeded: null }),
};
globalThis.window = globalThis.window || {};

const sessionPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/session.js'
);
const {
  switchSession, clusterVideoChapters, pickVideoSlot,
} = await import(sessionPath);

// ---------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------

let passed = 0;
let failed = 0;
const results = [];

function test(name, fn) {
  try {
    const r = fn();
    if (r && typeof r.then === 'function') {
      return r.then(() => { passed++; results.push(['PASS', name]); })
              .catch(e => {
                failed++;
                results.push(['FAIL', `${name}: ${e.message}`]);
              });
    }
    passed++; results.push(['PASS', name]);
  } catch (e) {
    failed++;
    results.push(['FAIL', `${name}: ${e.message}`]);
  }
}

function assertEqual(a, b, msg = '') {
  if (a !== b) {
    throw new Error(
      `expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}${msg ? ' — ' + msg : ''}`);
  }
}
function assertTrue(cond, msg) {
  if (!cond) throw new Error(msg || 'assertTrue failed');
}

// ---------------------------------------------------------------------
// Stubs
// ---------------------------------------------------------------------

function fakeFile(name, lastModified = 1_000_000) {
  return { name, size: 100, lastModified };
}

function fakeFileHandle(name, lastModified = 1_000_000) {
  return {
    kind: 'file',
    name,
    async getFile() { return fakeFile(name, lastModified); },
  };
}

// Subfolder-capable directory handle: entries() yields names+entries,
// getDirectoryHandle resolves a child directory by name.
function fakeTreeHandle(name, children) {
  const byName = new Map(children);
  return {
    kind: 'directory',
    name,
    async *entries() {
      for (const [n, c] of children) yield [n, c];
    },
    async getDirectoryHandle(n) {
      const c = byName.get(n);
      if (!c || c.kind !== 'directory') throw new Error('not a dir: ' + n);
      return c;
    },
    async getFileHandle(n) {
      const c = byName.get(n);
      if (!c || c.kind !== 'file') throw new Error('not a file: ' + n);
      return c;
    },
  };
}

// Build a discovery-row-shaped object the way sidecar.js's
// discoverFlightContentsRecursive does.
function videoRow(name, relativePath, lastModified = 1_000_000) {
  return {
    name,
    relativePath,
    handle: fakeFileHandle(name, lastModified),
    file: fakeFile(name, lastModified),
  };
}

// Build the ops bundle as the real React layer does, recording all calls
// including any multi-chapter envelopes routed through mountVideo.
function makeOps() {
  const calls = {
    tearDown: 0,
    setSource: [],
    mountLog: [],
    mountVideo: [],
    mountConfig: [],
    errors: [],
  };
  const ops = {
    tearDown: () => { calls.tearDown++; },
    setSource: (tag, dir) => { calls.setSource.push({ tag }); },
    mountLog: async (file, handle, rel) => {
      calls.mountLog.push({ name: file.name, rel });
    },
    mountVideo: async (fileOrEnv, handle, rel) => {
      // Mirror the React layer's branch: multi-chapter envelope records
      // the chapter array; single-file records the name.
      if (fileOrEnv && fileOrEnv.kind === 'multi-chapter') {
        calls.mountVideo.push({
          kind: 'multi-chapter',
          rel,
          chapterNames: fileOrEnv.chapters.map(c => c.file.name),
          directoryName: fileOrEnv.directoryHandle?.name || null,
        });
      } else {
        calls.mountVideo.push({ kind: 'single', name: fileOrEnv.name, rel });
      }
    },
    mountConfig: async (file, handle, rel) => {
      calls.mountConfig.push({ name: file.name, rel });
    },
    loadSidecar: async () => {},
    showPicker: () => {},
    reportError: (m) => { calls.errors.push(m); },
  };
  return { ops, calls };
}

// ---------------------------------------------------------------------
// clusterVideoChapters — pure helper tests.
// ---------------------------------------------------------------------

await test('clusterVideoChapters finds a 4-chapter GoPro cluster in a flat list', () => {
  const videos = [
    videoRow('GOPR0314.MP4', 'GOPR0314.MP4'),
    videoRow('GP010314.MP4', 'GP010314.MP4'),
    videoRow('GP020314.MP4', 'GP020314.MP4'),
    videoRow('GP030314.MP4', 'GP030314.MP4'),
  ];
  const cluster = clusterVideoChapters(videos);
  assertTrue(cluster !== null, 'cluster detected');
  assertEqual(cluster.chapters.length, 4);
  assertEqual(cluster.chapters[0].chapterIndex, 0);
  assertEqual(cluster.chapters[0].name, 'GOPR0314.MP4');
  assertEqual(cluster.chapters[3].chapterIndex, 3);
  assertEqual(cluster.chapters[3].name, 'GP030314.MP4');
  assertEqual(cluster.parentRelativePath, '');
});

await test('clusterVideoChapters returns null on single non-GoPro video', () => {
  const videos = [
    videoRow('flight_takeoff.mp4', 'flight_takeoff.mp4'),
  ];
  const cluster = clusterVideoChapters(videos);
  assertEqual(cluster, null, 'no cluster for non-GoPro single video');
});

await test('clusterVideoChapters returns null on single GoPro chapter (no siblings)', () => {
  const videos = [
    videoRow('GOPR0314.MP4', 'GOPR0314.MP4'),
  ];
  const cluster = clusterVideoChapters(videos);
  assertEqual(cluster, null, 'one chapter is not a cluster (>=2 required)');
});

await test('clusterVideoChapters scopes cluster to parent directory', () => {
  // Two chapters in Cockpit/, two in Tail/. Cluster should pick the
  // larger parent's group, not merge across directories.
  const videos = [
    videoRow('GOPR0314.MP4', 'Cockpit/GOPR0314.MP4'),
    videoRow('GP010314.MP4', 'Cockpit/GP010314.MP4'),
    videoRow('GOPR0500.MP4', 'Tail/GOPR0500.MP4'),
    videoRow('GP010500.MP4', 'Tail/GP010500.MP4'),
    videoRow('GP020500.MP4', 'Tail/GP020500.MP4'),
  ];
  const cluster = clusterVideoChapters(videos);
  assertTrue(cluster !== null);
  assertEqual(cluster.chapters.length, 3, 'larger Tail/ cluster wins');
  assertEqual(cluster.parentRelativePath, 'Tail');
  // Names belong to the Tail/ recording.
  assertEqual(cluster.chapters[0].name, 'GOPR0500.MP4');
});

await test('clusterVideoChapters carries relativePath onto each chapter', () => {
  const videos = [
    videoRow('GOPR0314.MP4', '11 May 26 Raw Video/GOPR0314.MP4'),
    videoRow('GP010314.MP4', '11 May 26 Raw Video/GP010314.MP4'),
    videoRow('GP020314.MP4', '11 May 26 Raw Video/GP020314.MP4'),
    videoRow('GP030314.MP4', '11 May 26 Raw Video/GP030314.MP4'),
  ];
  const cluster = clusterVideoChapters(videos);
  assertEqual(cluster.parentRelativePath, '11 May 26 Raw Video');
  assertEqual(
    cluster.chapters[0].relativePath, '11 May 26 Raw Video/GOPR0314.MP4');
  assertEqual(
    cluster.chapters[3].relativePath, '11 May 26 Raw Video/GP030314.MP4');
});

await test('clusterVideoChapters handles unsorted input (sorts by chapterIndex)', () => {
  // groupChapterSiblings sorts internally; this guards against future
  // refactors regressing the contract.
  const videos = [
    videoRow('GP020314.MP4', 'GP020314.MP4'),
    videoRow('GP030314.MP4', 'GP030314.MP4'),
    videoRow('GOPR0314.MP4', 'GOPR0314.MP4'),
    videoRow('GP010314.MP4', 'GP010314.MP4'),
  ];
  const cluster = clusterVideoChapters(videos);
  assertEqual(cluster.chapters.map(c => c.chapterIndex).join(','), '0,1,2,3');
});

// ---------------------------------------------------------------------
// pickVideoSlot — top-level chooser exercised by _applyMatch.
// ---------------------------------------------------------------------

await test('pickVideoSlot returns multi-chapter envelope when cluster found', async () => {
  const videos = [
    videoRow('GOPR0314.MP4', 'Raw/GOPR0314.MP4'),
    videoRow('GP010314.MP4', 'Raw/GP010314.MP4'),
    videoRow('GP020314.MP4', 'Raw/GP020314.MP4'),
    videoRow('GP030314.MP4', 'Raw/GP030314.MP4'),
  ];
  // dirHandle exposes Raw/ as a child dir.
  const rawDir = fakeTreeHandle('Raw', []);
  const root = fakeTreeHandle('flight', [['Raw', rawDir]]);
  const slot = await pickVideoSlot(videos, root);
  assertEqual(slot.kind, 'multi-chapter');
  assertEqual(slot.chapters.length, 4);
  assertEqual(slot.directoryHandle, rawDir, 'directoryHandle resolves to Raw/');
  assertEqual(slot.name, 'GOPR0314.MP4', 'envelope names the first chapter');
  assertEqual(slot.relativePath, 'Raw/GOPR0314.MP4');
});

await test('pickVideoSlot falls back to most-recent single row when no cluster', async () => {
  const videos = [
    videoRow('takeoff.mp4', 'takeoff.mp4', 1_000_000),
    videoRow('downwind.mp4', 'downwind.mp4', 2_000_000),
  ];
  const slot = await pickVideoSlot(videos, fakeTreeHandle('flight', []));
  assertTrue(slot !== null);
  assertEqual(slot.kind, undefined, 'not a multi-chapter envelope');
  assertEqual(slot.name, 'downwind.mp4', 'most recent wins');
});

await test('pickVideoSlot returns null for empty video list', async () => {
  const slot = await pickVideoSlot([], fakeTreeHandle('flight', []));
  assertEqual(slot, null);
});

await test('pickVideoSlot survives missing parent dir resolution', async () => {
  // Cluster lives under "Missing/" but the root doesn't expose it.
  const videos = [
    videoRow('GOPR0314.MP4', 'Missing/GOPR0314.MP4'),
    videoRow('GP010314.MP4', 'Missing/GP010314.MP4'),
  ];
  const slot = await pickVideoSlot(videos, fakeTreeHandle('flight', []));
  assertEqual(slot.kind, 'multi-chapter');
  // directoryHandle is null because Missing/ doesn't exist in the
  // root, but the envelope still ships — applyVideoFiles can still
  // build the timeline from the chapter files.
  assertEqual(slot.directoryHandle, null);
});

// ---------------------------------------------------------------------
// switchSession + folder intent — end-to-end (4-chapter scenario).
// ---------------------------------------------------------------------

await test('folder intent with 4 GoPro chapters mounts a multi-chapter video envelope', async () => {
  const { ops, calls } = makeOps();
  // Mirrors Vac's 2026-05-11 folder:
  //   log_007.csv at root
  //   11 May 26 Raw Video/ holds the 4 chapters
  const log = fakeFileHandle('log_007.csv');
  const rawVid = fakeTreeHandle('11 May 26 Raw Video', [
    ['GOPR0314.MP4', fakeFileHandle('GOPR0314.MP4', 1_000_000)],
    ['GP010314.MP4', fakeFileHandle('GP010314.MP4', 2_000_000)],
    ['GP020314.MP4', fakeFileHandle('GP020314.MP4', 3_000_000)],
    ['GP030314.MP4', fakeFileHandle('GP030314.MP4', 4_000_000)],
  ]);
  const root = fakeTreeHandle('2026-05-11', [
    ['log_007.csv', log],
    ['11 May 26 Raw Video', rawVid],
  ]);
  const r = await switchSession({ kind: 'folder', dirHandle: root }, ops);
  assertEqual(r.kind, 'auto-load');
  assertEqual(calls.mountLog.length, 1, 'log mounted');
  assertEqual(calls.mountVideo.length, 1, 'video mounted once');
  const v = calls.mountVideo[0];
  assertEqual(v.kind, 'multi-chapter');
  assertEqual(v.chapterNames.length, 4);
  assertEqual(v.chapterNames[0], 'GOPR0314.MP4');
  assertEqual(v.chapterNames[3], 'GP030314.MP4');
  assertEqual(v.directoryName, '11 May 26 Raw Video');
  assertEqual(v.rel, '11 May 26 Raw Video/GOPR0314.MP4');
});

await test('folder intent with a single non-GoPro video preserves single-file mount', async () => {
  const { ops, calls } = makeOps();
  const root = fakeTreeHandle('flight', [
    ['log_007.csv', fakeFileHandle('log_007.csv')],
    ['scratch.mp4', fakeFileHandle('scratch.mp4', 5_000_000)],
  ]);
  const r = await switchSession({ kind: 'folder', dirHandle: root }, ops);
  assertEqual(r.kind, 'auto-load');
  assertEqual(calls.mountVideo.length, 1);
  assertEqual(calls.mountVideo[0].kind, 'single');
  assertEqual(calls.mountVideo[0].name, 'scratch.mp4');
});

await test('folder intent with 4 chapters in one subdir + 2 in another picks the larger cluster', async () => {
  const { ops, calls } = makeOps();
  const cockpit = fakeTreeHandle('Cockpit', [
    ['GOPR0500.MP4', fakeFileHandle('GOPR0500.MP4')],
    ['GP010500.MP4', fakeFileHandle('GP010500.MP4')],
  ]);
  const raw = fakeTreeHandle('Raw Video', [
    ['GOPR0314.MP4', fakeFileHandle('GOPR0314.MP4')],
    ['GP010314.MP4', fakeFileHandle('GP010314.MP4')],
    ['GP020314.MP4', fakeFileHandle('GP020314.MP4')],
    ['GP030314.MP4', fakeFileHandle('GP030314.MP4')],
  ]);
  const root = fakeTreeHandle('flight', [
    ['log_007.csv', fakeFileHandle('log_007.csv')],
    ['Cockpit', cockpit],
    ['Raw Video', raw],
  ]);
  await switchSession({ kind: 'folder', dirHandle: root }, ops);
  assertEqual(calls.mountVideo.length, 1);
  const v = calls.mountVideo[0];
  assertEqual(v.kind, 'multi-chapter');
  assertEqual(v.chapterNames.length, 4, 'larger Raw Video/ cluster wins');
  assertEqual(v.directoryName, 'Raw Video');
});

// ---------------------------------------------------------------------
// Sidecar resume scenario — option A in the PR description.
//
// The sidecar records ONLY the first chapter (GOPR0314.MP4). On resume,
// the folder is re-walked and re-clustered. This test stands in for the
// resume path: a `restore` intent on the same 4-chapter folder must
// still surface the full multi-chapter envelope.
// ---------------------------------------------------------------------

await test('restore intent re-clusters chapters (sidecar option A)', async () => {
  const { ops, calls } = makeOps();
  const rawVid = fakeTreeHandle('Raw Video', [
    ['GOPR0314.MP4', fakeFileHandle('GOPR0314.MP4')],
    ['GP010314.MP4', fakeFileHandle('GP010314.MP4')],
    ['GP020314.MP4', fakeFileHandle('GP020314.MP4')],
    ['GP030314.MP4', fakeFileHandle('GP030314.MP4')],
  ]);
  const root = fakeTreeHandle('2026-05-11', [
    ['log_007.csv', fakeFileHandle('log_007.csv')],
    ['log_007.csv.replay.json',
      fakeFileHandle('log_007.csv.replay.json')],
    ['Raw Video', rawVid],
  ]);
  const r = await switchSession({ kind: 'restore', dirHandle: root }, ops);
  assertEqual(r.kind, 'auto-load');
  assertEqual(calls.mountVideo.length, 1);
  assertEqual(calls.mountVideo[0].kind, 'multi-chapter');
  assertEqual(calls.mountVideo[0].chapterNames.length, 4);
});

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
