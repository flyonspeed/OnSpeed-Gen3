// sidecar-schema-smoke.mjs — Node smoke test for the sidecar schema
// module's pure exports. Verifies emit/parse round-trips and the
// rejection branches for malformed inputs.
//
// Run:
//   node docs/site/tests/replay/sidecar-schema-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const schemaPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/sidecar-schema.js'
);

let mod;
try {
  mod = await import(schemaPath);
} catch (err) {
  console.error(`FAIL: could not import sidecar-schema.js: ${err.message}`);
  console.error(`      path: ${schemaPath}`);
  process.exit(1);
}

const {
  SIDECAR_EXT, SCHEMA_VERSION, SIDECAR_SCHEMA_URL,
  emptySession, parseSidecar, sidecarFileNameFor,
  logNameForSidecar, genSessionId, nowISO,
} = mod;

let passed = 0;
let failed = 0;
const results = [];

function test(name, fn) {
  try {
    fn();
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
// Constants + helpers
// ---------------------------------------------------------------------

test('SIDECAR_EXT is the doubled XMP convention', () => {
  assertEqual(SIDECAR_EXT, '.replay.json');
});

test('SCHEMA_VERSION is 1', () => {
  assertEqual(SCHEMA_VERSION, 1);
});

test('SIDECAR_SCHEMA_URL is the canonical flyonspeed.org URL', () => {
  assertEqual(SIDECAR_SCHEMA_URL, 'https://flyonspeed.org/schemas/replay/v1.json');
});

test('sidecarFileNameFor adds .replay.json on top of full log filename', () => {
  assertEqual(sidecarFileNameFor('log_007_fixed_3.csv'),
              'log_007_fixed_3.csv.replay.json');
});

test('sidecarFileNameFor returns empty on falsy input', () => {
  assertEqual(sidecarFileNameFor(''), '');
  assertEqual(sidecarFileNameFor(null), '');
  assertEqual(sidecarFileNameFor(undefined), '');
});

test('logNameForSidecar reverses sidecarFileNameFor', () => {
  assertEqual(logNameForSidecar('log_007.csv.replay.json'), 'log_007.csv');
  assertEqual(logNameForSidecar('log_007.csv'), '');
});

test('genSessionId is date-stamped + suffix', () => {
  const id = genSessionId(new Date('2026-05-22T12:00:00Z'));
  assertTrue(id.startsWith('session-2026-05-22-'), `unexpected: ${id}`);
  assertTrue(id.length >= 'session-2026-05-22-'.length + 4, `too short: ${id}`);
});

test('nowISO produces a parseable ISO string', () => {
  const s = nowISO();
  const d = new Date(s);
  assertTrue(!Number.isNaN(d.getTime()), `unparseable: ${s}`);
});

// ---------------------------------------------------------------------
// emptySession
// ---------------------------------------------------------------------

test('emptySession produces a v1 doc with required structure', () => {
  const doc = emptySession({
    log: { name: 'log_007.csv', hash: 'abc', sizeBytes: 100, rowCount: 50, durationSec: 12.5 },
  });
  assertEqual(doc.schemaVersion, 1);
  assertEqual(doc.session.revision, 1);
  assertEqual(doc.subject.log.name, 'log_007.csv');
  assertEqual(doc.subject.config, null);
  assertEqual(doc.subject.video, null);
  assertEqual(doc.sync, null);
  assertEqual(doc.hud.pitchOffsetDeg, 0);
  assertEqual(doc.marks.length, 0);
  assertEqual(doc.clips.length, 0);
  assertEqual(doc.summary, '');
});

test('emptySession with config + video propagates fields', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
    config: { name: 'c.cfg', hash: 'b', ahrsAlgorithm: 'ekfq' },
    video: { name: 'v.mp4', hash: 'd', durationSec: 50 },
  });
  assertEqual(doc.subject.config.name, 'c.cfg');
  assertEqual(doc.subject.config.ahrsAlgorithm, 'ekfq');
  assertEqual(doc.subject.video.name, 'v.mp4');
});

// ---------------------------------------------------------------------
// parseSidecar — happy path
// ---------------------------------------------------------------------

test('parseSidecar round-trips an emptySession output', () => {
  const doc = emptySession({
    log: { name: 'log.csv', hash: 'abc', sizeBytes: 10, rowCount: 5, durationSec: 1 },
  });
  const text = JSON.stringify(doc);
  const r = parseSidecar(text);
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.session.id, doc.session.id);
  assertEqual(r.value.subject.log.name, 'log.csv');
  assertEqual(r.value.schemaVersion, 1);
});

test('parseSidecar accepts a doc with marks + clips + summary', () => {
  const doc = emptySession({
    log: { name: 'log.csv', hash: 'abc', sizeBytes: 10, rowCount: 5, durationSec: 1 },
  });
  doc.marks.push({
    value: 3, logTimeMs: 12345, name: 'first', notes: 'stall break',
    createdAt: nowISO(), updatedAt: nowISO(),
  });
  doc.clips.push({
    id: 'clip-1', startLogMs: 12000, endLogMs: 18000,
    label: 'Stall #1', notes: 'pitch up before break',
    createdAt: nowISO(), updatedAt: nowISO(),
  });
  doc.summary = '# Notes\n\nStall break around 12s.';
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.marks.length, 1);
  assertEqual(r.value.clips.length, 1);
  assertEqual(r.value.summary.startsWith('# Notes'), true);
});

