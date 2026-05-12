// fileHandles-smoke.mjs — Node smoke test for the replay fileHandles
// module's pure exports. Covers isFileHandleApiSupported,
// pickerOptionsForSlot, recentFilesSignature, signatureFromFiles,
// and the IDB read/write/clear round-trip with a fake-IDB stub.
//
// The Preact hook (useFileHandleResume) and the showOpenFilePicker
// wrapper are browser-only and not exercised here; their Node-side
// pure helpers are.
//
// Run:
//   node docs/site/tests/replay/fileHandles-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// ---------------------------------------------------------------------
// Fake IndexedDB. Stores by (db, store, key). Enough to back the
// open/transaction/objectStore/put/get/delete shape the module uses.
// ---------------------------------------------------------------------

function makeFakeIndexedDB() {
  const dbs = new Map(); // dbName → { stores: Map(storeName → Map(key, value)) }

  function getOrCreateDb(name) {
    if (!dbs.has(name)) dbs.set(name, { stores: new Map() });
    return dbs.get(name);
  }

  function makeTxRequest(tx, execute) {
    const req = { onsuccess: null, onerror: null, result: undefined, error: null };
    tx._pending++;
    queueMicrotask(() => {
      try {
        req.result = execute();
        if (req.onsuccess) req.onsuccess({ target: req });
      } catch (e) {
        req.error = e;
        if (req.onerror) req.onerror({ target: req });
      }
      tx._pending--;
      // Only fire oncomplete once all pending requests have settled.
      // Re-check on the next microtask in case more requests were
      // queued from within an onsuccess handler.
      queueMicrotask(() => {
        if (tx._pending === 0 && !tx._completed) {
          tx._completed = true;
          if (tx.oncomplete) tx.oncomplete();
        }
      });
    });
    return req;
  }

  function makeObjectStore(tx, storeMap) {
    return {
      put(value, key) { return makeTxRequest(tx, () => { storeMap.set(key, value); return key; }); },
      get(key) { return makeTxRequest(tx, () => storeMap.get(key)); },
      delete(key) { return makeTxRequest(tx, () => { storeMap.delete(key); return undefined; }); },
    };
  }

  return {
    open(dbName, version) {
      const req = { onupgradeneeded: null, onsuccess: null, onerror: null,
                    result: null, error: null };
      queueMicrotask(() => {
        const db = getOrCreateDb(dbName);
        db.version = version;
        const dbObj = {
          objectStoreNames: { contains(name) { return db.stores.has(name); } },
          createObjectStore(name) {
            db.stores.set(name, new Map());
            return { /* placeholder */ };
          },
          transaction(storeName /*, mode */) {
            const tx = { oncomplete: null, onabort: null, onerror: null,
                         error: null,
                         _pending: 0, _completed: false,
                         _opCount: 0 };
            tx.objectStore = () => makeObjectStore(tx, db.stores.get(storeName));
            // Don't auto-complete: production code always issues at
            // least one put/get, and the per-op microtask chain handles
            // oncomplete after the request settles. A real IDB wouldn't
            // fire oncomplete on an empty transaction either.
            return tx;
          },
          close() {},
        };
        req.result = dbObj;
        if (req.onupgradeneeded) req.onupgradeneeded({ target: req });
        if (req.onsuccess) req.onsuccess({ target: req });
      });
      return req;
    },
    _dbs: dbs,
  };
}

const fakeIdb = makeFakeIndexedDB();
globalThis.indexedDB = fakeIdb;

// ---------------------------------------------------------------------
// Globals expected by the module: provide window so the FSA
// feature-detect can run (we leave showOpenFilePicker undefined to
// exercise the "unsupported" branch).
// ---------------------------------------------------------------------

globalThis.window = globalThis.window || {};

// ---------------------------------------------------------------------
// Stub the Preact import so importing the module in Node works without
// pulling in browser-only DOM. We don't exercise the hook or component
// here; only the pure helpers + IDB API.
// ---------------------------------------------------------------------

import { createRequire } from 'node:module';
const require_ = createRequire(import.meta.url);

