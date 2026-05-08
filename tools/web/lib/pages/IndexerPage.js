// Indexer page (/indexer): five OnSpeed display modes (Energy
// Display, Attitude, Indexer, Decel Display, Historic G) backed by
// the live WebSocket feed.
//
// Modes are pure functions of the WebSocket record from
// `useWebSocket()`.  Mode selection persists to localStorage so a
// reload comes back to the last view.
//
// This file is the canonical source of mode names. The C++ M5
// firmware (software/OnSpeed-M5-Display/src/main.cpp), the X-Plane
// plugin menu/button (software/OnSpeed-XPlane-Plugin/), and the docs
// site (docs/site/docs/) all mirror the names from the MODES table
// below. Keep them in sync when changing names.

import { html, useState, useEffect, useRef } from '../vendor/preact-standalone.js';
import * as G from '../core/geometry.js';
import { fmt } from '../core/format.js';
import { Mode0, Mode1, Mode2, Mode3, Mode4 } from '../modes.js';
import { useWebSocket } from '../ws/wsClient.js';
import { PageShell } from '../shell/PageShell.js';

// localStorage throws in private-browsing Safari and when storage is
// full.  Wrap reads + writes so the UI never crashes from storage
// state — the page just won't persist mode/datafields choices.
function safeLsGet(key) {
  try { return localStorage.getItem(key); } catch { return null; }
}
function safeLsSet(key, value) {
  try { localStorage.setItem(key, value); } catch { /* ignore */ }
}

// Names follow Vac's canonical terminology (VAF threads 228078,
// 225345). Mode 0 is the energy-management primary page; Mode 3 is
// the deceleration display — these were swapped in older code.
const MODES = [
  { id: 'energy',   label: 'Energy',     C: Mode0 },
  { id: 'attitude', label: 'Attitude',   C: Mode1 },
  { id: 'indexer',  label: 'Indexer',    C: Mode2 },
  { id: 'decel',    label: 'Decel',      C: Mode3 },
  { id: 'historic', label: 'Historic G', C: Mode4 },
];

// Migrate legacy `liveview-mode` localStorage values written by the
// pre-rename code so a stale browser doesn't fall back to the default
// and lose the pilot's last-mode preference. We can't tell legacy
// `'energy'` (Mode 3 in old code) from new `'energy'` (Mode 0 in new
// code) by value alone, so we bump the storage key: the new code
// reads/writes `liveview-mode-v2`, runs a one-shot migration from the
// legacy `liveview-mode` if v2 isn't set yet, then forgets the legacy
// key.
const MODE_LS_KEY = 'liveview-mode-v2';
const LEGACY_MODE_LS_KEY = 'liveview-mode';
const LEGACY_MODE_IDS = {
  aoa:            'energy',
  attitude:       'attitude',
  'indexer-only': 'indexer',
  energy:         'decel',
  ghistory:       'historic',
};

function readPersistedMode() {
  const v2 = safeLsGet(MODE_LS_KEY);
  if (v2 && MODES.some(m => m.id === v2)) return v2;
  const legacy = safeLsGet(LEGACY_MODE_LS_KEY);
  if (legacy) {
    const migrated = LEGACY_MODE_IDS[legacy];
    if (migrated) {
      safeLsSet(MODE_LS_KEY, migrated);
      return migrated;
    }
  }
  return 'energy';
}

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
  ['OAT',      r => fmt(r.oatC, 1) + '°C'],
];

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

