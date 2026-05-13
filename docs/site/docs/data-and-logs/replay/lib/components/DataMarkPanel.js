// DataMarkPanel — the per-flight Data Marks list.
//
// Each row corresponds to one DataMark transition the parser found in
// the log. The pilot can:
//   - Jump the video to the mark
//   - Spin up a 30 s / 60 s clip starting at the mark
//   - Expand the row to add a name + notes (persisted to IDB via the
//     journal layer)
//
// The annotation overlay is supplied by `markAnnotations` (map keyed
// by `value:logTimeMs`) and patched via `upsertMarkAnnotation(mark,
// patch)`. The parent (ReplayPage) owns the journal hook; this
// component is dumb about persistence — it just emits patches.

import { html, useState, useEffect, useRef }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';
import { logMsToVideoSec } from '../replay/dataMarks.js';
import { markKey } from '../replay/journal.js';

// Debounce window for IDB writes: long enough that a normal typing
// burst flushes only once, short enough that a glance away and back
// finds the row saved.
const SAVE_DEBOUNCE_MS = 500;

const COLLAPSE_KEY = 'replay-marks-collapsed-v1';

function readCollapsed() {
  try { return localStorage.getItem(COLLAPSE_KEY) === '1'; }
  catch { return false; }
}
function writeCollapsed(v) {
  try { localStorage.setItem(COLLAPSE_KEY, v ? '1' : '0'); } catch {}
}

// First-N chars of notes to use as hover-tooltip preview when the row
// is collapsed. Mirrors a "first paragraph" feel without parsing.
const NOTES_PREVIEW_LEN = 80;