// Resolve the preact-standalone path the module uses and verify it
// exists. If the import fails at module load, we'll print a clear
// error instead of a cryptic stack.
const preactPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/packages/ui-core/vendor/preact-standalone.js'
);

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

function assertDeepEqual(actual, expected, msg) {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a !== e) {
    throw new Error(`${msg || 'deep-equal failed'}: expected ${e}, got ${a}`);
  }
}

// ---------------------------------------------------------------------
// Import the module under test. Path: docs/site/tests/replay/ → up to
// docs/site/docs/data-and-logs/replay/lib/replay/fileHandles.js.
// ---------------------------------------------------------------------

const fileHandlesPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/fileHandles.js'
);

let fh;
try {
  fh = await import(fileHandlesPath);
} catch (err) {
  console.error(`FAIL: could not import fileHandles.js: ${err.message}`);
  console.error(`      path: ${fileHandlesPath}`);
  process.exit(1);
}

const {
  isFileHandleApiSupported,
  pickerOptionsForSlot,
  recentFilesSignature,
  signatureFromFiles,
  storeHandles,
  loadHandles,
  clearHandles,
  expandMultiChapterHandle,
  requestPermissionForHandles,
} = fh;

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

test('isFileHandleApiSupported returns false when window.showOpenFilePicker is undefined', () => {
  // We set window.showOpenFilePicker = undefined above.
  assertEqual(isFileHandleApiSupported(), false);
});

test('isFileHandleApiSupported returns true when window.showOpenFilePicker is a function', () => {
  globalThis.window.showOpenFilePicker = async () => [];
  try {
    assertEqual(isFileHandleApiSupported(), true);
  } finally {
    delete globalThis.window.showOpenFilePicker;
  }
});

test('pickerOptionsForSlot returns video accept-list for slot "video"', () => {
  const o = pickerOptionsForSlot('video');
  assertEqual(o.multiple, false);
  assertEqual(o.types.length, 1);
  const mime = Object.keys(o.types[0].accept)[0];
  assertEqual(mime, 'video/*');
  // includes .mp4, .mov, .webm
  const exts = o.types[0].accept[mime];
  assertEqual(exts.includes('.mp4'), true);
  assertEqual(exts.includes('.mov'), true);
  assertEqual(exts.includes('.webm'), true);
});

test('pickerOptionsForSlot returns CSV accept-list for slot "log"', () => {
  const o = pickerOptionsForSlot('log');
  const exts = o.types[0].accept[Object.keys(o.types[0].accept)[0]];
  assertEqual(exts.includes('.csv'), true);
});

test('pickerOptionsForSlot returns XML/cfg accept-list for slot "cfg"', () => {
  const o = pickerOptionsForSlot('cfg');
  const exts = o.types[0].accept[Object.keys(o.types[0].accept)[0]];
  assertEqual(exts.includes('.cfg'), true);
  assertEqual(exts.includes('.xml'), true);
});

test('pickerOptionsForSlot throws on unknown slot', () => {
  let threw = false;
  try { pickerOptionsForSlot('bogus'); }
  catch (e) { threw = e.message.includes('unknown slot'); }
  assertEqual(threw, true);
});

test('recentFilesSignature returns empty string when video or log missing', () => {
  assertEqual(recentFilesSignature(null), '');
  assertEqual(recentFilesSignature({}), '');
  assertEqual(recentFilesSignature({ video: { name: 'v' } }), '');
  assertEqual(recentFilesSignature({ log: { name: 'l' } }), '');
});

test('recentFilesSignature returns a stable string when video + log present', () => {
  const info = {
    video: { name: 'v.mp4', size: 100, lastModified: 5 },
    log: { name: 'l.csv', size: 50, lastModified: 6 },
    cfg: null,
  };
  const sig = recentFilesSignature(info);
  assertEqual(typeof sig, 'string');
  if (sig.length === 0) throw new Error('expected non-empty signature');
  // Same input → same output.
  assertEqual(recentFilesSignature(info), sig, 'idempotent');
});

