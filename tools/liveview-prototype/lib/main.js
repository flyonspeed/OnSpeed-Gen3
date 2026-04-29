import { scenarios } from './scenarios.js';
import { buildFrame } from './frameBuilder.js';

// State.
let currentScenario = 'idle';
let scenarioStart = performance.now();
let currentMode = 'aoa';

// Subscribers — Stage 1+ register these to receive { record } messages.
const subscribers = [];
export function subscribe(fn) { subscribers.push(fn); return () => {
  const i = subscribers.indexOf(fn);
  if (i >= 0) subscribers.splice(i, 1);
}; }

// Stage 0: keep the pump alive with a no-op subscriber. Stage 1+ replaces this
// with the AOA mode's update callback.
subscribe(_rec => {});

// 20 Hz tick — emit current scenario's record to all subscribers.
function tick() {
  const t = performance.now() - scenarioStart;
  const fn = scenarios[currentScenario];
  if (!fn) return;
  const r = fn(t);
  for (const s of subscribers) s(r);

  // Also push the record into the wasm-live iframe via inject_serial_byte.
  pushToWasm(r);
}

setInterval(tick, 50);  // 20 Hz

// Wasm-live driver. Wait for the iframe's Module to be ready, then call
// inject_serial_byte for every byte of every frame.
let injectFn = null;
const wasmIframe = document.getElementById('wasm-iframe');
if (wasmIframe) {
  wasmIframe.addEventListener('load', () => {
    const iwin = wasmIframe.contentWindow;
    // Module.cwrap may not be ready yet; poll briefly.
    const tryBind = () => {
      try {
        if (iwin.Module && iwin.Module.cwrap) {
          injectFn = iwin.Module.cwrap('inject_serial_byte', null, ['number']);
          console.log('[prototype] WASM bridge bound');
          return;
        }
      } catch (e) { /* cross-origin? same-origin since both served by us */ }
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

document.querySelectorAll('#mode-nav button[data-mode]').forEach(btn => {
  btn.addEventListener('click', () => {
    currentMode = btn.dataset.mode;
    document.querySelectorAll('[data-mode-panel]').forEach(p => {
      p.style.display = p.dataset.modePanel === currentMode ? '' : 'none';
    });
  });
});

document.getElementById('theme-toggle').addEventListener('click', () => {
  const cur = document.documentElement.dataset.theme || 'dark';
  const next = cur === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  localStorage.setItem('liveview-theme', next);
});

const savedTheme = localStorage.getItem('liveview-theme');
if (savedTheme) document.documentElement.dataset.theme = savedTheme;
