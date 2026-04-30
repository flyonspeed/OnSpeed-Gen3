// Render-smoke tests for Preact pages.
//
// Runs Preact's real render() into a minimal mock DOM, then asserts
// that key DOM landmarks (`<svg>` for IndexerPage, `.live-page` div
// `<ul>` nav for PageShell) appear in the rendered tree.
//
// The mock DOM implements just enough of the Node / Element API for
// Preact to run: createElement / createTextNode / appendChild /
// removeChild / insertBefore / setAttribute / removeAttribute /
// addEventListener.  Anything beyond that raises in tests, which keeps
// the stub honest.
//
// Run with:  node tools/web/test/render-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// ---------------------------------------------------------------------
// Mock DOM
// ---------------------------------------------------------------------
class MockNode {
  constructor(localName) {
    this.localName  = localName;
    this.nodeType   = localName === '#text' ? 3 : 1;
    this.parentNode = null;
    this.childNodes = [];
    this.attributes = [];
    this._attrs    = new Map();
    this._listeners = new Map();
    this.style      = {};
    this.l          = null;
    this.data       = '';
    // Preact reads/writes these properties directly.
    this.value      = '';
    this.checked    = false;
    this.innerHTML  = '';
    this.dataset    = {};
  }
  appendChild(child) {
    if (child.parentNode) child.parentNode.removeChild(child);
    child.parentNode = this;
    this.childNodes.push(child);
    return child;
  }
  removeChild(child) {
    const i = this.childNodes.indexOf(child);
    if (i >= 0) {
      this.childNodes.splice(i, 1);
      child.parentNode = null;
    }
    return child;
  }
  insertBefore(child, ref) {
    if (child.parentNode) child.parentNode.removeChild(child);
    if (!ref) return this.appendChild(child);
    const i = this.childNodes.indexOf(ref);
    if (i < 0) return this.appendChild(child);
    this.childNodes.splice(i, 0, child);
    child.parentNode = this;
    return child;
  }
  get firstChild() { return this.childNodes[0] || null; }
  get nextSibling() {
    const p = this.parentNode;
    if (!p) return null;
    const i = p.childNodes.indexOf(this);
    return p.childNodes[i + 1] || null;
  }
  setAttribute(name, value) {
    this._attrs.set(name, String(value));
    // Mirror into `attributes` array for Preact's diff path.
    let entry = this.attributes.find(a => a.name === name);
    if (!entry) {
      entry = { name, value: String(value) };
      this.attributes.push(entry);
    } else {
      entry.value = String(value);
    }
  }
  removeAttribute(name) {
    this._attrs.delete(name);
    const i = this.attributes.findIndex(a => a.name === name);
    if (i >= 0) this.attributes.splice(i, 1);
  }
  getAttribute(name) {
    return this._attrs.get(name) ?? null;
  }
  addEventListener(type, handler) {
    if (!this._listeners.has(type)) this._listeners.set(type, []);
    this._listeners.get(type).push(handler);
  }
  removeEventListener(type, handler) {
    const arr = this._listeners.get(type);
    if (!arr) return;
    const i = arr.indexOf(handler);
    if (i >= 0) arr.splice(i, 1);
  }
}

const mockDocument = {
  createElement(tag) { return new MockNode(tag); },
  createElementNS(_ns, tag) { return new MockNode(tag); },
  createTextNode(text) {
    const n = new MockNode('#text');
    n.data = String(text);
    return n;
  },
  getElementById: () => null,
  querySelector: () => null,
  querySelectorAll: () => [],
  addEventListener: () => {},
  readyState: 'complete',
};

globalThis.document    = mockDocument;
globalThis.location    = { pathname: '/', protocol: 'http:', hostname: 'localhost' };
globalThis.localStorage = { getItem: () => null, setItem: () => {} };
globalThis.performance  = globalThis.performance || { now: () => 0 };
globalThis.WebSocket    = function () {
  return {
    readyState: 0,
    close: () => {},
    addEventListener: () => {},
  };
};
globalThis.WebSocket.CONNECTING = 0;
globalThis.WebSocket.OPEN       = 1;
globalThis.WebSocket.CLOSING    = 2;
globalThis.WebSocket.CLOSED     = 3;
// CalWizardPage's onMount calls getJson('/api/calwiz/state'); stub
// fetch with a no-op promise so render-smoke can mount the page
// without hitting a real backend.
globalThis.fetch = () => new Promise(() => {});
globalThis.history = { replaceState: () => {} };
globalThis.window = globalThis;
// The Preact bundle reads `document.body` via M(_,t,o); ensure t has
// nothing special.  Keep a fresh container per render to avoid Preact's
// reuse logic seeing stale state.

// ---------------------------------------------------------------------
// Tree walker
// ---------------------------------------------------------------------
function* walk(node) {
  if (!node) return;
  yield node;
  for (const c of node.childNodes) yield* walk(c);
}

function findFirst(root, localName) {
  for (const n of walk(root)) if (n.localName === localName) return n;
  return null;
}

function findFirstWithAttr(root, name, value) {
  for (const n of walk(root)) {
    const v = n.getAttribute && n.getAttribute(name);
    if (v != null && (value == null || v.split(/\s+/).includes(value))) return n;
  }
  return null;
}

// ---------------------------------------------------------------------
// Imports + tests
// ---------------------------------------------------------------------
const preact = await import(new URL('../lib/vendor/preact-standalone.js', import.meta.url));
const { html, render } = preact;

