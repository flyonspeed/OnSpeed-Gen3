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
const { discoverFlightContents, findSidecarHandle } = mod;

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

// Final report
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
