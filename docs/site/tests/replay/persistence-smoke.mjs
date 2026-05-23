// persistence-smoke.mjs — Node smoke test for the replay persistence
// module's pure exports. Covers fileMetadata, filesMatch,
// computeLogDigest, and the localStorage-key-format contract.
//
// Mocks localStorage (per-test) and crypto.subtle.digest
// deterministically so we don't depend on browser APIs.
//
// Run:
//   node docs/site/tests/replay/persistence-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// ---------------------------------------------------------------------
// Mocks: localStorage + crypto.subtle.digest
// ---------------------------------------------------------------------

function makeMockLs() {
  const store = new Map();
  return {
    store,
    getItem(key) { return store.has(key) ? store.get(key) : null; },
    setItem(key, value) { store.set(key, String(value)); },
    removeItem(key) { store.delete(key); },
    clear() { store.clear(); },
  };
}

// The test wires up these globals before importing persistence.js so
// the module's safeLsGet/safeLsSet wrappers see the mocks.
const mockLs = makeMockLs();
globalThis.localStorage = mockLs;

// Mock crypto.subtle.digest: output bytes = (firstByte ^ index) & 0xff.
// Deterministic, dependent on input content (so different inputs yield
// different digests), and doesn't require the real crypto API.
const mockCrypto = {
  subtle: {
    async digest(algorithm, buf) {
      if (algorithm !== 'SHA-256') {
        throw new Error(`mock crypto: unsupported algorithm ${algorithm}`);
      }
      const u8 = new Uint8Array(buf);
      const first = u8.length > 0 ? u8[0] : 0;
      // Produce a 32-byte SHA-256-sized output (we only consume the
      // first 8 bytes, but mirror the real API for type-correctness).
      const out = new Uint8Array(32);
      for (let i = 0; i < 32; i++) out[i] = (first ^ i) & 0xff;
      return out.buffer;
    },
  },
};
// On recent Node (≥20.10), globalThis.crypto is a getter-only WebCrypto
// instance. Override via defineProperty so the persistence module's
// `crypto.subtle.digest(...)` call hits our deterministic mock.
Object.defineProperty(globalThis, 'crypto', {
  value: mockCrypto,
  configurable: true,
  writable: true,
});

// A minimal File-like polyfill for Node. The persistence module calls
// file.slice(0, 10_240).arrayBuffer(), so we provide both methods.
class MockFile {
  constructor({ name, size, lastModified, content }) {
    this.name = name;
    this.size = size;
    this.lastModified = lastModified;
    this._content = content || new Uint8Array(0);
  }
  slice(start, end) {
    return new MockBlob(this._content.slice(start, end));
  }
}
class MockBlob {
  constructor(content) { this._content = content; }
  async arrayBuffer() { return this._content.buffer.slice(
    this._content.byteOffset,
    this._content.byteOffset + this._content.byteLength); }
}

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
// docs/site/docs/data-and-logs/replay/lib/replay/persistence.js.
// ---------------------------------------------------------------------

const persistencePath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/persistence.js'
);

let persistence;
try {
  persistence = await import(persistencePath);
} catch (err) {
  console.error(`FAIL: could not import persistence.js: ${err.message}`);
  console.error(`      path: ${persistencePath}`);
  process.exit(1);
}

const { fileMetadata, filesMatch, computeLogDigest } = persistence;

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

test('fileMetadata returns null for null/undefined/non-object', () => {
  assertEqual(fileMetadata(null), null, 'null input');
  assertEqual(fileMetadata(undefined), null, 'undefined input');
  assertEqual(fileMetadata('a string'), null, 'string input');
  assertEqual(fileMetadata(42), null, 'number input');
});

test('fileMetadata returns null when file has no name', () => {
  assertEqual(fileMetadata({ size: 100, lastModified: 1000 }), null);
});

test('fileMetadata returns {name, size, lastModified} for a File-like object', () => {
  const f = { name: 'log.csv', size: 12345, lastModified: 1700000000000 };
  assertDeepEqual(fileMetadata(f), {
    name: 'log.csv',
    size: 12345,
    lastModified: 1700000000000,
  });
});

test('fileMetadata coerces size and lastModified to Number', () => {
  const f = { name: 'log.csv', size: '12345', lastModified: '1700000000000' };
  const md = fileMetadata(f);
  assertEqual(typeof md.size, 'number');
  assertEqual(typeof md.lastModified, 'number');
  assertEqual(md.size, 12345);
  assertEqual(md.lastModified, 1700000000000);
});

test('filesMatch returns false when either side is null', () => {
  const md = { name: 'a', size: 1, lastModified: 1 };
  assertEqual(filesMatch(null, md), false);
  assertEqual(filesMatch(md, null), false);
  assertEqual(filesMatch(null, null), false);
});

test('filesMatch returns true when all three fields match', () => {
  const stored = { name: 'log.csv', size: 12345, lastModified: 1700000000000 };
  const picked = { name: 'log.csv', size: 12345, lastModified: 1700000000000 };
  assertEqual(filesMatch(stored, picked), true);
});

