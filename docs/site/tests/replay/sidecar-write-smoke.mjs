// sidecar-write-smoke.mjs — Node smoke test for writeSidecar.
// Verifies atomic-write semantics, revision/updatedAt bumps, and the
// useSidecar lifecycle (no Preact import — we exercise writeSidecar
// directly, which is the load-bearing path).
//
// Run:
//   node docs/site/tests/replay/sidecar-write-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// Sidecar.js imports Preact for the hook + banner. We stub the
// indexedDB and window globals so the FSA helpers don't crash, and
// rely on Node 22's native fetch-style import resolution to pull in
// the actual preact-standalone module.
globalThis.indexedDB = globalThis.indexedDB || {
  open: () => {
    const req = { onsuccess: null, onerror: null, onupgradeneeded: null, result: null };
    queueMicrotask(() => {
      req.result = {
        objectStoreNames: { contains: () => true },
        createObjectStore: () => ({}),
        transaction: () => ({
          objectStore: () => ({ put: () => ({ onsuccess: null }), get: () => ({ onsuccess: null }) }),
          oncomplete: null, onabort: null, onerror: null,
        }),
        close: () => {},
      };
      if (req.onsuccess) req.onsuccess();
    });
    return req;
  },
};
globalThis.window = globalThis.window || {};

const sidecarPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/sidecar.js'
);

let mod;
try {
  mod = await import(sidecarPath);
} catch (err) {
  console.error(`FAIL: could not import sidecar.js: ${err.message}`);
  console.error(`      path: ${sidecarPath}`);
  process.exit(1);
}

const {
  writeSidecar, readSidecarFromHandle,
  discoverFlightContents, findSidecarHandle,
  mutateSidecar, flushSidecar,
} = mod;

const schemaMod = await import(path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/sidecar-schema.js'
));
const { emptySession, parseSidecar } = schemaMod;

// ---------------------------------------------------------------------
// In-memory writable file handle. Mirrors FileSystemFileHandle's
// .getFile() / .createWritable() surface.
// ---------------------------------------------------------------------

