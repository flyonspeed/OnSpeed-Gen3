// sidecar-migration.js — one-time copy from the IDB journal into a
// fresh sidecar doc.
//
// Engineers who have used the replay tool before this PR landed have
// their marks, clips, sync, and HUD-pitch-offset state stuck in
// IndexedDB. When they pick a flight folder for the first time after
// this PR and the log has no sidecar yet, we want their existing
// notes to roll over into the new sidecar — otherwise it looks like
// their work disappeared.
//
// Phase 1 reconciliation rule: per-record `updatedAt`-wins is left
// for PR 2. Here we just do the unconditional copy when the sidecar
// is brand new. The migration runs once per logHash; subsequent loads
// pull from the sidecar and never touch IDB again.
//
// Public surface:
//
//   migrateJournalToSidecar({
//     logHash,
//     sidecarDoc,            // mutated in place (or replaced for clarity)
//     journalRecord,         // result of loadJournal(logHash) — may be null
//     storedSync,            // result of localStorage sync-load — may be null
//     storedClips,           // result of localStorage clips-load — may be []
//   }) -> sidecarDoc (mutated)
//
// Idempotent: running it twice doesn't duplicate. We dedupe marks by
// (value, logTimeMs) and clips by `id`. Anything already in the
// sidecar takes precedence (we don't overwrite engineer-edited fields
// with stale IDB ones).
//
// Logs a single `console.log` line per migration so the engineer can
// see in DevTools that their notes rolled in.

import { nowISO } from './sidecar-schema.js';

function ensureArr(v) { return Array.isArray(v) ? v : []; }
function num(v) { return typeof v === 'number' && Number.isFinite(v); }
function str(v) { return typeof v === 'string'; }

function markKeyOf(value, logTimeMs) {
  return String(value) + ':' + String(logTimeMs);
}

// Merge journal marks into doc.marks. Existing entries in doc.marks
// (matched on the (value, logTimeMs) key) win. New entries get
// createdAt/updatedAt stamped if the journal didn't supply ISO times.
function mergeMarks(docMarks, journalMarks) {
  const existing = new Set(
    docMarks.map(m => markKeyOf(m.value, m.logTimeMs)));
  let added = 0;
  for (const jm of journalMarks) {
    if (!num(jm.value) || !num(jm.logTimeMs)) continue;
    const k = markKeyOf(jm.value, jm.logTimeMs);
    if (existing.has(k)) continue;
    const ca = num(jm.createdAt) ? new Date(jm.createdAt).toISOString()
             : str(jm.createdAt) ? jm.createdAt : nowISO();
    const ua = num(jm.updatedAt) ? new Date(jm.updatedAt).toISOString()
             : str(jm.updatedAt) ? jm.updatedAt : ca;
    docMarks.push({
      value: jm.value,
      logTimeMs: jm.logTimeMs,
      name: str(jm.name) ? jm.name : '',
      notes: str(jm.notes) ? jm.notes : '',
      createdAt: ca,
      updatedAt: ua,
    });
    existing.add(k);
    added++;
  }
  return added;
}

function mergeClips(docClips, journalClips) {
  const existing = new Set(docClips.map(c => c.id).filter(Boolean));
  let added = 0;
  for (const jc of journalClips) {
    if (!str(jc.id) || !jc.id) continue;
    if (existing.has(jc.id)) continue;
    if (!num(jc.startLogMs) || !num(jc.endLogMs)) continue;
    const ca = num(jc.createdAt) ? new Date(jc.createdAt).toISOString()
             : str(jc.createdAt) ? jc.createdAt : nowISO();
    const ua = num(jc.updatedAt) ? new Date(jc.updatedAt).toISOString()
             : str(jc.updatedAt) ? jc.updatedAt : ca;
    docClips.push({
      id: jc.id,
      startLogMs: jc.startLogMs,
      endLogMs: jc.endLogMs,
      label: str(jc.label) ? jc.label : '',
      notes: str(jc.notes) ? jc.notes : '',
      createdAt: ca,
      updatedAt: ua,
    });
    existing.add(jc.id);
    added++;
  }
  return added;
}

// Convert the bare clip array `[{startMs, endMs, label}]` from
// localStorage persistence into the sidecar's `clips` shape. The
// localStorage clips don't carry an id — we synthesize one using the
// timestamps so re-running the migration doesn't duplicate.
function mergeLocalStorageClips(docClips, lsClips) {
  const existing = new Set(docClips.map(c => c.id).filter(Boolean));
  let added = 0;
  for (const lc of lsClips) {
    if (!num(lc.startMs) || !num(lc.endMs)) continue;
    const id = `ls-${lc.startMs}-${lc.endMs}`;
    if (existing.has(id)) continue;
    const now = nowISO();
    docClips.push({
      id,
      startLogMs: lc.startMs,
      endLogMs: lc.endMs,
      label: str(lc.label) ? lc.label : '',
      notes: '',
      createdAt: now,
      updatedAt: now,
    });
    existing.add(id);
    added++;
  }
  return added;
}

export function migrateJournalToSidecar({
  logHash,
  sidecarDoc,
  journalRecord,
  storedSync,
  storedClips,
} = {}) {
  if (!sidecarDoc) return null;

  // Ensure the arrays exist before merging.
  if (!Array.isArray(sidecarDoc.marks)) sidecarDoc.marks = [];
  if (!Array.isArray(sidecarDoc.clips)) sidecarDoc.clips = [];

  let marksAdded = 0;
  let clipsAdded = 0;

  if (journalRecord) {
    marksAdded += mergeMarks(sidecarDoc.marks, ensureArr(journalRecord.marks));
    clipsAdded += mergeClips(sidecarDoc.clips, ensureArr(journalRecord.clips));
    if (num(journalRecord.hudPitchOffsetDeg) &&
        (sidecarDoc.hud.pitchOffsetDeg === 0 ||
         sidecarDoc.hud.pitchOffsetDeg === undefined)) {
      sidecarDoc.hud.pitchOffsetDeg = journalRecord.hudPitchOffsetDeg;
    }
  }

  // Merge localStorage clips (older sources) under their synthesized ids.
  clipsAdded += mergeLocalStorageClips(sidecarDoc.clips, ensureArr(storedClips));

  // Seed sync from localStorage if the sidecar doesn't have one yet.
  if (!sidecarDoc.sync && storedSync &&
      num(storedSync.logTakeoffMs) && num(storedSync.videoTakeoffSec)) {
    sidecarDoc.sync = {
      logAnchorTimestampMs: storedSync.logTakeoffMs,
      videoAnchorSec: storedSync.videoTakeoffSec,
      method: 'manual-takeoff',
      confidence: 'medium',
    };
  }

  if (marksAdded > 0 || clipsAdded > 0) {
    console.log(
      `sidecar-migration: rolled in ${marksAdded} mark(s) + ` +
      `${clipsAdded} clip(s) from IDB/localStorage for log ${logHash || '?'}`);
  }

  return sidecarDoc;
}
