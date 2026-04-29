import { scenarios } from './scenarios.js';
import { buildFrame } from './frameBuilder.js';
import { mountAoa } from './modes/aoa.js';
import { mountAttitude } from './modes/attitude.js';
import { mountIndexerOnly } from './modes/indexer-only.js';
import { mountEnergy } from './modes/energy.js';
import { mountGHistory } from './modes/ghistory.js';

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

// Mount the AOA mode panel and wire it to the data pump.
const svgRoot = document.getElementById('svg-root');
const aoaPanel = mountAoa(svgRoot);
subscribe(rec => aoaPanel.update(rec));

// Mount the Attitude (Mode 1) panel into its own div. The mode-button
// handler below toggles `style.display` on the data-mode-panel divs;
// this widget runs every tick regardless so it's ready when shown.
const attitudeRoot = document.querySelector('[data-mode-panel="attitude"]');
if (attitudeRoot) {
  const attitudePanel = mountAttitude(attitudeRoot);
  subscribe(rec => attitudePanel.update(rec));
}

const indexerOnlyRoot = document.querySelector('[data-mode-panel="indexer-only"]');
if (indexerOnlyRoot) {
  const indexerOnlyPanel = mountIndexerOnly(indexerOnlyRoot);
  subscribe(rec => indexerOnlyPanel.update(rec));
}

const energyRoot = document.querySelector('[data-mode-panel="energy"]');
if (energyRoot) {
  const energyPanel = mountEnergy(energyRoot);
  subscribe(rec => energyPanel.update(rec));
}

const ghistoryRoot = document.querySelector('[data-mode-panel="ghistory"]');
if (ghistoryRoot) {
  const ghistoryPanel = mountGHistory(ghistoryRoot);
  subscribe(rec => ghistoryPanel.update(rec));
}

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

// Track the wasm-live's currently-displayed mode so we can synthesize
// the right number of BtnB keypresses to cycle it to match. The wasm
// sim starts on mode 0 (AOA) per its setup() default and advances 0→4→0
// on each BtnB.wasPressed.
const MODE_ORDER = ['aoa', 'attitude', 'indexer-only', 'energy', 'ghistory'];
let wasmModeIdx = 0;

function syncWasmMode(targetMode) {
  if (!wasmIframe || !wasmIframe.contentDocument) return;
  const targetIdx = MODE_ORDER.indexOf(targetMode);
  if (targetIdx < 0 || targetIdx === wasmModeIdx) return;

  const idoc = wasmIframe.contentDocument;
  const canvas = idoc.getElementById('canvas');
  if (!canvas) return;
  // emscripten SDL2 listens for keydown on document/canvas. lgfx's panel
  // setup binds Down=GPIO38=BtnB. Synthesize one keydown+keyup per step
  // and advance our shadow counter.
  const steps = (targetIdx - wasmModeIdx + MODE_ORDER.length) % MODE_ORDER.length;
  let i = 0;
  const fireOne = () => {
    if (i >= steps) return;
    canvas.focus();
    const opts = { keyCode: 40, which: 40, code: 'ArrowDown', key: 'ArrowDown', bubbles: true };
    canvas.dispatchEvent(new KeyboardEvent('keydown', opts));
    setTimeout(() => canvas.dispatchEvent(new KeyboardEvent('keyup', opts)), 30);
    i++;
    setTimeout(fireOne, 200);  // give the wasm sim time to consume each press
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

// (No theme toggle — avionics palette is dark-only by design.)
