// Indexer page (/indexer): five OnSpeed display modes (Energy
// Display, Attitude, Indexer, Decel Display, Historic G) backed by
// the live WebSocket feed.
//
// Renderer family: packages/ui-core/components/svg/m5modes/. Same
// renderers the docs-site replay tool uses. Page-side adapter
// (wsRecordToState) converts the WebSocket record into the
// canonical M5State the renderers consume. Mode selection persists
// to localStorage so a reload comes back to the last view.
//
// This file is the canonical source of mode names. The C++ M5
// firmware (software/OnSpeed-M5-Display/src/main.cpp), the X-Plane
// plugin menu/button (software/OnSpeed-XPlane-Plugin/), and the docs
// site (docs/site/docs/) all mirror the names from the MODES table
// below. Keep them in sync when changing names.

import { html, useState, useEffect, useRef } from '../../../../packages/ui-core/vendor/preact-standalone.js';
import * as G from '../../../../packages/ui-core/core/geometry.js';
import { fmt } from '../../../../packages/ui-core/core/format.js';
import {
  EnergyMode, AttitudeMode, IndexerMode, DecelMode, HistoricGMode,
} from '../../../../packages/ui-core/components/svg/m5modes/index.js';
import { wsRecordToState } from '../../../../packages/ui-core/adapters/wsRecordToState.js';
import { useWebSocket } from '../ws/wsClient.js';
import { PageShell } from '../shell/PageShell.js';
import { makeEmaState, updateEma, resetEma } from '../core/ema.js';

// EMA alpha for Mode 3's decel pointer / readout.  Matches the M5
// firmware's `decelSmoothingAlpha` in SerialRead.cpp:28 (0.04 at 20 Hz
// ≈ 1.25 s τ).  Both surfaces poll at 20 Hz, so a literal alpha gives
// a literal time-constant match — Mode 3 tracks the M5 hardware gauge
// in time-constant during normal continuous operation.  Brief
// disagreement after a WiFi reconnect is intrinsic: the M5 hard-resets
// its local SavGol and EMA on >500 ms serial gaps to flush stale window
// samples, while the JS side has no SavGol of its own (the firmware's
// SavGol runs continuously through any WiFi outage), so the JS EMA
// holds its last value rather than zeroing.  See issue #362 and the
// spec at docs/superpowers/specs/2026-05-08-decel-rate-smoothing-design.md.
const DECEL_EMA_ALPHA = 0.04;

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
  { id: 'energy',   label: 'Energy',     C: EnergyMode },
  { id: 'attitude', label: 'Attitude',   C: AttitudeMode },
  { id: 'indexer',  label: 'Indexer',    C: IndexerMode },
  { id: 'decel',    label: 'Decel',      C: DecelMode },
  { id: 'historic', label: 'Historic G', C: HistoricGMode },
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

