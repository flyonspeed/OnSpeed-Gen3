// LogsPage (/logs): SD log file listing.  Backed by /api/logs (which
// also classifies files into log .csv/.log + non-log "other" files
// based on the extension; LogsPage mirrors that split visually).
//
// Per-row controls:
//   - Checkbox per non-active row → bulk delete via /api/logs/delete-bulk.
//   - Direct download links to /download?file=… for browser-native
//     streaming (no JSON wrapper — the legacy /download path is
//     intact and serves the file as Content-Type: text/csv).
//   - Trash icon per row → individual delete via the bulk endpoint
//     with a single-name body.
//
// Active-log row is rendered without checkbox or trash icon and
// labeled "(active)" — the API's IsActiveLogFile() check guards
// the actual delete, but reflecting it in the UI prevents confusion.

import { html, useState, useEffect } from '../vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { getJson, postJson, ApiError } from '../shell/apiClient.js';

function formatBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  return (n / (1024 * 1024)).toFixed(2) + ' MB';
}

function formatDuration(ms) {
  if (!ms || ms <= 0) return '—';
  const total = Math.round(ms / 1000);
  const m = Math.floor(total / 60);
  const s = total % 60;
  return m + ':' + String(s).padStart(2, '0');
}

function formatStart(meta) {
  if (!meta) return '—';
  if (meta.utcStart) return meta.utcStart;
  if (meta.timeOfDayStart) return meta.timeOfDayStart;
  return '—';
}

function isLogFile(name) {
  const lower = name.toLowerCase();
  return lower.endsWith('.csv') || lower.endsWith('.log');
}

// Inline trash icon (Feather "trash-2" geometry).  Inherits color from
// the enclosing button's text color via `currentColor`, so a `.greybutton`
// renders it black and a future variant doesn't need a separate asset.
const TrashIcon = () => html`
  <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14"
       viewBox="0 0 24 24" fill="none" stroke="currentColor"
       stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
       aria-hidden="true">
    <polyline points="3 6 5 6 21 6"></polyline>
    <path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"></path>
    <path d="M10 11v6"></path>
    <path d="M14 11v6"></path>
    <path d="M9 6V4a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v2"></path>
  </svg>`;

