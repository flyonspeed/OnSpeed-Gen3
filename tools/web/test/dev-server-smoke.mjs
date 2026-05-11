// dev-server-smoke.mjs — start the dev server, fetch every registered
// page, extract every <link href> / <script src> referenced from each
// page's HTML, fetch all of them, assert no 404s.
//
// This is the test class that would have caught the asset-404 bugs
// from PRs #512 and #524. Neither would have shown up in the existing
// unit tests because none of them load the URL the firmware will
// actually serve.
//
// No real browser, no Playwright, no npm install — just plain
// Node fetch() against the dev server's HTTP surface.
//
// Run:
//   node tools/web/test/dev-server-smoke.mjs
//
// Exit codes:
//   0 — all assets resolved
//   1 — at least one asset 404'd or test infra failed

import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const REPO_ROOT  = path.resolve(__dirname, '..', '..', '..');
const SERVER     = path.join(REPO_ROOT, 'tools', 'web', 'dev-server', 'server.mjs');
const PORT       = 9099;  // not 9001, to avoid clashing with a dev session
const BASE       = `http://localhost:${PORT}`;

// Pages every dev server registers. Mirrors PAGES in server.mjs.
const PAGES = ['/indexer', '/calwiz', '/', '/reboot', '/format',
               '/upgrade', '/logs', '/sensorconfig'];

// 404s we accept and ignore. The favicon is the only one currently;
// the firmware doesn't serve a favicon either.
const ACCEPTABLE_404S = new Set(['/favicon.ico']);

let passed = 0;
let failed = 0;
const failures = [];

function fail(msg) {
  failed++;
  failures.push(msg);
  console.error(`FAIL ${msg}`);
}
function pass(msg) {
  passed++;
  console.log(`PASS ${msg}`);
}

async function waitForServer(timeoutMs = 10000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await fetch(`${BASE}/`, { signal: AbortSignal.timeout(500) });
      if (r.ok || r.status === 404) return;  // server is up, just maybe no /
    } catch {
      // not yet
    }
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error(`dev server didn't come up on ${BASE} within ${timeoutMs}ms`);
}

// Extract <link href> and <script src> URLs from an HTML string.
// Returns an array of {tag, url} pairs.  Tolerates either quote style
// and either order of attributes.
function extractAssetUrls(html) {
  const out = [];
  const linkRe = /<link\b[^>]*\bhref\s*=\s*["']([^"']+)["'][^>]*>/gi;
  const scriptRe = /<script\b[^>]*\bsrc\s*=\s*["']([^"']+)["'][^>]*>/gi;
  for (const m of html.matchAll(linkRe))   out.push({ tag: 'link',   url: m[1] });
  for (const m of html.matchAll(scriptRe)) out.push({ tag: 'script', url: m[1] });
  return out;
}

async function checkPage(pagePath) {
  const url = `${BASE}${pagePath}`;
  const r = await fetch(url, { redirect: 'follow' });
  if (!r.ok) {
    fail(`${pagePath}: HTTP ${r.status}`);
    return;
  }
  const ct = r.headers.get('content-type') || '';
  if (!ct.includes('text/html')) {
    fail(`${pagePath}: content-type ${ct}, expected text/html`);
    return;
  }
  const html = await r.text();
  const assets = extractAssetUrls(html);
  if (assets.length === 0) {
    fail(`${pagePath}: no <link> or <script> tags found in HTML`);
    return;
  }
  pass(`${pagePath} → 200, ${assets.length} assets referenced`);
  // Now HEAD/GET each asset URL.  Same-origin only — skip any external
  // resources (e.g. CDN refs, which the firmware shouldn't have but
  // tests should tolerate).
  for (const a of assets) {
    let assetUrl;
    try {
      assetUrl = new URL(a.url, BASE).toString();
    } catch {
      fail(`${pagePath}: malformed asset URL ${a.url}`);
      continue;
    }
    if (!assetUrl.startsWith(BASE)) continue;  // external; skip
    const ar = await fetch(assetUrl);
    if (ar.status === 404 &&
        ACCEPTABLE_404S.has(new URL(assetUrl).pathname)) {
      continue;
    }
    if (!ar.ok) {
      fail(`${pagePath} → ${a.tag} ${a.url}: HTTP ${ar.status}`);
    } else {
      pass(`  ${a.tag} ${a.url} → ${ar.status}`);
    }
  }
}

async function main() {
  console.log(`[dev-server-smoke] starting dev server on :${PORT}...`);
  const child = spawn('node', [SERVER, '--port', String(PORT), '--mock'], {
    cwd: REPO_ROOT,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let serverOutput = '';
  child.stdout.on('data', (b) => { serverOutput += b.toString(); });
  child.stderr.on('data', (b) => { serverOutput += b.toString(); });
  child.on('exit', (code) => {
    if (code !== null && code !== 0) {
      console.error(`[dev-server-smoke] dev server exited unexpectedly (code=${code})`);
      console.error('--- server output ---\n' + serverOutput);
    }
  });

  try {
    await waitForServer();
    for (const p of PAGES) {
      await checkPage(p);
    }
  } catch (err) {
    fail(`harness: ${err.message}`);
    console.error('--- server output ---\n' + serverOutput);
  } finally {
    child.kill('SIGTERM');
    // Give the server a moment to clean up
    await new Promise((r) => setTimeout(r, 200));
  }

  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) {
    console.error('FAILURES:');
    for (const f of failures) console.error('  - ' + f);
    process.exit(1);
  }
  process.exit(0);
}

main().catch((err) => {
  console.error('[dev-server-smoke] unexpected error:', err);
  process.exit(1);
});