// IAS and AOA mirror the SVG indexer's M5-style dashes — one dash per
// missing digit in the field's natural width — so the data-fields
// table reads the same as the gauges above when air data is invalid.
// Other fields stay on `fmt`'s single em-dash since they're not
// gated on bIasAlive (PAlt, G, Pitch, Roll come from independent
// sensors).
const fmtDegOrTwoDash = (v) => {
  const s = fmt(v, 2);
  return s === '—' ? '--' : s + '°';
};
const DATA_FIELDS = [
  ['AOA',      r => r.aoaIsValid ? r.aoaDeg.toFixed(2) + '°' : '--'],
  ['Der AOA',  r => fmtDegOrTwoDash(r.derivedAoaDeg)],
  ['FltPath',  r => fmt(r.flightPathDeg, 1) + '°'],
  ['IAS',      r => (r.iasKt == null ? '---' : Math.round(r.iasKt) + ' kt')],
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

// Two thresholds, intentionally separate. They start equal at 3 s but the
// design rationale differs: the overlay can react quickly to any wire gap
// (issue #464 item 1 plans to drop it toward ~300 ms to match the M5's
// 300 ms NO-DATA overlay), but the G-history sampler should only freeze on
// a genuine multi-second outage so the strip chart doesn't stutter on
// brief WiFi micro-gaps. Lowering one is not an automatic reason to lower
// the other — name the decision points so #464 item 1's implementer sees
// them as separate calls.
const STALENESS_THRESHOLD_SEC    = 3;  // StaleOverlay (covers the page when stale)
const G_HISTORY_STALENESS_SEC    = 3;  // useGHistory sampler gate

// Decel-rate EMA — kept in a useRef so it accumulates across renders
// and across mode switches (the user can flip away from Mode 3 and
// back without resetting the smoothing state).  Ingests on every WS
// frame so the smoothed value is always current; Mode 3 reads it via
// the `decelRateSmoothed` prop.  Bug #362.
//
// Validity gating mirrors the M5's iasIsValid-edge SavGol reset
// (SerialRead.cpp::SerialProcess after PR #486):
//
//   - During invalid (rec.aoaIsValid === false): skip the update.
//     wsClient maps the JSON's null-on-invalid DecelRate to 0; without
//     this guard the EMA would seed with zeros during taxi.
//   - On the false→true edge: reset the EMA state so the first valid
//     post-transition sample seeds fresh.  Without this reset, the
//     state holds the last pre-taxi smoothed value (e.g. -0.5 kt/s
//     from the prior approach) and relaxes toward 0 over ~25 frames
//     (~1.25s at α=0.04) while the firmware's freshly-reset SavGol
//     ships ~0; pilot would see a stale negative reading on /indexer
//     Mode 3 for ~1.25s after takeoff while the M5 hardware shows 0.
//
// Tracks #484; companion to PR #482 / PR #486.
function useDecelEma(rec) {
  const stateRef = useRef(makeEmaState(DECEL_EMA_ALPHA));
  const lastRecRef = useRef(null);
  const lastValidRef = useRef(false);
  const [smoothed, setSmoothed] = useState(null);

  useEffect(() => {
    if (rec === lastRecRef.current) return;
    lastRecRef.current = rec;
    if (rec == null) return;
    const valid = rec.aoaIsValid !== false;
    if (valid && !lastValidRef.current) {
      resetEma(stateRef.current);
    }
    lastValidRef.current = valid;
    if (!valid) return;
    const out = updateEma(stateRef.current, rec.decelRate);
    if (out !== null) setSmoothed(out);
  }, [rec]);

  return smoothed;
}

// G-history ring buffer — kept in a useRef so it persists across
// renders.
function useGHistory(rec, ageSec) {
  const buf = useRef(new Float32Array(G.MODE4_BUFFER_LEN));
  const writeIdx = useRef(0);
  const lastSampleMs = useRef(0);
  const tick = useRef(0);
  const hasSamples = useRef(false);
  const [, force] = useState(0);

  // First-time init: fill with 1.0 G so the chart starts visually flat
  // once samples begin arriving.  GHistory is gated on `hasSamples` so
  // nothing renders until the first WS frame lands; this seed only
  // matters for the partial-fill window between sample 1 and sample N.
  useEffect(() => { buf.current.fill(1.0); }, []);

  // Sample at 5 Hz (matches main.cpp:465's 200 ms gate).
  // Skip the push when the record is absent or the feed is stale —
  // mirrors the M5's serialDataFresh() gate on its own G-history loop.
  useEffect(() => {
    if (!rec) return;
    if (ageSec >= G_HISTORY_STALENESS_SEC) return;
    const now = performance.now();
    if (now - lastSampleMs.current >= G.MODE4_SAMPLE_MS) {
      buf.current[writeIdx.current] = rec.verticalG ?? 1.0;
      writeIdx.current = (writeIdx.current + 1) % G.MODE4_BUFFER_LEN;
      lastSampleMs.current = now;
      hasSamples.current = true;
      tick.current++;
      force(tick.current);
    }
  }, [rec, ageSec]);

  return { buf: buf.current, writeIdx: writeIdx.current,
           hasSamples: hasSamples.current };
}

// useDisplaySnapshot — latches a handful of fields at the M5 panel's
// text-readout cadence (500 ms by default). Matches main.cpp's
// `updateRateNumbers = 500` block: the hardware M5 refreshes IAS / G /
// percent-lift / etc. corner numerals every half-second so the pilot
// can read them; the indexer chevrons + slip ball still animate at
// the wire rate.
//
// The hook is given the live `rec` (20 Hz) plus the already-EMA-
// smoothed decel rate (via useDecelEma above). On each interval it
// snapshots the fields the adapter will route into the display*
// state slots. Returns null until the first interval fires, then a
// frozen { iasKt, paltFt, pitchDeg, verticalG, percentLift,
// decelRateSmoothed } object.
function useDisplaySnapshot(rec, decelRateSmoothed, intervalMs = 500) {
  const recRef = useRef(rec);
  const decelRef = useRef(decelRateSmoothed);
  // Keep refs current without re-firing the interval — the interval
  // reads the latest values from the refs.
  recRef.current = rec;
  decelRef.current = decelRateSmoothed;

  const [snap, setSnap] = useState(null);
  useEffect(() => {
    const id = setInterval(() => {
      const r = recRef.current;
      if (!r) return;
      setSnap(Object.freeze({
        iasKt:             r.iasKt,
        paltFt:            r.paltFt,
        pitchDeg:          r.pitchDeg,
        verticalG:         r.verticalG,
        percentLift:       r.percentLift,
        decelRateSmoothed: decelRef.current,
      }));
    }, intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return snap;
}

// Empty record used before the first WebSocket frame arrives.
// aoaIsValid false makes the modes hide their variable elements.
// iasKt null lets the IAS readout dash to '---' the same way it
// would on a powered-but-not-flying bench (bIasAlive=false case).
const EMPTY_REC = {
  aoaIsValid: false,
  pitchDeg: 0, rollDeg: 0, iasKt: null, paltFt: 0, vsiFpm: 0,
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
  const decelRateSmoothed = useDecelEma(rec);
  const displaySnap = useDisplaySnapshot(rec, decelRateSmoothed, 500);

  // Build the canonical M5State for this frame. Pre-WS-frame we feed
  // EMPTY_REC so the adapter still produces a valid state — corner
  // readouts dash, indexer chevrons sit at percent=0, slip ball
  // centered. The 500 ms snapshot starts as null until the first
  // interval fires; the adapter renders dashes during that window.
  const state = wsRecordToState(rec || EMPTY_REC, displaySnap, gHist);

  const setModeAndPersist = (m) => {
    setMode(m);
    safeLsSet(MODE_LS_KEY, m);
  };
  const toggleDf = () => {
    const next = !dfExpanded;
    setDfExpanded(next);
    safeLsSet('liveview-datafields-expanded', next ? '1' : '0');
  };

  const ActiveMode = MODES.find(m => m.id === mode)?.C ?? EnergyMode;
  return html`
    <${PageShell} active="indexer">
      <div id="indexer-app">
        <${StatusRow} status=${status} ageSec=${ageSec} />
        <${ModeNav} current=${mode} onChange=${setModeAndPersist} />
        <main id="liveview-main">
          <div id="mode-container">
            <${ActiveMode} state=${state} stale=${stale} />
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