test('parseSidecar accepts a doc with sync set', () => {
  const doc = emptySession({
    log: { name: 'log.csv', hash: 'abc', sizeBytes: 10, rowCount: 5, durationSec: 1 },
  });
  doc.sync = {
    logAnchorTimestampMs: 142853,
    videoAnchorSec: 12.4,
    method: 'manual-datamark',
    confidence: 'high',
  };
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.sync.logAnchorTimestampMs, 142853);
});

// ---------------------------------------------------------------------
// parseSidecar — rejection branches
// ---------------------------------------------------------------------

test('parseSidecar rejects non-string input', () => {
  const r = parseSidecar(123);
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('must be a string'), r.error);
});

test('parseSidecar rejects invalid JSON', () => {
  const r = parseSidecar('{this is not json');
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('invalid JSON'), r.error);
});

test('parseSidecar rejects non-object top-level', () => {
  const r = parseSidecar('[]');
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('object'), r.error);
});

test('parseSidecar rejects missing schemaVersion', () => {
  const r = parseSidecar(JSON.stringify({ session: {}, subject: {} }));
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('schemaVersion'), r.error);
});

test('parseSidecar rejects mismatched schemaVersion', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  doc.schemaVersion = 99;
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('unsupported schemaVersion'), r.error);
});

test('parseSidecar rejects missing session.id', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  doc.session.id = '';
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('session.id'), r.error);
});

test('parseSidecar rejects missing subject.log', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  doc.subject.log = null;
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('subject.log'), r.error);
});

test('parseSidecar rejects wrong-type mark field', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  doc.marks.push({ value: 'three', logTimeMs: 1 });
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('marks[0].value'), r.error);
});

test('parseSidecar rejects wrong-type clip field', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  doc.clips.push({ id: 'c-1', startLogMs: 'oops', endLogMs: 1 });
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('clips[0].startLogMs'), r.error);
});

test('parseSidecar tolerates missing hud (fills in defaults)', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  delete doc.hud;
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.hud.pitchOffsetDeg, 0);
});

test('parseSidecar tolerates missing summary (fills in empty)', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  delete doc.summary;
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.summary, '');
});

test('parseSidecar tolerates unknown top-level fields (preserves what it knows)', () => {
  // Hand-rolled doc with a stranger field. Should still parse, since
  // we don't enforce strictness on unknown keys.
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  const text = JSON.stringify({ ...doc, futureField: { x: 1 } });
  const r = parseSidecar(text);
  assertEqual(r.ok, true, r.ok ? '' : r.error);
});

// ---------------------------------------------------------------------
// relativePath (PR #640 follow-up — Sam's playtest finding #1)
// ---------------------------------------------------------------------

test('emptySession seeds relativePath fields from inputs', () => {
  const doc = emptySession({
    log:    { name: 'log_007.csv',  relativePath: '11 May 26 Cockpit/log_007.csv',
              hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
    config: { name: 'onspeed2.cfg', relativePath: '11 May 26 Cockpit/onspeed2.cfg',
              hash: '', ahrsAlgorithm: '' },
    video:  { name: 'GOPR0314.MP4', relativePath: '11 May 26 Raw Video/GOPR0314.MP4',
              hash: '', durationSec: 0 },
  });
  assertEqual(doc.subject.log.relativePath,
              '11 May 26 Cockpit/log_007.csv');
  assertEqual(doc.subject.config.relativePath,
              '11 May 26 Cockpit/onspeed2.cfg');
  assertEqual(doc.subject.video.relativePath,
              '11 May 26 Raw Video/GOPR0314.MP4');
});

test('emptySession defaults relativePath to empty string when omitted', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  assertEqual(doc.subject.log.relativePath, '');
});

test('parseSidecar round-trips relativePath through emit/parse', () => {
  const doc = emptySession({
    log:   { name: 'log_007.csv',  relativePath: 'Cockpit/log_007.csv',
             hash: '', sizeBytes: 1, rowCount: 1, durationSec: 1 },
    video: { name: 'GOPR0314.MP4', relativePath: 'Raw Video/GOPR0314.MP4',
             hash: '', durationSec: 0 },
  });
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.subject.log.relativePath,   'Cockpit/log_007.csv');
  assertEqual(r.value.subject.video.relativePath, 'Raw Video/GOPR0314.MP4');
});

test('parseSidecar accepts an older sidecar with no relativePath field', () => {
  // Hand-rolled doc that pre-dates the relativePath bump. Should still
  // parse — the field is optional and the reader falls back to basename.
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  // Strip relativePath to simulate a v1-era sidecar.
  delete doc.subject.log.relativePath;
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, true, r.ok ? '' : r.error);
  assertEqual(r.value.subject.log.name, 'l.csv');
});

test('parseSidecar rejects a non-string relativePath when present', () => {
  const doc = emptySession({
    log: { name: 'l.csv', hash: 'a', sizeBytes: 1, rowCount: 1, durationSec: 1 },
  });
  doc.subject.log.relativePath = 42; // wrong type
  const r = parseSidecar(JSON.stringify(doc));
  assertEqual(r.ok, false);
  assertTrue(r.error.includes('relativePath'), r.error);
});

// ---------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
