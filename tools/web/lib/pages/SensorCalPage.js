// SensorCalPage (/sensorconfig): Preact replacement for the legacy
// server-rendered sensor calibration page.
//
// The cal procedure is unchanged on the firmware side.  The pilot
// enters "true" pitch / roll / pressure altitude; the firmware samples
// the IMU and pressure sensors, computes new biases, and writes them
// back to config.  The form-submit shape (GET /sensorconfig?confirm=
// yes&trueAircraftPitch=...&trueAircraftRoll=...&trueAircraftPalt=...)
// is the legacy contract verbatim, so HandleSensorConfig's sample loop
// is untouched.
//
// What this page adds over the server-rendered version:
//
// 1. Smoothed live readouts.  Pitch / roll each accumulate a 2-second
//    window of polled samples before the field auto-populates with the
//    mean.  A "Refresh" button per field clears the window and re-
//    fills it.  The previous page only sampled once at GET-time;
//    pilots got an instantaneous reading instead of a settled one.
//
// 2. EFIS-aware PAlt seeding.  PAlt is the calibration target — the
//    page never seeds it from the OnSpeed static sensor (which would
//    produce zero bias).  Seed only from EFIS types that actually
//    carry baro on the wire (Dynon, Garmin, MGL).  VN-300 has no
//    static-pressure field, so VN-300 setups see a blank field with a
//    placeholder prompting local-field-elevation entry.
//
// 3. Live "Current sensor calibration" panel populated from
//    /api/sensors/biases.  Same eight-row table the legacy page
//    showed; identical formatting.
//
// Smoothing source: poll /api/sensors/biases at 10 Hz rather than
// subscribing to the WebSocket.  The WS frame's `pitchDeg` is the AHRS-
// smoothed pitch (Madgwick / EKF6 output), but the cal handler computes
// biases from `g_AHRS.PitchWithBias()` (a direct accel-pitch with bias
// applied).  /api/sensors/biases exposes `live.truePitchDeg` /
// `live.trueRollDeg`, which are exactly that quantity, so smoothing
// those gives the pilot the same suggestion the legacy form's GET-time
// snapshot did, just settled instead of instantaneous.  The endpoint
// only reads g_Config + g_AHRS outputs (no fresh sensor sample), so
// 10 Hz polling is cheap.
//
// The smoothing logic lives in `lib/core/smoothedField.js` so future
// pages (e.g. an offline log analyzer) can use the same hook.

import { html, useState, useEffect } from '../../../../packages/ui-core/vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { getJson, ApiError } from '../shell/apiClient.js';
import { useSmoothedField } from '../core/smoothedField.js';

// 2-second smoothing window at the 10 Hz polling rate (20 samples).
const SMOOTH_WINDOW_SAMPLES = 20;
const POLL_INTERVAL_MS      = 100;

// Format helpers — match the legacy page's String(float) output (2
// decimals for all floats — Arduino's String(float) defaults to 2
// decimals — and integer counts).  Keeps the dev-page rendering byte-
// comparable to firmware output where possible.
function fmt2(v) { return Number(v).toFixed(2); }
function fmt0(v) { return Math.round(Number(v)).toString(); }

