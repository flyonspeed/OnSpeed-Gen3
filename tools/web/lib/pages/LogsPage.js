// LogsPage (/logs): per-flight cards listing SD log sessions, plus
// stacked sections for non-log files and crash dumps. Backed by
// /api/logs.
//
// One row per flight (the .csv). Paired sidecars (.dbg writer log,
// .meta schema JSON) hang off the same card as inline download links;
// they are NOT separate rows. Deleting a card deletes the trio atomically
// via the firmware's bulk-delete handler.
//
// Active session: download links stay enabled; checkbox and trash icon
// hidden. The firmware's IsActiveLogFile() guard refuses sidecar deletes
// of the active session even if the UI ever requests them.

import { html, useState, useEffect } from '../../../../packages/ui-core/vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { getJsonWithRetry, postJson, ApiError } from '../shell/apiClient.js';

function formatBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  const mb = n / (1024 * 1024);
  // Two decimals under 10 MB, one under 100, integer above. Avoids
  // "170.17 MB" reading too precise on a phone screen.
  if (mb < 10)  return mb.toFixed(2) + ' MB';
  if (mb < 100) return mb.toFixed(1) + ' MB';
  return Math.round(mb) + ' MB';
}

function formatDuration(ms) {
  if (!ms || ms <= 0) return null;
  const total = Math.round(ms / 1000);
  const m = Math.floor(total / 60);
  const s = total % 60;
  return m + ':' + String(s).padStart(2, '0');
}

// "2026-04-30T18:32:11Z" -> "Apr 30, 18:32 UTC". GPS-fix-derived; null on
// most cards because not all installs have GPS time wired to the EFIS.
// Falls back to EFIS time-of-day ("HH:MM:SS") when only that is set.
function formatStart(meta) {
  if (!meta) return null;
  if (meta.utcStart) {
    const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(meta.utcStart);
    if (m) {
      const months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
      return `${months[+m[2]-1]} ${+m[3]}, ${m[4]}:${m[5]} UTC`;
    }
    return meta.utcStart;
  }
  if (meta.timeOfDayStart) return meta.timeOfDayStart + ' local';
  return null;
}

function isLogFile(name) {
  const lower = name.toLowerCase();
  return lower.endsWith('.csv') || lower.endsWith('.log');
}

// Inline trash icon (Feather "trash-2").
const TrashIcon = () => html`
  <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"
       viewBox="0 0 24 24" fill="none" stroke="currentColor"
       stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
       aria-hidden="true">
    <polyline points="3 6 5 6 21 6"></polyline>
    <path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"></path>
    <path d="M10 11v6"></path>
    <path d="M14 11v6"></path>
    <path d="M9 6V4a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v2"></path>
  </svg>`;

// Inline download icon.
const DlIcon = () => html`
  <svg xmlns="http://www.w3.org/2000/svg" width="11" height="11"
       viewBox="0 0 24 24" fill="none" stroke="currentColor"
       stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"
       aria-hidden="true" style=${{ marginRight: '3px', verticalAlign: '-1px' }}>
    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
    <polyline points="7 10 12 15 17 10"></polyline>
    <line x1="12" y1="15" x2="12" y2="3"></line>
  </svg>`;

// Render a single labeled stat ("Duration: 30:42").
const Stat = ({ label, value }) => html`
  <div class="log-card-stat">
    <span class="log-card-stat-label">${label}</span>
    <span class="log-card-stat-value">${value}</span>
  </div>`;

// Per-flight card. One log session = one card. CSV download is the
// primary pill; .dbg / .meta pills appear when those sidecars exist.
// Active row hides the trash icon and checkbox but keeps download
// affordances so a pilot can grab the in-progress logs mid-flight.
const LogCard = ({ file, active, selected, busyDeleting, onToggle, onDelete }) => {
  const meta = file.meta;
  const baseName = file.name.replace(/\.[^.]+$/, '');
  const startStr = formatStart(meta);
  const durStr = formatDuration(meta && meta.durationMs);
  const iasStr = (meta && meta.maxIasKt > 0) ? meta.maxIasKt.toFixed(0) + ' kt' : null;
  const altStr = (meta && meta.maxPaltFt > 0) ? meta.maxPaltFt.toFixed(0) + ' ft' : null;
  return html`
    <div class="log-card ${active ? 'log-card-active' : ''}">
      <div class="log-card-left">
        <div class="log-card-header">
          ${!active && html`
            <input type="checkbox" class="log-card-check"
                   checked=${selected}
                   onChange=${onToggle} />`}
          <div class="log-card-name">
            ${file.name}
            ${active && html`<span class="log-card-active-badge">active</span>`}
          </div>
        </div>
        ${startStr && html`
          <div class="log-card-when">${startStr}</div>`}
        <div class="log-card-stats">
          ${durStr && html`<${Stat} label="Dur" value=${durStr} />`}
          ${iasStr && html`<${Stat} label="IAS" value=${iasStr} />`}
          ${altStr && html`<${Stat} label="Alt" value=${altStr} />`}
          <${Stat} label="Size" value=${formatBytes(file.size)} />
        </div>
      </div>
      <div class="log-card-right">
        <div class="log-card-downloads">
          <a class="dl-pill dl-pill-primary"
             href=${'/download?file=' + encodeURIComponent(file.name)}>
            <${DlIcon} />csv
          </a>
          ${file.hasDbg && html`
            <a class="dl-pill"
               href=${'/download?file=' + encodeURIComponent(baseName + '.dbg')}>
              <${DlIcon} />dbg
            </a>`}
          ${file.hasMeta && html`
            <a class="dl-pill"
               href=${'/download?file=' + encodeURIComponent(baseName + '.meta')}>
              <${DlIcon} />meta
            </a>`}
        </div>
        ${active
          ? html`<span class="log-card-trash-spacer" aria-hidden="true"></span>`
          : html`<button type="button" class="log-card-trash greybutton"
                         disabled=${busyDeleting}
                         onClick=${onDelete}
                         title="Delete ${file.name} and its sidecars">
                   <${TrashIcon} />
                 </button>`}
      </div>
    </div>`;
};

