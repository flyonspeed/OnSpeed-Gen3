// CSS-coverage tests.
//
// Locks in the styling fixes from PR7 so future PRs can't reintroduce
// the regressions:
//   - legacy form/button/layout classes are present in the bundle
//     (so Preact pages render with red `.button`, `.content-container`,
//     `.round-box`, `.form-divs flex-col-N`, etc., not native OS UI)
//   - chrome `ul` and `li` rules are scoped to `#liveview-nav-ul`
//     (so a content `<ul>` doesn't pick up the dark-bar background)
//
// pageHeader (the legacy server-rendered chrome) now links to the same
// /static/app-<sha>.css bundle the Preact pages use, so /aoaconfig and
// /sensorconfig render against the exact same stylesheet that the
// dev-server serves.  Single source of truth — no drift possible.
//
// Run with:  node tools/web/test/css-coverage.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';
import fs from 'node:fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const REPO_ROOT  = path.resolve(__dirname, '..', '..', '..');

const PAGE_SHELL_CSS  = path.join(REPO_ROOT, 'tools', 'web', 'lib', 'shell', 'PageShell.css');
const LEGACY_FORMS_CSS = path.join(REPO_ROOT, 'tools', 'web', 'lib', 'shell', 'legacy-forms.css');
const LEGACY_HEADER_H     = path.join(REPO_ROOT, 'software', 'OnSpeed-Gen3-ESP32',
                                      'Web', 'html_header.h');

let passed = 0, failed = 0;
const results = [];

function test(name, fn) {
  try {
    fn();
    passed++;
    results.push(['  PASS', name]);
  } catch (e) {
    failed++;
    results.push(['  FAIL', `${name}: ${e.message}`]);
  }
}

function read(p) {
  if (!fs.existsSync(p)) throw new Error(`missing file: ${p}`);
  return fs.readFileSync(p, 'utf-8');
}

// Concat the two CSS files we ship to firmware.  The bundler does the
// same in scripts/build_web_bundle.py::_bundle_css() — keep this list
// in sync if the bundler grows additional sources.
function bundledCss() {
  return read(PAGE_SHELL_CSS) + '\n' + read(LEGACY_FORMS_CSS);
}

// ---------------------------------------------------------------------
// Bundled CSS must contain the form/button/layout vocabulary the
// Preact pages and the legacy /aoaconfig template reference.
// ---------------------------------------------------------------------
const REQUIRED_SELECTORS = [
  '.button',
  '.greybutton',
  '.redbutton',
  '.blackbutton',
  '.inputField',
  '.content-container',
  '.round-box',
  '.form-divs',
  '.flex-col-1',
  '.flex-col-12',
  '@media screen and (max-width: 768px)',
  '.upload-btn-wrapper',
  '.upload-btn',
  '.switch-field',
  '.error-field',
  '.error',
  '.sp-row',
  '.sp-btn',
  '.sp-mult',
  '.sp-aoa-input',
  '.sp-live-btn',
  '.sp-info',
  '.sp-vs-info',
  '.curvelabel',
  '.radio-group',
  '.quick-help',
  // Header chrome — lives in PageShell.css.
  '.header-container',
  '.firmware',
  '.logo',
  // section h2 (titled card).
  'section h2',
];

for (const sel of REQUIRED_SELECTORS) {
  test(`bundled CSS has selector: ${sel}`, () => {
    const css = bundledCss();
    if (!css.includes(sel))
      throw new Error(`selector \`${sel}\` missing from bundled CSS`);
  });
}

// ---------------------------------------------------------------------
// Chrome rules MUST be scoped to #liveview-nav-ul.  An unscoped
// `ul { background-color: #333 }` rule poisons every content `<ul>`.
// ---------------------------------------------------------------------
test('PageShell.css: nav ul is scoped to #liveview-nav-ul', () => {
  const css = read(PAGE_SHELL_CSS);
  // Look for an unscoped `ul {` rule (allowing whitespace/comments).
  // The chrome rule should always be `ul#liveview-nav-ul {`.
  const lines = css.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim();
    // Allow comment lines.
    if (line.startsWith('/*') || line.startsWith('*')) continue;
    // Match a bare `ul {` with no preceding ID/class qualifier.
    if (/^ul\s*\{/.test(line)) {
      throw new Error(`PageShell.css line ${i + 1}: bare \`ul {\` rule — must be \`ul#liveview-nav-ul {\``);
    }
  }
});

test('PageShell.css: nav li is scoped', () => {
  const css = read(PAGE_SHELL_CSS);
  const lines = css.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim();
    if (line.startsWith('/*') || line.startsWith('*')) continue;
    if (/^li\s*\{/.test(line)) {
      throw new Error(`PageShell.css line ${i + 1}: bare \`li {\` rule — must be scoped to \`#liveview-nav-ul\``);
    }
    if (/^li\s+a\s*[,{]/.test(line)) {
      throw new Error(`PageShell.css line ${i + 1}: bare \`li a\` selector — must be scoped`);
    }
  }
});

// ---------------------------------------------------------------------
// The legacy nav `<ul>` in html_header.h must carry the
// `id="liveview-nav-ul"` attribute the scoped CSS targets.
// ---------------------------------------------------------------------
test('legacy html_header.h: nav <ul> has id="liveview-nav-ul"', () => {
  const html = read(LEGACY_HEADER_H);
  if (!html.includes('<ul id="liveview-nav-ul">'))
    throw new Error('html_header.h: nav `<ul>` missing `id="liveview-nav-ul"`');
});

// ---------------------------------------------------------------------
// PageShell.js's nav element renders with `id="liveview-nav-ul"` so
// the JS-driven menu state and the scoped CSS agree.
// ---------------------------------------------------------------------
test('PageShell.js: nav <ul> has id="liveview-nav-ul"', () => {
  const js = read(path.join(REPO_ROOT, 'tools', 'web', 'lib', 'shell', 'PageShell.js'));
  if (!js.includes('id="liveview-nav-ul"'))
    throw new Error('PageShell.js: nav `<ul>` missing `id="liveview-nav-ul"`');
});

// ---------------------------------------------------------------------
// Print summary
// ---------------------------------------------------------------------
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