// ---------------------------------------------------------------------
// Current calibration panel — read-only table populated from
// /api/sensors/biases.  Mirrors the legacy "Current sensor calibration"
// table verbatim so a pilot familiar with the old page recognizes it.
// ---------------------------------------------------------------------
function BiasesPanel({ biases }) {
  if (!biases) {
    return html`
      <table>
        <tbody>
          <tr><td colspan="3">Loading current calibration...</td></tr>
        </tbody>
      </table>`;
  }
  const b = biases.biases;
  const live = biases.live;
  return html`
    <table>
      <tbody>
        <tr><td style=${{ paddingRight: '20px' }}>Pfwd Bias:</td>
            <td>${fmt0(b.pFwdCounts)}</td><td>Counts</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>P45 Bias:</td>
            <td>${fmt0(b.p45Counts)}</td><td>Counts</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>Static Bias:</td>
            <td>${fmt2(b.pStaticMb)}</td><td>millibars</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>gx Bias:</td>
            <td>${fmt2(b.gxDegPerSec)}</td><td></td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>gy Bias:</td>
            <td>${fmt2(b.gyDegPerSec)}</td><td></td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>gz Bias:</td>
            <td>${fmt2(b.gzDegPerSec)}</td><td></td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>IMU Pitch:</td>
            <td>${fmt2(live.imuPitchDeg)}</td><td>Degrees</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>Pitch Bias:</td>
            <td>${fmt2(b.pitchDeg)}</td><td>Degrees</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>Calculated True AC Pitch:</td>
            <td>${fmt2(live.truePitchDeg)}</td><td>Degrees</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>IMU Roll:</td>
            <td>${fmt2(live.imuRollDeg)}</td><td>Degrees</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>Roll Bias:</td>
            <td>${fmt2(b.rollDeg)}</td><td>Degrees</td></tr>
        <tr><td style=${{ paddingRight: '20px' }}>Calculated True AC Roll:</td>
            <td>${fmt2(live.trueRollDeg)}</td><td>Degrees</td></tr>
      </tbody>
    </table>`;
}

// ---------------------------------------------------------------------
// SmoothedFieldRow — one labeled smoothed value with a Refresh button.
// `pendingText` is shown while the buffer fills; `value` is the mean
// after that.  The pilot can override the value by typing in the input
// at any time; typing clears the smoothed source ("override" state)
// until the user hits Refresh, which re-arms the smoothing.
// ---------------------------------------------------------------------
function SmoothedFieldRow({ label, smoothed, formatter, override, setOverride }) {
  const onRefresh = () => {
    setOverride(null);    // re-arm the smoothing — drop any manual edit
    smoothed.refresh();
  };
  const onInput = (e) => setOverride(e.target.value);

  // What's in the input?  If the pilot has typed an override, that wins.
  // Otherwise show the smoothed mean (or empty while pending).
  const value = override != null
    ? override
    : (smoothed.value != null ? formatter(smoothed.value) : '');

  // Status caption next to the input.  Inline style — no new CSS
  // classes needed; the firmware-bundled CSS already covers the form
  // chrome.
  const statusBase = { fontSize: '0.85em', marginLeft: '8px' };
  let status;
  if (override != null) {
    status = html`<span style=${{ ...statusBase, color: '#666' }}>manual override</span>`;
  } else if (smoothed.pending) {
    status = html`<span style=${{ ...statusBase, color: '#888' }}>filling buffer (${SMOOTH_WINDOW_SAMPLES} samples)…</span>`;
  } else {
    status = html`<span style=${{ ...statusBase, color: '#080' }}>smoothed (${SMOOTH_WINDOW_SAMPLES}-sample mean)</span>`;
  }

  return html`
    <tr>
      <td><label>${label}</label></td>
      <td>
        <input class="inputField" type="text" value=${value}
               onInput=${onInput} />
      </td>
      <td>
        <button type="button" class="button"
                onClick=${onRefresh}>Refresh</button>
      </td>
      <td>${status}</td>
    </tr>`;
}

// ---------------------------------------------------------------------
// PaltFieldRow — pilot-entered, no smoothing.  PAlt is the calibration
// target so it never auto-populates from the OnSpeed static sensor.
// EFIS types that carry baro on the wire (Dynon, Garmin, MGL) seed the
// initial value; VN-300 and "no EFIS" leave the field blank with a
// placeholder.
// ---------------------------------------------------------------------
function PaltFieldRow({ value, setValue, source }) {
  const statusBase = { fontSize: '0.85em', marginLeft: '8px' };
  let status;
  switch (source) {
    case 'baro':
      status = html`<span style=${{ ...statusBase, color: '#080' }}>pre-populated from EFIS baro</span>`;
      break;
    case 'vn300':
      status = html`<span style=${{ ...statusBase, color: '#666' }}>VN-300 carries no baro — enter local field PAlt</span>`;
      break;
    default:
      status = html`<span style=${{ ...statusBase, color: '#666' }}>enter local field PAlt (altimeter set 29.92)</span>`;
  }
  return html`
    <tr>
      <td><label>True Aircraft Pressure Altitude (feet)</label></td>
      <td>
        <input class="inputField" type="text"
               name="trueAircraftPalt"
               required
               value=${value ?? ''}
               placeholder="e.g. 1234"
               onInput=${(e) => setValue(e.target.value)} />
      </td>
      <td></td>
      <td>${status}</td>
    </tr>`;
}