// Indexer-local status row — connection state plus age timer, sitting
// under the global PageShell nav.
const StatusRow = ({ status, ageSec }) => html`
  <header id="liveview-header">
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

// Frame age in seconds at which the WebSocket feed is considered stale.
// Mirrors the StaleOverlay threshold and the M5's serialDataFresh() gate.
const STALENESS_THRESHOLD_SEC = 3;

// G-history ring buffer — kept in a useRef so it persists across
// renders.
function useGHistory(rec, ageSec) {
  const buf = useRef(new Float32Array(G.MODE4_BUFFER_LEN));
  const writeIdx = useRef(0);
  const lastSampleMs = useRef(0);
  const tick = useRef(0);
  const [, force] = useState(0);

  // First-time init: fill with 1.0 G so the chart starts visually flat.
  useEffect(() => { buf.current.fill(1.0); }, []);

  // Sample at 5 Hz (matches main.cpp:465's 200 ms gate).
  // Skip the push when the record is absent or the feed is stale —
  // mirrors the M5's serialDataFresh() gate on its own G-history loop.
  useEffect(() => {
    if (!rec) return;
    if (ageSec >= STALENESS_THRESHOLD_SEC) return;
    const now = performance.now();
    if (now - lastSampleMs.current >= G.MODE4_SAMPLE_MS) {
      buf.current[writeIdx.current] = rec.verticalG ?? 1.0;
      writeIdx.current = (writeIdx.current + 1) % G.MODE4_BUFFER_LEN;
      lastSampleMs.current = now;
      tick.current++;
      force(tick.current);
    }
  }, [rec, ageSec]);

  return { buf: buf.current, writeIdx: writeIdx.current };
}

// Empty record used before the first WebSocket frame arrives.
// aoaIsValid false makes the modes hide their variable elements.
const EMPTY_REC = {
  aoaIsValid: false,
  pitchDeg: 0, rollDeg: 0, iasKt: 0, paltFt: 0, vsiFpm: 0,
  verticalG: 1, lateralG: 0, percentLift: 0, flightPathDeg: 0,
  flapsDeg: 0, flapsMinDeg: 0, flapsMaxDeg: 33,
  decelRate: 0, gOnsetRate: 0, dataMark: 0,
  tonesOnPctLift: 0, onSpeedFastPctLift: 0, onSpeedSlowPctLift: 0,
  stallWarnPctLift: 0, pipPctLift: 0,
};

export function IndexerPage() {
  const { rec, status, ageSec } = useWebSocket();
  const [mode, setMode] = useState(readPersistedMode);
  const [dfExpanded, setDfExpanded] = useState(safeLsGet('liveview-datafields-expanded') === '1');

  // The WebSocket arrives at 20 Hz and the hook triggers a render on
  // every frame; that IS the animation tick.  Don't add a separate
  // setInterval — it would double-render at 40 Hz (the WS render plus
  // the interval render) and rebuild Mode 4's 300 SVG circles 12k
  // times/sec on iPhone Safari, which is exactly the workload the
  // legacy WASM /indexer failed at.  When WS goes silent, the
  // StaleOverlay covers the panel and there's nothing to animate.
  const stale = ageSec >= STALENESS_THRESHOLD_SEC;
  const gHist = useGHistory(rec, ageSec);

  const setModeAndPersist = (m) => {
    setMode(m);
    safeLsSet(MODE_LS_KEY, m);
  };
  const toggleDf = () => {
    const next = !dfExpanded;
    setDfExpanded(next);
    safeLsSet('liveview-datafields-expanded', next ? '1' : '0');
  };

  const ActiveMode = MODES.find(m => m.id === mode)?.C ?? Mode0;
  return html`
    <${PageShell} active="indexer">
      <div id="indexer-app">
        <${StatusRow} status=${status} ageSec=${ageSec} />
        <${ModeNav} current=${mode} onChange=${setModeAndPersist} />
        <main id="liveview-main">
          <div id="mode-container">
            <${ActiveMode} r=${rec || EMPTY_REC} stale=${stale}
                           gBuf=${gHist.buf} gWriteIdx=${gHist.writeIdx} />
          </div>
          <${DataFields} rec=${rec} ageSec=${ageSec}
                         expanded=${dfExpanded} onToggle=${toggleDf} />
        </main>
        <div id="footer-warning">
          For diagnostic purposes only. NOT SAFE FOR FLIGHT
        </div>
      </div>
    <//>`;
}
