// replay-entry.js — mounts the replay app into the docs-site page.
//
// Sibling file replay.md embeds:
//   <div id="replay-app"></div>
//   <script type="module" src="replay/replay-entry.js"></script>
//
// This module imports ReplayPage from the relocated lib/ tree and
// renders it into the mount point. No firmware-bundle entry.js
// chrome — the MkDocs page provides its own header / nav.

import { html, render } from './lib/vendor/preact-standalone.js';
import { ReplayPage } from './lib/pages/ReplayPage.js';

function start() {
  const root = document.getElementById('replay-app');
  if (!root) {
    console.error('replay-entry: #replay-app mount point not found');
    return;
  }
  render(html`<${ReplayPage} />`, root);
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', start);
} else {
  start();
}
