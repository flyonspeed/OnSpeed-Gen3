// RebootPage (/reboot): confirmation button → POST /api/reboot, then
// poll GET / every 2 s until the device answers (it's back online).
//
// Two phases:
//   "confirm"  — initial state.  Reboot button + Cancel link.
//   "rebooting" — POST sent, polling begins.  Shows a progress
//                 indicator until either the device responds (we
//                 redirect to "/") or the user gives up.

import { html, useState, useEffect } from '../vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { postJson } from '../shell/apiClient.js';

// Page-prefixed to avoid colliding with FormatPage's POLL_INTERVAL_MS.
// The bundler concatenates every page into a single global script
// scope, so module-scope const names must be unique across pages.  The
// bundler's duplicate-identifier guard fails the build if two pages
// declare the same top-level name.
const REBOOT_POLL_INTERVAL_MS = 2000;

export function RebootPage() {
  const [phase, setPhase] = useState('confirm');
  const [error, setError] = useState(null);
  const [elapsedMs, setElapsedMs] = useState(0);

  // Poll loop: fire a HEAD/GET to / every 2 s.  When it returns OK,
  // navigate the user to "/" (which is the post-reboot landing page).
  // Network errors during the boot window are expected — keep
  // retrying.
  useEffect(() => {
    if (phase !== 'rebooting') return undefined;
    const start = Date.now();
    let cancelled = false;
    const tick = async () => {
      if (cancelled) return;
      setElapsedMs(Date.now() - start);
      try {
        const r = await fetch('/', { method: 'GET', cache: 'no-store' });
        if (r.ok) {
          location.href = '/';
          return;
        }
      } catch {
        // Connection refused / DNS / aborted — device still rebooting.
      }
      if (!cancelled) setTimeout(tick, REBOOT_POLL_INTERVAL_MS);
    };
    setTimeout(tick, REBOOT_POLL_INTERVAL_MS);
    return () => { cancelled = true; };
  }, [phase]);

  const onConfirm = async () => {
    setError(null);
    try {
      await postJson('/api/reboot', {});
    } catch (e) {
      // /api/reboot returns ok before the actual restart, but a
      // proxy may report a connection drop as the device tears down
      // WiFi.  Treat any error as success — the polling loop is the
      // real check.
    }
    setPhase('rebooting');
  };

  return html`
    <${PageShell} active="reboot">
      <div style=${{ maxWidth: '480px', margin: '0 auto', padding: '12px' }}>
        ${phase === 'confirm' && html`
          <p style=${{ color: 'red' }}>
            Confirm that you want to reboot OnSpeed.
          </p>
          ${error && html`<p style=${{ color: 'red' }}>${error}</p>`}
          <button type="button" class="button" onClick=${onConfirm}>Reboot</button>
          ${' '}<a href="/">Cancel</a>`}
        ${phase === 'rebooting' && html`
          <p>OnSpeed is rebooting.  Waiting for the device to come back…</p>
          <p>Elapsed: ${(elapsedMs / 1000).toFixed(0)} s</p>
          <p style=${{ color: '#888' }}>
            This page will redirect home automatically once the device
            answers.
          </p>`}
      </div>
    <//>`;
}
