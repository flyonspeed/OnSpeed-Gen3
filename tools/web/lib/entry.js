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
// `replay` is intentionally NOT in this static map.  Static-importing
// `ReplayPage` would brick the firmware bundle: the bundler in
// `scripts/build_web_bundle.py` strips `lib/replay/` and
// `lib/pages/ReplayPage.js`, so a static `import { ReplayPage }`
// resolves to an undefined identifier in the concatenated PROGMEM
// blob and aborts every page before mounting.
//
// Dev-server only: when the page id is `replay`, lazy-load the module
// via dynamic `import()`.  The bundler's regex grammar matches only
// the statement form (`import ... from '...';`); dynamic `import()`
// passes through untouched, so the firmware bundle carries the string
// but never evaluates it (the firmware never serves
// `<div data-page="replay">`).
//
// FOLLOW-UP (Sam, 2026-05-10): the long-term home for the Replay tool
// is the docs site (https://flyonspeed.github.io/OnSpeed-Gen3/), NOT
// the on-airplane firmware bundle.  Migrate the dev-server-only entry
// out of `tools/web/lib/` and into the docs-site build pipeline; once
// that's done this dynamic-import branch and the bundler's
// `replay/` exclusion both delete.
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

async function resolveComponent(pageId) {
  if (PAGES[pageId]) return PAGES[pageId];
  if (pageId === 'replay') {
    // Dynamic; absent from the firmware bundle by design.
    const mod = await import('./pages/ReplayPage.js');
    return mod.ReplayPage;
  }
  return null;
}

export function start() {
  if (typeof document === 'undefined') return;
  const root = document.getElementById('app');
  if (!root) {
    console.error('OnSpeed: #app element not found');
    return;
  }
  const pageId = root.dataset.page;
  resolveComponent(pageId).then(Component => {
    if (!Component) {
      console.error(`OnSpeed: no page component registered for "${pageId}"`);
      return;
    }
    render(html`<${Component} />`, root);
  }).catch(err => {
    console.error(`OnSpeed: failed to load page "${pageId}":`, err);
  });
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
