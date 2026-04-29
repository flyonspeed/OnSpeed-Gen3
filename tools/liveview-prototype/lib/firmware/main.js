// Firmware-side entry point. Equivalent to lib/main.js (the prototype
// harness's entry point) but driven by a real WebSocket instead of
// synthetic scenarios.
//
// The bundler wires `start` to fire on DOMContentLoaded.

import { mountAoa } from '../modes/aoa.js';
import { mountAttitude } from '../modes/attitude.js';
import { mountIndexerOnly } from '../modes/indexer-only.js';
import { mountEnergy } from '../modes/energy.js';
import { mountGHistory } from '../modes/ghistory.js';
import { connect } from './wsClient.js';
import { mountDataFields } from './dataFields.js';
import { mountStaleOverlay } from './staleOverlay.js';

export function start() {
  // Mount each mode panel into its slot. The HTML scaffold in
  // build_liveview_html.py creates `<div data-mode-panel="...">`
  // containers ready to receive these.
  const panels = {};
  const staleOverlays = [];

  // Mount each mode panel + a stale-data overlay last so the overlay
  // paints on top when serial data goes stale (matches the M5's
  // red-X-of-death behavior at main.cpp:655-686).
  function mountWithOverlay(name, root, mountFn) {
    if (!root) return;
    const panel = mountFn(root);
    panels[name] = panel;
    if (panel.el && panel.el.tagName === 'svg') {
      staleOverlays.push(mountStaleOverlay(panel.el));
    }
  }

  mountWithOverlay('aoa',           document.querySelector('[data-mode-panel="aoa"]'),           mountAoa);
  mountWithOverlay('attitude',      document.querySelector('[data-mode-panel="attitude"]'),      mountAttitude);
  mountWithOverlay('indexer-only',  document.querySelector('[data-mode-panel="indexer-only"]'),  mountIndexerOnly);
  mountWithOverlay('energy',        document.querySelector('[data-mode-panel="energy"]'),        mountEnergy);
  mountWithOverlay('ghistory',      document.querySelector('[data-mode-panel="ghistory"]'),      mountGHistory);

  // Datafields table.
  const toggleBtn = document.getElementById('datafields-toggle');
  const dfContainer = document.getElementById('datafields');
  let dataFields = null;
  if (toggleBtn && dfContainer) {
    dataFields = mountDataFields(toggleBtn, dfContainer);
  }

  // Mode toggle. Default to AOA on first load. Persist to localStorage
  // so reload doesn't yank the pilot back to AOA mid-flight.
  const STORED_MODE_KEY = 'liveview-mode';
  let currentMode = localStorage.getItem(STORED_MODE_KEY) || 'aoa';
  applyMode();

  document.querySelectorAll('#mode-nav button[data-mode]').forEach(btn => {
    btn.addEventListener('click', () => {
      currentMode = btn.dataset.mode;
      localStorage.setItem(STORED_MODE_KEY, currentMode);
      applyMode();
    });
  });

  function applyMode() {
    document.querySelectorAll('[data-mode-panel]').forEach(p => {
      p.style.display = p.dataset.modePanel === currentMode ? '' : 'none';
    });
    document.querySelectorAll('#mode-nav button[data-mode]').forEach(b => {
      b.classList.toggle('active', b.dataset.mode === currentMode);
    });
  }

  // Status line.
  const statusEl = document.getElementById('connectionstatus');
  const ageEl    = document.getElementById('age-indicator');

  // Wire WebSocket → panels. We deliver every record to every mounted
  // panel — the inactive ones still update their internal state so
  // switching modes shows fresh data immediately.
  connect({
    onRecord: (rec) => {
      for (const k in panels) {
        try { panels[k].update(rec); }
        catch (e) { console.log('panel update error:', k, e); }
      }
      if (dataFields) dataFields.update(rec);
    },
    onStatus: (msg) => {
      if (statusEl) statusEl.textContent = msg;
    },
    onAge: (sec) => {
      if (ageEl) {
        ageEl.textContent = sec.toFixed(1) + 's';
        ageEl.style.color = (sec >= 1) ? 'var(--red, #ff0018)' : '';
      }
      if (dataFields) dataFields.setAge(sec);

      // Stale-data overlay threshold matches the M5's serialDataFresh()
      // semantic (main.cpp:655) — once data hasn't arrived for 3 s,
      // paint the red X + "NO DATA" pill across each mode panel.
      const stale = sec >= 3;
      for (const o of staleOverlays) o.setStale(stale);
    },
  });
}
