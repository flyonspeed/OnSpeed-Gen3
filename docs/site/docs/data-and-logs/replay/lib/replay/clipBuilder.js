// ClipBuilder — clip-list UI for the replay tool.
//
// Renders the clip list, the in/out marker controls, and per-clip
// editing (label rename, scrub-to-clip, in/out adjust, delete, export).
// The page above owns the clips array; this module is presentational
// glue.
//
// Each clip is shaped { startMs, endMs, label? } in log-time
// milliseconds — the same wire shape Stream A persists.
//
// The page injects two callbacks for the export pipeline:
//   onExportMp4(clip):    kicks off an MP4 export, returns a handle
//                         with `progress` (0..1) and `cancel()`.
//   exportingClipIdx:     number | null — index of the clip currently
//                         being exported (so its row can show progress
//                         in place of the Export button).
//
// The component itself doesn't manage that state — it only renders
// what the parent hands it. Keeps the rendering pure: same props,
// same DOM, no internal effects.

import { html, useState } from '../../../../packages/ui-core/vendor/preact-standalone.js';

// Map a log timestamp (ms) to a video time (seconds). Inlined here
// rather than imported from dataMarks so the module has no replay
// dependencies (testability + reuse).
function logMsToVideoSec(logMs, sync) {
  if (!sync) return null;
  if (!Number.isFinite(sync.logTakeoffMs) ||
      !Number.isFinite(sync.videoTakeoffSec)) return null;
  return sync.videoTakeoffSec + (logMs - sync.logTakeoffMs) / 1000;
}

