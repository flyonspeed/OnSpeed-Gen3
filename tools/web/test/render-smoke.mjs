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
// Pages that subscribe to window-level events (CalWizardPage's
// keyboard handlers, IndexerPage's resize observer, etc.) need
// these as no-ops in the mock environment.
globalThis.addEventListener    = () => {};
globalThis.removeEventListener = () => {};
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
const logsMod    = await import(new URL('../lib/pages/LogsPage.js', import.meta.url));
const indexMod   = await import(new URL('../lib/pages/IndexPage.js', import.meta.url));
const rebootMod  = await import(new URL('../lib/pages/RebootPage.js', import.meta.url));
const formatMod  = await import(new URL('../lib/pages/FormatPage.js', import.meta.url));
const upgradeMod = await import(new URL('../lib/pages/UpgradePage.js', import.meta.url));
const sensorCalMod = await import(new URL('../lib/pages/SensorCalPage.js', import.meta.url));
const modesMod   = await import(new URL('../lib/modes.js', import.meta.url));

let passed = 0, failed = 0;
const results = [];

// Tests can return a Promise; we await each one in order so async
// fetches + state updates can flush before we move on.
const _pending = [];
function test(name, fn) {
  _pending.push({ name, fn });
}

async function runTests() {
  for (const { name, fn } of _pending) {
    try {
      const r = fn();
      if (r && typeof r.then === 'function') await r;
      passed++;
      results.push(['  PASS', name]);
    } catch (e) {
      failed++;
      results.push(['  FAIL', `${name}: ${e.message}`]);
    }
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

// Render LogsPage, drive it through a real /api/logs response that
// includes a delete error from the most recent bulk request, and
// confirm the red error banner appears with the file name + reason.
//
// LogsPage doesn't expose deleteErrors state directly; we install a
// stub fetch + click the per-row trash button + await macro tasks
// until the second render flushes.  If LogsPage stops surfacing the
// per-file errors array, this test fails.
test('LogsPage renders the delete-error banner', async () => {
  const initialBody = {
    activeLog: 'active.csv',
    files: [
      { name: 'a.csv', size: 100 },
      { name: 'active.csv', size: 200 },
    ],
  };
  const deleteResponseBody = {
    deleted: [],
    errors: [{ name: 'a.csv', reason: 'SD busy' }],
  };

  let postCalled = false;
  const prevFetch = globalThis.fetch;
  globalThis.fetch = (url, opts = {}) => {
    if ((opts.method || 'GET') === 'POST') {
      postCalled = true;
      return Promise.resolve({
        ok: true,
        status: 200,
        statusText: 'OK',
        json: () => Promise.resolve(deleteResponseBody),
      });
    }
    return Promise.resolve({
      ok: true,
      status: 200,
      statusText: 'OK',
      json: () => Promise.resolve(initialBody),
    });
  };
  const prevConfirm = globalThis.window.confirm;
  globalThis.window.confirm = () => true;

  try {
    const root = renderInto(html`<${logsMod.LogsPage} />`);

    // Preact schedules `useEffect` callbacks via requestAnimationFrame
    // or, in environments without rAF, a 100ms setTimeout.  Walk the
    // event loop in 20ms slices, capped at 60 (~1.2s) so a regression
    // doesn't hang the test.
    const waitFor = async (predicate, label) => {
      for (let i = 0; i < 60; i++) {
        await new Promise(r => setTimeout(r, 20));
        if (predicate()) return;
      }
      throw new Error('timed out waiting for: ' + label);
    };

    await waitFor(() => findFirst(root, 'tbody'),
                  'initial /api/logs fetch + render');

    // Find the trash button on the non-active row and fire its onClick.
    // The button renders an inline <svg> trash icon; identify it by the
    // child <svg> element rather than text content.
    const buttons = [...walk(root)].filter(n => n.localName === 'button');
    const trash = buttons.find(b => {
      const kids = b.childNodes || [];
      return kids.some(k => k && k.localName === 'svg');
    });
    if (!trash) throw new Error('no trash (svg) button rendered');
    // Preact stores listeners on `l` keyed by `<event-type><capture>`,
    // where capture is the literal string "false" / "true".  So
    // `onClick` lives at `l['clickfalse']`.
    // Preact stores listeners on `l` keyed by `<event-type><capture>`.
    // The mock DOM doesn't expose `click` as a property, so Preact
    // keeps the original "Click" casing rather than lowercasing it.
    const click = trash.l && (trash.l['Clickfalse'] || trash.l['clickfalse']);
    if (typeof click !== 'function')
      throw new Error('trash button has no click handler (l keys: ' +
        Object.keys(trash.l || {}).join(',') + ')');
    click({ type: 'click' });

    await waitFor(() => postCalled, 'delete-bulk POST');
    await waitFor(() => [...walk(root)].some(n =>
      n.localName === '#text' && n.data && n.data.includes('a.csv (SD busy)')),
                  'delete-error banner text');
  } finally {
    globalThis.fetch = prevFetch;
    globalThis.window.confirm = prevConfirm;
  }
});

// ---------------------------------------------------------------------
// PR7 styling-regression guards.  These tests fail if a future PR
// drops a legacy form/button class from a Preact page or attaches an
// inline dark background to a content `<ul>` (which would mean the
// global chrome rule leaked into content).
// ---------------------------------------------------------------------
test('IndexPage content <ul> has no inline background (chrome leak guard)', () => {
  const root = renderInto(html`<${indexMod.IndexPage} />`);
  const uls = [...walk(root)].filter(n => n.localName === 'ul');
  // Filter out the nav ul (id="liveview-nav-ul") — chrome owns its
  // styling.  Content `<ul>`s must not carry an inline background.
  const contentUls = uls.filter(u => u.getAttribute('id') !== 'liveview-nav-ul');
  if (contentUls.length === 0)
    throw new Error('IndexPage no longer renders a content <ul>; update guard');
  for (const ul of contentUls) {
    const bg = ul.style && ul.style.backgroundColor;
    if (bg && bg !== '' && bg !== 'transparent')
      throw new Error(`content <ul> has inline background "${bg}"`);
    // Also no inline `style="background:..."` attribute.
    const styleAttr = ul.getAttribute('style') || '';
    if (/background/i.test(styleAttr))
      throw new Error(`content <ul> has inline style "${styleAttr}"`);
  }
});

test('RebootPage renders class="button"', () => {
  const root = renderInto(html`<${rebootMod.RebootPage} />`);
  const buttons = [...walk(root)].filter(n => n.localName === 'button'
                  && (n.getAttribute('class') || '').split(/\s+/).includes('button'));
  if (buttons.length === 0)
    throw new Error('RebootPage has no class="button" element — styling regression');
});

test('FormatPage renders class="button"', () => {
  const root = renderInto(html`<${formatMod.FormatPage} />`);
  const buttons = [...walk(root)].filter(n => n.localName === 'button'
                  && (n.getAttribute('class') || '').split(/\s+/).includes('button'));
  if (buttons.length === 0)
    throw new Error('FormatPage has no class="button" element — styling regression');
});

test('UpgradePage renders class="button"', () => {
  const root = renderInto(html`<${upgradeMod.UpgradePage} />`);
  const buttons = [...walk(root)].filter(n => n.localName === 'button'
                  && (n.getAttribute('class') || '').split(/\s+/).includes('button'));
  if (buttons.length === 0)
    throw new Error('UpgradePage has no class="button" element — styling regression');
});

test('SensorCalPage exports a function', () => {
  if (typeof sensorCalMod.SensorCalPage !== 'function')
    throw new Error('SensorCalPage not exported');
});

test('SensorCalPage renders a <form>', () => {
  // The render-smoke fetch stub returns a never-resolving promise, so
  // SensorCalPage's getJson('/api/sensors/biases') hangs and `biases`
  // stays null.  The page should still render its form skeleton with
  // the "Loading current calibration..." placeholder in the bias panel.
  const root = renderInto(html`<${sensorCalMod.SensorCalPage} />`);
  if (!findFirst(root, 'form'))
    throw new Error('expected a <form> element after rendering SensorCalPage');
});

test('SensorCalPage submit button is disabled while pitch/roll/PAlt are pending', () => {
  // No WS frames have been delivered (the WebSocket stub never fires
  // onmessage), so pitchSmooth.pending and rollSmooth.pending are both
  // true, and PAlt has no manual entry yet.  The "Calibrate Sensors"
  // button must therefore render as disabled — guarding against the
  // C++ all-empty fallback that would silently calibrate against
  // pitch=0 / roll=0.  See PR #451 review.
  const root = renderInto(html`<${sensorCalMod.SensorCalPage} />`);
  const submit = [...walk(root)].find(n => n.localName === 'button'
                                          && n.getAttribute('type') === 'submit');
  if (!submit) throw new Error('no submit button rendered');
  if (submit.getAttribute('disabled') == null)
    throw new Error('expected submit button to be disabled while smoothing windows are pending');
});

test('CalWizardPage renders form-divs and flex-col layout classes', () => {
  const root = renderInto(html`<${calwizMod.CalWizardPage} />`);
  const formDivs = [...walk(root)].filter(n =>
    (n.getAttribute && n.getAttribute('class') || '').split(/\s+/).includes('form-divs'));
  if (formDivs.length === 0)
    throw new Error('CalWizardPage has no class="form-divs" element — styling regression');
});

// Issue #358: when the producer ships JSON `null` for IAS (bIasAlive
// false), the corner readouts must render an em-dash placeholder
// instead of "0", "NaN", or "null".  Mirrors the M5 firmware's
// "--" gating in main.cpp.
function makeNullAirDataRecord() {
  return {
    aoaDeg: -100, aoaIsValid: false, derivedAoaDeg: 0,
    pitchDeg: 0, rollDeg: 0,
    verticalG: 1, lateralG: 0, pitchRate: 0,
    iasKt: null, paltFt: 1000, oatC: null,
    vsiFpm: 0, flightPathDeg: 0,
    decelRate: 0,
    percentLift: 0,
    tonesOnPctLift: 30, onSpeedFastPctLift: 50, onSpeedSlowPctLift: 60,
    stallWarnPctLift: 90, pipPctLift: 70,
    flapsDeg: 0, flapsMinDeg: 0, flapsMaxDeg: 33,
    gOnsetRate: 0, dataMark: 0,
  };
}

function findTextNodes(root) {
  return [...walk(root)].filter(n => n.localName === '#text' && n.data);
}

// Defense-in-depth: rendered text nodes must never contain "null",
// "NaN", or "undefined" — those are signs that fmt() / a corner gate
// missed a non-finite input and a JS coercion leaked through.
function assertNoPlaceholderLeaks(texts) {
  for (const t of texts) {
    if (/null|nan|undefined/i.test(t))
      throw new Error('rendered text contains placeholder leak: ' + JSON.stringify(t));
  }
}

test('Mode0 renders three-dash placeholder for IAS when iasKt is null', () => {
  const r = makeNullAirDataRecord();
  const root = renderInto(html`<${modesMod.Mode0} r=${r} stale=${false} />`);
  const texts = findTextNodes(root).map(n => n.data);
  // IAS dashes are three ASCII hyphens — one per missing digit in the
  // 3-digit IAS field — so the placeholder right-aligns to the "IAS"
  // label above (CornerReadout measures the label and end-anchors the
  // value text).  Mirrors the M5 firmware's snprintf("---").
  if (!texts.some(t => t === '---'))
    throw new Error('expected "---" text node for null IAS, got: ' + texts.join('|'));
  assertNoPlaceholderLeaks(texts);
});

// Walk a CornerReadout's <g data-widget="corner"> group and return its
// two text children's `.data` strings as { label, value }. The
// CornerReadout structure (svg/index.js:94-111) is fixed: first <text>
// is the label, second <text> is the value.
function readCorner(g) {
  const texts = [...walk(g)].filter(n => n.localName === 'text');
  const inner = (t) => {
    // Preact renders ${value} as a single child #text node.
    const child = (t.childNodes || []).find(c => c.localName === '#text');
    return child ? child.data : '';
  };
  return { label: inner(texts[0]), value: inner(texts[1]) };
}

function findCornerByLabel(root, label) {
  const groups = [...walk(root)].filter(n =>
    n.localName === 'g' && n.getAttribute('data-widget') === 'corner');
  return groups.map(readCorner).find(c => c.label === label);
}

// Mode 1 has two CornerReadouts that emit dash placeholders: IAS on
// the left ("---", three ASCII hyphens for the 3-digit IAS field) and
// AOA on the right ("--", two for the 2-digit percent-lift field).
// Each test fixture pins exactly one source: the other corner stays
// numeric so the corner-by-label lookup confirms the dashes land in
// the expected position.
test('Mode1 IAS-corner shows "---" when iasKt is null (AOA stays numeric)', () => {
  const r = { ...makeNullAirDataRecord(), iasKt: null,
              aoaIsValid: true, percentLift: 42 };
  const root = renderInto(html`<${modesMod.Mode1} r=${r} stale=${false} />`);
  const texts = findTextNodes(root).map(n => n.data);
  const ias = findCornerByLabel(root, 'IAS');
  if (!ias) throw new Error('Mode1 has no IAS corner');
  if (ias.value !== '---')
    throw new Error(`IAS corner value should be "---", got ${JSON.stringify(ias.value)}`);
  const aoa = findCornerByLabel(root, 'AOA');
  if (!aoa) throw new Error('Mode1 has no AOA corner');
  if (aoa.value === '--' || aoa.value === '---')
    throw new Error('AOA corner should render numeric percent-lift, got dashes');
  assertNoPlaceholderLeaks(texts);
});

test('Mode1 AOA-corner shows "--" when aoaIsValid is false (IAS stays numeric)', () => {
  const r = { ...makeNullAirDataRecord(), iasKt: 100,
              aoaIsValid: false, aoaDeg: -100 };
  const root = renderInto(html`<${modesMod.Mode1} r=${r} stale=${false} />`);
  const texts = findTextNodes(root).map(n => n.data);
  const aoa = findCornerByLabel(root, 'AOA');
  if (!aoa) throw new Error('Mode1 has no AOA corner');
  if (aoa.value !== '--')
    throw new Error(`AOA corner value should be "--", got ${JSON.stringify(aoa.value)}`);
  const ias = findCornerByLabel(root, 'IAS');
  if (!ias) throw new Error('Mode1 has no IAS corner');
  if (ias.value === '---' || ias.value === '--')
    throw new Error('IAS corner should render the numeric IAS, got dashes');
  assertNoPlaceholderLeaks(texts);
});

test('Mode3 renders "---" for IAS when iasKt is null', () => {
  const r = makeNullAirDataRecord();
  const root = renderInto(html`<${modesMod.Mode3} r=${r} stale=${false} />`);
  const texts = findTextNodes(root).map(n => n.data);
  if (!texts.some(t => t === '---'))
    throw new Error('expected "---" text node for null IAS in Mode3, got: ' + texts.join('|'));
  assertNoPlaceholderLeaks(texts);
});

test('Mode0 renders centered "--" placeholder for percent-lift when aoaIsValid is false', () => {
  const r = makeNullAirDataRecord();
  const root = renderInto(html`<${modesMod.Mode0} r=${r} stale=${false} />`);
  const groups = [...walk(root)].filter(n =>
    n.localName === 'g' && n.getAttribute('data-widget') === 'percent-lift-number');
  if (groups.length !== 1)
    throw new Error(`expected 1 percent-lift-number group, got ${groups.length}`);
  // The component renders two <text> nodes (outline + fill); both should
  // show the same dashes string.  text-anchor="middle" centers them on
  // the chevron x — same convention as the M5 firmware's centering math.
  const texts = [...walk(groups[0])].filter(n => n.localName === 'text');
  for (const t of texts) {
    const child = (t.childNodes || []).find(c => c.localName === '#text');
    const data = child ? child.data : '';
    if (data !== '--')
      throw new Error(`PercentLiftNumber text should be "--", got ${JSON.stringify(data)}`);
    if (t.getAttribute('text-anchor') !== 'middle')
      throw new Error(`PercentLiftNumber should center the dashes (text-anchor=middle)`);
  }
});

// ---------------------------------------------------------------------
// M5-accurate mode renderers (PR 2 of Project B2).
// ---------------------------------------------------------------------

const m5modesMod = await import(
  new URL('../lib/components/svg/m5modes/index.js', import.meta.url));

// A plausible default state object — the shape M5Sim.read() returns.
// Each mode test passes this (or a slight variation) and asserts the
// rendered SVG contains the expected widget tags.
function makeM5State(overrides = {}) {
  return Object.freeze({
    displayIAS:         80,
    displayPalt:      3000,
    displayPitch:        5,
    displayVerticalG:    1.0,
    displayPercentLift: 50,
    displayDecelRate:   -0.5,
    Slip:               -34,
    PercentLift:        50.0,
    gOnsetRate:          0.5,
    IAS:                80,
    Palt:             3000,
    IasIsValid:       true,
    displayType:         0,
    iVSI:              400,
    OAT:                20,
    FlightPath:          3,
    Pitch:               5,
    Roll:               10,
    TonesOnPctLift:     30,
    OnSpeedFastPctLift: 40,
    OnSpeedSlowPctLift: 50,
    StallWarnPctLift:   80,
    PipPctLift:         32,
    FlapsMinDeg:         0,
    FlapsMaxDeg:        33,
    FlapPos:            10,
    gHistoryIndex:       0,
    gHistory:    new Float32Array(300).fill(1.0),
    SpinRecoveryCue:     0,
    DataMark:            7,
    ...overrides,
  });
}

test('EnergyMode renders the indexer + flap circle + slip ball widgets', () => {
  const root = renderInto(
    html`<${m5modesMod.EnergyMode} state=${makeM5State()} stale=${false} />`);
  for (const widget of ['indexer', 'flap-circle', 'slip', 'edge-tape',
                         'percent-lift-number']) {
    const found = findFirstWithAttr(root, 'data-widget', widget);
    if (!found) throw new Error(`EnergyMode missing data-widget="${widget}"`);
  }
});

test('IndexerMode renders indexer but not flap-circle (numericDisplay=false)', () => {
  const root = renderInto(
    html`<${m5modesMod.IndexerMode} state=${makeM5State()} stale=${false} />`);
  if (!findFirstWithAttr(root, 'data-widget', 'indexer'))
    throw new Error('IndexerMode missing data-widget="indexer"');
  if (findFirstWithAttr(root, 'data-widget', 'flap-circle'))
    throw new Error('IndexerMode should NOT render flap-circle');
});

test('AttitudeMode renders the horizon + bank arc + flight-path marker', () => {
  const root = renderInto(
    html`<${m5modesMod.AttitudeMode} state=${makeM5State()} stale=${false} />`);
  for (const widget of ['horizon', 'pitch-ladder', 'bank-arc', 'fpv',
                         'aircraft-symbol']) {
    if (!findFirstWithAttr(root, 'data-widget', widget))
      throw new Error(`AttitudeMode missing data-widget="${widget}"`);
  }
});

test('DecelMode renders the decel gauge', () => {
  const root = renderInto(
    html`<${m5modesMod.DecelMode} state=${makeM5State()} stale=${false} />`);
  if (!findFirstWithAttr(root, 'data-widget', 'decel-gauge'))
    throw new Error('DecelMode missing data-widget="decel-gauge"');
});

test('HistoricGMode renders the g-history strip', () => {
  const root = renderInto(
    html`<${m5modesMod.HistoricGMode} state=${makeM5State()} stale=${false} />`);
  if (!findFirstWithAttr(root, 'data-widget', 'g-history'))
    throw new Error('HistoricGMode missing data-widget="g-history"');
});

test('EnergyMode shows IAS dashes when IasIsValid is false', () => {
  const state = makeM5State({ IasIsValid: false });
  const root = renderInto(
    html`<${m5modesMod.EnergyMode} state=${state} stale=${false} />`);
  // The IAS corner's <text> for value should contain "---".
  const texts = [...walk(root)].filter(n => n.localName === '#text');
  if (!texts.some(t => t.data === '---'))
    throw new Error(
      'EnergyMode with IasIsValid=false should render "---" placeholder, ' +
      'got: ' + texts.map(t => t.data).filter(s => s).join('|'));
});

test('EnergyMode slip ball cx responds to state.Slip', () => {
  // Find the slip ball <circle> for two distinct Slip values; their cx
  // attributes must differ. This catches a sabotage where SlipBall is
  // hard-wired (e.g. always reads 0 or always reads a constant) — a
  // failure mode the "data-widget present" tests don't see.
  const findBallCx = (slip) => {
    const root = renderInto(
      html`<${m5modesMod.EnergyMode} state=${makeM5State({ Slip: slip })}
                                      stale=${false} />`);
    const slipGroup = findFirstWithAttr(root, 'data-widget', 'slip');
    if (!slipGroup) return null;
    // The <circle> is the last element; its cx is the ball position.
    const circles = [...walk(slipGroup)].filter(n => n.localName === 'circle');
    if (circles.length === 0) return null;
    return parseFloat(circles[circles.length - 1].getAttribute('cx'));
  };
  const cxLeft  = findBallCx(-50);   // left of center
  const cxZero  = findBallCx(0);     // centered
  const cxRight = findBallCx(50);    // right of center
  if (cxLeft == null || cxZero == null || cxRight == null) {
    throw new Error('EnergyMode: failed to find slip ball circle');
  }
  if (!(cxLeft < cxZero && cxZero < cxRight)) {
    throw new Error(
      `EnergyMode slip ball cx should monotonically increase with state.Slip; ` +
      `got cxLeft=${cxLeft}, cxZero=${cxZero}, cxRight=${cxRight}`);
  }
});

test('AttitudeMode horizon responds to state.Roll', () => {
  // Sanity that AttitudeMode actually consumes state.Roll. The horizon
  // polygon's `points` attribute changes when roll changes; we don't
  // need to validate the geometry, just that it differs.
  const renderAt = (roll) => {
    const root = renderInto(
      html`<${m5modesMod.AttitudeMode} state=${makeM5State({ Roll: roll })}
                                        stale=${false} />`);
    const horizon = findFirstWithAttr(root, 'data-widget', 'horizon');
    const polygon = horizon
      ? [...walk(horizon)].find(n => n.localName === 'polygon')
      : null;
    return polygon ? polygon.getAttribute('points') : null;
  };
  const at0  = renderAt(0);
  const at30 = renderAt(30);
  if (!at0 || !at30) throw new Error('AttitudeMode: no horizon polygon found');
  if (at0 === at30) {
    throw new Error('AttitudeMode horizon polygon does not respond to Roll');
  }
});

await runTests();
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
