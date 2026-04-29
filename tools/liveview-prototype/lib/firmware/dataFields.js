// Collapsible data-fields table.
//
// Mirrors the legacy /live's <div id="datafields_aoa"> table. Same
// 13 rows the existing page shows; the firmware port preserves them
// for diagnostic continuity. The whole section is collapsed by
// default; state persists in localStorage so an opened panel stays
// open across page loads.

const STORAGE_KEY = 'liveview-datafields-expanded';

// (label, record-key, formatter) — formatter receives a raw value
// from the record and returns a display string.
const FIELDS = [
  ['AOA',      'aoaDeg',        v => fmt(v, 2) + '°'],
  ['Der AOA',  'derivedAoaDeg', v => fmt(v, 2) + '°'],
  ['FltPath',  'flightPathDeg', v => fmt(v, 1) + '°'],
  ['IAS',      'iasKt',         v => fmt(v, 0) + ' kt'],
  ['PAlt',     'paltFt',        v => fmt(v, 0) + ' ft'],
  ['iVSI',     'vsiFpm',        v => fmt(v, 0) + ' fpm'],
  ['Vert G',   'verticalG',     v => fmt(v, 2) + ' G'],
  ['Lat G',    'lateralG',      v => fmt(v, 2) + ' G'],
  ['Pitch',    'pitchDeg',      v => fmt(v, 1) + '°'],
  ['Roll',     'rollDeg',       v => fmt(v, 1) + '°'],
  ['DataMark', 'dataMark',      v => fmt(v, 0)],
  ['Flaps',    'flapsDeg',      v => fmt(v, 0) + '°'],
];

// toFixed() preserves the sign on values that round to zero, so a
// pitch of -0.04 renders as "-0.0". Collapse any such result to its
// unsigned form before display. Lifted from the legacy /live.
function fmt(value, digits) {
  if (value === undefined || value === null || isNaN(value)) return '—';
  let s = Number(value).toFixed(digits);
  if (parseFloat(s) === 0) s = Math.abs(value).toFixed(digits);
  return s;
}

export function mountDataFields(toggleBtn, container) {
  // Build static row scaffold once.
  const table = document.createElement('table');
  const cells = {};  // key → <td> for the value column
  for (const [label, key] of FIELDS) {
    const tr = document.createElement('tr');
    const labelTd = document.createElement('td');
    labelTd.textContent = label;
    const valTd = document.createElement('td');
    valTd.textContent = '—';
    cells[key] = valTd;
    tr.appendChild(labelTd);
    tr.appendChild(valTd);
    table.appendChild(tr);
  }
  // Age is a special row driven by tickAge below.
  const ageTr = document.createElement('tr');
  const ageLabelTd = document.createElement('td');
  ageLabelTd.textContent = 'Age';
  const ageValTd = document.createElement('td');
  ageValTd.textContent = '—';
  cells.age = ageValTd;
  ageTr.appendChild(ageLabelTd);
  ageTr.appendChild(ageValTd);
  table.appendChild(ageTr);

  container.appendChild(table);

  // Restore expanded state.
  let expanded = localStorage.getItem(STORAGE_KEY) === '1';
  applyState();

  toggleBtn.addEventListener('click', () => {
    expanded = !expanded;
    localStorage.setItem(STORAGE_KEY, expanded ? '1' : '0');
    applyState();
  });

  function applyState() {
    container.style.display = expanded ? '' : 'none';
    toggleBtn.textContent = expanded ? 'Hide data fields' : 'Show data fields';
  }

  // 500 ms gate matches the legacy /live's text-refresh cadence so
  // the values aren't ticking faster than the human eye can read.
  let lastTextUpdateMs = 0;
  let lastRec = null;

  function update(rec) {
    lastRec = rec;
    const now = Date.now();
    if (now - lastTextUpdateMs < 500) return;
    if (!expanded) return;  // skip work when hidden

    for (const [_, key, fmtFn] of FIELDS) {
      const v = rec[key];
      cells[key].textContent = (key === 'aoaDeg' && !rec.aoaIsValid)
        ? 'N/A'
        : fmtFn(v);
    }
    lastTextUpdateMs = now;
  }

  function setAge(ageSec) {
    if (!expanded) return;
    cells.age.textContent = ageSec.toFixed(1) + ' s';
    cells.age.style.color = (ageSec >= 1) ? 'var(--red, #ff0018)' : '';
  }

  return { update, setAge };
}
