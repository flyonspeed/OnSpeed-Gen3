// API schema invariants — validates the JSON shape of every
// /api/* mock fixture under tools/web/dev-server/mocks/.
//
// Same convention as geometry-invariants.mjs: regex-cheap, no test
// framework, vanilla Node 20.  Run with:
//
//   node tools/web/test/api-schema.mjs
//
// Exit code 0 = all pass.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const MOCKS_DIR  = path.resolve(__dirname, '..', 'dev-server', 'mocks');

let failed = 0;
let passed = 0;
const results = [];

function ok(cond, msg) {
  if (cond) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', msg]); }
}
function eq(actual, expected, msg) {
  if (actual === expected) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)}`]); }
}

// ----- Loader ------------------------------------------------------------

function loadMock(name) {
  const p = path.join(MOCKS_DIR, name + '.json');
  ok(fs.existsSync(p), `mock fixture exists: ${name}.json`);
  if (!fs.existsSync(p)) return null;
  let parsed;
  try {
    parsed = JSON.parse(fs.readFileSync(p, 'utf-8'));
  } catch (e) {
    failed++;
    results.push(['  FAIL', `${name}.json: ${e.message}`]);
    return null;
  }
  passed++;
  results.push(['  PASS', `${name}.json: parses as JSON`]);
  return parsed;
}

function exactKeys(obj, keys, where) {
  if (!obj || typeof obj !== 'object') {
    ok(false, `${where}: object expected, got ${typeof obj}`);
    return;
  }
  const got = Object.keys(obj).sort();
  const want = [...keys].sort();
  ok(got.length === want.length && got.every((k, i) => k === want[i]),
     `${where}: keys = ${got.join(',')} (want ${want.join(',')})`);
}

function isNumber(v) { return typeof v === 'number' && Number.isFinite(v); }
function isString(v) { return typeof v === 'string'; }
function isBoolean(v) { return typeof v === 'boolean'; }
function isInt(v)    { return typeof v === 'number' && Number.isInteger(v); }

// ----- /api/sample/* ----------------------------------------------------

const aoa = loadMock('api-sample-aoa');
if (aoa) {
  exactKeys(aoa, ['aoa'], '/api/sample/aoa');
  ok(isNumber(aoa.aoa) || aoa.aoa === -100, '/api/sample/aoa: aoa is number');
}

const flapsRaw = loadMock('api-sample-flaps-raw');
if (flapsRaw) {
  exactKeys(flapsRaw, ['adcCounts', 'position'], '/api/sample/flaps-raw');
  ok(isInt(flapsRaw.adcCounts) && flapsRaw.adcCounts >= 0,
     '/api/sample/flaps-raw: adcCounts is non-negative int');
  ok(isInt(flapsRaw.position),
     '/api/sample/flaps-raw: position is int');
}

for (const name of ['api-sample-volume', 'api-sample-pfwd', 'api-sample-p45']) {
  const m = loadMock(name);
  if (!m) continue;
  if (name.endsWith('volume')) {
    exactKeys(m, ['adcCounts'], '/api/sample/volume');
    ok(isInt(m.adcCounts) && m.adcCounts >= 0,
       '/api/sample/volume: adcCounts is non-negative int');
  } else {
    exactKeys(m, ['counts'], `/${name.replace(/-/g, '/').replace('/api/sample','/api/sample')}`);
    ok(isInt(m.counts) && m.counts >= 0, `${name}: counts is non-negative int`);
  }
}

// ----- /api/audiotest/* -------------------------------------------------

for (const trigger of ['api-audiotest', 'api-audiotest-stop', 'api-vnochime-test']) {
  const m = loadMock(trigger);
  if (!m) continue;
  exactKeys(m, ['ok'], trigger);
  eq(m.ok, true, `${trigger}: ok=true`);
}

const audiotestStatus = loadMock('api-audiotest-status');
if (audiotestStatus) {
  exactKeys(audiotestStatus, ['state'], '/api/audiotest/status');
  ok(audiotestStatus.state === 'running' || audiotestStatus.state === 'idle',
     '/api/audiotest/status: state in {running, idle}');
}

// ----- /api/logs --------------------------------------------------------

const logs = loadMock('api-logs');
if (logs) {
  exactKeys(logs, ['activeLog', 'totalSize', 'files'], '/api/logs');
  ok(isString(logs.activeLog), '/api/logs: activeLog is string');
  ok(isInt(logs.totalSize) && logs.totalSize >= 0,
     '/api/logs: totalSize is non-negative int');
  ok(Array.isArray(logs.files), '/api/logs: files is array');
  for (const [i, f] of logs.files.entries()) {
    ok(isString(f.name), `/api/logs.files[${i}].name is string`);
    ok(isInt(f.size) && f.size >= 0, `/api/logs.files[${i}].size is non-negative int`);
    ok(isBoolean(f.hasMeta), `/api/logs.files[${i}].hasMeta is bool`);
    if (f.hasMeta) {
      ok(f.meta && typeof f.meta === 'object', `/api/logs.files[${i}].meta is object`);
      const expectedMetaKeys = [
        'durationMs', 'rowCount', 'maxIasKt', 'maxPaltFt',
        'firmware', 'firmwareSha', 'efisType',
        'gpsFixSeen', 'utcStart', 'timeOfDayStart',
      ];
      exactKeys(f.meta, expectedMetaKeys, `/api/logs.files[${i}].meta`);
      ok(isInt(f.meta.durationMs), `meta.durationMs is int`);
      ok(isInt(f.meta.rowCount),   `meta.rowCount is int`);
      ok(isNumber(f.meta.maxIasKt), `meta.maxIasKt is number`);
      ok(isNumber(f.meta.maxPaltFt), `meta.maxPaltFt is number`);
      ok(isString(f.meta.firmware), `meta.firmware is string`);
      ok(isString(f.meta.efisType), `meta.efisType is string`);
      ok(isBoolean(f.meta.gpsFixSeen), `meta.gpsFixSeen is bool`);
    }
  }
}

const deleteBulk = loadMock('api-logs-delete-bulk');
if (deleteBulk) {
  exactKeys(deleteBulk, ['ok', 'deleted', 'errors'], '/api/logs/delete-bulk');
  ok(isBoolean(deleteBulk.ok), 'delete-bulk: ok is bool');
  ok(Array.isArray(deleteBulk.deleted), 'delete-bulk: deleted is array');
  ok(deleteBulk.deleted.every(isString), 'delete-bulk: deleted entries are strings');
  ok(Array.isArray(deleteBulk.errors), 'delete-bulk: errors is array');
  for (const [i, e] of deleteBulk.errors.entries()) {
    exactKeys(e, ['name', 'reason'], `delete-bulk.errors[${i}]`);
    ok(isString(e.name), `delete-bulk.errors[${i}].name is string`);
    ok(isString(e.reason), `delete-bulk.errors[${i}].reason is string`);
  }
}

// ----- /api/format, /api/reboot ----------------------------------------

const format = loadMock('api-format');
if (format) {
  exactKeys(format, ['taskId'], '/api/format');
  ok(isString(format.taskId), '/api/format: taskId is string');
  ok(format.taskId.startsWith('format-'),
     '/api/format: taskId has format- prefix');
}

const formatStatus = loadMock('api-format-status');
if (formatStatus) {
  ok('state' in formatStatus, '/api/format/status: has state');
  ok(['running', 'done', 'failed', 'idle'].includes(formatStatus.state),
     '/api/format/status: state in {running, done, failed, idle}');
  // progress and error are optional
  for (const k of Object.keys(formatStatus)) {
    ok(['state', 'progress', 'error'].includes(k),
       `/api/format/status: only documented keys (saw ${k})`);
  }
}

const reboot = loadMock('api-reboot');
if (reboot) {
  exactKeys(reboot, ['ok'], '/api/reboot');
  eq(reboot.ok, true, '/api/reboot: ok=true');
}

// ----- /api/calwiz/state -----------------------------------------------

const calwiz = loadMock('api-calwiz-state');
if (calwiz) {
  exactKeys(calwiz, ['aircraft', 'currentFlapIndex', 'flaps'], '/api/calwiz/state');
  exactKeys(calwiz.aircraft,
            ['grossWeightLb', 'bestGlideKt', 'vfeKt', 'gLimit'],
            'calwiz.aircraft');
  ok(isInt(calwiz.aircraft.grossWeightLb), 'aircraft.grossWeightLb is int');
  ok(isNumber(calwiz.aircraft.bestGlideKt), 'aircraft.bestGlideKt is number');
  ok(isNumber(calwiz.aircraft.vfeKt),       'aircraft.vfeKt is number');
  ok(isNumber(calwiz.aircraft.gLimit),      'aircraft.gLimit is number');

  ok(isInt(calwiz.currentFlapIndex) && calwiz.currentFlapIndex >= 0,
     'currentFlapIndex is non-negative int');
  ok(Array.isArray(calwiz.flaps), 'flaps is array');
  ok(calwiz.currentFlapIndex < calwiz.flaps.length,
     'currentFlapIndex is in range of flaps[]');

  const flapKeys = [
    'index', 'degrees',
    'alpha0Deg', 'alphaStallDeg',
    'ldMaxAoaDeg', 'onSpeedFastAoaDeg', 'onSpeedSlowAoaDeg',
    'stallWarnAoaDeg', 'stallAoaDeg', 'maneuveringAoaDeg',
  ];
  for (const [i, f] of calwiz.flaps.entries()) {
    exactKeys(f, flapKeys, `flaps[${i}]`);
    eq(f.index, i, `flaps[${i}].index = ${i}`);
    ok(isInt(f.degrees), `flaps[${i}].degrees is int`);
    // alpha0 is typically negative; not asserted here (per
    // CLAUDE.md, body angle convention).
    ok(isNumber(f.alpha0Deg),     `flaps[${i}].alpha0Deg is number`);
    ok(isNumber(f.alphaStallDeg), `flaps[${i}].alphaStallDeg is number`);
    ok(isNumber(f.ldMaxAoaDeg),   `flaps[${i}].ldMaxAoaDeg is number`);
  }
}

// ----- /api/version ----------------------------------------------------

const version = loadMock('api-version');
if (version) {
  exactKeys(version, ['version', 'gitShortSha', 'buildDate'], '/api/version');
  ok(isString(version.version),     'version.version is string');
  ok(isString(version.gitShortSha), 'version.gitShortSha is string');
  ok(isString(version.buildDate),   'version.buildDate is string');
}

// ----- Report ---------------------------------------------------------

console.log('API schema invariants:');
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
