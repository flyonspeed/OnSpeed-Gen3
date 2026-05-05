// FormatPage (/format): SD-card format with confirm + status polling.
//
// Phases:
//   "confirm"   — pilot must explicitly confirm.  Format button + Cancel.
//   "running"   — POST /api/format started, polling /api/format/status.
//   "done"      — format succeeded.
//   "failed"    — format reported an error; show the message.
//
// /api/format runs the format inline today (see ApiHandlers.cpp's
// async-shim comment); the taskId / status pair is in place so a
// future PR can move it to a real background task without breaking
// this client.

import { html, useState, useEffect } from '../vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { getJson, postJson, ApiError } from '../shell/apiClient.js';

// Page-prefixed to avoid colliding with RebootPage's POLL_INTERVAL_MS.
// The bundler concatenates every page into a single global script
// scope, so module-scope const names must be unique across pages.
const FORMAT_POLL_INTERVAL_MS = 1000;

export function FormatPage() {
  const [phase, setPhase] = useState('confirm');
  const [taskId, setTaskId] = useState(null);
  const [error, setError] = useState(null);

  // Status-poll loop: query /api/format/status?id=… until it reports
  // a terminal state (done / failed) or the user navigates away.
  useEffect(() => {
    if (phase !== 'running' || !taskId) return undefined;
    let cancelled = false;
    const tick = async () => {
      if (cancelled) return;
      try {
        const s = await getJson('/api/format/status?id=' + encodeURIComponent(taskId));
        if (cancelled) return;
        if (s.state === 'done') {
          setPhase('done');
          return;
        }
        if (s.state === 'failed') {
          setError(s.error || 'format failed');
          setPhase('failed');
          return;
        }
      } catch (e) {
        // Polling errors (connection drop while the format runs) are
        // recoverable — keep trying.  A persistent failure is reported
        // when the task transitions to 'failed' upstream.
      }
      if (!cancelled) setTimeout(tick, FORMAT_POLL_INTERVAL_MS);
    };
    setTimeout(tick, FORMAT_POLL_INTERVAL_MS);
    return () => { cancelled = true; };
  }, [phase, taskId]);

  const onConfirm = async () => {
    setError(null);
    try {
      const r = await postJson('/api/format', {});
      setTaskId(r.taskId);
      setPhase('running');
    } catch (e) {
      const msg = (e instanceof ApiError) ? e.message : String(e.message || e);
      setError(msg);
    }
  };

  return html`
    <${PageShell} active="format">
      <div style=${{ maxWidth: '560px', margin: '0 auto', padding: '12px' }}>
        ${phase === 'confirm' && html`
          <p style=${{ color: 'red' }}>
            Confirm that you want to format the internal SD card.
            You will lose all the files currently on the card.
          </p>
          ${error && html`<p style=${{ color: 'red' }}>${error}</p>`}
          <button type="button" class="button" onClick=${onConfirm}>Format SD Card</button>
          ${' '}<a href="/">Cancel</a>`}
        ${phase === 'running' && html`
          <p>Formatting SD card… please do not power-cycle.</p>`}
        ${phase === 'done' && html`
          <p>SD card has been formatted.</p>
          <p><a href="/logs">Go to logs</a> | <a href="/">Home</a></p>`}
        ${phase === 'failed' && html`
          <p style=${{ color: 'red' }}>
            SD card format ERROR: ${error || 'unknown failure'}
          </p>
          <p><a href="/">Home</a></p>`}
      </div>
    <//>`;
}
