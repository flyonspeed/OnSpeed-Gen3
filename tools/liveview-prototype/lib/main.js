// Synthetic-scenario harness entry point. Mounts each Preact mode
// panel into its data-mode-panel slot and ticks them at 20 Hz from
// canned scenarios (idle / cruise / approach / stall warn).
//
// The firmware-served version (lib/firmware/App.js) consumes a real
// WebSocket feed and uses the same Mode components, so anything we
// see here is what pilots will see at /indexer.

import { html, render } from './vendor/preact-standalone.js';
import { Mode0, Mode1, Mode2, Mode3, Mode4 } from './modes.js';
import * as G from './geometry.js';
import { scenarios } from './scenarios.js';
import { buildFrame } from './frameBuilder.js';

let currentScenario = 'idle';
let scenarioStart = performance.now();
let currentMode = 'aoa';

// Mode 4's ring buffer is owned here (not inside the component) so it
// survives across renders. The firmware-side App uses a useRef inside
// a useGHistory hook for the same reason.
const gBuf = new Float32Array(G.MODE4_BUFFER_LEN);
gBuf.fill(1.0);
let gWriteIdx = 0;
let lastSampleMs = 0;

const PANELS = [
  { id: 'aoa',          C: Mode0 },
  { id: 'attitude',     C: Mode1 },
  { id: 'indexer-only', C: Mode2 },
  { id: 'energy',       C: Mode3 },
  { id: 'ghistory',     C: Mode4 },
];

function paintAll(rec) {
  for (const p of PANELS) {
    const root = document.querySelector(`[data-mode-panel="${p.id}"]`);
    if (!root) continue;
    render(html`<${p.C} r=${rec} stale=${false}
                       gBuf=${gBuf} gWriteIdx=${gWriteIdx} />`, root);
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

  paintAll(r);
  pushToWasm(r);
}

setInterval(tick, 50);  // 20 Hz

// ----- WASM-live A/B bridge -----
//
// Same iframe-driven pattern as before: pipe each scenario record
// through the M5 sim's _inject_serial_byte so the wasm-live panel
// renders the exact same data we feed our Preact panels.
let injectFn = null;
const wasmIframe = document.getElementById('wasm-iframe');
if (wasmIframe) {
  wasmIframe.addEventListener('load', () => {
    const iwin = wasmIframe.contentWindow;
    const tryBind = () => {
      try {
        if (iwin.Module && iwin.Module.cwrap) {
          injectFn = iwin.Module.cwrap('inject_serial_byte', null, ['number']);
          console.log('[prototype] WASM bridge bound');
          return;
        }
      } catch (e) { /* same-origin guard, ignore */ }
      setTimeout(tryBind, 200);
    };
    setTimeout(tryBind, 200);
  });
}

function pushToWasm(record) {
  if (!injectFn) return;
  const buf = buildFrame(record);
  for (let i = 0; i < buf.length; i++) injectFn(buf[i]);
}

// ----- UI wiring -----

document.querySelectorAll('#scenario-nav button[data-scenario]').forEach(btn => {
  btn.addEventListener('click', () => {
    currentScenario = btn.dataset.scenario;
    scenarioStart = performance.now();
  });
});

// Track the wasm-live's currently-displayed mode so we can synthesize
// the right number of BtnB (ArrowDown) keypresses to cycle it to match.
const MODE_ORDER = PANELS.map(p => p.id);
let wasmModeIdx = 0;

function syncWasmMode(targetMode) {
  if (!wasmIframe || !wasmIframe.contentDocument) return;
  const targetIdx = MODE_ORDER.indexOf(targetMode);
  if (targetIdx < 0 || targetIdx === wasmModeIdx) return;
  const canvas = wasmIframe.contentDocument.getElementById('canvas');
  if (!canvas) return;
  const steps = (targetIdx - wasmModeIdx + MODE_ORDER.length) % MODE_ORDER.length;
  let i = 0;
  const fireOne = () => {
    if (i >= steps) return;
    canvas.focus();
    const opts = { keyCode: 40, which: 40, code: 'ArrowDown', key: 'ArrowDown', bubbles: true };
    canvas.dispatchEvent(new KeyboardEvent('keydown', opts));
    setTimeout(() => canvas.dispatchEvent(new KeyboardEvent('keyup', opts)), 30);
    i++;
    setTimeout(fireOne, 200);
  };
  fireOne();
  wasmModeIdx = targetIdx;
}

document.querySelectorAll('#mode-nav button[data-mode]').forEach(btn => {
  btn.addEventListener('click', () => {
    currentMode = btn.dataset.mode;
    document.querySelectorAll('[data-mode-panel]').forEach(p => {
      p.style.display = p.dataset.modePanel === currentMode ? '' : 'none';
    });
    syncWasmMode(currentMode);
  });
});