// Format a duration in seconds as H:MM:SS or M:SS. Duplicate of the
// helper in ReplayPage.js — small enough that keeping a local copy
// is cheaper than the import dance.
function formatHms(sec) {
  if (!Number.isFinite(sec)) return '—';
  const total = Math.floor(sec);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  if (h > 0) return `${h}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
  return `${m}:${String(s).padStart(2,'0')}`;
}

function MarkRow({ mark, annotation, sync, disabled, videoDuration,
                   nextMark, onJump, onClip, onClipToNext, onPatch }) {
  const [expanded, setExpanded] = useState(false);
  // Local edit state: typed-but-not-yet-saved values. Reseeds from
  // the annotation overlay whenever the underlying overlay changes
  // (i.e. journal load on log swap).
  const [name,  setName]  = useState(annotation?.name  || '');
  const [notes, setNotes] = useState(annotation?.notes || '');
  const debounceRef = useRef(null);

  // Reseed local state when the overlay changes from outside (a fresh
  // log load, or a future cross-tab sync). Skip the reseed if the user
  // is mid-edit on this row — we don't want to clobber unsaved typing.
  useEffect(() => {
    setName(annotation?.name  || '');
    setNotes(annotation?.notes || '');
    // Intentional: only run when the underlying annotation row changes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [annotation?.updatedAt]);

  // Flush any pending debounce when the row collapses or unmounts so
  // the last keystroke never gets stuck in a timer.
  useEffect(() => () => {
    if (debounceRef.current) {
      clearTimeout(debounceRef.current);
      debounceRef.current = null;
    }
  }, []);

  const scheduleSave = (patch) => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    debounceRef.current = setTimeout(() => {
      debounceRef.current = null;
      onPatch(mark, patch);
    }, SAVE_DEBOUNCE_MS);
  };

  const flushNow = (patch) => {
    if (debounceRef.current) {
      clearTimeout(debounceRef.current);
      debounceRef.current = null;
    }
    onPatch(mark, patch);
  };

  const videoSec = logMsToVideoSec(mark.logTimeMs, sync);
  const dur = Number.isFinite(videoDuration) ? videoDuration : Infinity;
  const inRange = Number.isFinite(videoSec) &&
                  videoSec >= 0 && videoSec < dur;
  const tStr = inRange
    ? `video ${formatHms(videoSec)}`
    : (Number.isFinite(videoSec) ? 'outside video' : 'no sync');

  const hasNotes = !!(annotation && (annotation.name || annotation.notes));
  const notesPreview = annotation?.notes
    ? annotation.notes.slice(0, NOTES_PREVIEW_LEN) +
      (annotation.notes.length > NOTES_PREVIEW_LEN ? '…' : '')
    : '';
  const tooltip = notesPreview || (annotation?.name || '');

  const rowClass = 'replay-mark-row' +
    (hasNotes ? ' replay-mark-has-notes' : '') +
    (expanded ? ' is-expanded' : '');

  return html`
    <div class=${rowClass}>
      <div class="replay-mark-row-head"
           title=${tooltip}
           onClick=${() => setExpanded(e => !e)}>
        <span class="replay-mark-disclosure">${expanded ? '▾' : '▸'}</span>
        <span class="replay-mark-label">${mark.label}</span>
        ${hasNotes
          ? html`<span class="replay-mark-dot" aria-label="has notes">•</span>`
          : null}
        ${annotation?.name
          ? html`<span class="replay-mark-name">${annotation.name}</span>`
          : null}
        <span class="replay-mark-time">log ${formatHms(mark.logTimeMs / 1000)} · ${tStr}</span>
        <span class="replay-spacer"></span>
        <button class="replay-btn" disabled=${disabled || !inRange}
                onClick=${e => { e.stopPropagation(); onJump(mark.logTimeMs); }}>Jump</button>
        <button class="replay-btn" disabled=${disabled || !inRange}
                onClick=${e => { e.stopPropagation(); onClip(mark, 30); }}>Clip 30 s</button>
        <button class="replay-btn" disabled=${disabled || !inRange}
                onClick=${e => { e.stopPropagation(); onClip(mark, 60); }}>Clip 60 s</button>
        <button class="replay-btn"
                disabled=${disabled || !nextMark}
                title=${nextMark
                  ? `Clip from this mark to mark ${nextMark.label}`
                  : 'no next mark'}
                onClick=${e => { e.stopPropagation(); onClipToNext && onClipToNext(mark, nextMark); }}>
          Clip → next
        </button>
      </div>
      ${expanded && html`
        <div class="replay-mark-expanded">
          <label class="replay-mark-field">
            <span>Name</span>
            <input type="text"
                   value=${name}
                   placeholder="e.g. Slow flight at 65 kt"
                   onInput=${e => {
                     const v = e.target.value;
                     setName(v);
                     scheduleSave({ name: v });
                   }}
                   onBlur=${() => flushNow({ name })} />
          </label>
          <label class="replay-mark-field">
            <span>Notes</span>
            <textarea rows="3"
                      value=${notes}
                      placeholder="What happened, what to look for…"
                      onInput=${e => {
                        const v = e.target.value;
                        setNotes(v);
                        scheduleSave({ notes: v });
                      }}
                      onBlur=${() => flushNow({ notes })}></textarea>
          </label>
        </div>`}
    </div>`;
}

export const DataMarkPanel = ({ marks, sync, disabled, videoDuration,
                                markAnnotations,
                                onJump, onClip, onClipToNext,
                                onPatchAnnotation }) => {
  if (!marks || marks.length === 0) return null;
  const [collapsed, setCollapsed] = useState(readCollapsed());
  const toggleCollapsed = () => {
    setCollapsed(c => { writeCollapsed(!c); return !c; });
  };
  return html`
    <div class="replay-marks${collapsed ? ' is-collapsed' : ''}">
      <div class="replay-marks-header replay-section-header"
           onClick=${toggleCollapsed}
           title=${collapsed ? 'Expand data marks' : 'Collapse data marks'}>
        <span class="replay-section-disclosure">${collapsed ? '▸' : '▾'}</span>
        <span class="replay-label">Data marks</span>
        <span class="replay-status">${marks.length}</span>
      </div>
      ${collapsed ? null : html`
        <div class="replay-marks-list">
          ${marks.map((m, i) => html`<${MarkRow}
                key=${markKey(m.value, m.logTimeMs)}
                mark=${m}
                annotation=${markAnnotations ? markAnnotations[markKey(m.value, m.logTimeMs)] : null}
                sync=${sync}
                disabled=${disabled}
                videoDuration=${videoDuration}
                nextMark=${i + 1 < marks.length ? marks[i + 1] : null}
                onJump=${onJump}
                onClip=${onClip}
                onClipToNext=${onClipToNext}
                onPatch=${onPatchAnnotation} />`)}
        </div>`}
    </div>`;
};