const indexerMod = await import(new URL('../lib/pages/IndexerPage.js', import.meta.url));
const shellMod   = await import(new URL('../lib/shell/PageShell.js',  import.meta.url));
const calwizMod  = await import(new URL('../lib/pages/CalWizardPage.js', import.meta.url));

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

function renderInto(vnode) {
  const root = new MockNode('div');
  render(vnode, root);
  return root;
}

test('IndexerPage exports a function', () => {
  if (typeof indexerMod.IndexerPage !== 'function')
    throw new Error('IndexerPage not exported');
});

test('IndexerPage renders an <svg> landmark', () => {
  const root = renderInto(html`<${indexerMod.IndexerPage} />`);
  const svg = findFirst(root, 'svg');
  if (!svg) throw new Error('expected an <svg> element after rendering IndexerPage');
});

test('IndexerPage renders the mode-nav with five buttons', () => {
  const root = renderInto(html`<${indexerMod.IndexerPage} />`);
  const buttons = [...walk(root)].filter(n => n.localName === 'button'
                                            && n.getAttribute('data-mode'));
  if (buttons.length !== 5)
    throw new Error(`expected 5 mode buttons, got ${buttons.length}`);
});

test('PageShell renders nav <ul> and header-container', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="indexer"
                                                      children=${[]} />`);
  if (!findFirst(root, 'ul')) throw new Error('no <ul> nav');
  // Match the legacy chrome: a `.header-container` div hosting the
  // logo + version, separate from the nav <ul>.
  const header = findFirstWithAttr(root, 'class', 'header-container');
  if (!header) throw new Error('no .header-container');
});

test('PageShell highlights the active page', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="indexer"
                                                      children=${[]} />`);
  // Find an <a> with both href="/indexer" and class containing "active".
  const links = [...walk(root)].filter(n => n.localName === 'a'
                                          && n.getAttribute('href') === '/indexer');
  const active = links.find(l => (l.getAttribute('class') || '').split(/\s+/).includes('active'));
  if (!active) throw new Error('expected the /indexer link to be marked active');
});

test('PageShell Tools dropdown lists the legacy items', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="indexer"
                                                      children=${[]} />`);
  const want = [
    ['/logs',    'Log Files'],
    ['/format',  'Format SD Card'],
    ['/upgrade', 'Firmware Upgrade'],
    ['/reboot',  'Reboot System'],
  ];
  for (const [href, _label] of want) {
    const a = [...walk(root)].find(n => n.localName === 'a'
                                       && n.getAttribute('href') === href);
    if (!a) throw new Error(`Tools dropdown missing link to ${href}`);
  }
});

test('CalWizardPage exports a function', () => {
  if (typeof calwizMod.CalWizardPage !== 'function')
    throw new Error('CalWizardPage not exported');
});

test('CalWizardPage renders the intro step', () => {
  const root = renderInto(html`<${calwizMod.CalWizardPage} />`);
  // Intro step has a `<form>` for aircraft params.
  if (!findFirst(root, 'form'))
    throw new Error('expected a <form> on the intro step');
});

test('analyzeDecel returns ok=false on too-few samples', () => {
  const out = calwizMod.analyzeDecel([], { gLimit: '4' });
  if (out.ok) throw new Error('expected ok=false on empty samples');
  if (!out.error) throw new Error('expected error message');
});

test('analyzeDecel rejects degenerate fits (slope ≈ 0)', () => {
  // Stub window.regression to return a degenerate fit (slope=0,
  // intercept=mean — exactly what the real regression.js returns
  // when the denominator collapses).  This pins the wizard's
  // ≈0-slope guard without needing the UMD vendor bundle in Node.
  const prev = globalThis.window.regression;
  // First polynomial call (IAS-vs-index) — slope -1 so the
  // "airspeed increasing" guard does NOT fire; we want this fixture
  // to reach the kFit check.
  // Subsequent calls return slope 0.
  let callCount = 0;
  globalThis.window.regression = {
    polynomial: () => {
      callCount++;
      if (callCount === 1) {
        return { equation: [-1.0, 100.0], r2: 0.99 };
      }
      return { equation: [0.0, 5.0], r2: 0.0 };
    },
  };
  try {
    // Build a fixture with a real stall (CP rising) so analyzeDecel
    // gets past the "stall not detected" guard.  Sample count > 50.
    const samples = [];
    for (let i = 0; i < 200; i++) {
      samples.push({
        iasKt: 80 - i * 0.1,
        coeffP: 0.3 + i * 0.001,
        derivedAoaDeg: 5 + i * 0.05,
        flapsPosDeg: 0,
        flapIndex: 0,
      });
    }
    const out = calwizMod.analyzeDecel(samples, {
      gLimit: '4', grossWeightLb: '2400', currentWeightLb: '2200',
      bestGlideKt: '85', vfeKt: '100',
    });
    if (out.ok) throw new Error('expected ok=false on a degenerate-fit sample set');
    if (!out.error) throw new Error('expected error message');
  } finally {
    globalThis.window.regression = prev;
  }
});

test('PageShell Settings dropdown lists the legacy items', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="indexer"
                                                      children=${[]} />`);
  const want = ['/aoaconfig', '/sensorconfig', '/calwiz'];
  for (const href of want) {
    const a = [...walk(root)].find(n => n.localName === 'a'
                                       && n.getAttribute('href') === href);
    if (!a) throw new Error(`Settings dropdown missing link to ${href}`);
  }
});

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
