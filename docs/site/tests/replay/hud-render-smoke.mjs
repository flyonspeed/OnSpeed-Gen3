// hud-render-smoke.mjs — render the redesigned HudOverlay against a
// few fixed state snapshots and assert the derived widget positions
// move as expected. Lifts the mock-DOM + harness pattern from
// m5modes-render-smoke.mjs.
//
// The HUD reuses the M5 inset's attitude indicator components
// (HorizonLine, PitchLadder, BankArc, AircraftSymbol, FlightPathMarker)
// scaled up via a single `<g transform>` so the geometry is one source
// of truth. These tests assert the scaled group exists, that the
// pitch-ladder + horizon-line apply pitchOffsetDeg, that the FPM stays
// on raw pitch, and that the top readouts + slip ball + VVI render.
//
// Run:
//   node docs/site/tests/replay/hud-render-smoke.mjs

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

function findAllWithAttr(root, name, value) {
  const out = [];
  for (const n of walk(root)) {
    const v = n.getAttribute && n.getAttribute(name);
    if (v != null && (value == null || v.split(/\s+/).includes(value))) out.push(n);
  }
  return out;
}

function findAllByLocalName(root, localName) {
  const out = [];
  for (const n of walk(root)) if (n.localName === localName) out.push(n);
  return out;
}

// ---------------------------------------------------------------------
// Imports + harness — preact instance is the one the HUD also uses.
// ---------------------------------------------------------------------
const UI_CORE = path.resolve(__dirname,
  '../../../../packages/ui-core');

const preact = await import(
  new URL(`${UI_CORE}/vendor/preact-standalone.js`, import.meta.url));
const { html, render } = preact;

const { HudOverlay } = await import(
  new URL(`${UI_CORE}/components/svg/HudOverlay.js`, import.meta.url));

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

// Minimal M5State subset that HudOverlay reads.
function makeState(overrides = {}) {
  return Object.freeze({
    displayIAS:         80,
    displayPalt:      3000,
    Pitch:               0,
    Roll:                0,
    FlightPath:          0,
    LateralG:            0,
    iVSI:                0,
    PercentLift:        50.0,
    StallWarnPctLift:   80,
    IasIsValid:       true,
    ...overrides,
  });
}

// ---------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------

test('HudOverlay renders top readouts, slip ball, and VVI', () => {
  // VVI threshold is 100 fpm; bump iVSI so the widget renders.
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ iVSI: 800 })} />`);
  for (const widget of ['hud-top-ias', 'hud-top-palt', 'slip', 'hud-vvi']) {
    if (!findFirstWithAttr(root, 'data-widget', widget))
      throw new Error(`HudOverlay missing data-widget="${widget}"`);
  }
});

test('HudOverlay returns null for missing state', () => {
  const root = renderInto(html`<${HudOverlay} state=${null} />`);
  if (findFirstWithAttr(root, 'data-widget', 'hud-top-ias'))
    throw new Error('HudOverlay should render nothing when state is null');
});

test('MH box renders when magneticHeading is provided', () => {
  const withMh = renderInto(
    html`<${HudOverlay} state=${makeState()} magneticHeading=${137} />`);
  if (!findFirstWithAttr(withMh, 'data-widget', 'hud-top-mh')) {
    throw new Error('hud-top-mh missing when magneticHeading set');
  }
  // Zero-padded to three digits.
  const mh = findFirstWithAttr(withMh, 'data-widget', 'hud-top-mh');
  const texts = findAllByLocalName(mh, 'text');
  const joined = texts.flatMap(t => t.childNodes.map(c => c.data || '')).join('|');
  if (!joined.includes('137')) {
    throw new Error(`MH box should display "137"; got ${joined}`);
  }
});

test('MH box hidden when magneticHeading is null', () => {
  const noMh = renderInto(html`<${HudOverlay} state=${makeState()} />`);
  if (findFirstWithAttr(noMh, 'data-widget', 'hud-top-mh')) {
    throw new Error('hud-top-mh should not render without magneticHeading');
  }
});

test('Scaled ADI group wraps the inset attitude components', () => {
  const root = renderInto(html`<${HudOverlay} state=${makeState()} />`);
  // The scaled <g> carries a translate-scale-translate transform.
  const groups = findAllByLocalName(root, 'g');
  const scaled = groups.find(g => {
    const t = g.getAttribute('transform') || '';
    return t.includes('scale(3)');
  });
  if (!scaled) throw new Error('Scaled-up ADI <g transform="...scale(3)..."> not found');
  // It should contain the inset's pitch-ladder, bank-arc, aircraft, fpv,
  // and horizon-line widgets.
  for (const w of ['horizon-line', 'pitch-ladder', 'bank-arc', 'aircraft-symbol', 'fpv']) {
    if (!findFirstWithAttr(scaled, 'data-widget', w)) {
      throw new Error(`Scaled ADI group missing inset widget data-widget="${w}"`);
    }
  }
});

test('pitchOffsetDeg shifts pitch ladder (not the FPM)', () => {
  // The pitch ladder's first tick (the i=0 horizon-aligned tick) lives at
  // (pxc, pyc) = (cx + pitch*scale*sinR, cy + pitch*scale*cosR). For
  // roll=0 the first short tick at i=0 has y = pyc. Increasing
  // pitchOffsetDeg from 0 to 5 must shift the ladder DOWN (pyc grows
  // with positive pitch), while the FPM cy must NOT change (it tracks
  // raw Pitch only).
  const tickYAndFpmCy = (offset) => {
    const root = renderInto(
      html`<${HudOverlay} state=${makeState({ Pitch: 0, FlightPath: 0 })}
                          pitchOffsetDeg=${offset} />`);
    const ladder = findFirstWithAttr(root, 'data-widget', 'pitch-ladder');
    if (!ladder) throw new Error('pitch-ladder not found');
    const tickLines = findAllByLocalName(ladder, 'line');
    if (!tickLines.length) throw new Error('no pitch-ladder ticks rendered');
    // Use the first short tick's y midpoint.
    const y1 = parseFloat(tickLines[0].getAttribute('y1'));
    const y2 = parseFloat(tickLines[0].getAttribute('y2'));
    const mid = (y1 + y2) / 2;
    const fpm = findFirstWithAttr(root, 'data-widget', 'fpv');
    const circles = fpm ? findAllByLocalName(fpm, 'circle') : [];
    const fpmCy = circles.length ? parseFloat(circles[0].getAttribute('cy')) : null;
    return { mid, fpmCy };
  };
  const a = tickYAndFpmCy(0);
  const b = tickYAndFpmCy(5);
  if (!(b.mid > a.mid)) {
    throw new Error(`pitchOffsetDeg=5 should push the ladder down (mid_b > mid_a); got a=${a.mid}, b=${b.mid}`);
  }
  if (a.fpmCy !== b.fpmCy) {
    throw new Error(`FPM cy should NOT change with pitchOffsetDeg; got a=${a.fpmCy}, b=${b.fpmCy}`);
  }
});

test('IAS value renders in the IAS box', () => {
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ displayIAS: 87 })} />`);
  const ias = findFirstWithAttr(root, 'data-widget', 'hud-top-ias');
  if (!ias) throw new Error('hud-top-ias missing');
  const texts = findAllByLocalName(ias, 'text');
  const joined = texts.flatMap(t => t.childNodes.map(c => c.data || '')).join('|');
  if (!joined.includes('87')) {
    throw new Error(`IAS box value "87" not found; got ${joined}`);
  }
});

