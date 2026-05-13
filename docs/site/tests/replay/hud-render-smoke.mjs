// hud-render-smoke.mjs — render the FlySto-style HudOverlay against a
// few fixed state snapshots and assert the derived widget positions
// move as expected. Lifts the mock-DOM + harness pattern from
// m5modes-render-smoke.mjs.
//
// The HUD composes the OnSpeed logo, pitch ladder, bank arc, FPM,
// VVI trend, ALT tape, and slip ball. These tests assert each widget
// renders, that pitchOffsetDeg shifts the pitch ladder but NOT the
// FPM, and that the ALT tape responds to altitude changes.
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

function findAllByLocalName(root, localName) {
  const out = [];
  for (const n of walk(root)) if (n.localName === localName) out.push(n);
  return out;
}

// Parse the `translate(0 N)` portion of a transform string and return N.
// HudPitchLadder's transform looks like:
//   rotate(-roll cx cy) translate(0 transY)
// We want the second translate's y component.
function parseLadderTranslateY(transformStr) {
  const m = (transformStr || '').match(/translate\(\s*0\s+(-?\d+(?:\.\d+)?)\s*\)/);
  return m ? parseFloat(m[1]) : null;
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

test('HudOverlay renders logo, ALT tape, ADI widgets, slip ball, VVI', () => {
  // VVI threshold is 100 fpm; bump iVSI so the widget renders.
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ iVSI: 800 })} />`);
  const required = [
    'hud-onspeed-logo',
    'hud-pitch-ladder', 'hud-bank-arc', 'hud-fpm',
    'slip', 'hud-vvi', 'hud-alt-tape',
  ];
  for (const widget of required) {
    if (!findFirstWithAttr(root, 'data-widget', widget))
      throw new Error(`HudOverlay missing data-widget="${widget}"`);
  }
});

test('HudOverlay returns null for missing state', () => {
  const root = renderInto(html`<${HudOverlay} state=${null} />`);
  if (findFirstWithAttr(root, 'data-widget', 'hud-alt-tape'))
    throw new Error('HudOverlay should render nothing when state is null');
  if (findFirstWithAttr(root, 'data-widget', 'hud-onspeed-logo'))
    throw new Error('HudOverlay should render nothing when state is null');
});

test('pitchOffsetDeg shifts the pitch ladder transform', () => {
  // HudPitchLadder applies `translate(0 transY)` where
  // transY = (pitch + offset) * HUD_PITCH_PX_PER_DEG.  At Pitch=0,
  // offset=5 the translate-y must be 5 * 18 = 90; at offset=0 it
  // must be 0.
  const ladderTranslate = (offset) => {
    const root = renderInto(
      html`<${HudOverlay} state=${makeState({ Pitch: 0 })}
                          pitchOffsetDeg=${offset} />`);
    const ladder = findFirstWithAttr(root, 'data-widget', 'hud-pitch-ladder');
    if (!ladder) throw new Error('hud-pitch-ladder not found');
    return parseLadderTranslateY(ladder.getAttribute('transform'));
  };
  const a = ladderTranslate(0);
  const b = ladderTranslate(5);
  if (a !== 0) throw new Error(`expected translate-y=0 at offset=0; got ${a}`);
  if (b !== 90) throw new Error(`expected translate-y=90 at offset=5; got ${b}`);
});

test('FPM stays on raw pitch (pitchOffsetDeg does not move it)', () => {
  // HudFpm renders a circle at cy = HUD_FPM_CY + (flightPath-pitch)*18.
  // With Pitch=FlightPath=0 the circle sits at HUD_FPM_CY regardless
  // of offset; bumping pitchOffsetDeg must NOT change cy.
  const fpmCy = (offset) => {
    const root = renderInto(
      html`<${HudOverlay} state=${makeState({ Pitch: 0, FlightPath: 0 })}
                          pitchOffsetDeg=${offset} />`);
    const fpm = findFirstWithAttr(root, 'data-widget', 'hud-fpm');
    if (!fpm) throw new Error('hud-fpm not found');
    const circles = findAllByLocalName(fpm, 'circle');
    if (!circles.length) throw new Error('hud-fpm has no circle');
    return parseFloat(circles[0].getAttribute('cy'));
  };
  const a = fpmCy(0);
  const b = fpmCy(5);
  if (a !== b) {
    throw new Error(`FPM cy should NOT change with pitchOffsetDeg; got a=${a}, b=${b}`);
  }
});

test('ALT tape shows hundreds digit derived from displayPalt', () => {
  // For displayPalt=6143 the hundreds digit shown in the Garmin box
  // is floor(6143/100) = "61".
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ displayPalt: 6143 })} />`);
  const tape = findFirstWithAttr(root, 'data-widget', 'hud-alt-tape');
  if (!tape) throw new Error('hud-alt-tape not found');
  const texts = findAllByLocalName(tape, 'text');
  const joined = texts.flatMap(t => t.childNodes.map(c => c.data || '')).join('|');
  if (!joined.includes('61')) {
    throw new Error(`ALT tape should show hundreds "61"; got ${joined}`);
  }
  // Tens (rounded down to nearest 20): 6143 → nearest20Below=6140,
  // tensCurr = 40 → "40".
  if (!joined.includes('40')) {
    throw new Error(`ALT tape should show tensCurr "40"; got ${joined}`);
  }
});

test('ALT tape baro reads "29.92in"', () => {
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ displayPalt: 3000 })} />`);
  const tape = findFirstWithAttr(root, 'data-widget', 'hud-alt-tape');
  if (!tape) throw new Error('hud-alt-tape not found');
  const texts = findAllByLocalName(tape, 'text');
  const joined = texts.flatMap(t => t.childNodes.map(c => c.data || '')).join('|');
  if (!joined.includes('29.92in')) {
    throw new Error(`ALT tape should show "29.92in"; got ${joined}`);
  }
});

test('VVI numeric only renders above HUD_VVI_THRESHOLD', () => {
  const renderAt = (vsi) => renderInto(
    html`<${HudOverlay} state=${makeState({ iVSI: vsi })} />`);
  const numericPresent = (vsi) => {
    const root = renderAt(vsi);
    const vvi = findFirstWithAttr(root, 'data-widget', 'hud-vvi');
    if (!vvi) return false;
    // The numeric readout is a <text> whose content starts with a sign
    // followed by a digit; ticks render "1" and "2" without signs.
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
    const ladder = findFirstWithAttr(root, 'data-widget', 'hud-pitch-ladder');
    const fpm = findFirstWithAttr(root, 'data-widget', 'hud-fpm');
    const fpmCircle = fpm && findAllByLocalName(fpm, 'circle')[0];
    out[name] = {
      ladderTransform: ladder ? ladder.getAttribute('transform') : null,
      ladderTranslateY: ladder ? parseLadderTranslateY(ladder.getAttribute('transform')) : null,
      fpmCx: fpmCircle ? parseFloat(fpmCircle.getAttribute('cx')) : null,
      fpmCy: fpmCircle ? parseFloat(fpmCircle.getAttribute('cy')) : null,
    };
  }
  // The ladder translate-y should differ across the level/highPitch
  // scenarios (pitch * 18 px/deg).
  if (out.level.ladderTranslateY === out.highPitch.ladderTranslateY) {
    throw new Error('level and highPitch should produce different ladder translate-y; both at ' + out.level.ladderTranslateY);
  }
  console.log('hud-render snapshot:', JSON.stringify(out));
});

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
