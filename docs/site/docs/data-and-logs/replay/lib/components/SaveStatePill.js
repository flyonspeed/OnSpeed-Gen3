// SaveStatePill — toolbar pill showing current sidecar save state.
//
// Replaces the legacy "save your notes" banner. The pill is always
// visible; its label tells the engineer at a glance whether their
// work is persisted.
//
// States (driven by props):
//   - 'no-flight'    → "💾 No flight loaded"   (no log loaded yet)
//   - 'no-handle'    → "💾 Auto-save pending"  (log loaded, no sidecar yet)
//   - 'saving'       → "💾 Auto-saving"
//   - 'saved'        → "💾 Saved Ns ago"       (computed from lastSavedAt)
//   - 'dirty'        → "💾 Pending save"
//   - 'error'        → "⚠️ Save failed"
//   - 'autosave-off' → "💾 Auto-save off"
//
// Interactions:
//   - Click   → manual save (force-flush, ignoring debounce). Wired
//               via the `onManualSave` callback.
//   - Right-click → toggle "auto-save off for this session". Wired via
//               the `onToggleAutoSave` callback. Per-session only — not
//               persisted across reloads, matching the plan's design.

import { html, useState, useEffect }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';

function formatAgo(ts) {
  if (!ts) return '';
  const ago = Math.max(0, Math.round((Date.now() - ts) / 1000));
  if (ago < 2) return 'just now';
  if (ago < 60) return `${ago}s ago`;
  const mins = Math.floor(ago / 60);
  if (mins < 60) return `${mins}m ago`;
  const hrs = Math.floor(mins / 60);
  return `${hrs}h ago`;
}

export function SaveStatePill({
  hasLog,
  hasHandle,
  saveState,
  lastSavedAt,
  lastError,
  autoSaveDisabled,
  onManualSave,
  onToggleAutoSave,
}) {
  // Re-render once per second so the "Saved Ns ago" label ticks. The
  // tick is cheap (the pill is a single span) and keeps the engineer's
  // mental model in sync with the actual age of the on-disk file.
  const [, force] = useState(0);
  useEffect(() => {
    if (saveState !== 'saved' || !lastSavedAt) return undefined;
    const id = setInterval(() => force(n => n + 1), 1000);
    return () => clearInterval(id);
  }, [saveState, lastSavedAt]);

  let label;
  let cls = 'replay-sidecar-pill';
  let title = '';

  if (autoSaveDisabled) {
    label = '💾 Auto-save off';
    cls += ' is-off';
    title = 'Right-click to re-enable auto-save. Click to save now.';
  } else if (!hasLog) {
    label = '💾 No flight loaded';
    cls += ' is-idle';
    title = 'Pick a flight folder or log to enable auto-save.';
  } else if (saveState === 'error') {
    label = '⚠️ Save failed';
    cls += ' is-error';
    title = lastError || 'Save failed. Click to retry.';
  } else if (saveState === 'saving') {
    label = '💾 Auto-saving';
    cls += ' is-saving';
    title = 'Writing sidecar to disk.';
  } else if (!hasHandle && saveState === 'dirty') {
    label = '💾 Auto-save pending';
    cls += ' is-pending';
    title = 'Sidecar will be created on next debounce. Click to save now.';
  } else if (saveState === 'dirty') {
    label = '💾 Pending save';
    cls += ' is-dirty';
    title = 'Edits pending. Click to flush now.';
  } else if (saveState === 'saved' && lastSavedAt) {
    label = `💾 Saved ${formatAgo(lastSavedAt)}`;
    cls += ' is-saved';
    title = 'Auto-saved. Click to save again now. Right-click to disable auto-save.';
  } else if (hasHandle && saveState === 'saved') {
    label = '💾 Saved';
    cls += ' is-saved';
    title = 'Auto-saved. Click to save again now. Right-click to disable auto-save.';
  } else {
    // No handle yet, no dirty edits — fresh load with sidecar absent.
    label = '💾 Auto-save ready';
    cls += ' is-ready';
    title = 'First edit will create the sidecar next to your log.';
  }

  const handleClick = (e) => {
    e.preventDefault();
    if (typeof onManualSave === 'function') onManualSave();
  };
  const handleContext = (e) => {
    e.preventDefault();
    if (typeof onToggleAutoSave === 'function') onToggleAutoSave();
  };

  return html`
    <button class=${cls}
            type="button"
            title=${title}
            onClick=${handleClick}
            onContextMenu=${handleContext}>
      ${label}
    </button>
  `;
}
