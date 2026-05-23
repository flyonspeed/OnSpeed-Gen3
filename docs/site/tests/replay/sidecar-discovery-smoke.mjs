// sidecar-discovery-smoke.mjs — Node smoke test for
// discoverFlightContents + findSidecarHandle.
//
// Uses an in-memory stub directory handle that mimics
// FileSystemDirectoryHandle's `entries()` async iterator. Verifies the
// classifier ignores dotfiles, picks up logs/configs/videos/sidecars
// by extension, and that findSidecarHandle case-insensitively matches.
//
// Run:
//   node docs/site/tests/replay/sidecar-discovery-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// Same stubs as the write smoke — the FSA helpers need indexedDB and
// window to be defined, but we don't exercise IDB here.
globalThis.indexedDB = globalThis.indexedDB || {
  open: () => ({ onsuccess: null, onerror: null, onupgradeneeded: null }),
};
globalThis.window = globalThis.window || {};

const sidecarPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/sidecar.js'
);
const mod = await import(sidecarPath);
const {
  discoverFlightContents, discoverFlightContentsRecursive,
  findSidecarHandle, findDiscoveryRowFor,
  resolveRelativePath, computeRelativePath,
} = mod;

// ---------------------------------------------------------------------
// Stubs
// ---------------------------------------------------------------------

function makeFileEntry(name, { sizeBytes = 100, fail = false } = {}) {
  return {
    kind: 'file',
    name,
    async getFile() {
      if (fail) throw new Error('simulated cloud-only file');
      return { name, size: sizeBytes, lastModified: Date.now() };
    },
  };
}

function makeDirEntry(name) {
  return { kind: 'directory', name };
}

function makeDirHandle(entries) {
  return {
    kind: 'directory',
    async *entries() {
      for (const [name, entry] of entries) yield [name, entry];
    },
  };
}

// Subfolder-aware stub. Entries can be files or sub-directory handles
// (which expose their own `entries()` + getDirectoryHandle/getFileHandle).
function makeTreeHandle(name, children) {
  // children: array of [name, entryOrSubtree]; if entryOrSubtree is a
  // makeTreeHandle, it becomes a directory child.
  const byName = new Map(children);
  return {
    kind: 'directory',
    name,
    async *entries() {
      for (const [n, c] of children) yield [n, c];
    },
    async getDirectoryHandle(n) {
      const c = byName.get(n);
      if (!c || c.kind !== 'directory') throw new Error('not a directory: ' + n);
      return c;
    },
    async getFileHandle(n) {
      const c = byName.get(n);
      if (!c || c.kind !== 'file') throw new Error('not a file: ' + n);
      return c;
    },
    async resolve(target) {
      // Walk the tree breadth-first for the target reference. Returns
      // segment array on hit, null on miss.
      const queue = [{ handle: this, prefix: [] }];
      while (queue.length) {
        const { handle, prefix } = queue.shift();
        for await (const [n, c] of handle.entries()) {
          if (c === target) return [...prefix, n];
          if (c.kind === 'directory') queue.push({ handle: c, prefix: [...prefix, n] });
        }
      }
      return null;
    },
  };
}

let passed = 0;
let failed = 0;
const results = [];

function test(name, fn) {
  try {
    const r = fn();
    if (r && typeof r.then === 'function') {
      return r.then(
        () => { passed++; results.push(['PASS', name]); },
        (e) => { failed++; results.push(['FAIL', `${name}: ${e.message}`]); },
      );
    }
    passed++;
    results.push(['PASS', name]);
  } catch (e) {
    failed++;
    results.push(['FAIL', `${name}: ${e.message}`]);
  }
}

