// Bundle-execution test.
//
// The Preact source modules are exercised by render-smoke.mjs, but
// that loads each module a la carte via `import` — so it cannot
// catch bugs introduced by the bundler's concatenation pass: a module
// scope `import { Foo }` that resolves at module-load time becomes a
// free identifier `Foo` in the concatenated script, and any free
// identifier that has no top-level declaration in the bundle becomes
// a runtime ReferenceError that aborts the entire script before
// `start()` runs.  The browser's only signal is a blank page.
//
// This test runs the actual artifact the firmware ships:
//
//   1. Invoke `scripts/build_web_bundle.py` to (re)generate the
//      PROGMEM headers.
//   2. Read static_app_js.h, extract the `0x..` byte array, gunzip.
//   3. For every page id in PAGES (mirrored from build_web_bundle.py),
//      eval the bundle in a `vm.Script` with a minimal DOM stub
//      whose #app element has `data-page="<id>"`.
//   4. Assert the script eval returns without throwing.
//
// It will fail loudly on:
//   - top-level identifiers referenced in entry.js but not present
//     in the bundle (the bug that bricked /indexer + every other
//     Preact page on master once the Replay page was registered
//     while its source was excluded from the bundle);
//   - duplicate-identifier collisions the bundler's regex check
//     missed (indented declarations etc.);
//   - any other runtime error during top-level evaluation
//     (TypeError on object spread, etc.).
//
// Run with:  node tools/web/test/bundle-execution.mjs

import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import zlib from 'node:zlib';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const REPO_ROOT  = path.resolve(__dirname, '..', '..', '..');
const BUNDLER    = path.join(REPO_ROOT, 'scripts', 'build_web_bundle.py');
const HEADER     = path.join(REPO_ROOT, 'software', 'OnSpeed-Gen3-ESP32',
                             'Web', 'static_app_js.h');

// Mirror PAGES in scripts/build_web_bundle.py.  If a page id is added
// to the bundler, add it here too — the test will then verify the
// bundle still loads cleanly for that page.
const PAGE_IDS = [
  'indexer',
  'calwiz',
  'home',
  'reboot',
  'format',
  'upgrade',
  'logs',
  'sensorconfig',
];

function buildBundle() {
  // Force regeneration so the test can never go stale against an old
  // header file lying around in the workspace.
  if (fs.existsSync(HEADER)) fs.unlinkSync(HEADER);
  execFileSync('python3', [BUNDLER], { cwd: REPO_ROOT, stdio: 'inherit' });
}

function extractBundleSource() {
  const text = fs.readFileSync(HEADER, 'utf8');
  const hex  = text.match(/0x[0-9a-fA-F]{2}/g) || [];
  if (hex.length === 0) {
    throw new Error(`bundle-execution: no 0x.. bytes in ${HEADER}`);
  }
  const gz = Buffer.from(hex.map(h => parseInt(h, 16)));
  return zlib.gunzipSync(gz).toString('utf8');
}

// Minimal DOM stub.  Only the surface the bundle's top-level code
// touches before the test calls it a pass: looking up `#app`,
// reading meta tags, registering a DOMContentLoaded handler, etc.
// We intentionally keep this thin; we are NOT trying to render — we
// are checking that the script evaluates without throwing.
class StubElement {
  constructor(tag) {
    this.tagName    = (tag || 'DIV').toUpperCase();
    this.localName  = (tag || 'div').toLowerCase();
    this.children   = [];
    this.childNodes = [];
    this.attributes = [];
    this._attrs     = new Map();
    this.dataset    = {};
    this.style      = {};
    this.classList  = {
      add()    {}, remove() {}, toggle() {},
      contains() { return false; },
    };
    this.parentNode  = null;
    this.firstChild  = null;
    this.nextSibling = null;
    this.innerHTML   = '';
    this.textContent = '';
  }
  appendChild(c) { this.children.push(c); this.childNodes.push(c); c.parentNode = this; return c; }
  removeChild(c) { return c; }
  insertBefore(c) { this.children.push(c); this.childNodes.push(c); return c; }
  setAttribute(k, v) { this._attrs.set(k, String(v)); }
  getAttribute(k) { return this._attrs.has(k) ? this._attrs.get(k) : null; }
  removeAttribute(k) { this._attrs.delete(k); }
  hasAttribute(k) { return this._attrs.has(k); }
  addEventListener() {}
  removeEventListener() {}
  querySelector() { return null; }
  querySelectorAll() { return []; }
  getBoundingClientRect() {
    return { x:0, y:0, width:0, height:0, left:0, top:0, right:0, bottom:0 };
  }
}

