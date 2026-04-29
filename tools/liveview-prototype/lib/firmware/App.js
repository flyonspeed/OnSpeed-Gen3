// Top-level App component for the firmware-served /indexer page.
//
// One render function. One state shape. Modes are pure functions of
// the WebSocket record. Compare against the legacy approach of
// imperative `mountX(parent) → {update}` widgets juggling DOM
// references manually.

import { html, render, useState, useEffect, useRef } from '../vendor/preact-standalone.js';
import * as G from '../geometry.js';
import { Mode0, Mode1, Mode2, Mode3, Mode4 } from '../modes.js';
import { connect } from './wsClient.js';

// localStorage throws in private-browsing Safari and when storage is
// full. Wrap reads + writes so the UI never crashes from storage
// state — the page just won't persist mode/datafields choices.
function safeLsGet(key) {
  try { return localStorage.getItem(key); } catch { return null; }
}
function safeLsSet(key, value) {
  try { localStorage.setItem(key, value); } catch { /* ignore */ }
}

const MODES = [
  { id: 'aoa',           label: 'AOA',      C: Mode0 },
  { id: 'attitude',      label: 'Attitude', C: Mode1 },
  { id: 'indexer-only',  label: 'Indexer',  C: Mode2 },
  { id: 'energy',        label: 'Energy',   C: Mode3 },
  { id: 'ghistory',      label: 'G-Hist',   C: Mode4 },
];

const DATA_FIELDS = [
  ['AOA',      r => r.aoaIsValid ? r.aoaDeg.toFixed(2) + '°' : 'N/A'],
  ['Der AOA',  r => fmt(r.derivedAoaDeg, 2) + '°'],
  ['FltPath',  r => fmt(r.flightPathDeg, 1) + '°'],
  ['IAS',      r => fmt(r.iasKt, 0) + ' kt'],
  ['PAlt',     r => fmt(r.paltFt, 0) + ' ft'],
  ['iVSI',     r => fmt(r.vsiFpm, 0) + ' fpm'],
  ['Vert G',   r => fmt(r.verticalG, 2) + ' G'],
  ['Lat G',    r => fmt(r.lateralG, 2) + ' G'],
  ['Pitch',    r => fmt(r.pitchDeg, 1) + '°'],
  ['Roll',     r => fmt(r.rollDeg, 1) + '°'],
  ['DataMark', r => fmt(r.dataMark, 0)],
  ['Flaps',    r => fmt(r.flapsDeg, 0) + '°'],
];

function fmt(v, d) {
  if (v === undefined || v === null || isNaN(v)) return '—';
  let s = Number(v).toFixed(d);
  if (parseFloat(s) === 0) s = Math.abs(v).toFixed(d);
  return s;
}

const DataFields = ({ rec, ageSec, expanded, onToggle }) => html`
  <div id="datafields-wrap">
    <button id="datafields-toggle" type="button" onClick=${onToggle}>
      ${expanded ? 'Hide data fields' : 'Show data fields'}
    </button>
    ${expanded && rec && html`
      <div id="datafields">
        <table>
          ${DATA_FIELDS.map(([label, get]) => html`
            <tr><td>${label}</td><td>${get(rec)}</td></tr>`)}
          <tr><td>Age</td>
              <td style=${{ color: ageSec >= 1 ? 'var(--red, #ff0018)' : '' }}>
                ${ageSec.toFixed(1)} s</td></tr>
        </table>
      </div>`}
  </div>`;

// The /indexer page renders without the standard OnSpeed `pageHeader`
// nav (HandleIndexer in ConfigWebServer.cpp sends only the bundle, no
// chrome) so the SVG can fill the viewport on a phone. Embed a small
// nav row here so pilots can still hop back to the home page or the
// legacy LiveView without typing the URL.
const Header = ({ status, ageSec }) => html`
  <header id="liveview-header">
    <nav id="liveview-nav">
      <a href="/">Home</a>
      <a href="/live">LiveView</a>
    </nav>
    <div id="status-line">
      <span id="connectionstatus">${status}</span>
      <span id="age-indicator" style=${{ color: ageSec >= 1 ? 'var(--red, #ff0018)' : '' }}>
        ${ageSec.toFixed(1)}s</span>
    </div>
  </header>`;