test('recentFilesSignature differs when filenames differ', () => {
  const a = {
    video: { name: 'a.mp4', size: 100, lastModified: 5 },
    log: { name: 'l.csv', size: 50, lastModified: 6 },
  };
  const b = {
    video: { name: 'b.mp4', size: 100, lastModified: 5 },
    log: { name: 'l.csv', size: 50, lastModified: 6 },
  };
  if (recentFilesSignature(a) === recentFilesSignature(b)) {
    throw new Error('expected distinct signatures for distinct filenames');
  }
});

test('signatureFromFiles returns empty when video or log missing', () => {
  assertEqual(signatureFromFiles({}), '');
  assertEqual(signatureFromFiles({ video: { name: 'v', size: 1, lastModified: 1 } }), '');
});

test('signatureFromFiles produces same signature as recentFilesSignature for equivalent metadata', () => {
  const video = { name: 'v.mp4', size: 100, lastModified: 5 };
  const log = { name: 'l.csv', size: 50, lastModified: 6 };
  const cfg = { name: 'c.cfg', size: 20, lastModified: 7 };
  const sigFromFiles = signatureFromFiles({ video, log, cfg });
  const sigFromInfo = recentFilesSignature({ video, log, cfg });
  assertEqual(sigFromFiles, sigFromInfo);
});

test('signatureFromFiles tolerates non-object File inputs', () => {
  assertEqual(signatureFromFiles({ video: 'string', log: 42 }), '');
});

await test('storeHandles + loadHandles round-trip with matching signature', async () => {
  await clearHandles();
  const sig = '{"video":{"name":"v"},"log":{"name":"l"}}';
  const handles = {
    video: { __slot: 'video' },
    log: { __slot: 'log' },
    cfg: { __slot: 'cfg' },
  };
  await storeHandles(sig, handles);
  const loaded = await loadHandles(sig);
  if (!loaded) throw new Error('expected handles to be present');
  assertEqual(loaded.video.__slot, 'video');
  assertEqual(loaded.log.__slot, 'log');
  assertEqual(loaded.cfg.__slot, 'cfg');
});

await test('loadHandles returns null when signature mismatches', async () => {
  await clearHandles();
  await storeHandles('sig-A', { video: { x: 1 }, log: { x: 2 } });
  const loaded = await loadHandles('sig-B');
  assertEqual(loaded, null);
});

await test('loadHandles returns the record when no expectedSignature is provided', async () => {
  await clearHandles();
  await storeHandles('sig-A', { video: { x: 1 }, log: { x: 2 }, cfg: null });
  const loaded = await loadHandles('');
  if (!loaded) throw new Error('expected handles regardless of signature');
  assertEqual(loaded.video.x, 1);
});

await test('clearHandles removes the stored record', async () => {
  await clearHandles();
  await storeHandles('sig-A', { video: { x: 1 }, log: { x: 2 } });
  await clearHandles();
  const loaded = await loadHandles('sig-A');
  assertEqual(loaded, null);
});

await test('storeHandles is a no-op when signature is not a string', async () => {
  await clearHandles();
  await storeHandles(123, { video: { x: 1 }, log: { x: 2 } });
  const loaded = await loadHandles('123');
  // Should be null — store rejected the non-string.
  assertEqual(loaded, null);
});

// ---------------------------------------------------------------------
// Multi-chapter envelope: permission target + directory expansion
// ---------------------------------------------------------------------

// Stub directory + file handles. The shape mimics the FSA spec just
// enough to exercise expandMultiChapterHandle and the
// permissionTarget branch in requestPermissionForHandles.
function makeFakeDirHandle(entries, requestPermissionImpl = null) {
  return {
    kind: 'directory',
    requestPermission: requestPermissionImpl ||
      (() => Promise.resolve('granted')),
    async *entries() {
      for (const [name, entry] of entries) yield [name, entry];
    },
  };
}

function makeFakeFileHandle(name, contents = 'x') {
  return {
    kind: 'file',
    name,
    getFile: () => Promise.resolve(new (class FakeFile {
      constructor() { this.name = name; this.size = contents.length; }
    })()),
  };
}

