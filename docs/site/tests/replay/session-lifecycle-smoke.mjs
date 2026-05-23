// session-lifecycle-smoke.mjs — Node smoke test for switchSession.
//
// Verifies that switchSession is the single mutation point: every
// intent (fresh, folder, file-pick, restore) triggers tearDown first,
// then mounts files via the ops bundle. Folder→folder switches don't
// leak state from the prior call.
//
// Uses pure call tracking via the ops bundle — no Preact, no real FSA.
//
// Run:
//   node docs/site/tests/replay/session-lifecycle-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// session.js indirectly imports sidecar.js, which probes for indexedDB
// + window. Stub them out — we don't exercise IDB here.
globalThis.indexedDB = globalThis.indexedDB || {
  open: () => ({ onsuccess: null, onerror: null, onupgradeneeded: null }),
};
globalThis.window = globalThis.window || {};

const sessionPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/session.js'
);
const { switchSession } = await import(sessionPath);

// ---------------------------------------------------------------------
// Test harness
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
  if (a !== b) throw new Error(`expected ${JSON.stringify(b)}, got ${JSON.stringify(a)}${msg ? ' — ' + msg : ''}`);
}

// ---------------------------------------------------------------------
// Stubs
// ---------------------------------------------------------------------

function fakeFile(name, size = 100) {
  return { name, size, lastModified: Date.now() };
}

function fakeFileHandle(name) {
  return {
    kind: 'file',
    name,
    async getFile() { return fakeFile(name); },
  };
}

// Directory handle that exposes the given entries via entries(). Each
// entry can be a file handle or a directory.
function fakeDirHandle(name, entries = []) {
  return {
    kind: 'directory',
    name,
    async *entries() {
      for (const e of entries) yield [e.name, e];
    },
  };
}

// Build an ops bundle that records every call. `tearDownCount` tracks
// how many tearDowns ran (a counter so re-entry is observable).
function makeOps() {
  const calls = {
    tearDown: 0,
    setSource: [],
    mountLog: [],
    mountVideo: [],
    mountConfig: [],
    loadSidecar: 0,
    showPicker: [],
    errors: [],
  };
  const ops = {
    tearDown: () => { calls.tearDown++; },
    setSource: (tag, dir) => { calls.setSource.push({ tag, dir: dir ? dir.name : null }); },
    mountLog: async (file, handle, rel) => { calls.mountLog.push({ name: file.name, rel }); },
    mountVideo: async (file, handle, rel) => { calls.mountVideo.push({ name: file.name, rel }); },
    mountConfig: async (file, handle, rel) => { calls.mountConfig.push({ name: file.name, rel }); },
    loadSidecar: async () => { calls.loadSidecar++; },
    showPicker: (logs, sidecarMap, onChosen) => {
      calls.showPicker.push({ logs: logs.map(l => l.name), sidecarMap, onChosen });
    },
    reportError: (msg) => { calls.errors.push(msg); },
  };
  return { ops, calls };
}

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

await test('fresh intent calls tearDown + setSource(fresh)', async () => {
  const { ops, calls } = makeOps();
  const r = await switchSession({ kind: 'fresh' }, ops);
  assertEqual(calls.tearDown, 1);
  assertEqual(calls.setSource.length, 1);
  assertEqual(calls.setSource[0].tag, 'fresh');
  assertEqual(calls.setSource[0].dir, null);
  assertEqual(r.kind, 'empty');
  assertEqual(calls.errors.length, 0);
});

await test('folder intent with one log + cfg auto-loads (no picker)', async () => {
  const { ops, calls } = makeOps();
  const dir = fakeDirHandle('Flight 2026-05-11', [
    fakeFileHandle('log_007.csv'),
    fakeFileHandle('onspeed.cfg'),
  ]);
  const r = await switchSession({ kind: 'folder', dirHandle: dir }, ops);
  assertEqual(calls.tearDown, 1);
  assertEqual(calls.setSource[0].tag, 'folder');
  assertEqual(calls.mountLog.length, 1, 'log mounted');
  assertEqual(calls.mountLog[0].name, 'log_007.csv');
  assertEqual(calls.mountConfig.length, 1, 'cfg mounted');
  assertEqual(calls.showPicker.length, 0, 'no picker for single log');
  assertEqual(r.kind, 'auto-load');
});

