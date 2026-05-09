// Bundle entry point.  The bundler concatenates every JS file under
// lib/ into a single PROGMEM-served bundle.  The page stub the firmware
// emits has `<div id="app" data-page="indexer">` (or whichever page);
// `start()` looks up the matching page component and renders it inside
// PageShell.

import { html, render } from './vendor/preact-standalone.js';
import { IndexerPage } from './pages/IndexerPage.js';
import { CalWizardPage } from './pages/CalWizardPage.js';
import { IndexPage } from './pages/IndexPage.js';
import { RebootPage } from './pages/RebootPage.js';
import { FormatPage } from './pages/FormatPage.js';
import { UpgradePage } from './pages/UpgradePage.js';
import { LogsPage } from './pages/LogsPage.js';
import { SensorCalPage } from './pages/SensorCalPage.js';

// Page registry.  Keep this in sync with the bundler's stub list (see
// `scripts/build_web_bundle.py`'s PAGES table).  Adding a page means
// adding a stub there AND a row here.
//
// `replay` is intentionally absent: the Video Replay tool lives under
// lib/replay/ and lib/pages/ReplayPage.js, both excluded from the
// firmware bundle by `scripts/build_web_bundle.py` (it is a
// dev-server / offline-analysis tool).  Importing ReplayPage here
// would resolve to an undefined identifier in the concatenated
// bundle and abort the entire script before any page mounts.
const PAGES = {
  indexer:      IndexerPage,
  calwiz:       CalWizardPage,
  home:         IndexPage,
  reboot:       RebootPage,
  format:       FormatPage,
  upgrade:      UpgradePage,
  logs:         LogsPage,
  sensorconfig: SensorCalPage,
};

export function start() {
  if (typeof document === 'undefined') return;
  const root = document.getElementById('app');
  if (!root) {
    console.error('OnSpeed: #app element not found');
    return;
  }
  const pageId = root.dataset.page;
  const Component = PAGES[pageId];
  if (!Component) {
    console.error(`OnSpeed: no page component registered for "${pageId}"`);
    return;
  }
  render(html`<${Component} />`, root);
}

// Auto-start when loaded as a top-level script.  Defer in the script
// tag handles the DOMContentLoaded case; if a caller imports this
// module as part of a build pipeline, they can re-call start() after
// hydrating.
if (typeof document !== 'undefined') {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
  } else {
    start();
  }
}
