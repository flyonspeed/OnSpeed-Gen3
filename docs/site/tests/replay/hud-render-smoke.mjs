// hud-render-smoke.mjs — render the full-frame HudOverlay against a
// few fixed state snapshots and assert the derived widget positions
// move as expected. Lifts the mock-DOM + harness pattern from
// m5modes-render-smoke.mjs.
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

test('HudOverlay renders all primary widgets at level flight', () => {
  const root = renderInto(html`<${HudOverlay} state=${makeState()} />`);
  for (const widget of ['hud-pitch-ladder', 'hud-bank-arc', 'hud-fpm',
                         'hud-tape-left', 'hud-tape-right', 'slip']) {
    if (!findFirstWithAttr(root, 'data-widget', widget))
      throw new Error(`HudOverlay missing data-widget="${widget}"`);
  }
});

test('HudOverlay returns null for missing state', () => {
  const root = renderInto(html`<${HudOverlay} state=${null} />`);
  if (findFirstWithAttr(root, 'data-widget', 'hud-pitch-ladder'))
    throw new Error('HudOverlay should render nothing when state is null');
});

test('Pitch ladder rotation transform tracks state.Roll', () => {
  const transformAt = (roll) => {
    const root = renderInto(
      html`<${HudOverlay} state=${makeState({ Roll: roll })} />`);
    const ladder = findFirstWithAttr(root, 'data-widget', 'hud-pitch-ladder');
    return ladder ? ladder.getAttribute('transform') : null;
  };
  const t0  = transformAt(0);
  const t30 = transformAt(-30);
  if (!t0 || !t30) throw new Error('hud-pitch-ladder transform missing');
  if (t0 === t30) throw new Error('hud-pitch-ladder transform does not respond to Roll');
  // -roll → rotate(+30 ...): for state.Roll = -30 the rotation magnitude
  // should be the additive inverse of the rotation at +30.
  if (!t30.includes('rotate(30')) {
    throw new Error(`hud-pitch-ladder transform at Roll=-30 should rotate +30: got ${t30}`);
  }
});

test('FPM has NO lateral motion (issue #542)', () => {
  // Vertical-only FPM: matches the AI inset's FlightPathMarker math.
  // Lateral FPM motion requires yaw rate / ground track, which the
  // wire format does not yet expose. Verify cx is constant across
  // a range of LateralG values.
  const findRingCx = (lateralG) => {
    const root = renderInto(
      html`<${HudOverlay} state=${makeState({ LateralG: lateralG })} />`);
    const fpm = findFirstWithAttr(root, 'data-widget', 'hud-fpm');
    if (!fpm) return null;
    const circles = findAllByLocalName(fpm, 'circle');
    return circles.length ? parseFloat(circles[0].getAttribute('cx')) : null;
  };
  const cxNeg  = findRingCx(-0.50);
  const cxZero = findRingCx( 0.00);
  const cxPos  = findRingCx( 0.50);
  if (cxNeg == null || cxZero == null || cxPos == null) {
    throw new Error('FPM ring circle not found');
  }
  if (cxNeg !== cxZero || cxZero !== cxPos) {
    throw new Error(
      `FPM cx should be constant (issue #542); ` +
      `got cxNeg=${cxNeg}, cxZero=${cxZero}, cxPos=${cxPos}`);
  }
});

test('FPM vertical position tracks FlightPath - Pitch', () => {
  const findRingCy = (pitch, flightPath) => {
    const root = renderInto(
      html`<${HudOverlay} state=${makeState({ Pitch: pitch, FlightPath: flightPath })} />`);
    const fpm = findFirstWithAttr(root, 'data-widget', 'hud-fpm');
    const circles = fpm ? findAllByLocalName(fpm, 'circle') : [];
    return circles.length ? parseFloat(circles[0].getAttribute('cy')) : null;
  };
  // Climbing (FlightPath > Pitch): FPM should sit BELOW horizon (FPM
  // cy > center). Descending (FlightPath < Pitch): FPM above horizon.
  const climbCy = findRingCy(5, 8);     // FlightPath-Pitch = +3
  const levelCy = findRingCy(5, 5);     // = 0
  const descCy  = findRingCy(5, 2);     // = -3
  if (climbCy == null || levelCy == null || descCy == null) {
    throw new Error('FPM ring not found');
  }
  if (!(climbCy > levelCy && levelCy > descCy)) {
    throw new Error(
      `FPM cy should increase with FlightPath-Pitch; ` +
      `got descCy=${descCy}, levelCy=${levelCy}, climbCy=${climbCy}`);
  }
});

test('Bank arc inner group rotates by -Roll', () => {
  const findInnerTransform = (roll) => {
    const root = renderInto(
      html`<${HudOverlay} state=${makeState({ Roll: roll })} />`);
    const arc = findFirstWithAttr(root, 'data-widget', 'hud-bank-arc');
    if (!arc) return null;
    // The first <g> child is the rotating tick group.
    for (const c of arc.childNodes) {
      if (c.localName === 'g' && c.getAttribute('transform')) {
        return c.getAttribute('transform');
      }
    }
    return null;
  };
  const t0  = findInnerTransform(0);
  const t30 = findInnerTransform(30);
  if (!t0 || !t30) throw new Error('hud-bank-arc inner transform missing');
  if (t0 === t30) throw new Error('hud-bank-arc inner transform does not respond to Roll');
  if (!t30.includes('rotate(-30')) {
    throw new Error(`hud-bank-arc inner should rotate -30 at Roll=30: got ${t30}`);
  }
});

