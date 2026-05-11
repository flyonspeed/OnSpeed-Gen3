// Synthetic-scenario harness entry point. Mounts each Preact mode
// panel into its data-mode-panel slot and ticks them at 20 Hz from
// canned scenarios (idle / cruise / approach / stall warn).
//
// The firmware-served /indexer page uses the same m5modes/ renderers
// via the same wsRecordToState adapter, so anything we see here is
// what pilots see at /indexer in the field.

import { html, render } from '../../../packages/ui-core/vendor/preact-standalone.js';
import {
  EnergyMode, AttitudeMode, IndexerMode, DecelMode, HistoricGMode,
} from '../../../packages/ui-core/components/svg/m5modes/index.js';
import { wsRecordToState } from '../../../packages/ui-core/adapters/wsRecordToState.js';
import * as G from '../../../packages/ui-core/core/geometry.js';
import { scenarios } from './scenarios.js';
import { makeEmaState, updateEma, resetEma } from './core/ema.js';

let currentScenario = 'idle';
let scenarioStart = performance.now();
let currentMode = 'energy';

// Mode 4's ring buffer is owned here (not inside the component) so it
// survives across renders. IndexerPage uses a useRef inside a
// useGHistory hook for the same reason.
const gBuf = new Float32Array(G.MODE4_BUFFER_LEN);
gBuf.fill(1.0);
let gWriteIdx = 0;
let lastSampleMs = 0;

// Decel-rate EMA, mirroring IndexerPage's useDecelEma hook so the
// scenario harness shows the same smoothed pointer behavior the pilot
// sees at /indexer Mode 3 in the field.  alpha matches the M5
// firmware's `decelSmoothingAlpha` (SerialRead.cpp:28).  Bug #362.
const decelEma = makeEmaState(0.04);

// 500 ms display snapshot — matches IndexerPage::useDisplaySnapshot
// which mirrors main.cpp's `updateRateNumbers = 500`. Without this
// the corner readouts twitch at 20 Hz, which doesn't match the
// hardware panel and isn't what we want to show in the harness either.
const SNAPSHOT_MS = 500;
let displaySnap = null;
let lastSnapshotMs = 0;

// IDs mirror tools/web/lib/pages/IndexerPage.js MODES table.
const PANELS = [
  { id: 'energy',   C: EnergyMode },
  { id: 'attitude', C: AttitudeMode },
  { id: 'indexer',  C: IndexerMode },
  { id: 'decel',    C: DecelMode },
  { id: 'historic', C: HistoricGMode },
];

function paintAll(state) {
  for (const p of PANELS) {
    const root = document.querySelector(`[data-mode-panel="${p.id}"]`);
    if (!root) continue;
    render(html`<${p.C} state=${state} stale=${false} />`, root);
  }
}

function tick() {
  const t = performance.now() - scenarioStart;
  const fn = scenarios[currentScenario];
  if (!fn) return;
  const r = fn(t);

  // Tick the G-history ring buffer at 5 Hz.
  const now = performance.now();
  if (now - lastSampleMs >= G.MODE4_SAMPLE_MS) {
    gBuf[gWriteIdx] = r.verticalG ?? 1.0;
    gWriteIdx = (gWriteIdx + 1) % G.MODE4_BUFFER_LEN;
    lastSampleMs = now;
  }

  // Tick the decel EMA every frame.
  const decelRateSmoothed = updateEma(decelEma, r.decelRate);

  // Tick the 500 ms display snapshot.
  if (now - lastSnapshotMs >= SNAPSHOT_MS) {
    displaySnap = Object.freeze({
      iasKt:             r.iasKt,
      paltFt:            r.paltFt,
      pitchDeg:          r.pitchDeg,
      verticalG:         r.verticalG,
      percentLift:       r.percentLift,
      decelRateSmoothed: decelRateSmoothed,
    });
    lastSnapshotMs = now;
  }

  const state = wsRecordToState(r, displaySnap,
    { buf: gBuf, writeIdx: gWriteIdx, hasSamples: true });
  paintAll(state);
}

setInterval(tick, 50);  // 20 Hz

document.querySelectorAll('#scenario-nav button[data-scenario]').forEach(btn => {
  btn.addEventListener('click', () => {
    currentScenario = btn.dataset.scenario;
    scenarioStart = performance.now();
    // Each scenario should start from a clean filter state — otherwise
    // the smoothed needle drifts in for ~1.25 s after a switch (e.g.
    // stall → idle holds the needle near −2 kt/s while the new
    // scenario emits 0).  Pilots opening the harness should see the
    // reboot-fresh first-frame behavior, not residual state from the
    // last selection.
    resetEma(decelEma);
  });
});

document.querySelectorAll('#mode-nav button[data-mode]').forEach(btn => {
  btn.addEventListener('click', () => {
    currentMode = btn.dataset.mode;
    document.querySelectorAll('[data-mode-panel]').forEach(p => {
      p.style.display = p.dataset.modePanel === currentMode ? '' : 'none';
    });
  });
});