export function LogsPage() {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  // Per-file delete failures from /api/logs/delete-bulk's `errors` array.
  // Surfaced separately from the load-error banner so a single-file failure
  // (active log, SD busy) is visible even though the surviving files reload.
  const [deleteErrors, setDeleteErrors] = useState([]);
  const [selected, setSelected] = useState(new Set());
  const [busyDeleting, setBusyDeleting] = useState(false);

  const reload = async () => {
    setError(null);
    try {
      const d = await getJson('/api/logs');
      setData(d);
      setSelected(new Set());
    } catch (e) {
      setError((e instanceof ApiError) ? e.message : String(e));
    }
  };

  useEffect(() => { reload(); }, []);

  // Run a delete-bulk POST and refresh the listing.  Per-file failures
  // arrive in the response body's `errors` array (each
  // `{name, reason}`); store them so the page renders the banner.
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

  const allSelectableNames = data
    ? data.files
        .filter(f => f.name !== data.activeLog && isLogFile(f.name))
        .map(f => f.name)
    : [];
  const allChecked = allSelectableNames.length > 0 &&
                     allSelectableNames.every(n => selected.has(n));
  const someChecked = allSelectableNames.some(n => selected.has(n));

  const toggleAll = () => {
    setSelected(prev => {
      if (allChecked) return new Set();
      return new Set(allSelectableNames);
    });
  };

  const deleteSelected = async () => {
    if (selected.size === 0) return;
    if (!window.confirm(`Delete ${selected.size} file(s)?`)) return;
    await runDelete([...selected]);
  };

  const deleteOne = (name) => async () => {
    if (!window.confirm(`Delete ${name}?`)) return;
    await runDelete([name]);
  };

  // Compact summary like "Could not delete 2 file(s): activelog.csv
  // (active log), other.csv (SD busy)".  The reasons come straight
  // from the server.
  const deleteErrorSummary = () => {
    if (deleteErrors.length === 0) return '';
    const items = deleteErrors
      .map(e => `${e.name} (${e.reason || 'failed'})`)
      .join(', ');
    return `Could not delete ${deleteErrors.length} file(s): ${items}`;
  };

  // Split into logs + other files (config backups, boot_log.txt, etc.).
  const logs   = data ? data.files.filter(f => isLogFile(f.name)) : [];
  const others = data ? data.files.filter(f => !isLogFile(f.name)) : [];
  const logsTotal   = logs.reduce((a, f) => a + (f.size || 0), 0);
  const othersTotal = others.reduce((a, f) => a + (f.size || 0), 0);

  return html`
    <${PageShell} active="logs">
      <div style=${{ padding: '12px' }}>
        ${error && html`<p style=${{ color: 'red' }}>SD card busy or unreachable: ${error}</p>`}
        ${deleteErrors.length > 0 && html`
          <p style=${{ color: 'red' }}>${deleteErrorSummary()}</p>`}
        ${!data && !error && html`<p>Loading…</p>`}
        ${data && html`
          <h2>Logs</h2>
          <p>${logs.length} log${logs.length === 1 ? '' : 's'},
             ${formatBytes(logsTotal)} total</p>

          <table>
            <thead>
              <tr>
                <th><input type="checkbox" title="Select all"
                           checked=${allChecked}
                           ref=${(el) => { if (el) el.indeterminate = someChecked && !allChecked; }}
                           onChange=${toggleAll} /></th>
                <th style=${{ textAlign: 'left' }}>Name</th>
                <th style=${{ textAlign: 'left' }}>Start</th>
                <th style=${{ textAlign: 'left' }}>Duration</th>
                <th style=${{ textAlign: 'right' }}>Max IAS</th>
                <th style=${{ textAlign: 'right' }}>Max PAlt</th>
                <th style=${{ textAlign: 'right' }}>Size</th>
                <th></th>
              </tr>
            </thead>
            <tbody>
              ${logs.map(f => {
                const active = f.name === data.activeLog;
                const meta = f.meta;
                return html`
                  <tr>
                    <td>${active
                          ? ''
                          : html`<input type="checkbox" checked=${selected.has(f.name)}
                                        onChange=${toggle(f.name)} />`}
                    </td>
                    <td>
                      <a href=${'/download?file=' + encodeURIComponent(f.name)}>${f.name}</a>
                      ${active && html` <span style=${{ color: '#888' }}>(active)</span>`}
                    </td>
                    <td>${meta ? formatStart(meta) : '—'}</td>
                    <td>${meta ? formatDuration(meta.durationMs) : '—'}</td>
                    <td style=${{ textAlign: 'right' }}>${meta ? meta.maxIasKt.toFixed(0) + ' kt' : '—'}</td>
                    <td style=${{ textAlign: 'right' }}>${meta ? meta.maxPaltFt.toFixed(0) + ' ft' : '—'}</td>
                    <td style=${{ textAlign: 'right' }}>${formatBytes(f.size)}</td>
                    <td>${active
                          ? ''
                          : html`<button type="button" class="greybutton"
                                         style=${{ padding: '4px 10px', fontWeight: 'bold' }}
                                         disabled=${busyDeleting}
                                         onClick=${deleteOne(f.name)}><${TrashIcon} /></button>`}
                    </td>
                  </tr>`;
              })}
            </tbody>
          </table>
          <p>
            <button type="button" class="redbutton"
                    style=${{ padding: '8px 16px' }}
                    disabled=${selected.size === 0 || busyDeleting}
                    onClick=${deleteSelected}>
              Delete selected (${selected.size})
            </button>
            ${' '}
            <button type="button" class="greybutton"
                    style=${{ padding: '8px 16px' }}
                    onClick=${reload}>Refresh</button>
          </p>

          ${others.length > 0 && html`
            <h2>Other files</h2>
            <p>${others.length} file${others.length === 1 ? '' : 's'},
               ${formatBytes(othersTotal)} total</p>
            <table>
              <tbody>
                ${others.map(f => html`
                  <tr>
                    <td><a href=${'/download?file=' + encodeURIComponent(f.name)}>${f.name}</a></td>
                    <td style=${{ textAlign: 'right', paddingLeft: '20px' }}>
                      ${formatBytes(f.size)}
                    </td>
                    <td><button type="button" class="greybutton"
                                style=${{ padding: '4px 10px', fontWeight: 'bold' }}
                                disabled=${busyDeleting}
                                onClick=${deleteOne(f.name)}><${TrashIcon} /></button></td>
                  </tr>`)}
              </tbody>
            </table>`}`}
      </div>
    <//>`;
}