await test('folder intent with multiple logs surfaces picker', async () => {
  const { ops, calls } = makeOps();
  const dir = fakeDirHandle('Flight 2026-05-11', [
    fakeFileHandle('log_007.csv'),
    fakeFileHandle('log_008.csv'),
  ]);
  const r = await switchSession({ kind: 'folder', dirHandle: dir }, ops);
  assertEqual(calls.tearDown, 1);
  assertEqual(calls.showPicker.length, 1);
  assertEqual(calls.showPicker[0].logs.length, 2);
  // Mounts wait for picker resolution.
  assertEqual(calls.mountLog.length, 0);
  assertEqual(r.kind, 'picker');
});

await test('sequential folder switches tear down fully between them', async () => {
  const { ops, calls } = makeOps();
  const dirA = fakeDirHandle('Folder A', [
    fakeFileHandle('a_log.csv'),
  ]);
  const dirB = fakeDirHandle('Folder B', [
    fakeFileHandle('b_log.csv'),
  ]);
  await switchSession({ kind: 'folder', dirHandle: dirA }, ops);
  await switchSession({ kind: 'folder', dirHandle: dirB }, ops);
  assertEqual(calls.tearDown, 2, 'tearDown ran for each switch');
  assertEqual(calls.mountLog.length, 2);
  assertEqual(calls.mountLog[0].name, 'a_log.csv');
  assertEqual(calls.mountLog[1].name, 'b_log.csv');
  assertEqual(calls.setSource[1].dir, 'Folder B', 'second switch records new folder');
});

await test('file-pick intent tears down and mounts only the chosen slot', async () => {
  const { ops, calls } = makeOps();
  // First load a folder so there's prior state.
  const dir = fakeDirHandle('Flight', [fakeFileHandle('log_007.csv')]);
  await switchSession({ kind: 'folder', dirHandle: dir }, ops);
  // Then pick a standalone log file.
  const f = fakeFile('other_log.csv');
  await switchSession({ kind: 'file-pick', slot: 'log', file: f }, ops);
  assertEqual(calls.tearDown, 2, 'tearDown ran for both switches');
  // setSource called: once for folder, once for file-pick (flips to 'files').
  assertEqual(calls.setSource[1].tag, 'files');
  assertEqual(calls.setSource[1].dir, null, 'dirHandle cleared on file-pick');
  // Two log mounts (one for the folder pass, one for the file-pick).
  assertEqual(calls.mountLog.length, 2);
  assertEqual(calls.mountLog[1].name, 'other_log.csv');
});

await test('restore intent walks the dir handle like a folder pick', async () => {
  const { ops, calls } = makeOps();
  const dir = fakeDirHandle('Flight', [
    fakeFileHandle('log_007.csv'),
  ]);
  const r = await switchSession({ kind: 'restore', dirHandle: dir }, ops);
  assertEqual(calls.tearDown, 1);
  assertEqual(calls.setSource[0].tag, 'restore');
  assertEqual(calls.mountLog.length, 1);
  assertEqual(r.kind, 'auto-load');
});

await test('empty folder reports an error (no logs)', async () => {
  const { ops, calls } = makeOps();
  const dir = fakeDirHandle('Empty', []);
  const r = await switchSession({ kind: 'folder', dirHandle: dir }, ops);
  assertEqual(calls.tearDown, 1);
  assertEqual(calls.errors.length, 1, 'one error reported');
  assertEqual(r.kind, 'empty');
});

await test('orphaned sidecar (no matching log) reports orphan error', async () => {
  const { ops, calls } = makeOps();
  const dir = fakeDirHandle('Stripped', [
    fakeFileHandle('log_007.csv.replay.json'),
  ]);
  const r = await switchSession({ kind: 'folder', dirHandle: dir }, ops);
  assertEqual(r.kind, 'orphan');
  assertEqual(calls.errors.length, 1);
  assertEqual(r.missingLogName, 'log_007.csv');
});

await test('null intent is a no-op', async () => {
  const { ops, calls } = makeOps();
  const r = await switchSession(null, ops);
  assertEqual(r, null);
  assertEqual(calls.tearDown, 0);
});

await test('null ops is a no-op', async () => {
  const r = await switchSession({ kind: 'fresh' }, null);
  assertEqual(r, null);
});

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
