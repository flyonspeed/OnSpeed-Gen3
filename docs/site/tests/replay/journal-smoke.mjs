// journal-smoke.mjs — Node smoke test for replay/journal.js. Covers
// loadJournal, upsertMark, upsertClipAnnotation, loadMarkAnnotations
// against a fake-IDB stub.
//
// Run:
//   node docs/site/tests/replay/journal-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// ---------------------------------------------------------------------
// Fake IndexedDB. Same shape as fileHandles-smoke.mjs but with
// keyPath-based stores: put() reads the key from the value itself, and
// the keyPath property name is supplied at store creation time.
// ---------------------------------------------------------------------

function makeFakeIndexedDB() {
  const dbs = new Map();

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
      queueMicrotask(() => {
        if (tx._pending === 0 && !tx._completed) {
          tx._completed = true;
          if (tx.oncomplete) tx.oncomplete();
        }
      });
    });
    return req;
  }

  function makeObjectStore(tx, storeEntry) {
    const { data, keyPath } = storeEntry;
    return {
      put(value, key) {
        return makeTxRequest(tx, () => {
          const k = keyPath != null ? value[keyPath] : key;
          data.set(k, value);
          return k;
        });
      },
      get(key) { return makeTxRequest(tx, () => data.get(key)); },
      delete(key) { return makeTxRequest(tx, () => { data.delete(key); return undefined; }); },
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
          createObjectStore(name, opts) {
            const keyPath = opts && opts.keyPath ? opts.keyPath : null;
            db.stores.set(name, { data: new Map(), keyPath });
            return { /* placeholder */ };
          },
          transaction(storeName /*, mode */) {
            const tx = { oncomplete: null, onabort: null, onerror: null,
                         error: null,
                         _pending: 0, _completed: false };
            tx.objectStore = () => makeObjectStore(tx, db.stores.get(storeName));
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

// ---------------------------------------------------------------------
// Import the module under test.
// ---------------------------------------------------------------------

const journalPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/journal.js'
);

let journal;
try {
  journal = await import(journalPath);
} catch (err) {
  console.error(`FAIL: could not import journal.js: ${err.message}`);
  console.error(`      path: ${journalPath}`);
  process.exit(1);
}

const {
  markKey,
  loadJournal,
  upsertMark,
  upsertClipAnnotation,
  loadMarkAnnotations,
} = journal;

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

test('markKey is value:logTimeMs', () => {
  assertEqual(markKey(20, 935123), '20:935123');
  assertEqual(markKey(0, 0), '0:0');
});

await test('loadJournal returns null for unknown hash', async () => {
  const r = await loadJournal('does-not-exist');
  assertEqual(r, null);
});

await test('loadJournal returns null for empty/invalid hash', async () => {
  assertEqual(await loadJournal(''), null);
  assertEqual(await loadJournal(null), null);
  assertEqual(await loadJournal(42), null);
});

await test('upsertMark creates a record and a mark', async () => {
  await upsertMark('hash-A', { value: 20, logTimeMs: 1000 }, { name: 'Slow flight' });
  const r = await loadJournal('hash-A');
  if (!r) throw new Error('expected record');
  assertEqual(r.logHash, 'hash-A');
  assertEqual(r.marks.length, 1);
  assertEqual(r.marks[0].value, 20);
  assertEqual(r.marks[0].logTimeMs, 1000);
  assertEqual(r.marks[0].name, 'Slow flight');
  if (!Number.isFinite(r.marks[0].createdAt)) throw new Error('expected createdAt');
  if (!Number.isFinite(r.marks[0].updatedAt)) throw new Error('expected updatedAt');
});

await test('upsertMark patches existing mark without overwriting unrelated fields', async () => {
  await upsertMark('hash-B', { value: 5, logTimeMs: 2000 }, { name: 'First press', notes: 'rotation' });
  await upsertMark('hash-B', { value: 5, logTimeMs: 2000 }, { notes: 'rotation OK' });
  const r = await loadJournal('hash-B');
  assertEqual(r.marks.length, 1, 'should still be one mark');
  assertEqual(r.marks[0].name, 'First press', 'name preserved');
  assertEqual(r.marks[0].notes, 'rotation OK', 'notes patched');
});

await test('upsertMark distinguishes marks by (value, logTimeMs)', async () => {
  await upsertMark('hash-C', { value: 5, logTimeMs: 1000 }, { name: 'a' });
  await upsertMark('hash-C', { value: 5, logTimeMs: 2000 }, { name: 'b' });
  await upsertMark('hash-C', { value: 6, logTimeMs: 1000 }, { name: 'c' });
  const r = await loadJournal('hash-C');
  assertEqual(r.marks.length, 3);
});

await test('upsertMark ignores invalid keys', async () => {
  await upsertMark('hash-D', { value: NaN, logTimeMs: 1 }, { name: 'x' });
  await upsertMark('hash-D', { value: 1, logTimeMs: NaN }, { name: 'x' });
  await upsertMark('hash-D', null, { name: 'x' });
  const r = await loadJournal('hash-D');
  // Record may or may not exist depending on whether any earlier
  // path created it; what matters is no marks were added.
  if (r) assertEqual(r.marks.length, 0);
});

await test('loadMarkAnnotations returns overlay map keyed by value:logTimeMs', async () => {
  await upsertMark('hash-E', { value: 10, logTimeMs: 100 }, { name: 'Mark 10', notes: 'turn 1' });
  await upsertMark('hash-E', { value: 11, logTimeMs: 200 }, { notes: 'turn 2' });
  const ann = await loadMarkAnnotations('hash-E');
  assertEqual(ann['10:100'].name, 'Mark 10');
  assertEqual(ann['10:100'].notes, 'turn 1');
  assertEqual(ann['11:200'].name, '');
  assertEqual(ann['11:200'].notes, 'turn 2');
});

await test('loadMarkAnnotations returns empty object for unknown hash', async () => {
  const ann = await loadMarkAnnotations('nope');
  if (typeof ann !== 'object' || ann === null) throw new Error('expected object');
  assertEqual(Object.keys(ann).length, 0);
});

await test('upsertClipAnnotation creates a record and a clip', async () => {
  await upsertClipAnnotation('hash-F', 'clip-1', { label: 'go-around', notes: 'demo' });
  const r = await loadJournal('hash-F');
  assertEqual(r.clips.length, 1);
  assertEqual(r.clips[0].id, 'clip-1');
  assertEqual(r.clips[0].label, 'go-around');
  assertEqual(r.clips[0].notes, 'demo');
});

await test('upsertClipAnnotation patches existing clip', async () => {
  await upsertClipAnnotation('hash-G', 'clip-X', { label: 'orig', notes: 'a' });
  await upsertClipAnnotation('hash-G', 'clip-X', { notes: 'b' });
  const r = await loadJournal('hash-G');
  assertEqual(r.clips.length, 1);
  assertEqual(r.clips[0].label, 'orig', 'label preserved');
  assertEqual(r.clips[0].notes, 'b', 'notes patched');
});

await test('clip annotations are independent of marks', async () => {
  await upsertMark('hash-H', { value: 1, logTimeMs: 100 }, { name: 'm1' });
  await upsertClipAnnotation('hash-H', 'c1', { label: 'clip1' });
  await upsertMark('hash-H', { value: 2, logTimeMs: 200 }, { name: 'm2' });
  await upsertClipAnnotation('hash-H', 'c2', { label: 'clip2' });
  const r = await loadJournal('hash-H');
  assertEqual(r.marks.length, 2);
  assertEqual(r.clips.length, 2);
  // Verify a later mark upsert doesn't disturb a previously-written clip.
  await upsertMark('hash-H', { value: 1, logTimeMs: 100 }, { notes: 'updated' });
  const r2 = await loadJournal('hash-H');
  assertEqual(r2.clips.length, 2);
  assertEqual(r2.clips[0].label, 'clip1');
  assertEqual(r2.marks[0].notes, 'updated');
});

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------

await new Promise(r => setTimeout(r, 10));

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