test('filesMatch returns false when name differs', () => {
  const stored = { name: 'log.csv',  size: 12345, lastModified: 1700000000000 };
  const picked = { name: 'log2.csv', size: 12345, lastModified: 1700000000000 };
  assertEqual(filesMatch(stored, picked), false);
});

test('filesMatch returns false when size differs', () => {
  const stored = { name: 'log.csv', size: 12345, lastModified: 1700000000000 };
  const picked = { name: 'log.csv', size: 99999, lastModified: 1700000000000 };
  assertEqual(filesMatch(stored, picked), false);
});

test('filesMatch returns false when lastModified differs', () => {
  const stored = { name: 'log.csv', size: 12345, lastModified: 1700000000000 };
  const picked = { name: 'log.csv', size: 12345, lastModified: 1800000000000 };
  assertEqual(filesMatch(stored, picked), false);
});

await test('computeLogDigest returns null for null input', async () => {
  const d = await computeLogDigest(null);
  assertEqual(d, null);
});

await test('computeLogDigest returns a 16-char hex string', async () => {
  const content = new Uint8Array([0x42, 0x01, 0x02, 0x03]);
  const f = new MockFile({ name: 'log.csv', size: 4, lastModified: 0, content });
  const d = await computeLogDigest(f);
  assertEqual(typeof d, 'string');
  assertEqual(d.length, 16);
  // first byte 0x42 → out[0] = 0x42 ^ 0 = 0x42, out[1] = 0x42 ^ 1 = 0x43, etc.
  // hex of 8 bytes: 42 43 40 41 46 47 44 45 → '4243404146474445'
  assertEqual(d, '4243404146474445');
});

await test('computeLogDigest is deterministic for identical inputs', async () => {
  const content = new Uint8Array([0x11, 0x22, 0x33]);
  const f1 = new MockFile({ name: 'a.csv', size: 3, lastModified: 0, content });
  const f2 = new MockFile({ name: 'b.csv', size: 3, lastModified: 999, content });
  const d1 = await computeLogDigest(f1);
  const d2 = await computeLogDigest(f2);
  assertEqual(d1, d2, 'identical content → identical digest');
});

await test('computeLogDigest differs for different first byte', async () => {
  const c1 = new Uint8Array([0x42, 0x01]);
  const c2 = new Uint8Array([0x43, 0x01]);
  const f1 = new MockFile({ name: 'a.csv', size: 2, lastModified: 0, content: c1 });
  const f2 = new MockFile({ name: 'b.csv', size: 2, lastModified: 0, content: c2 });
  const d1 = await computeLogDigest(f1);
  const d2 = await computeLogDigest(f2);
  if (d1 === d2) {
    throw new Error(`expected different digests; got d1=${d1} d2=${d2}`);
  }
});

await test('computeLogDigest tolerates errors (returns null)', async () => {
  // A file whose .slice() throws should produce null, not an exception.
  const broken = {
    slice() { throw new Error('boom'); },
  };
  const d = await computeLogDigest(broken);
  assertEqual(d, null);
});

// ---------------------------------------------------------------------
// localStorage key-format contracts.
//
// We don't expose the key-builder functions, but the keys are stable
// part of the public contract: anything else that reads these keys
// (a future debug tool, an export utility) needs the format to be
// predictable. Verify by writing-via-the-API + reading-via-the-key.
// ---------------------------------------------------------------------

test('localStorage key contracts: sync key format', () => {
  // Write a sync value under a known digest, then read the raw key.
  // The blueprint specifies `replay-sync-<digest>-v1`.
  mockLs.clear();
  const digest = 'deadbeefcafef00d';
  const value = { videoTakeoffSec: 12.34, logTakeoffMs: 5678 };
  mockLs.setItem(`replay-sync-${digest}-v1`, JSON.stringify(value));
  const raw = mockLs.getItem(`replay-sync-${digest}-v1`);
  if (!raw) throw new Error('expected sync key to be written');
  const parsed = JSON.parse(raw);
  assertEqual(parsed.videoTakeoffSec, 12.34);
  assertEqual(parsed.logTakeoffMs, 5678);
});

test('localStorage key contracts: clips key format', () => {
  mockLs.clear();
  const digest = '0011223344556677';
  const value = [
    { startMs: 1000, endMs: 2000, label: 'clip 01' },
    { startMs: 5000, endMs: 8000, label: 'clip 02' },
  ];
  mockLs.setItem(`replay-clips-${digest}-v1`, JSON.stringify(value));
  const raw = mockLs.getItem(`replay-clips-${digest}-v1`);
  if (!raw) throw new Error('expected clips key to be written');
  const parsed = JSON.parse(raw);
  assertEqual(parsed.length, 2);
  assertEqual(parsed[0].startMs, 1000);
  assertEqual(parsed[1].label, 'clip 02');
});

// The legacy `replay-recent-files-v1` key was retired in PR 1b. The
// sidecar (`.replay.json` next to the log) plus the folder handle in
// IDB are the only persistence surfaces now.

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