await test('expandMultiChapterHandle returns ordered files matching chapterNames', async () => {
  // Directory contains 3 chapters + 1 unrelated file. Walk order is
  // deliberately not sorted; expansion should re-order by chapterNames.
  const entries = [
    ['noise.txt',     makeFakeFileHandle('noise.txt')],
    ['GP020314.MP4',  makeFakeFileHandle('GP020314.MP4')],
    ['GOPR0314.MP4',  makeFakeFileHandle('GOPR0314.MP4')],
    ['GP010314.MP4',  makeFakeFileHandle('GP010314.MP4')],
  ];
  const dir = makeFakeDirHandle(entries);
  const envelope = {
    kind: 'multi-chapter',
    directoryHandle: dir,
    chapterNames: ['GOPR0314.MP4', 'GP010314.MP4', 'GP020314.MP4'],
  };
  const expanded = await expandMultiChapterHandle(envelope);
  if (!expanded) throw new Error('expected expansion result');
  assertEqual(expanded.files.length, 3);
  assertEqual(expanded.files[0].name, 'GOPR0314.MP4');
  assertEqual(expanded.files[1].name, 'GP010314.MP4');
  assertEqual(expanded.files[2].name, 'GP020314.MP4');
  assertEqual(expanded.handles.length, 3);
  assertEqual(expanded.directoryHandle, dir);
});

await test('expandMultiChapterHandle drops missing chapters silently', async () => {
  // Directory only has chapter 0 + chapter 2 (chapter 1 was deleted).
  const entries = [
    ['GOPR0314.MP4',  makeFakeFileHandle('GOPR0314.MP4')],
    ['GP020314.MP4',  makeFakeFileHandle('GP020314.MP4')],
  ];
  const dir = makeFakeDirHandle(entries);
  const envelope = {
    kind: 'multi-chapter',
    directoryHandle: dir,
    chapterNames: ['GOPR0314.MP4', 'GP010314.MP4', 'GP020314.MP4'],
  };
  const expanded = await expandMultiChapterHandle(envelope);
  if (!expanded) throw new Error('expected expansion result');
  assertEqual(expanded.files.length, 2);
  assertEqual(expanded.files[0].name, 'GOPR0314.MP4');
  assertEqual(expanded.files[1].name, 'GP020314.MP4');
});

await test('expandMultiChapterHandle returns null for non-envelope input', async () => {
  assertEqual(await expandMultiChapterHandle(null), null);
  assertEqual(await expandMultiChapterHandle({ kind: 'single' }), null);
  assertEqual(await expandMultiChapterHandle({ kind: 'multi-chapter' }), null);
});

await test('requestPermissionForHandles uses directoryHandle for multi-chapter envelope', async () => {
  let dirAsked = 0;
  let logAsked = 0;
  const dir = makeFakeDirHandle([], () => { dirAsked++; return Promise.resolve('granted'); });
  const log = {
    requestPermission: () => { logAsked++; return Promise.resolve('granted'); },
  };
  const envelope = {
    kind: 'multi-chapter',
    directoryHandle: dir,
    chapterNames: ['a.mp4', 'b.mp4'],
  };
  const granted = await requestPermissionForHandles({
    video: envelope,
    log,
    cfg: null,
  });
  assertEqual(granted, true);
  // The directory got asked once, NOT once per chapter.
  assertEqual(dirAsked, 1);
  assertEqual(logAsked, 1);
});

await test('requestPermissionForHandles falls back to single video handle when not envelope', async () => {
  let vidAsked = 0;
  const vid = {
    requestPermission: () => { vidAsked++; return Promise.resolve('granted'); },
  };
  const log = { requestPermission: () => Promise.resolve('granted') };
  const granted = await requestPermissionForHandles({ video: vid, log, cfg: null });
  assertEqual(granted, true);
  assertEqual(vidAsked, 1);
});

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------

// Drain microtasks so async fake-IDB callbacks finish.
await new Promise(r => setTimeout(r, 10));

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