// Format a duration in seconds as H:MM:SS or M:SS.
function formatHms(sec) {
  if (!Number.isFinite(sec)) return '—';
  const total = Math.max(0, Math.floor(sec));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  if (h > 0) return `${h}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
  return `${m}:${String(s).padStart(2,'0')}`;
}

// Default label for a new clip. Pads the index so a sort order on
// label is stable; pilots usually rename anyway.
export function defaultClipLabel(index) {
  return `clip ${String(index + 1).padStart(2, '0')}`;
}

// Build a clip from a video time + sync, with a configurable duration
// window. Used by both quick-add buttons (30s/60s windows) and the
// mark-in/mark-out flow (no duration; caller passes both ends).
export function buildClipFromPlayhead(videoSec, durationSec, sync, label) {
  if (!sync) return null;
  if (!Number.isFinite(sync.logTakeoffMs) ||
      !Number.isFinite(sync.videoTakeoffSec)) return null;
  if (!Number.isFinite(videoSec)) return null;
  const startMs = sync.logTakeoffMs + (videoSec - sync.videoTakeoffSec) * 1000;
  const endMs   = startMs + durationSec * 1000;
  return { startMs, endMs, label: label || '' };
}

// Build a clip from explicit in/out video times. Used by the mark-in /
// mark-out flow. Returns null if either is missing or in <= out.
export function buildClipFromMarkers(inVideoSec, outVideoSec, sync, label) {
  if (!sync) return null;
  if (!Number.isFinite(sync.logTakeoffMs) ||
      !Number.isFinite(sync.videoTakeoffSec)) return null;
  if (!Number.isFinite(inVideoSec) || !Number.isFinite(outVideoSec)) return null;
  if (outVideoSec <= inVideoSec) return null;
  const startMs = sync.logTakeoffMs + (inVideoSec  - sync.videoTakeoffSec) * 1000;
  const endMs   = sync.logTakeoffMs + (outVideoSec - sync.videoTakeoffSec) * 1000;
  return { startMs, endMs, label: label || '' };
}

// Mutate one clip in the list immutably, copying everything else.
// Exported so tests can hit it directly.
export function updateClipAt(clips, index, patch) {
  return clips.map((c, i) => (i === index ? { ...c, ...patch } : c));
}

// Remove a clip by index. Exported for tests.
export function removeClipAt(clips, index) {
  return clips.filter((_, i) => i !== index);
}

// Clip in/out adjustment: clamp the new value so endMs > startMs by
// at least 100ms. Returns the validated patch, or null if the change
// would produce an invalid window.
//
// Reason for the 100ms floor: a clip with start == end produces an
// empty export, and the encoder errors out late in the pipeline. Catch
// it at the edit site so the pilot sees feedback immediately.
export function validateClipEdit(clip, patch) {
  const next = { ...clip, ...patch };
  if (!Number.isFinite(next.startMs) || !Number.isFinite(next.endMs)) return null;
  if (next.endMs - next.startMs < 100) return null;
  return next;
}

// ---- Component ----------------------------------------------------

export const ClipBuilder = ({
  clips,
  setClips,
  sync,
  syncReady,
  videoEl,                // for scrub-to-clip
  exportingClipIdx,       // index of the clip currently being exported
  exportProgress,         // 0..1 progress of that export
  exportLabel,            // label of the exporting clip (for status)
  disabled,
  // Callbacks bubbling up to ReplayPage:
  onAddQuick,             // (durationSec)
  onExport,               // (clip)        — single-clip export (composite)
  onExportAll,            // ()            — sequential all-clips export
  onCancel,               // ()            — cancel the running export
  onScrubTo,              // (videoSec)    — seek the live video
  // Mark-in/mark-out flow:
  pendingInVideoSec,      // currently-marked in-point, or null
  onMarkIn,               // ()
  onMarkOut,              // ()
  onCancelMark,           // ()            — discard pendingIn
  mp4Available,           // boolean — feature-detect result
  mp4UnavailableTooltip,  // string — shown on hover when grayed out
  // Overlay-only export — runs alongside the composite path:
  onExportOverlays,       // (clip, idx)  — render selected-modes overlay MP4 batch
  onCancelOverlays,       // ()            — cancel overlay export
  overlayExporting,       // boolean — true while any overlay batch is running
  overlayCurrentMode,     // string | null — id of the mode currently being encoded
  overlayProgress,        // 0..1 — progress within the current mode
  overlayAvailable,       // boolean — feature-detect result for overlay path
  selectedOverlayModes,   // string[] — modes the user picked (default: ['indexer'])
  onChangeOverlayModes,   // (string[]) — replaces the selection list
  overlayModeOrder,       // string[]  — canonical mode list for the checkboxes
  overlaySize,            // 'native' | '0.2' | '0.3' | '0.5'
  onChangeOverlaySize,    // (string) — updates the size selection
  // Burn-in MP4 (composite) layout toggle. true = ADI bottom-left +
  // Energy bottom-right; false = single mode in bottom-right (legacy).
  standardClipOverlay,
  onChangeStandardClipOverlay,
  // Forwarded to each row so Set-in-here / Set-out-here resolve to a
  // global timeline-second across multi-chapter playback.
  getCurrentVideoSec,
}) => {
  const cancelMarkBtn = pendingInVideoSec != null
    ? html`
        <button class="replay-btn-ghost" onClick=${onCancelMark} disabled=${disabled}>
          Cancel mark-in
        </button>`
    : null;

  // Helper: render the "Export" button or, for the currently-exporting
  // clip, an inline progress widget with cancel.
  const renderExportControls = (clip, i) => {
    if (exportingClipIdx === i) {
      return html`
        <div class="replay-clip-progress" role="status">
          <progress class="replay-progress"
                    max="1" value=${exportProgress ?? 0}></progress>
          <span class="replay-clip-progress-pct">
            ${Math.round((exportProgress ?? 0) * 100)}%
          </span>
          <button class="replay-btn-ghost" onClick=${onCancel}>Cancel</button>
        </div>`;
    }
    // Disable Export when (a) globally disabled, (b) some other clip
    // is exporting (sequence has to drain), (c) sync isn't ready,
    // (d) MP4 isn't supported in this browser.
    const exportDisabled = disabled || exportingClipIdx != null ||
                           !syncReady || !mp4Available || overlayExporting;
    return html`
      <button class="replay-btn"
              disabled=${exportDisabled}
              title=${mp4Available ? '' : (mp4UnavailableTooltip || '')}
              onClick=${() => onExport(clip, i)}>
        Export MP4
      </button>`;
  };

  // Overlay-only export: button + per-mode checkbox row. Each MP4 is
  // 320×240 (native M5 panel size) against the panel's own black
  // background — drop it into iMovie / Final Cut, scale and position
  // on top of the source footage.
  const renderOverlayControls = (clip, i) => {
    if (overlayExporting && overlayCurrentMode) {
      return html`
        <div class="replay-clip-progress" role="status">
          <span class="replay-clip-progress-pct">
            ${overlayCurrentMode}
          </span>
          <progress class="replay-progress"
                    max="1" value=${overlayProgress ?? 0}></progress>
          <span class="replay-clip-progress-pct">
            ${Math.round((overlayProgress ?? 0) * 100)}%
          </span>
          <button class="replay-btn-ghost" onClick=${onCancelOverlays}>Cancel</button>
        </div>`;
    }
    const selectedCount = (selectedOverlayModes || []).length;
    const overlayDisabled = disabled || exportingClipIdx != null ||
                            !syncReady || !overlayAvailable || overlayExporting ||
                            selectedCount === 0;
    if (!onExportOverlays) return null;
    return html`
      <button class="replay-btn"
              disabled=${overlayDisabled}
              title=${overlayAvailable
                ? `Render ${selectedCount} overlay MP4${selectedCount === 1 ? '' : 's'} at 320×240, M5 panel as the background (no chroma).`
                : (mp4UnavailableTooltip || '')}
              onClick=${() => onExportOverlays(clip, i)}>
        Overlays · NLE${selectedCount > 1 ? ` (${selectedCount})` : ''}
      </button>`;
  };

  // Mode-picker checkbox row. Lets the user toggle which M5 modes the
  // overlay-export batch should include. Shown once at the top of the
  // clip list (not per-row) — the selection applies to all overlay
  // exports until changed.
  const toggleMode = (m) => {
    if (!onChangeOverlayModes || !Array.isArray(selectedOverlayModes)) return;
    const has = selectedOverlayModes.includes(m);
    const next = has
      ? selectedOverlayModes.filter(x => x !== m)
      : [...selectedOverlayModes, m];
    // Keep canonical order so the export output is deterministic.
    const order = overlayModeOrder || [];
    next.sort((a, b) => order.indexOf(a) - order.indexOf(b));
    onChangeOverlayModes(next);
  };
  const renderOverlayModePicker = () => {
    if (!overlayAvailable || !onExportOverlays || !overlayModeOrder) return null;
    return html`
      <div class="replay-overlay-modes" role="group" aria-label="Overlay modes">
        <span class="replay-label">Overlay modes</span>
        ${overlayModeOrder.map(m => html`
          <label class="replay-overlay-mode-toggle">
            <input type="checkbox"
                   checked=${(selectedOverlayModes || []).includes(m)}
                   disabled=${overlayExporting}
                   onChange=${() => toggleMode(m)} />
            ${m}
          </label>`)}
        ${onChangeOverlaySize ? html`
          <span class="replay-spacer"></span>
          <label class="replay-overlay-mode-toggle">
            Size:
            <select class="replay-overlay-size-select"
                    value=${overlaySize || '0.2'}
                    disabled=${overlayExporting}
                    onChange=${(e) => onChangeOverlaySize(e.target.value)}>
              <option value="native">Native (320×240)</option>
              <option value="0.2">20% of source</option>
              <option value="0.3">30% of source</option>
              <option value="0.5">50% of source</option>
            </select>
          </label>` : null}
      </div>`;
  };

  return html`
    <div class="replay-clips">
      <div class="replay-clips-header">
        <span class="replay-label">Clips</span>
        <span class="replay-status">${clips.length}</span>
        <span class="replay-spacer"></span>

        <button class="replay-btn"
                disabled=${disabled || !syncReady}
                onClick=${() => onAddQuick(30)}>
          + 30 s clip from playhead
        </button>
        <button class="replay-btn"
                disabled=${disabled || !syncReady}
                onClick=${() => onAddQuick(60)}>
          + 60 s clip from playhead
        </button>

        ${pendingInVideoSec == null
          ? html`
              <button class="replay-btn"
                      disabled=${disabled || !syncReady}
                      onClick=${onMarkIn}>
                Mark clip in
              </button>`
          : html`
              <span class="replay-status replay-status-attention">
                in @ ${formatHms(pendingInVideoSec)} — scrub and click Mark
                clip out
              </span>
              <button class="replay-btn-primary"
                      disabled=${disabled || !syncReady}
                      onClick=${onMarkOut}>
                Mark clip out
              </button>
              ${cancelMarkBtn}`}

        ${clips.length > 0 && html`
          <button class="replay-btn-primary"
                  disabled=${disabled || !mp4Available ||
                             exportingClipIdx != null || !syncReady}
                  title=${mp4Available ? '' : (mp4UnavailableTooltip || '')}
                  onClick=${onExportAll}>
            Export all
          </button>`}
        ${onChangeStandardClipOverlay && html`
          <label class="replay-overlay-mode-toggle"
                 title="Burn BOTH ADI (bottom-left) and Energy (bottom-right) into the source video. When off, the live preview's single mode burns in the bottom-right.">
            <input type="checkbox"
                   checked=${!!standardClipOverlay}
                   disabled=${exportingClipIdx != null}
                   onChange=${(e) => onChangeStandardClipOverlay(e.target.checked)} />
            Standard (ADI + Energy)
          </label>`}
      </div>

      ${renderOverlayModePicker()}

      ${clips.length === 0
        ? html`<div class="replay-clips-empty">
            no clips yet — scrub to a moment, click "+ 30 s" / "+ 60 s" or
            "Mark clip in" → "Mark clip out".
          </div>`
        : html`<div class="replay-clips-list">
            ${clips.map((c, i) => html`
              <${ClipRow}
                clip=${c}
                index=${i}
                sync=${sync}
                videoEl=${videoEl}
                disabled=${disabled}
                isExporting=${exportingClipIdx === i}
                onScrubTo=${onScrubTo}
                onPatch=${(patch) => {
                  const next = validateClipEdit(c, patch);
                  if (next) setClips(updateClipAt(clips, i, next));
                }}
                onRemove=${() => setClips(removeClipAt(clips, i))}
                renderExport=${() => renderExportControls(c, i)}
                renderOverlayExport=${() => renderOverlayControls(c, i)}
                getCurrentVideoSec=${getCurrentVideoSec} />
            `)}
          </div>`}
    </div>`;
};

// ---- Single-row component ----------------------------------------
//
// Each clip row shows: editable label, formatted in/out times,
// duration, scrub buttons, set-in-here/set-out-here buttons, export
// button (or progress widget), delete button.
const ClipRow = ({
  clip, index, sync, videoEl, disabled,
  isExporting,
  onScrubTo, onPatch, onRemove, renderExport, renderOverlayExport,
  // Multi-chapter playback wants the global timeline-second, not the
  // raw videoEl.currentTime. Function-style prop so each click reads
  // a fresh value rather than a render-time snapshot.
  getCurrentVideoSec,
}) => {
  const [labelDraft, setLabelDraft] = useState(clip.label || '');

  const startSec = logMsToVideoSec(clip.startMs, sync);
  const endSec   = logMsToVideoSec(clip.endMs,   sync);
  const spanSec  = (clip.endMs - clip.startMs) / 1000;

  // Snap the start/end to the current playhead. Useful for tightening
  // a clip after rough-marking it: scrub to where you actually want
  // it to start/end, click "Set in here" / "Set out here".
  //
  // For multi-chapter timelines `getCurrentVideoSec` returns the global
  // timeline second; for single-file playback it falls back to
  // videoEl.currentTime via the default below.
  const readCurrentSec = () => {
    if (typeof getCurrentVideoSec === 'function') {
      const t = getCurrentVideoSec();
      if (Number.isFinite(t)) return t;
    }
    const v = videoEl?.current || null;
    return v ? v.currentTime : null;
  };
  const setInHere = () => {
    if (!sync) return;
    const t = readCurrentSec();
    if (!Number.isFinite(t)) return;
    const newStartMs = sync.logTakeoffMs +
                       (t - sync.videoTakeoffSec) * 1000;
    onPatch({ startMs: newStartMs });
  };
  const setOutHere = () => {
    if (!sync) return;
    const t = readCurrentSec();
    if (!Number.isFinite(t)) return;
    const newEndMs = sync.logTakeoffMs +
                     (t - sync.videoTakeoffSec) * 1000;
    onPatch({ endMs: newEndMs });
  };

  // Commit the label draft on blur or Enter. Avoids a per-keystroke
  // setState dispatch into the parent clips array.
  const commitLabel = () => {
    if ((clip.label || '') !== labelDraft) {
      onPatch({ label: labelDraft });
    }
  };

  return html`
    <div class=${'replay-clip-row' + (isExporting ? ' is-exporting' : '')}>
      <input class="replay-clip-label-input"
             type="text"
             value=${labelDraft}
             placeholder=${`clip ${String(index + 1).padStart(2, '0')}`}
             disabled=${disabled || isExporting}
             onInput=${(e) => setLabelDraft(e.target.value)}
             onBlur=${commitLabel}
             onKeyDown=${(e) => {
               if (e.key === 'Enter') { e.preventDefault(); commitLabel(); }
               if (e.key === 'Escape') { setLabelDraft(clip.label || ''); }
             }} />

      <span class="replay-mark-time">
        ${Number.isFinite(startSec) ? formatHms(startSec) : '—'}
        → ${Number.isFinite(endSec) ? formatHms(endSec) : '—'}
        · ${spanSec.toFixed(1)} s
      </span>

      <span class="replay-spacer"></span>

      <button class="replay-btn-ghost"
              disabled=${disabled || isExporting || !Number.isFinite(startSec)}
              title="Seek video to this clip's start"
              onClick=${() => Number.isFinite(startSec) && onScrubTo(startSec)}>
        Scrub
      </button>
      <button class="replay-btn-ghost"
              disabled=${disabled || isExporting}
              title="Move this clip's start to the current playhead"
              onClick=${setInHere}>
        Set in here
      </button>
      <button class="replay-btn-ghost"
              disabled=${disabled || isExporting}
              title="Move this clip's end to the current playhead"
              onClick=${setOutHere}>
        Set out here
      </button>

      ${renderExport()}
      ${renderOverlayExport ? renderOverlayExport() : null}

      <button class="replay-btn-ghost"
              disabled=${disabled || isExporting}
              title="Delete this clip"
              onClick=${onRemove}>×</button>
    </div>`;
};
