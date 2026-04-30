// Render-smoke tests for Preact pages.
//
// Runs Preact's real render() into a minimal mock DOM, then asserts
// that key DOM landmarks (`<svg>` for IndexerPage, `.live-page` div
// for LivePage, `<ul>` nav for PageShell) appear in the rendered tree.
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
const liveMod    = await import(new URL('../lib/pages/LivePage.js',    import.meta.url));
const shellMod   = await import(new URL('../lib/shell/PageShell.js',  import.meta.url));

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

test('LivePage exports a function', () => {
  if (typeof liveMod.LivePage !== 'function')
    throw new Error('LivePage not exported');
});

test('LivePage renders an <svg> AOA panel', () => {
  const root = renderInto(html`<${liveMod.LivePage} />`);
  const svg = findFirst(root, 'svg');
  if (!svg) throw new Error('expected an <svg> element in LivePage');
});

test('LivePage renders both AOA / AHRS tab buttons', () => {
  const root = renderInto(html`<${liveMod.LivePage} />`);
  const tabs = [...walk(root)].filter(n => n.localName === 'button'
                                         && n.getAttribute('role') === 'tab');
  if (tabs.length !== 2)
    throw new Error(`expected 2 tab buttons, got ${tabs.length}`);
});

test('LivePage data table has 13 rows (12 fields + Age)', () => {
  const root = renderInto(html`<${liveMod.LivePage} />`);
  const rows = [...walk(root)].filter(n => n.localName === 'tr');
  if (rows.length !== 13)
    throw new Error(`expected 13 data rows, got ${rows.length}`);
});

test('PageShell renders nav <ul> and header-container', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="live"
                                                      children=${[]} />`);
  if (!findFirst(root, 'ul')) throw new Error('no <ul> nav');
  // Match the legacy chrome: a `.header-container` div hosting the
  // logo + version, separate from the nav <ul>.
  const header = findFirstWithAttr(root, 'class', 'header-container');
  if (!header) throw new Error('no .header-container');
});

test('PageShell highlights the active page', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="live"
                                                      children=${[]} />`);
  // Find an <a> with both href="/live" and class containing "active".
  const links = [...walk(root)].filter(n => n.localName === 'a'
                                          && n.getAttribute('href') === '/live');
  const active = links.find(l => (l.getAttribute('class') || '').split(/\s+/).includes('active'));
  if (!active) throw new Error('expected the /live link to be marked active');
});

test('PageShell Tools dropdown lists the legacy items', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="live"
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

test('PageShell Settings dropdown lists the legacy items', () => {
  const root = renderInto(html`<${shellMod.PageShell} active="live"
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
