// sidecar-schema.js — schema definition + parser for the `.replay.json`
// sidecar document.
//
// One sidecar per log file. Named `<logname>.<ext>.replay.json` (XMP
// doubled-extension convention) and stored next to its log. Carries
// engineer-authored annotations (marks, clips, sync, HUD offsets,
// freeform summary) so analysis work travels with the log file rather
// than living trapped in IndexedDB.
//
// Pure data layer: no IDB, no DOM, no Preact imports. The Preact hook
// and File-System-Access integration live in sidecar.js.
//
// Schema v1 shape (matches PLAN_REPLAY_SIDECAR_PERSISTENCE_v2.md):
//
//   {
//     "$schema": "https://flyonspeed.org/schemas/replay/v1.json",
//     "schemaVersion": 1,
//     "session": { id, title, createdAt, updatedAt, createdBy, author, revision },
//     "subject": { log, config|null, video|null },
//     "sync": { logAnchorTimestampMs, videoAnchorSec, method, confidence } | null,
//     "hud":  { pitchOffsetDeg, leftInsetMode, rightInsetMode },
//     "marks":   [{ value, logTimeMs, name, notes, createdAt, updatedAt }],
//     "clips":   [{ id, startLogMs, endLogMs, label, notes, createdAt, updatedAt }],
//     "summary": ""
//   }
//
// `parseSidecar` returns `{ok: true, value}` or `{ok: false, error}` —
// hand-rolled type guards in the journal.js / persistence.js style, no
// Ajv because the docs site has a strict CSP that blocks the inline
// `new Function` Ajv leans on.

export const SIDECAR_EXT = '.replay.json';
export const SCHEMA_VERSION = 1;
export const OPTIMISTIC_LOCK_FIELD = 'revision';
export const SIDECAR_SCHEMA_URL = 'https://flyonspeed.org/schemas/replay/v1.json';

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

export function nowISO() {
  return new Date().toISOString();
}

// Random session id. Date-stamped + 4-char suffix so a folder full of
// sidecars sorts chronologically on filename alone.
export function genSessionId(dateForTest) {
  const d = dateForTest || new Date();
  const y = d.getUTCFullYear();
  const m = String(d.getUTCMonth() + 1).padStart(2, '0');
  const day = String(d.getUTCDate()).padStart(2, '0');
  const suffix = Math.random().toString(36).slice(2, 6);
  return `session-${y}-${m}-${day}-${suffix}`;
}

// Sidecar filename for a given log filename. Doubled-extension XMP
// convention: `log_007_fixed_3.csv` → `log_007_fixed_3.csv.replay.json`.
// Two reasons over `<base>.replay.json`:
//   - `log_007.csv` and `log_007.cfg` siblings (Vac's actual pattern)
//     wouldn't collide on a single `.replay.json` per basename.
//   - The reader can recover the source filename without state.
export function sidecarFileNameFor(logFileName) {
  if (!logFileName || typeof logFileName !== 'string') return '';
  return logFileName + SIDECAR_EXT;
}

// Given a sidecar filename, return the log filename it attaches to, or
// '' if the name doesn't end in `.replay.json`.
export function logNameForSidecar(sidecarFileName) {
  if (!sidecarFileName || typeof sidecarFileName !== 'string') return '';
  if (!sidecarFileName.endsWith(SIDECAR_EXT)) return '';
  return sidecarFileName.slice(0, sidecarFileName.length - SIDECAR_EXT.length);
}

// ---------------------------------------------------------------------
// Empty session builder
// ---------------------------------------------------------------------

// Build a fresh sidecar doc seeded with metadata about the subject log /
// config / video. `subject` may carry partial info — only `log` is
// required (the rest are optional). `createdBy` is a free-form string;
// callers should pass the page's git SHA or build id.
//
// Note: the caller may immediately mutate the returned object (e.g. to
// drop in initial marks from IDB). All `createdAt` / `updatedAt`
// timestamps are set to the same instant so a fresh sidecar reads as
// consistent.
export function emptySession({ log, config, video, createdBy, author } = {}) {
  const now = nowISO();
  return {
    $schema: SIDECAR_SCHEMA_URL,
    schemaVersion: SCHEMA_VERSION,
    session: {
      id: genSessionId(),
      title: (log && log.name) ? log.name : '',
      createdAt: now,
      updatedAt: now,
      createdBy: typeof createdBy === 'string' ? createdBy : 'OnSpeed Replay',
      author: typeof author === 'string' ? author : '',
      revision: 1,
    },
    subject: {
      log: log ? {
        name: typeof log.name === 'string' ? log.name : '',
        hash: typeof log.hash === 'string' ? log.hash : '',
        sizeBytes: Number.isFinite(log.sizeBytes) ? log.sizeBytes : 0,
        rowCount: Number.isFinite(log.rowCount) ? log.rowCount : 0,
        durationSec: Number.isFinite(log.durationSec) ? log.durationSec : 0,
      } : null,
      config: config ? {
        name: typeof config.name === 'string' ? config.name : '',
        hash: typeof config.hash === 'string' ? config.hash : '',
        ahrsAlgorithm: typeof config.ahrsAlgorithm === 'string'
          ? config.ahrsAlgorithm : '',
      } : null,
      video: video ? {
        name: typeof video.name === 'string' ? video.name : '',
        hash: typeof video.hash === 'string' ? video.hash : '',
        durationSec: Number.isFinite(video.durationSec) ? video.durationSec : 0,
      } : null,
    },
    sync: null,
    hud: {
      pitchOffsetDeg: 0,
      leftInsetMode: '',
      rightInsetMode: '',
    },
    marks: [],
    clips: [],
    summary: '',
  };
}

