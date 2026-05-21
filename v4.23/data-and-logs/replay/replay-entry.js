// replay-entry.js — mounts the replay app into the docs-site page.
//
// Sibling file replay.md embeds:
//   <div id="replay-app"></div>
//   <script type="module" src="replay-entry.js"></script>
//
// The bare filenames matter: replay.md is rendered at URL
// /data-and-logs/replay/ (MkDocs use_directory_urls), and the assets
// are siblings of the rendered page, not nested one level deeper.
//
// IMPORTANT — RELATIVE-PATH COUNT IS URL-SPACE, NOT SOURCE-SPACE:
// The packages/ui-core/ import below uses 2 `..` because the page is
// deployed under /latest/data-and-logs/replay/ (mike versions docs at
// /latest/ and /<release>/). Going up 2 reaches /latest/, then
// packages/ui-core/ resolves to /latest/packages/ui-core/ where the
// files actually live in the deployed site. Source-file resolution
// via the docs/site/docs/packages → ../../../packages symlink works
// at the same 2-up count. Longer ..-walks (e.g. 5) work locally
// because mkdocs serves from / with no version prefix, but break in
// production because they walk past /latest/ to / and 404.
//
// Tracked in issue #525 ("emit a replay bundle so we stop counting
// ..s by hand"). Once that ships, all of these imports collapse into
// a single sibling-relative <script src="replay-bundle.js"> tag.
//
// This module imports ReplayPage from the relocated lib/ tree and
// renders it into the mount point. No firmware-bundle entry.js
// chrome — the MkDocs page provides its own header / nav.

import { html, render } from '../../packages/ui-core/vendor/preact-standalone.js';
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