function assertEqual(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(`${msg || 'assertion failed'}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

function assertTrue(cond, msg) {
  if (!cond) throw new Error(msg || 'assertTrue failed');
}

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

await test('discoverFlightContents classifies a representative flight folder', async () => {
  // Mirrors Vac's 11 May 26 Cockpit folder shape, simplified:
  //   log_007.csv, log_007_fixed.csv, log_007_fixed_3.csv,
  //   log_007_fixed_3.csv.replay.json,
  //   onspeed2.cfg, boot_log.txt, .DS_Store
  const dir = makeDirHandle([
    ['log_007.csv', makeFileEntry('log_007.csv')],
    ['log_007_fixed.csv', makeFileEntry('log_007_fixed.csv')],
    ['log_007_fixed_3.csv', makeFileEntry('log_007_fixed_3.csv')],
    ['log_007_fixed_3.csv.replay.json',
      makeFileEntry('log_007_fixed_3.csv.replay.json')],
    ['onspeed2.cfg', makeFileEntry('onspeed2.cfg')],
    ['boot_log.txt', makeFileEntry('boot_log.txt')],
    ['.DS_Store', makeFileEntry('.DS_Store')],
  ]);
  const r = await discoverFlightContents(dir);
  assertEqual(r.logs.length, 3, 'three .csv files surfaced');
  assertEqual(r.configs.length, 1, 'one .cfg file surfaced');
  assertEqual(r.videos.length, 0);
  assertEqual(r.sidecars.length, 1, 'one .replay.json file surfaced');
  assertEqual(r.sidecars[0].name, 'log_007_fixed_3.csv.replay.json');
});

await test('discoverFlightContents ignores subdirectories (no recursion in Phase 1)', async () => {
  const dir = makeDirHandle([
    ['log_007.csv', makeFileEntry('log_007.csv')],
    ['Raw Video', makeDirEntry('Raw Video')],
    ['Sub Folder', makeDirEntry('Sub Folder')],
  ]);
  const r = await discoverFlightContents(dir);
  assertEqual(r.logs.length, 1);
  assertEqual(r.videos.length, 0, 'directory not classified as video');
});

await test('discoverFlightContents tolerates files whose getFile rejects', async () => {
  const dir = makeDirHandle([
    ['log_007.csv', makeFileEntry('log_007.csv', { fail: true })],
    ['ok.csv', makeFileEntry('ok.csv')],
  ]);
  const r = await discoverFlightContents(dir);
  assertEqual(r.logs.length, 2, 'both logs surface even if one is cloud-only');
  const failed = r.logs.find(l => l.name === 'log_007.csv');
  assertEqual(failed.file, null, 'failed-getFile log carries file=null');
});

await test('discoverFlightContents picks up video extensions', async () => {
  const dir = makeDirHandle([
    ['GOPR0314.MP4', makeFileEntry('GOPR0314.MP4')],
    ['GP010314.mp4', makeFileEntry('GP010314.mp4')],
    ['scratch.mov', makeFileEntry('scratch.mov')],
    ['weird.webm', makeFileEntry('weird.webm')],
  ]);
  const r = await discoverFlightContents(dir);
  assertEqual(r.videos.length, 4);
});

await test('discoverFlightContents ignores .cfg.bak and dotfiles', async () => {
  const dir = makeDirHandle([
    ['onspeed.cfg', makeFileEntry('onspeed.cfg')],
    ['onspeed.cfg.bak', makeFileEntry('onspeed.cfg.bak')],
    ['.hidden', makeFileEntry('.hidden')],
  ]);
  const r = await discoverFlightContents(dir);
  assertEqual(r.configs.length, 1);
  assertEqual(r.configs[0].name, 'onspeed.cfg');
});

await test('discoverFlightContents picks up XML configs', async () => {
  const dir = makeDirHandle([
    ['onspeed.xml', makeFileEntry('onspeed.xml')],
  ]);
  const r = await discoverFlightContents(dir);
  assertEqual(r.configs.length, 1);
  assertEqual(r.configs[0].name, 'onspeed.xml');
});

await test('discoverFlightContents returns empty on null handle', async () => {
  const r = await discoverFlightContents(null);
  assertEqual(r.logs.length, 0);
  assertEqual(r.configs.length, 0);
  assertEqual(r.videos.length, 0);
  assertEqual(r.sidecars.length, 0);
});

await test('findSidecarHandle picks the right sidecar by log filename', async () => {
  const dir = makeDirHandle([
    ['log_007.csv', makeFileEntry('log_007.csv')],
    ['log_007_fixed.csv', makeFileEntry('log_007_fixed.csv')],
    ['log_007.csv.replay.json', makeFileEntry('log_007.csv.replay.json')],
    ['log_007_fixed.csv.replay.json',
      makeFileEntry('log_007_fixed.csv.replay.json')],
  ]);
  const r = await discoverFlightContents(dir);
  const a = findSidecarHandle(r, 'log_007.csv');
  const b = findSidecarHandle(r, 'log_007_fixed.csv');
  assertTrue(a !== null);
  assertTrue(b !== null);
  assertEqual(a.name, 'log_007.csv.replay.json');
  assertEqual(b.name, 'log_007_fixed.csv.replay.json');
});

await test('findSidecarHandle is case-insensitive (APFS-friendly)', async () => {
  const dir = makeDirHandle([
    ['LOG_007.csv', makeFileEntry('LOG_007.csv')],
    ['log_007.csv.replay.json', makeFileEntry('log_007.csv.replay.json')],
  ]);
  const r = await discoverFlightContents(dir);
  const found = findSidecarHandle(r, 'LOG_007.csv');
  assertTrue(found !== null, 'sidecar should match across case');
  assertEqual(found.name, 'log_007.csv.replay.json');
});

await test('findSidecarHandle returns null when no matching sidecar', async () => {
  const dir = makeDirHandle([
    ['log_007.csv', makeFileEntry('log_007.csv')],
    ['log_999.csv.replay.json', makeFileEntry('log_999.csv.replay.json')],
  ]);
  const r = await discoverFlightContents(dir);
  const found = findSidecarHandle(r, 'log_007.csv');
  assertEqual(found, null);
});

// ---------------------------------------------------------------------
// relativePath plumbing — Sam's playtest finding #1.
// ---------------------------------------------------------------------

await test('discoverFlightContents stamps relativePath onto each row', async () => {
  const dir = makeDirHandle([
    ['log_007.csv', makeFileEntry('log_007.csv')],
    ['GOPR0314.MP4', makeFileEntry('GOPR0314.MP4')],
    ['onspeed.cfg', makeFileEntry('onspeed.cfg')],
    ['log_007.csv.replay.json', makeFileEntry('log_007.csv.replay.json')],
  ]);
  const r = await discoverFlightContents(dir);
  assertEqual(r.logs[0].relativePath, 'log_007.csv');
  assertEqual(r.videos[0].relativePath, 'GOPR0314.MP4');
  assertEqual(r.configs[0].relativePath, 'onspeed.cfg');
  assertEqual(r.sidecars[0].relativePath, 'log_007.csv.replay.json');
});

await test('discoverFlightContentsRecursive walks subfolders one level deep', async () => {
  // Mirrors Vac's actual flight folder layout — sidecar at the root,
  // log/cfg under "Cockpit/", video under "Raw Video/".
  const log = makeFileEntry('log_007.csv');
  const cfg = makeFileEntry('onspeed2.cfg');
  const vid = makeFileEntry('GOPR0314.MP4');
  const dir = makeTreeHandle('2026-05-11', [
    ['log_007.csv.replay.json', makeFileEntry('log_007.csv.replay.json')],
    ['11 May 26 Cockpit', makeTreeHandle('11 May 26 Cockpit', [
      ['log_007.csv', log],
      ['onspeed2.cfg', cfg],
      ['boot_log.txt', makeFileEntry('boot_log.txt')],
    ])],
    ['11 May 26 Raw Video', makeTreeHandle('11 May 26 Raw Video', [
      ['GOPR0314.MP4', vid],
    ])],
  ]);
  const r = await discoverFlightContentsRecursive(dir);
  assertEqual(r.logs.length, 1);
  assertEqual(r.videos.length, 1);
  assertEqual(r.configs.length, 1);
  assertEqual(r.sidecars.length, 1);
  assertEqual(r.logs[0].relativePath, '11 May 26 Cockpit/log_007.csv');
  assertEqual(r.videos[0].relativePath, '11 May 26 Raw Video/GOPR0314.MP4');
  assertEqual(r.configs[0].relativePath, '11 May 26 Cockpit/onspeed2.cfg');
  assertEqual(r.sidecars[0].relativePath, 'log_007.csv.replay.json');
});

await test('discoverFlightContentsRecursive respects maxDepth cap', async () => {
  const deepFile = makeFileEntry('deep.csv');
  const dir = makeTreeHandle('root', [
    ['a', makeTreeHandle('a', [
      ['b', makeTreeHandle('b', [
        ['c', makeTreeHandle('c', [
          ['deep.csv', deepFile],
        ])],
      ])],
    ])],
  ]);
  // maxDepth=2: visit root + a + a/b. a/b/c is past the cap.
  const r = await discoverFlightContentsRecursive(dir, 2);
  assertEqual(r.logs.length, 0, 'file four levels deep is past the cap');
  // maxDepth=4 reaches deep.csv.
  const r4 = await discoverFlightContentsRecursive(dir, 4);
  assertEqual(r4.logs.length, 1);
  assertEqual(r4.logs[0].relativePath, 'a/b/c/deep.csv');
});

await test('resolveRelativePath walks segments and returns the leaf handle', async () => {
  const log = makeFileEntry('log_007.csv');
  const dir = makeTreeHandle('root', [
    ['Cockpit', makeTreeHandle('Cockpit', [
      ['log_007.csv', log],
    ])],
  ]);
  const handle = await resolveRelativePath(dir, 'Cockpit/log_007.csv');
  assertTrue(handle === log, 'returns the file handle at the leaf');
});

await test('resolveRelativePath returns null on missing segment', async () => {
  const dir = makeTreeHandle('root', [
    ['Cockpit', makeTreeHandle('Cockpit', [
      ['log_007.csv', makeFileEntry('log_007.csv')],
    ])],
  ]);
  const handle = await resolveRelativePath(dir, 'Cockpit/missing.csv');
  assertEqual(handle, null);
  const handle2 = await resolveRelativePath(dir, 'NoDir/log_007.csv');
  assertEqual(handle2, null);
});

await test('computeRelativePath stitches segments returned by dirHandle.resolve', async () => {
  const log = makeFileEntry('log_007.csv');
  const dir = makeTreeHandle('root', [
    ['Cockpit', makeTreeHandle('Cockpit', [
      ['log_007.csv', log],
    ])],
  ]);
  const p = await computeRelativePath(dir, log);
  assertEqual(p, 'Cockpit/log_007.csv');
});

await test('computeRelativePath falls back to filename when dirHandle.resolve is unavailable', async () => {
  // No-resolve dirHandle (older Chromium): the helper still emits the
  // filename so callers get a non-empty hint they can stash.
  const dir = {
    kind: 'directory',
    async *entries() {},
  };
  const file = { kind: 'file', name: 'bare.csv' };
  const p = await computeRelativePath(dir, file);
  assertEqual(p, 'bare.csv');
});

await test('findDiscoveryRowFor matches on relativePath first', async () => {
  const dir = makeTreeHandle('root', [
    ['11 May 26 Cockpit', makeTreeHandle('11 May 26 Cockpit', [
      ['log_007.csv', makeFileEntry('log_007.csv')],
    ])],
    // A second log with the same basename in a different subfolder —
    // relativePath disambiguates.
    ['Old', makeTreeHandle('Old', [
      ['log_007.csv', makeFileEntry('log_007.csv')],
    ])],
  ]);
  const discovery = await discoverFlightContentsRecursive(dir);
  const subject = {
    name: 'log_007.csv',
    relativePath: '11 May 26 Cockpit/log_007.csv',
  };
  const row = findDiscoveryRowFor(discovery, 'logs', subject);
  assertTrue(row !== null, 'matches by relativePath');
  assertEqual(row.relativePath, '11 May 26 Cockpit/log_007.csv');
});

await test('findDiscoveryRowFor falls back to basename when relativePath is empty', async () => {
  // Older sidecar (or direct-file-pick workflow) — only name is set.
  const dir = makeTreeHandle('root', [
    ['Cockpit', makeTreeHandle('Cockpit', [
      ['log_007.csv', makeFileEntry('log_007.csv')],
    ])],
  ]);
  const discovery = await discoverFlightContentsRecursive(dir);
  const subject = { name: 'log_007.csv', relativePath: '' };
  const row = findDiscoveryRowFor(discovery, 'logs', subject);
  assertTrue(row !== null, 'falls back to basename match');
  assertEqual(row.name, 'log_007.csv');
});

await test('findDiscoveryRowFor returns null when neither path nor name match', async () => {
  const dir = makeDirHandle([
    ['log_007.csv', makeFileEntry('log_007.csv')],
  ]);
  const discovery = await discoverFlightContents(dir);
  const subject = { name: 'something_else.csv', relativePath: 'a/b/c.csv' };
  const row = findDiscoveryRowFor(discovery, 'logs', subject);
  assertEqual(row, null);
});

// Final report
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