function buildContext(pageId) {
  const appEl = new StubElement('div');
  appEl.id = 'app';
  appEl.dataset.page = pageId;

  const head = new StubElement('head');
  const body = new StubElement('body');
  body.appendChild(appEl);

  const meta = (name, content) => {
    const m = new StubElement('meta');
    m._attrs.set('name', name);
    m._attrs.set('content', content);
    return m;
  };
  const metas = [
    meta('onspeed-version', 'test'),
    meta('onspeed-logo',    ''),
    meta('onspeed-ws',      'ws://127.0.0.1:65535/ws'),
  ];

  // The bundle calls `document.addEventListener('DOMContentLoaded', start)`
  // when document.readyState !== 'loading'.  Setting readyState to
  // 'complete' makes `start()` fire synchronously via the else branch,
  // so any error inside start() surfaces during script eval.
  const document = {
    readyState: 'complete',
    documentElement: new StubElement('html'),
    head, body,
    createElement(tag)        { return new StubElement(tag); },
    createTextNode(t)         { const n = new StubElement('#text'); n.data = String(t); return n; },
    createElementNS(_ns, tag) { return new StubElement(tag); },
    getElementById(id)        { return id === 'app' ? appEl : null; },
    querySelector(sel) {
      if (sel === '#app') return appEl;
      const m = sel.match(/^meta\[name="([^"]+)"\]$/);
      if (m) return metas.find(x => x._attrs.get('name') === m[1]) || null;
      if (sel === 'head') return head;
      if (sel === 'body') return body;
      return null;
    },
    querySelectorAll(sel) {
      const r = this.querySelector(sel);
      return r ? [r] : [];
    },
    addEventListener() {},
    removeEventListener() {},
  };

  class StubWebSocket {
    constructor(url) { this.url = url; this.readyState = 0; }
    addEventListener() {} removeEventListener() {} send() {} close() {}
    set onopen   (_) {} set onmessage(_) {}
    set onclose  (_) {} set onerror  (_) {}
  }
  StubWebSocket.CONNECTING = 0;
  StubWebSocket.OPEN       = 1;
  StubWebSocket.CLOSING    = 2;
  StubWebSocket.CLOSED     = 3;

  const win = {
    document,
    location: {
      protocol: 'http:', host: 'localhost', hostname: 'localhost',
      port: '',           pathname: '/' + pageId,
      href: 'http://localhost/' + pageId,
      origin: 'http://localhost',
      search: '',         hash: '',
    },
    navigator: { userAgent: 'node-bundle-test' },
    WebSocket: StubWebSocket,
    // Fake timers — record-only. The bundle's reconnect-on-close
    // WebSocket client and Preact's render scheduler both queue
    // timers/RAFs at top-level eval; if those used Node's real
    // setTimeout, runInContext returns promptly but Node's event
    // loop blocks process exit (and the next-page eval) until every
    // timer drains. With ~7 pages × infinite reconnect retry, the
    // wall-clock cost is multiple minutes for a test that should be
    // checking only "does top-level eval throw?".
    requestAnimationFrame: () => 0,
    cancelAnimationFrame:  () => {},
    setTimeout:  () => 0,
    clearTimeout: () => {},
    setInterval: () => 0,
    clearInterval: () => {},
    queueMicrotask: () => {},
    console,
    addEventListener() {}, removeEventListener() {},
    fetch: () => Promise.reject(new Error('fetch stubbed out')),
    performance: { now: () => Date.now() },
    matchMedia: () => ({ matches: false, addEventListener(){}, removeEventListener(){} }),
    localStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
    history: { replaceState: () => {}, pushState: () => {} },
  };
  win.window     = win;
  win.self       = win;
  win.globalThis = win;
  return win;
}

function runForPage(bundleSrc, pageId) {
  const ctx = vm.createContext(buildContext(pageId));
  ctx.global = ctx;
  const script = new vm.Script(bundleSrc, { filename: 'bundle.js' });
  script.runInContext(ctx, { displayErrors: true });
}

// ---------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------
buildBundle();
const bundle = extractBundleSource();

let failed = 0;
for (const pageId of PAGE_IDS) {
  process.stdout.write(`bundle-execution[${pageId}] ... `);
  try {
    runForPage(bundle, pageId);
    process.stdout.write('ok\n');
  } catch (e) {
    failed += 1;
    process.stdout.write('FAIL\n');
    console.error(`  ${e.name}: ${e.message}`);
    if (e.stack) {
      const head = e.stack.split('\n').slice(0, 5).join('\n  ');
      console.error('  ' + head);
    }
  }
}

if (failed > 0) {
  console.error(`bundle-execution: ${failed}/${PAGE_IDS.length} pages broken`);
  process.exit(1);
}
console.log(`bundle-execution: ${PAGE_IDS.length}/${PAGE_IDS.length} pages OK`);