// ---------------------------------------------------------------------
// Type guards
// ---------------------------------------------------------------------

function isObject(v) { return v && typeof v === 'object' && !Array.isArray(v); }
function isString(v) { return typeof v === 'string'; }
function isNum(v)    { return typeof v === 'number' && Number.isFinite(v); }
function isBool(v)   { return typeof v === 'boolean'; }

function checkSession(s) {
  if (!isObject(s)) return 'session is missing or not an object';
  if (!isString(s.id) || !s.id) return 'session.id must be a non-empty string';
  if (!isString(s.title)) return 'session.title must be a string';
  if (!isString(s.createdAt)) return 'session.createdAt must be a string';
  if (!isString(s.updatedAt)) return 'session.updatedAt must be a string';
  if (!isString(s.createdBy)) return 'session.createdBy must be a string';
  if (!isString(s.author)) return 'session.author must be a string';
  if (!isNum(s.revision) || s.revision < 1) {
    return 'session.revision must be a positive number';
  }
  return null;
}

function checkLogSubject(l) {
  if (l === null) return 'subject.log is required';
  if (!isObject(l)) return 'subject.log must be an object';
  if (!isString(l.name)) return 'subject.log.name must be a string';
  if (!isString(l.hash)) return 'subject.log.hash must be a string';
  if (!isNum(l.sizeBytes))   return 'subject.log.sizeBytes must be a number';
  if (!isNum(l.rowCount))    return 'subject.log.rowCount must be a number';
  if (!isNum(l.durationSec)) return 'subject.log.durationSec must be a number';
  return null;
}

function checkOptionalSubject(name, v, fields) {
  if (v === null || v === undefined) return null;
  if (!isObject(v)) return `subject.${name} must be an object or null`;
  for (const [k, t] of fields) {
    const present = Object.prototype.hasOwnProperty.call(v, k);
    if (!present) continue;
    const val = v[k];
    if (t === 'string' && !isString(val)) {
      return `subject.${name}.${k} must be a string`;
    }
    if (t === 'number' && !isNum(val)) {
      return `subject.${name}.${k} must be a number`;
    }
  }
  return null;
}

function checkSubject(sub) {
  if (!isObject(sub)) return 'subject is missing or not an object';
  const logErr = checkLogSubject(sub.log);
  if (logErr) return logErr;
  const cfgErr = checkOptionalSubject('config', sub.config, [
    ['name', 'string'], ['hash', 'string'], ['ahrsAlgorithm', 'string'],
  ]);
  if (cfgErr) return cfgErr;
  const vidErr = checkOptionalSubject('video', sub.video, [
    ['name', 'string'], ['hash', 'string'], ['durationSec', 'number'],
  ]);
  if (vidErr) return vidErr;
  return null;
}

function checkSync(s) {
  if (s === null || s === undefined) return null;
  if (!isObject(s)) return 'sync must be an object or null';
  if (!isNum(s.logAnchorTimestampMs)) {
    return 'sync.logAnchorTimestampMs must be a number';
  }
  if (!isNum(s.videoAnchorSec)) {
    return 'sync.videoAnchorSec must be a number';
  }
  if (s.method !== undefined && !isString(s.method)) {
    return 'sync.method must be a string when present';
  }
  if (s.confidence !== undefined && !isString(s.confidence)) {
    return 'sync.confidence must be a string when present';
  }
  return null;
}