// Coredump row inside the (collapsible) Diagnostics section. Each card
// surfaces what the filename already encodes: which boot, which firmware,
// which task crashed.
const CoredumpCard = ({ dump }) => html`
  <div class="log-card log-card-diag">
    <div class="log-card-left">
      <div class="log-card-header">
        <div class="log-card-name">
          boot #${dump.boot} · ${dump.task}
        </div>
      </div>
      <div class="log-card-stats">
        <${Stat} label="Fw"   value=${dump.firmware} />
        <${Stat} label="Size" value=${formatBytes(dump.size)} />
      </div>
    </div>
    <div class="log-card-right">
      <div class="log-card-downloads">
        <a class="dl-pill dl-pill-primary"
           href=${'/download?file=' + encodeURIComponent('/coredumps/' + dump.name)}>
          <${DlIcon} />coredump
        </a>
      </div>
    </div>
  </div>`;

export function LogsPage() {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  const [deleteErrors, setDeleteErrors] = useState([]);
  const [selected, setSelected] = useState(new Set());
  const [busyDeleting, setBusyDeleting] = useState(false);
  const [retryStatus, setRetryStatus] = useState(null);
  const [diagOpen, setDiagOpen] = useState(false);

  const reload = async () => {
    setError(null);
    setRetryStatus(null);
    try {
      const d = await getJsonWithRetry('/api/logs', {
        maxAttempts: 4,
        onAttempt: (attempt) => {
          if (attempt > 1) {
            setRetryStatus(`SD card busy, retrying (attempt ${attempt} of 4)…`);
          }
        },
      });
      setRetryStatus(null);
      setData(d);
      setSelected(new Set());
    } catch (e) {
      setRetryStatus(null);
      if (e instanceof ApiError && e.status === 503) {
        setError('SD card is too busy right now. The flight log writer is monopolizing the card. Wait a few seconds and try again.');
      } else {
        setError((e instanceof ApiError) ? e.message : String(e));
      }
    }
  };

  useEffect(() => { reload(); }, []);

  const runDelete = async (names) => {
    setBusyDeleting(true);
    setDeleteErrors([]);
    try {
      const r = await postJson('/api/logs/delete-bulk', { names });
      const errs = Array.isArray(r && r.errors) ? r.errors : [];
      setDeleteErrors(errs);
      await reload();
    } catch (e) {
      setError((e instanceof ApiError) ? e.message : String(e));
    } finally {
      setBusyDeleting(false);
    }
  };

  const toggle = (name) => () => {
    setSelected(prev => {
      const next = new Set(prev);
      if (next.has(name)) next.delete(name);
      else next.add(name);
      return next;
    });
  };

  // Logs and other-files split. Sidecars (.dbg/.meta) and coredumps are
  // surfaced through hasDbg/hasMeta and coredumps[] respectively, so they
  // don't show up here even if the firmware (transitionally) returns them.
  const logs   = data ? data.files.filter(f => isLogFile(f.name)) : [];
  const others = data ? data.files.filter(f => !isLogFile(f.name)
                                            && !f.name.toLowerCase().endsWith('.dbg')
                                            && !f.name.toLowerCase().endsWith('.meta')) : [];
  const coredumps = data && Array.isArray(data.coredumps) ? data.coredumps : [];

  const logsTotal   = logs.reduce((a, f) => a + (f.size || 0), 0);
  const othersTotal = others.reduce((a, f) => a + (f.size || 0), 0);

  const selectableNames = logs.filter(f => f.name !== data?.activeLog).map(f => f.name);
  const allChecked = selectableNames.length > 0 &&
                     selectableNames.every(n => selected.has(n));
  const someChecked = selectableNames.some(n => selected.has(n));

  const toggleAll = () => {
    setSelected(prev => allChecked ? new Set() : new Set(selectableNames));
  };

  const deleteSelected = async () => {
    if (selected.size === 0) return;
    if (!window.confirm(`Delete ${selected.size} flight log(s) and their sidecars?`)) return;
    await runDelete([...selected]);
  };

  const deleteOne = (name) => async () => {
    if (!window.confirm(`Delete ${name} and its sidecars?`)) return;
    await runDelete([name]);
  };

  const deleteErrorSummary = () => {
    if (deleteErrors.length === 0) return '';
    const items = deleteErrors
      .map(e => `${e.name} (${e.reason || 'failed'})`)
      .join(', ');
    return `Could not delete ${deleteErrors.length} file(s): ${items}`;
  };

  return html`
    <${PageShell} active="logs">
      <div class="logs-page">
        ${error && html`<p class="banner banner-err">${error}</p>`}
        ${retryStatus && html`<p class="banner banner-warn">${retryStatus}</p>`}
        ${deleteErrors.length > 0 && html`
          <p class="banner banner-err">${deleteErrorSummary()}</p>`}
        ${!data && !error && !retryStatus && html`<p>Loading…</p>`}
        ${data && html`
          <section class="logs-section">
            <header class="logs-section-head">
              <h2>Flight logs</h2>
              <div class="logs-section-meta">
                ${logs.length} flight${logs.length === 1 ? '' : 's'} ·
                ${formatBytes(logsTotal)} total
              </div>
            </header>
            ${logs.length > 1 && html`
              <div class="logs-bulk">
                <label class="bulk-check">
                  <input type="checkbox"
                         checked=${allChecked}
                         ref=${(el) => { if (el) el.indeterminate = someChecked && !allChecked; }}
                         onChange=${toggleAll} />
                  Select all (excluding active)
                </label>
                <div class="bulk-actions">
                  <button type="button" class="redbutton"
                          disabled=${selected.size === 0 || busyDeleting}
                          onClick=${deleteSelected}>
                    Delete selected (${selected.size})
                  </button>
                  <button type="button" class="greybutton"
                          onClick=${reload}>Refresh</button>
                </div>
              </div>`}
            <div class="log-cards">
              ${logs.map(f => html`
                <${LogCard}
                  file=${f}
                  active=${f.name === data.activeLog}
                  selected=${selected.has(f.name)}
                  busyDeleting=${busyDeleting}
                  onToggle=${toggle(f.name)}
                  onDelete=${deleteOne(f.name)} />`)}
            </div>
          </section>

          ${others.length > 0 && html`
            <section class="logs-section">
              <header class="logs-section-head">
                <h2>Other files</h2>
                <div class="logs-section-meta">
                  ${others.length} file${others.length === 1 ? '' : 's'} ·
                  ${formatBytes(othersTotal)} total
                </div>
              </header>
              <div class="log-cards">
                ${others.map(f => html`
                  <div class="log-card">
                    <div class="log-card-left">
                      <div class="log-card-header">
                        <div class="log-card-name">${f.name}</div>
                      </div>
                      <div class="log-card-stats">
                        <${Stat} label="Size" value=${formatBytes(f.size)} />
                      </div>
                    </div>
                    <div class="log-card-right">
                      <div class="log-card-downloads">
                        <a class="dl-pill dl-pill-primary"
                           href=${'/download?file=' + encodeURIComponent(f.name)}>
                          <${DlIcon} />download
                        </a>
                      </div>
                      <button type="button" class="log-card-trash greybutton"
                              disabled=${busyDeleting}
                              onClick=${deleteOne(f.name)}
                              title="Delete ${f.name}">
                        <${TrashIcon} />
                      </button>
                    </div>
                  </div>`)}
              </div>
            </section>`}

          <section class="logs-section">
            <header class="logs-section-head logs-section-collapsible"
                    onClick=${() => setDiagOpen(o => !o)}>
              <h2>
                <span class="logs-disclosure">${diagOpen ? '▾' : '▸'}</span>
                Diagnostics
                <span class="logs-count-badge">${coredumps.length}</span>
              </h2>
              <div class="logs-section-meta">
                ${coredumps.length === 0
                  ? 'No crash dumps'
                  : `${coredumps.length} crash dump${coredumps.length === 1 ? '' : 's'}`}
              </div>
            </header>
            ${diagOpen && coredumps.length > 0 && html`
              <div class="log-cards">
                ${coredumps.map(d => html`<${CoredumpCard} dump=${d} />`)}
              </div>`}
            ${diagOpen && coredumps.length === 0 && html`
              <p class="logs-empty">The box has not panicked since the last format.</p>`}
          </section>`}
      </div>
    <//>`;
}