// ---------------------------------------------------------------------
// EfisNote — green note above the form, mirroring the legacy "EFIS
// data detected" banner.  Three cases:
//   - baro:  pitch/roll/PAlt all pre-populated from EFIS
//   - vn300: pitch/roll from EFIS; PAlt entered manually
//   - none:  no banner (pitch/roll come from the smoothing window
//            against the OnSpeed AHRS values; same as today's fallback
//            when EFIS is stale)
// ---------------------------------------------------------------------
function EfisNote({ source }) {
  if (source === 'baro') {
    return html`
      <p style=${{ color: 'green' }}>
        <b>EFIS data detected:</b> pitch, roll, and altitude pre-populated from EFIS.
        You can override if needed.
      </p>`;
  }
  if (source === 'vn300') {
    return html`
      <p style=${{ color: 'green' }}>
        <b>EFIS data detected:</b> pitch and roll pre-populated from EFIS.
        Enter local field PAlt manually (set altimeter to 29.92 inHg and read).
      </p>`;
  }
  return null;
}

// ---------------------------------------------------------------------
// SensorCalPage — top-level component.
// ---------------------------------------------------------------------
export function SensorCalPage() {
  // Live calibration + AHRS snapshot from /api/sensors/biases.  Polled
  // at 10 Hz: the smoothed pitch/roll fields below feed off
  // `live.truePitchDeg` / `live.trueRollDeg`, which are
  // `g_AHRS.PitchWithBias()` / `RollWithBias()` — the same accel-pitch
  // quantity the cal handler uses to compute the bias delta.  The
  // bias-table values rarely change but ride along for free.
  const [biases, setBiases]         = useState(null);
  const [biasError, setBiasError]   = useState(null);

  // Pilot overrides — null means "use the smoothed value" or, for
  // PAlt, "use the EFIS-supplied value".  String when the pilot has
  // typed in the field.
  const [pitchOverride, setPitchOverride] = useState(null);
  const [rollOverride,  setRollOverride]  = useState(null);
  const [paltValue,     setPaltValue]     = useState(null);  // string or null

  // Smoothed accessors.  `useSmoothedField` ingests one sample per new
  // `biases` reference (each fetch returns a fresh object), so the
  // 10 Hz poll drives the window-fill at the documented rate.  The
  // accessor returns null for missing/non-finite values so the buffer
  // doesn't ingest junk responses.
  const pitchSmooth = useSmoothedField(
    biases,
    b => Number.isFinite(b?.live?.truePitchDeg) ? b.live.truePitchDeg : null,
    SMOOTH_WINDOW_SAMPLES);
  const rollSmooth  = useSmoothedField(
    biases,
    b => Number.isFinite(b?.live?.trueRollDeg)  ? b.live.trueRollDeg  : null,
    SMOOTH_WINDOW_SAMPLES);

  // Poll /api/sensors/biases at POLL_INTERVAL_MS.  Seed the PAlt field
  // on the first response if (and only if) the EFIS supplies baro;
  // VN-300 / no-EFIS leave it blank with a placeholder.
  useEffect(() => {
    let cancelled = false;
    let seeded    = false;

    const tick = async () => {
      try {
        const data = await getJson('/api/sensors/biases');
        if (cancelled) return;
        setBiases(data);
        setBiasError(null);
        if (!seeded && data.efis && data.efis.source === 'baro') {
          setPaltValue(fmt0(data.efis.paltFt));
          seeded = true;
        } else if (!seeded) {
          // Mark seeded after the first successful fetch even when
          // there's no baro source — we don't want to keep clobbering
          // a manually-typed PAlt on every subsequent poll.
          seeded = true;
        }
      } catch (e) {
        if (cancelled) return;
        setBiasError(e instanceof ApiError ? e.message : String(e));
      }
    };

    tick();
    const id = setInterval(tick, POLL_INTERVAL_MS);
    return () => { cancelled = true; clearInterval(id); };
  }, []);

  // Form-submit values.  Resolve in priority order:
  //   override (pilot-typed) → smoothed mean → empty.
  // The JS gate (`canSubmit` below) blocks submission when any field
  // would resolve to empty; the C++ second-pass guard in
  // HandleSensorConfig is a defense-in-depth layer that returns 400
  // on any empty arg (catches hand-crafted URLs, JS-disabled browsers,
  // future regressions in the gate).  Combined: there is no
  // silent-zero-calibration path through this page.
  const resolvePitch = () => pitchOverride ?? (pitchSmooth.value != null ? fmt2(pitchSmooth.value) : '');
  const resolveRoll  = () => rollOverride  ?? (rollSmooth.value  != null ? fmt2(rollSmooth.value)  : '');
  const efisSource = biases?.efis?.source ?? 'none';

  // A field is "ready" when:
  //   - The pilot has typed a non-empty override, OR
  //   - The smoothing buffer has filled AND the pilot hasn't cleared
  //     the override.
  // An empty-string override means the pilot deleted what was there;
  // resolvePitch() returns it verbatim (because nullish coalescing
  // only handles null/undefined), so the hidden input would submit
  // empty and the C++ all-empty fallback would silently calibrate
  // against pitch=0 / roll=0.  PAlt is pilot-typed (or pre-seeded
  // from EFIS baro) and must be non-empty.  See PR #451 review.
  const pitchReady = (pitchOverride === null && !pitchSmooth.pending)
                  || (pitchOverride !== null && pitchOverride !== '');
  const rollReady  = (rollOverride === null && !rollSmooth.pending)
                  || (rollOverride !== null && rollOverride !== '');
  const paltReady  = paltValue != null && paltValue !== '';
  const canSubmit  = pitchReady && rollReady && paltReady;

  return html`
    <${PageShell} active="sensorconfig">
      <div style=${{ maxWidth: '720px', margin: '0 auto', padding: '12px' }}>
        <br /><b>Current sensor calibration:</b><br /><br />
        ${biasError && html`<p style=${{ color: '#c00' }}>Could not load calibration snapshot: ${biasError}</p>`}
        <${BiasesPanel} biases=${biases} />
        <br />
        <p>
          This procedure will calibrate the system's accelerometers, gyros and pressure sensors.<br /><br />
          <b>Requirements:</b><br /><br />
          1. Do this configuration in no wind condition. Preferably inside a closed hangar, no moving air.<br />
          2. Box orientation is set up properly in <a href="/aoaconfig">System Configuration</a>.<br />
          3. If the aircraft is not in a level attitude, accept the smoothed pitch / roll readings or override them.<br />
          4. Enter current pressure altitude in feet. (Set your altimeter to 29.92 inHg and read the altitude.)
          <br /><br />
          Calibration will take a few seconds.
        </p>
        <${EfisNote} source=${efisSource} />
        <br />
        <form action="/sensorconfig" method="GET">
          <table>
            <tbody>
              <${SmoothedFieldRow}
                label="True Aircraft Pitch (degrees)"
                smoothed=${pitchSmooth}
                formatter=${fmt2}
                override=${pitchOverride}
                setOverride=${setPitchOverride} />
              <${SmoothedFieldRow}
                label="True Aircraft Roll (degrees)"
                smoothed=${rollSmooth}
                formatter=${fmt2}
                override=${rollOverride}
                setOverride=${setRollOverride} />
              <${PaltFieldRow}
                value=${paltValue}
                setValue=${setPaltValue}
                source=${efisSource} />
            </tbody>
          </table>

          <input type="hidden" name="trueAircraftPitch" value=${resolvePitch()} />
          <input type="hidden" name="trueAircraftRoll"  value=${resolveRoll()} />
          <input type="hidden" name="confirm" value="yes" />

          <br /><br /><br />
          <button type="submit" class="button" disabled=${!canSubmit}>Calibrate Sensors</button>
          <a href="/">Cancel</a>
        </form>
      </div>
    <//>`;
}
