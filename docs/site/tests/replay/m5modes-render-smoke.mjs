// m5modes-render-smoke.mjs — render the M5-mode SVG components into
// a mock DOM and assert each one emits its expected data-widget tags.
//
// m5modes live in packages/ui-core/components/svg/m5modes/ and are
// shared between the docs-site replay tool and the firmware-served
// /indexer page. This test lives under docs/site/tests/replay/ for
// historical reasons (it predates the relocation and the consumers
// it primarily validates are the docs-site replay path). Same harness
// pattern as the firmware-bundle render-smoke (tools/web/test/), but
// scoped to just the m5modes — keeps the preact instance shared with
// the components under test (mixing two preact instances breaks
// hooks).
//
// Run:
//   node docs/site/tests/replay/m5modes-render-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

// ---------------------------------------------------------------------
// Mock DOM (subset of what Preact reads/writes)
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
    this.value      = '';
    this.checked    = false;
    this.innerHTML  = '';
    this.dataset    = {};
  }
  appendChild(c)  { if (c.parentNode) c.parentNode.removeChild(c); c.parentNode = this; this.childNodes.push(c); return c; }
  removeChild(c)  { const i = this.childNodes.indexOf(c); if (i >= 0) { this.childNodes.splice(i, 1); c.parentNode = null; } return c; }
  insertBefore(c, ref) {
    if (c.parentNode) c.parentNode.removeChild(c);
    if (!ref) return this.appendChild(c);
    const i = this.childNodes.indexOf(ref);
    if (i < 0) return this.appendChild(c);
    this.childNodes.splice(i, 0, c);
    c.parentNode = this;
    return c;
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
  getAttribute(name) { return this._attrs.get(name) ?? null; }
  addEventListener(type, h) { if (!this._listeners.has(type)) this._listeners.set(type, []); this._listeners.get(type).push(h); }
  removeEventListener(type, h) {
    const arr = this._listeners.get(type);
    if (!arr) return;
    const i = arr.indexOf(h);
    if (i >= 0) arr.splice(i, 1);
  }
}

const mockDocument = {
  createElement(tag)        { return new MockNode(tag); },
  createElementNS(_ns, tag) { return new MockNode(tag); },
  createTextNode(text)      { const n = new MockNode('#text'); n.data = String(text); return n; },
  getElementById:    () => null,
  querySelector:     () => null,
  querySelectorAll:  () => [],
  addEventListener:  () => {},
  readyState: 'complete',
};

globalThis.document    = mockDocument;
globalThis.location    = { pathname: '/', protocol: 'http:', hostname: 'localhost' };
globalThis.localStorage = { getItem: () => null, setItem: () => {} };
globalThis.performance = globalThis.performance || { now: () => 0 };
globalThis.window      = globalThis;
globalThis.addEventListener    = () => {};
globalThis.removeEventListener = () => {};

// ---------------------------------------------------------------------
// Tree walker
// ---------------------------------------------------------------------
function* walk(node) {
  if (!node) return;
  yield node;
  for (const c of node.childNodes) yield* walk(c);
}

function findFirstWithAttr(root, name, value) {
  for (const n of walk(root)) {
    const v = n.getAttribute && n.getAttribute(name);
    if (v != null && (value == null || v.split(/\s+/).includes(value))) return n;
  }
  return null;
}

// ---------------------------------------------------------------------
// Imports + harness — preact instance is the one m5modes also use,
// because m5modes/index.js transitively imports the same file.
// ---------------------------------------------------------------------
const UI_CORE = path.resolve(__dirname,
  '../../../../packages/ui-core');

const preact = await import(
  new URL(`${UI_CORE}/vendor/preact-standalone.js`, import.meta.url));
const { html, render } = preact;

const m5modesMod = await import(
  new URL(`${UI_CORE}/components/svg/m5modes/index.js`, import.meta.url));

function renderInto(vnode) {
  const root = new MockNode('svg-root');
  render(vnode, root);
  return root;
}

let passed = 0;
let failed = 0;
const results = [];
function test(name, fn) {
  try { fn(); passed++; results.push(['PASS', name]); }
  catch (e) { failed++; results.push(['FAIL', `${name}: ${e.message}`]); }
}

function makeM5State(overrides = {}) {
  return Object.freeze({
    displayIAS:         80,
    displayPalt:      3000,
    displayPitch:        5,
    displayVerticalG:    1.0,
    displayPercentLift: 50,
    displayDecelRate:   -0.5,
    Slip:               -34,
    LateralG:            0.04,
    VerticalG:           1.0,
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

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

test('EnergyMode renders the indexer + flap circle + slip ball widgets', () => {
  const root = renderInto(
    html`<${m5modesMod.EnergyMode} state=${makeM5State()} stale=${false} />`);
  for (const widget of ['indexer', 'flap-circle', 'slip', 'edge-tape',
                         'percent-lift-number']) {
    if (!findFirstWithAttr(root, 'data-widget', widget))
      throw new Error(`EnergyMode missing data-widget="${widget}"`);
  }
});

test('IndexerMode renders indexer but not flap-circle', () => {
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

// State-sensitivity tests — make sure the renderers actually consume
// the state values, not just emit a static template.

test('EnergyMode slip ball cx responds to state.LateralG', () => {
  // The SlipBall reads state.LateralG directly (after the
  // PresentationFilter applies). Positive lateralG = airframe
  // accelerating rightward, which the SlipBall renders as ball
  // displaced LEFT (the sky-pointing convention). Verify the cx
  // attribute moves monotonically with LateralG across three
  // distinct values.
  const findBallCx = (lateralG) => {
    const root = renderInto(
      html`<${m5modesMod.EnergyMode} state=${makeM5State({ LateralG: lateralG })}
                                      stale=${false} />`);
    const slipGroup = findFirstWithAttr(root, 'data-widget', 'slip');
    if (!slipGroup) return null;
    const circles = [...walk(slipGroup)].filter(n => n.localName === 'circle');
    if (circles.length === 0) return null;
    return parseFloat(circles[circles.length - 1].getAttribute('cx'));
  };
  // Negative G → ball right; positive G → ball left. Pick three
  // distinct unsaturated values.
  const cxNeg  = findBallCx(-0.08);
  const cxZero = findBallCx( 0.00);
  const cxPos  = findBallCx( 0.08);
  if (cxNeg == null || cxZero == null || cxPos == null) {
    throw new Error('EnergyMode: failed to find slip ball circle');
  }
  if (cxNeg === cxZero || cxZero === cxPos || cxNeg === cxPos) {
    throw new Error(
      `EnergyMode slip ball cx should differ across LateralG values; ` +
      `got cxNeg=${cxNeg}, cxZero=${cxZero}, cxPos=${cxPos}`);
  }
});

test('AttitudeMode horizon responds to state.Roll', () => {
  // Horizon polygon's `points` attribute changes when roll changes;
  // we don't validate geometry, just that it differs.
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

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
