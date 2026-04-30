// LivePage (/live): two-tab read-only diagnostic view.
//
//   AOA tab   — AOA indexer (vertical bar with chevrons + L/Dmax pips)
//                plus a 13-row data table.
//   AHRS tab  — Attitude horizon plus the same data table.
//
// Single WebSocket consumer.  No writes, no config, no other side
// effects.  Lives at /live for pilots' bookmark muscle memory.

import { html, useState } from '../vendor/preact-standalone.js';
import { useWebSocket } from '../ws/wsClient.js';
import { PageShell } from '../shell/PageShell.js';
import { Indexer, Horizon, PitchLadder, AircraftSymbol, BankArc, TopPointer }
  from '../components/svg/index.js';

const TABS = [
  { id: 'aoa',  label: 'AOA Indexer' },
  { id: 'ahrs', label: 'AHRS' },
];

// 13-row data table (matches the legacy /live row set).  Each entry is
// `[label, getter, options]`.  `options.unit` is appended after the
// formatted number; `options.degrees` true treats undefined as N/A.
const DATA_ROWS = [
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

const StatusRow = ({ status, ageSec }) => html`
  <div class="live-status">
    <span class="live-status-label"><strong>Status:</strong> ${status}</span>
    <span class="live-status-age" style=${{ color: ageSec >= 1 ? 'var(--red, #ff0018)' : '' }}>
      ${ageSec.toFixed(1)} s
    </span>
  </div>`;

const TabBar = ({ active, onChange }) => html`
  <nav class="live-tabs" role="tablist">
    ${TABS.map(t => html`
      <button type="button" role="tab"
              aria-selected=${t.id === active}
              class=${t.id === active ? 'active' : ''}
              onClick=${() => onChange(t.id)}>${t.label}</button>`)}
  </nav>`;

// AOA panel.  Builds anchor percentages from the WS frame and drops
// them into the shared <Indexer/>.  When the device hasn't sent a
// frame yet (rec === null) we render a placeholder so layout doesn't
// jump.
const AoaPanel = ({ rec }) => {
  const anchors = rec ? buildAnchors(rec) : null;
  return html`
    <div class="live-svg-panel">
      <svg viewBox="0 0 320 240" width="100%" preserveAspectRatio="xMidYMid meet"
           aria-label="AOA indexer">
        ${anchors && html`
          <${Indexer} percentLift=${rec.percentLift}
                      anchors=${anchors}
                      flashFlag=${false}
                      aoaIsValid=${rec.aoaIsValid} />`}
      </svg>
      ${!rec && html`<p class="live-placeholder">Waiting for first frame…</p>`}
    </div>`;
};

// Anchor percentages used by the indexer SVG. The fields come straight
// from the firmware-side onspeed_core PercentLift helper.
function buildAnchors(rec) {
  // [aoaPct, fastPct, slowPct, stallWarnPct, ldmaxPct, tonesOnPct, pipPct]
  return [
    rec.percentLift,
    rec.onSpeedFastPctLift,
    rec.onSpeedSlowPctLift,
    rec.stallWarnPctLift,
    rec.pipPctLift,
    rec.tonesOnPctLift,
    rec.pipPctLift,
  ];
}

const AhrsPanel = ({ rec }) => {
  const pitchDeg = rec?.pitchDeg ?? 0;
  const rollDeg  = rec?.rollDeg ?? 0;
  return html`
    <div class="live-svg-panel">
      <svg viewBox="0 0 320 240" width="100%" preserveAspectRatio="xMidYMid meet"
           aria-label="Attitude indicator">
        <${Horizon} pitchDeg=${pitchDeg} rollDeg=${rollDeg} />
        <${PitchLadder} pitchDeg=${pitchDeg} rollDeg=${rollDeg} />
        <${BankArc} rollDeg=${rollDeg} />
        <${AircraftSymbol} />
        <${TopPointer} />
      </svg>
      ${!rec && html`<p class="live-placeholder">Waiting for first frame…</p>`}
    </div>`;
};

const DataTable = ({ rec, ageSec }) => html`
  <table class="live-data-table">
    <tbody>
      ${DATA_ROWS.map(([label, get]) => html`
        <tr><th>${label}</th><td>${rec ? get(rec) : '—'}</td></tr>`)}
      <tr><th>Age</th>
        <td style=${{ color: ageSec >= 1 ? 'var(--red, #ff0018)' : '' }}>
          ${ageSec.toFixed(1)} s
        </td></tr>
    </tbody>
  </table>`;

export function LivePage() {
  const { rec, status, ageSec } = useWebSocket();
  const [tab, setTab] = useState('aoa');
  return html`
    <${PageShell} active="live">
      <div class="live-page">
        <header class="live-header">
          <h1>LiveView</h1>
          <${StatusRow} status=${status} ageSec=${ageSec} />
        </header>
        <${TabBar} active=${tab} onChange=${setTab} />
        <section class="live-body">
          ${tab === 'aoa'
            ? html`<${AoaPanel} rec=${rec} />`
            : html`<${AhrsPanel} rec=${rec} />`}
          <${DataTable} rec=${rec} ageSec=${ageSec} />
        </section>
        <p class="live-warning">
          For diagnostic purposes only.  NOT SAFE FOR FLIGHT.
        </p>
      </div>
    <//>`;
}