test('IAS tape box renders the current IAS value', () => {
  const root = renderInto(html`<${HudOverlay} state=${makeState({ displayIAS: 87 })} />`);
  const tape = findFirstWithAttr(root, 'data-widget', 'hud-tape-left');
  if (!tape) throw new Error('hud-tape-left missing');
  const texts = findAllByLocalName(tape, 'text');
  // The text-anchor="middle" + central baseline element is the box;
  // we check it contains "87".
  const hasBox = texts.some(t => {
    const child = t.childNodes[0];
    return child && child.data === '87';
  });
  if (!hasBox) {
    const found = texts.map(t => t.childNodes[0]?.data).join('|');
    throw new Error(`IAS box value "87" not found; got texts=${found}`);
  }
});

test('IAS tape renders dashes when IasIsValid is false', () => {
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ IasIsValid: false })} />`);
  const tape = findFirstWithAttr(root, 'data-widget', 'hud-tape-left');
  const texts = findAllByLocalName(tape, 'text');
  const hasDashes = texts.some(t => t.childNodes[0]?.data === '---');
  if (!hasDashes) throw new Error('IAS dashes not rendered when invalid');
});

test('VSI numeric only renders above HUD_VSI_THRESHOLD', () => {
  // FlySto-style numeric VSI: hidden at idle, visible when climb/
  // descent is meaningful (|vsi| >= HUD_VSI_THRESHOLD fpm).
  const renderAt = (vsi) => renderInto(
    html`<${HudOverlay} state=${makeState({ iVSI: vsi })} />`);
  if (findFirstWithAttr(renderAt(0), 'data-widget', 'hud-vsi')) {
    throw new Error('VSI should NOT render at iVSI=0');
  }
  if (findFirstWithAttr(renderAt(50), 'data-widget', 'hud-vsi')) {
    throw new Error('VSI should NOT render at iVSI=50 (below threshold)');
  }
  if (!findFirstWithAttr(renderAt(1500), 'data-widget', 'hud-vsi')) {
    throw new Error('VSI should render at iVSI=1500');
  }
});

test('G readout renders state.VerticalG with one decimal', () => {
  const root = renderInto(
    html`<${HudOverlay} state=${makeState({ VerticalG: 2.4 })} />`);
  const g = findFirstWithAttr(root, 'data-widget', 'hud-g');
  if (!g) throw new Error('hud-g widget missing');
  const texts = findAllByLocalName(g, 'text');
  // Preact may split the template into multiple text nodes (e.g.
  // "2.4" and " G"). Concatenate every text node's data and look
  // for the joined string anywhere inside.
  const joined = texts
    .flatMap(t => t.childNodes.map(c => c.data || ''))
    .join('');
  if (!joined.includes('2.4') || !joined.includes('G')) {
    throw new Error(`G readout missing "2.4" or "G"; joined=${joined}`);
  }
});

// Fixed snapshots: render four canonical states and emit a JSON
// blob of the key derived positions. Useful for eyeballing diffs in
// PR review and for future snapshot comparison if we want it.
test('Snapshot: derived positions for canonical states', () => {
  const scenarios = [
    { name: 'level', state: makeState() },
    { name: 'left30Climb', state: makeState({ Roll: -30, Pitch: 12, FlightPath: 15, displayIAS: 95 }) },
    { name: 'slippedTurn', state: makeState({ Roll: 20, Pitch: 3, FlightPath: 1, LateralG: -0.12, displayIAS: 75 }) },
    { name: 'highPitch',   state: makeState({ Pitch: 25, FlightPath: 18, displayIAS: 60, iVSI: -1200 }) },
  ];
  const out = {};
  for (const { name, state } of scenarios) {
    const root = renderInto(html`<${HudOverlay} state=${state} />`);
    const ladder = findFirstWithAttr(root, 'data-widget', 'hud-pitch-ladder');
    const arc    = findFirstWithAttr(root, 'data-widget', 'hud-bank-arc');
    const fpm    = findFirstWithAttr(root, 'data-widget', 'hud-fpm');
    const fpmCircle = fpm && findAllByLocalName(fpm, 'circle')[0];
    out[name] = {
      pitchLadderTransform: ladder?.getAttribute('transform') ?? null,
      bankInnerTransform: (() => {
        for (const c of arc?.childNodes || []) {
          if (c.localName === 'g' && c.getAttribute('transform')) {
            return c.getAttribute('transform');
          }
        }
        return null;
      })(),
      fpmCx: fpmCircle ? parseFloat(fpmCircle.getAttribute('cx')) : null,
      fpmCy: fpmCircle ? parseFloat(fpmCircle.getAttribute('cy')) : null,
    };
  }
  // Sanity: every scenario should produce a unique pitchLadderTransform
  // and the FPM cx/cy should differ where the inputs differ.
  const transforms = new Set(Object.values(out).map(s => s.pitchLadderTransform));
  if (transforms.size !== scenarios.length) {
    throw new Error(
      `expected unique pitch-ladder transforms per scenario, got ${transforms.size}: ` +
      JSON.stringify(out, null, 2));
  }
  // Stable diff-friendly output.
  // eslint-disable-next-line no-console
  console.log('hud-render snapshot:', JSON.stringify(out));
});

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
