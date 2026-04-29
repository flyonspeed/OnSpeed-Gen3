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

export function start() {
  // Mount each mode panel into its slot. The HTML scaffold in
  // build_liveview_html.py creates `<div data-mode-panel="...">`
  // containers ready to receive these.
  const panels = {};
  const aoaRoot = document.querySelector('[data-mode-panel="aoa"]');
  if (aoaRoot) panels.aoa = mountAoa(aoaRoot);

  const attRoot = document.querySelector('[data-mode-panel="attitude"]');
  if (attRoot) panels.attitude = mountAttitude(attRoot);

  const idxRoot = document.querySelector('[data-mode-panel="indexer-only"]');
  if (idxRoot) panels['indexer-only'] = mountIndexerOnly(idxRoot);

  const energyRoot = document.querySelector('[data-mode-panel="energy"]');
  if (energyRoot) panels.energy = mountEnergy(energyRoot);

  const ghistRoot = document.querySelector('[data-mode-panel="ghistory"]');
  if (ghistRoot) panels.ghistory = mountGHistory(ghistRoot);

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
    },
  });
}