function makeMemoryFileHandle(name, initial = '') {
  let contents = initial;
  let openWrites = 0;
  return {
    kind: 'file',
    name,
    _read() { return contents; },
    _openWrites() { return openWrites; },
    async getFile() {
      return {
        name,
        size: contents.length,
        async text() { return contents; },
        async arrayBuffer() {
          const buf = new ArrayBuffer(contents.length);
          const view = new Uint8Array(buf);
          for (let i = 0; i < contents.length; i++) view[i] = contents.charCodeAt(i) & 0xff;
          return buf;
        },
      };
    },
    async createWritable() {
      openWrites++;
      // Buffer the writes; commit on close (atomic).
      let pending = '';
      return {
        async write(text) {
          if (typeof text === 'string') pending += text;
          else if (text && text.data) pending += text.data;
        },
        async close() {
          contents = pending;
          openWrites--;
        },
        async abort() {
          openWrites--;
        },
      };
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

await test('writeSidecar writes JSON, bumps revision + updatedAt', async () => {
  const handle = makeMemoryFileHandle('log_007.csv.replay.json');
  const doc = emptySession({
    log: { name: 'log_007.csv', hash: 'abc', sizeBytes: 100, rowCount: 50, durationSec: 12 },
  });
  const before = doc.session.updatedAt;
  // Force a tiny delay so updatedAt actually changes.
  await new Promise(r => setTimeout(r, 5));
  const startRev = doc.session.revision;
  const out = await writeSidecar(handle, doc);
  assertEqual(out.session.revision, startRev + 1, 'revision bumps by 1');
  assertTrue(out.session.updatedAt !== before, 'updatedAt is fresh');
  // Verify the file content matches and parses cleanly.
  const stored = handle._read();
  const parsed = parseSidecar(stored);
  assertEqual(parsed.ok, true, parsed.ok ? '' : parsed.error);
  assertEqual(parsed.value.session.revision, startRev + 1);
  assertEqual(parsed.value.subject.log.name, 'log_007.csv');
});

await test('writeSidecar closes the writable stream', async () => {
  const handle = makeMemoryFileHandle('x.csv.replay.json');
  const doc = emptySession({
    log: { name: 'x.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  await writeSidecar(handle, doc);
  assertEqual(handle._openWrites(), 0, 'writable stream closed after write');
});

await test('writeSidecar increments revision on repeat writes', async () => {
  const handle = makeMemoryFileHandle('y.csv.replay.json');
  const doc = emptySession({
    log: { name: 'y.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  await writeSidecar(handle, doc);
  await writeSidecar(handle, doc);
  await writeSidecar(handle, doc);
  assertEqual(doc.session.revision, 4); // started at 1, +3 writes = 4
});

await test('writeSidecar rejects when handle has no createWritable', async () => {
  let threw = false;
  try {
    await writeSidecar({}, emptySession({
      log: { name: 'z.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
    }));
  } catch (e) {
    threw = e.message.includes('createWritable');
  }
  assertEqual(threw, true);
});

await test('writeSidecar rejects when doc is missing session', async () => {
  const handle = makeMemoryFileHandle('w.csv.replay.json');
  let threw = false;
  try {
    await writeSidecar(handle, { schemaVersion: 1 });
  } catch (e) {
    threw = e.message.includes('session');
  }
  assertEqual(threw, true);
});

await test('readSidecarFromHandle round-trips an output of writeSidecar', async () => {
  const handle = makeMemoryFileHandle('rt.csv.replay.json');
  const doc = emptySession({
    log: { name: 'rt.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  doc.marks.push({
    value: 3, logTimeMs: 555, name: 'mark', notes: 'n',
    createdAt: '2026-05-22T10:00:00Z', updatedAt: '2026-05-22T10:01:00Z',
  });
  await writeSidecar(handle, doc);
  const r = await readSidecarFromHandle(handle);
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.marks.length, 1);
  assertEqual(r.value.marks[0].name, 'mark');
});

await test('readSidecarFromHandle returns error on malformed input', async () => {
  const handle = makeMemoryFileHandle('bad.csv.replay.json', '{not json');
  const r = await readSidecarFromHandle(handle);
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('invalid JSON'), r.error);
});

await test('readSidecarFromHandle returns error on no-handle input', async () => {
  const r = await readSidecarFromHandle(null);
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('invalid handle'), r.error);
});

// ---------------------------------------------------------------------
// relativePath round-trip — PR #640 follow-up.
// ---------------------------------------------------------------------

await test('writeSidecar round-trips relativePath for log/config/video', async () => {
  const handle = makeMemoryFileHandle('log_007.csv.replay.json');
  const doc = emptySession({
    log: {
      name: 'log_007.csv',
      relativePath: '11 May 26 Cockpit/log_007.csv',
      hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1,
    },
    config: {
      name: 'onspeed2.cfg',
      relativePath: '11 May 26 Cockpit/onspeed2.cfg',
      hash: '', ahrsAlgorithm: '',
    },
    video: {
      name: 'GOPR0314.MP4',
      relativePath: '11 May 26 Raw Video/GOPR0314.MP4',
      hash: '', durationSec: 0,
    },
  });
  await writeSidecar(handle, doc);
  const r = await readSidecarFromHandle(handle);
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.subject.log.relativePath,
              '11 May 26 Cockpit/log_007.csv');
  assertEqual(r.value.subject.config.relativePath,
              '11 May 26 Cockpit/onspeed2.cfg');
  assertEqual(r.value.subject.video.relativePath,
              '11 May 26 Raw Video/GOPR0314.MP4');
});

// ---------------------------------------------------------------------
// mutateSidecar / flushSidecar — PR 1b additions.
// ---------------------------------------------------------------------

await test('mutateSidecar bumps revision and updatedAt', async () => {
  const doc = emptySession({
    log: { name: 'a.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  const r0 = doc.session.revision;
  const u0 = doc.session.updatedAt;
  // Tiny sleep so the ISO timestamp moves.
  await new Promise(r => setTimeout(r, 5));
  const next = mutateSidecar(doc, { marks: [{ value: 1, logTimeMs: 100 }] });
  assertEqual(next.session.revision, r0 + 1, 'revision bumped');
  if (next.session.updatedAt === u0) {
    throw new Error('expected updatedAt to advance');
  }
  assertEqual(next.marks.length, 1);
  assertEqual(doc.session.revision, r0, 'input doc untouched');
});

await test('mutateSidecar merges subject patches without clobbering', async () => {
  const doc = emptySession({
    log: { name: 'a.csv', hash: 'a', sizeBytes: 1, rowCount: 0, durationSec: 0 },
  });
  const next = mutateSidecar(doc, {
    subject: { log: { rowCount: 50, durationSec: 12 } },
  });
  // Patch fields applied.
  assertEqual(next.subject.log.rowCount, 50);
  assertEqual(next.subject.log.durationSec, 12);
  // Untouched fields preserved.
  assertEqual(next.subject.log.name, 'a.csv');
  assertEqual(next.subject.log.hash, 'a');
});

await test('flushSidecar wraps writeSidecar with the same semantics', async () => {
  const handle = makeMemoryFileHandle('a.csv.replay.json');
  const doc = emptySession({
    log: { name: 'a.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  await flushSidecar(handle, doc);
  const r = await readSidecarFromHandle(handle);
  assertEqual(r.ok, true);
  assertEqual(r.value.subject.log.name, 'a.csv');
});

// Final report
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