test('IAS shows dashes when IasIsValid is false', () => {
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ IasIsValid: false })} />`);
  const ias = findFirstWithAttr(root, 'data-widget', 'hud-top-ias');
  const texts = findAllByLocalName(ias, 'text');
  const joined = texts.flatMap(t => t.childNodes.map(c => c.data || '')).join('|');
  if (!joined.includes('---')) {
    throw new Error('IAS box should show "---" when IasIsValid is false');
  }
});

test('VVI numeric only renders above HUD_VVI_THRESHOLD', () => {
  const renderAt = (vsi) => renderInto(
    html`<${HudOverlay} state=${makeState({ iVSI: vsi })} />`);
  const numericPresent = (vsi) => {
    const root = renderAt(vsi);
    const vvi = findFirstWithAttr(root, 'data-widget', 'hud-vvi');
    if (!vvi) return false;
    // The numeric readout is a <text> whose content contains a sign or
    // a 4-digit integer; ticks render "1" and "2" as well, so check
    // for the +/-NN0 pattern.
    const texts = findAllByLocalName(vvi, 'text');
    return texts.some(t => /^[+-]\d/.test(t.childNodes[0]?.data || ''));
  };
  if (numericPresent(0))   throw new Error('VVI numeric should NOT render at iVSI=0');
  if (numericPresent(50))  throw new Error('VVI numeric should NOT render at iVSI=50 (below threshold)');
  if (!numericPresent(1500)) throw new Error('VVI numeric should render at iVSI=1500');
});

test('Snapshot: canonical states yield distinct ladder transforms', () => {
  const scenarios = [
    { name: 'level',       state: makeState() },
    { name: 'left30Climb', state: makeState({ Roll: -30, Pitch: 12, FlightPath: 15, displayIAS: 95 }) },
    { name: 'slippedTurn', state: makeState({ Roll:  20, Pitch:  3, FlightPath:  1, LateralG: -0.12, displayIAS: 75 }) },
    { name: 'highPitch',   state: makeState({ Pitch: 25, FlightPath: 18, displayIAS: 60, iVSI: -1200 }) },
  ];
  const out = {};
  for (const { name, state } of scenarios) {
    const root = renderInto(html`<${HudOverlay} state=${state} />`);
    const groups = findAllByLocalName(root, 'g');
    const scaled = groups.find(g => (g.getAttribute('transform') || '').includes('scale(3)'));
    const ladder = scaled && findFirstWithAttr(scaled, 'data-widget', 'pitch-ladder');
    const ladderTicks = ladder ? findAllByLocalName(ladder, 'line') : [];
    const fpm = findFirstWithAttr(root, 'data-widget', 'fpv');
    const fpmCircle = fpm && findAllByLocalName(fpm, 'circle')[0];
    out[name] = {
      tickCount: ladderTicks.length,
      firstTickY1: ladderTicks[0] ? parseFloat(ladderTicks[0].getAttribute('y1')) : null,
      fpmCx: fpmCircle ? parseFloat(fpmCircle.getAttribute('cx')) : null,
      fpmCy: fpmCircle ? parseFloat(fpmCircle.getAttribute('cy')) : null,
    };
  }
  // The first-tick y should differ across the climb/level scenarios.
  if (out.level.firstTickY1 === out.highPitch.firstTickY1) {
    throw new Error('level and highPitch should produce different ladder positions; both at ' + out.level.firstTickY1);
  }
  console.log('hud-render snapshot:', JSON.stringify(out));
});

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