function checkHud(h) {
  if (!isObject(h)) return 'hud must be an object';
  if (h.pitchOffsetDeg !== undefined && !isNum(h.pitchOffsetDeg)) {
    return 'hud.pitchOffsetDeg must be a number when present';
  }
  if (h.leftInsetMode !== undefined && !isString(h.leftInsetMode)) {
    return 'hud.leftInsetMode must be a string when present';
  }
  if (h.rightInsetMode !== undefined && !isString(h.rightInsetMode)) {
    return 'hud.rightInsetMode must be a string when present';
  }
  return null;
}

function checkMark(m, i) {
  if (!isObject(m)) return `marks[${i}] must be an object`;
  if (!isNum(m.value))     return `marks[${i}].value must be a number`;
  if (!isNum(m.logTimeMs)) return `marks[${i}].logTimeMs must be a number`;
  if (m.name !== undefined && !isString(m.name)) {
    return `marks[${i}].name must be a string when present`;
  }
  if (m.notes !== undefined && !isString(m.notes)) {
    return `marks[${i}].notes must be a string when present`;
  }
  return null;
}

function checkClip(c, i) {
  if (!isObject(c)) return `clips[${i}] must be an object`;
  if (!isString(c.id) || !c.id) return `clips[${i}].id must be a non-empty string`;
  if (!isNum(c.startLogMs)) return `clips[${i}].startLogMs must be a number`;
  if (!isNum(c.endLogMs))   return `clips[${i}].endLogMs must be a number`;
  if (c.label !== undefined && !isString(c.label)) {
    return `clips[${i}].label must be a string when present`;
  }
  if (c.notes !== undefined && !isString(c.notes)) {
    return `clips[${i}].notes must be a string when present`;
  }
  return null;
}

// ---------------------------------------------------------------------
// parseSidecar — JSON text → validated doc | error
// ---------------------------------------------------------------------

export function parseSidecar(jsonText) {
  if (typeof jsonText !== 'string') {
    return { ok: false, error: 'parseSidecar: input must be a string' };
  }
  let doc;
  try {
    doc = JSON.parse(jsonText);
  } catch (e) {
    return { ok: false, error: `parseSidecar: invalid JSON: ${e.message}` };
  }
  if (!isObject(doc)) {
    return { ok: false, error: 'parseSidecar: top level must be an object' };
  }
  if (!isNum(doc.schemaVersion)) {
    return { ok: false, error: 'parseSidecar: schemaVersion must be a number' };
  }
  if (doc.schemaVersion !== SCHEMA_VERSION) {
    return {
      ok: false,
      error: `parseSidecar: unsupported schemaVersion ${doc.schemaVersion} (expected ${SCHEMA_VERSION})`,
    };
  }

  const sessionErr = checkSession(doc.session);
  if (sessionErr) return { ok: false, error: 'parseSidecar: ' + sessionErr };

  const subjectErr = checkSubject(doc.subject);
  if (subjectErr) return { ok: false, error: 'parseSidecar: ' + subjectErr };

  const syncErr = checkSync(doc.sync);
  if (syncErr) return { ok: false, error: 'parseSidecar: ' + syncErr };

  // hud is required (always emitted by emptySession). Tolerate missing
  // in inputs we read by treating it as the default empty hud — but the
  // value itself must still be the right shape when present.
  let hud = doc.hud;
  if (hud === undefined || hud === null) {
    hud = { pitchOffsetDeg: 0, leftInsetMode: '', rightInsetMode: '' };
  } else {
    const hudErr = checkHud(hud);
    if (hudErr) return { ok: false, error: 'parseSidecar: ' + hudErr };
  }

  if (!Array.isArray(doc.marks)) {
    return { ok: false, error: 'parseSidecar: marks must be an array' };
  }
  for (let i = 0; i < doc.marks.length; i++) {
    const err = checkMark(doc.marks[i], i);
    if (err) return { ok: false, error: 'parseSidecar: ' + err };
  }

  if (!Array.isArray(doc.clips)) {
    return { ok: false, error: 'parseSidecar: clips must be an array' };
  }
  for (let i = 0; i < doc.clips.length; i++) {
    const err = checkClip(doc.clips[i], i);
    if (err) return { ok: false, error: 'parseSidecar: ' + err };
  }

  if (doc.summary !== undefined && !isString(doc.summary)) {
    return { ok: false, error: 'parseSidecar: summary must be a string when present' };
  }

  // Build the normalized return value. Defaults patched in for fields a
  // tolerant reader fills in.
  return {
    ok: true,
    value: {
      $schema: isString(doc.$schema) ? doc.$schema : SIDECAR_SCHEMA_URL,
      schemaVersion: SCHEMA_VERSION,
      session: doc.session,
      subject: doc.subject,
      sync: doc.sync ?? null,
      hud,
      marks: doc.marks,
      clips: doc.clips,
      summary: doc.summary ?? '',
    },
  };
}

