// IndexPage (/): the root landing page.  A short welcome blurb plus
// the global PageShell nav.  The legacy `HandleIndex` was a few lines
// of static HTML embedded in the C++ handler; this Preact replacement
// is the same content rendered through the shared shell.

import { html } from '../vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';

export function IndexPage() {
  return html`
    <${PageShell} active="home">
      <div style=${{ maxWidth: '720px', margin: '0 auto', padding: '12px' }}>
        <h2>Welcome to the OnSpeed Wifi gateway</h2>
        <p>General usage guidelines:</p>
        <ul>
          <li>Connect from one device at a time.</li>
          <li>Visit one page at a time.</li>
          <li>During log file downloads, data recording and tones are disabled.</li>
          <li>Indexer is for debugging purposes only — do not use in flight.</li>
        </ul>
        <p>Use the menu above to reach the configuration, calibration,
           log management, and indexer pages.</p>
      </div>
    <//>`;
}