const ModeNav = ({ current, onChange }) => html`
  <nav id="mode-nav">
    ${MODES.map(m => html`
      <button data-mode=${m.id} type="button"
              class=${m.id === current ? 'active' : ''}
              onClick=${() => onChange(m.id)}>${m.label}</button>`)}
  </nav>`;

// G-history ring buffer — kept in a useRef so it persists across renders.
function useGHistory(rec) {
  const buf = useRef(new Float32Array(G.MODE4_BUFFER_LEN));
  const writeIdx = useRef(0);
  const lastSampleMs = useRef(0);
  const tick = useRef(0);
  const [, force] = useState(0);

  // First-time init: fill with 1.0 G so the chart starts visually flat.
  useEffect(() => { buf.current.fill(1.0); }, []);

  // Sample at 5 Hz (matches main.cpp:465's 200 ms gate).
  useEffect(() => {
    if (!rec) return;
    const now = performance.now();
    if (now - lastSampleMs.current >= G.MODE4_SAMPLE_MS) {
      buf.current[writeIdx.current] = rec.verticalG ?? 1.0;
      writeIdx.current = (writeIdx.current + 1) % G.MODE4_BUFFER_LEN;
      lastSampleMs.current = now;
      tick.current++;
      force(tick.current);
    }
  }, [rec]);

  return { buf: buf.current, writeIdx: writeIdx.current };
}

export function App() {
  const [rec, setRec] = useState(null);
  const [status, setStatus] = useState('CONNECTING...');
  const [ageSec, setAgeSec] = useState(0);
  // localStorage throws in private-browsing iOS Safari. Wrap in
  // try/catch so the page still works without persistence.
  const [mode, setMode] = useState(safeLsGet('liveview-mode') || 'aoa');
  const [dfExpanded, setDfExpanded] = useState(safeLsGet('liveview-datafields-expanded') === '1');

  useEffect(() => {
    return connect({ onRecord: setRec, onStatus: setStatus, onAge: setAgeSec }).disconnect;
  }, []);

  // The WebSocket arrives at 20 Hz and `setRec` triggers a render on
  // every frame; that IS the animation tick. Don't add a separate
  // setInterval — it would double-render at 40 Hz (the WS render plus
  // the interval render) and rebuild Mode 4's 300 SVG circles 12k
  // times/sec on iPhone Safari, which is exactly the workload the
  // legacy WASM /indexer failed at. When WS goes silent, the
  // StaleOverlay covers the panel and there's nothing to animate.

  const stale = ageSec >= 3;
  const gHist = useGHistory(rec);

  const setModeAndPersist = (m) => {
    setMode(m);
    safeLsSet('liveview-mode', m);
  };
  const toggleDf = () => {
    const next = !dfExpanded;
    setDfExpanded(next);
    safeLsSet('liveview-datafields-expanded', next ? '1' : '0');
  };

  const ActiveMode = MODES.find(m => m.id === mode)?.C ?? Mode0;
  return html`
    <${Header} status=${status} ageSec=${ageSec} />
    <${ModeNav} current=${mode} onChange=${setModeAndPersist} />
    <main id="liveview-main">
      <div id="mode-container">
        <${ActiveMode} r=${rec || EMPTY_REC} stale=${stale}
                       gBuf=${gHist.buf} gWriteIdx=${gHist.writeIdx} />
      </div>
      <${DataFields} rec=${rec} ageSec=${ageSec}
                     expanded=${dfExpanded} onToggle=${toggleDf} />
    </main>
    <footer id="liveview-footer">
      <div id="footer-warning">
        For diagnostic purposes only. NOT SAFE FOR FLIGHT
      </div>
    </footer>`;
}

// Empty record used before the first WebSocket frame arrives. aoaIsValid
// false makes the modes hide their variable elements.
const EMPTY_REC = {
  aoaIsValid: false,
  pitchDeg: 0, rollDeg: 0, iasKt: 0, paltFt: 0, vsiFpm: 0,
  verticalG: 1, lateralG: 0, percentLift: 0, flightPathDeg: 0,
  flapsDeg: 0, flapsMinDeg: 0, flapsMaxDeg: 33,
  decelRate: 0, gOnsetRate: 0, dataMark: 0,
  tonesOnPctLift: 0, onSpeedFastPctLift: 0, onSpeedSlowPctLift: 0,
  stallWarnPctLift: 0, pipPctLift: 0,
};

export function start() {
  render(html`<${App} />`, document.getElementById('app'));
}
