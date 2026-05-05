// CalWizardPage (/calwiz): multi-step calibration wizard.
//
// Steps (also stored in URL hash so refresh / back-button stay on the
// same step):
//   intro     — collect aircraft params
//   decel     — flight instructions, "Continue" arms the recorder
//   flydecel  — live WS connection, recording during the decel run,
//                stall auto-detection (5 deg/sec pitch-rate + negative
//                pitch angle, mirroring the legacy trigger)
//   review    — polynomial fit + chart + setpoints + Save / Discard
//
// Setpoint math mirrors the legacy javascript_calibration.h (the same
// IAS-to-AOA fit, the same multipliers, the same NAOA-fraction
// computation) so the Save POST shape is byte-comparable to the
// legacy save.  The differential test
// (test/test_calwiz_save_diff/) pins firmware-side equality; the JS
// here pins client-side parity by reusing the same helper math.

import { html, useState, useEffect, useRef } from '../vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { getJson, postJson, ApiError } from '../shell/apiClient.js';

// Setpoint multipliers — the legacy `javascript_calibration.h`
// constants.  IAS multiple of Vs → NAOA fraction = 1 / multiplier^2
// (lift equation).
const OS_FAST_MULTIPLIER = 1.35;
const OS_SLOW_MULTIPLIER = 1.25;
const STALL_WARN_MARGIN_KT = 5;

// WebSocket frame age that means "stale" — same 3 s threshold the
// other pages use.
const STALE_MS = 3000;

// Stall-detection trigger: |pitchRate| > 5 deg/sec AND pitch < 0
// (the legacy onMessage loop's recovery-detection condition).
const STALL_PITCH_RATE_DEG_PER_SEC = 5;

// ---------------------------------------------------------------------
// URL hash <-> step mapping.
// ---------------------------------------------------------------------
const STEPS = ['intro', 'decel', 'flydecel', 'review'];

function readStepFromHash() {
  if (typeof location === 'undefined') return 'intro';
  const h = (location.hash || '').replace(/^#/, '');
  return STEPS.includes(h) ? h : 'intro';
}
function writeStepToHash(step) {
  if (typeof location === 'undefined') return;
  if (step === 'intro') {
    history.replaceState(null, '', location.pathname);
  } else {
    location.hash = step;
  }
}

// ---------------------------------------------------------------------
// Aircraft-params form (intro step).
// ---------------------------------------------------------------------
function AircraftParamsForm({ params, setParams, onContinue }) {
  const update = (k) => (e) => setParams({ ...params, [k]: e.target.value });
  const presetMatch = (g) =>
    Math.abs(parseFloat(params.gLimit) - g) < 0.005;
  const isCustom =
    !presetMatch(3.80) && !presetMatch(4.40) && !presetMatch(6.00);
  return html`
    <form onSubmit=${(e) => { e.preventDefault(); onContinue(); }}>
      <div class="form-divs flex-col-12">
        <label>Aircraft max gross weight (lbs)</label>
        <input class="inputField" type="number" step="1"
               value=${params.grossWeightLb}
               onInput=${update('grossWeightLb')} />
      </div>
      <div class="form-divs flex-col-12">
        <label>Aircraft current weight (lbs)</label>
        <input class="inputField" type="number" step="1"
               value=${params.currentWeightLb}
               onInput=${update('currentWeightLb')} />
      </div>
      <div class="form-divs flex-col-12">
        <label>Best glide airspeed at max gross weight (KIAS)</label>
        <input class="inputField" type="number" step="0.1"
               value=${params.bestGlideKt}
               onInput=${update('bestGlideKt')} />
      </div>
      <div class="form-divs flex-col-12">
        <label>Max flap extension speed - Vfe (KIAS)</label>
        <input class="inputField" type="number" step="0.1"
               value=${params.vfeKt}
               onInput=${update('vfeKt')} />
      </div>
      <div class="form-divs flex-col-12">
        <label>Aircraft category / load factor limit (G)</label>
        <div class="radio-group">
          <label><input type="radio" name="gpreset" checked=${presetMatch(3.80)}
                        onChange=${() => setParams({ ...params, gLimit: '3.80' })} />
                 Normal (+3.8G / -1.52G)</label>
          <label><input type="radio" name="gpreset" checked=${presetMatch(4.40)}
                        onChange=${() => setParams({ ...params, gLimit: '4.40' })} />
                 Utility (+4.4G / -1.76G)</label>
          <label><input type="radio" name="gpreset" checked=${presetMatch(6.00)}
                        onChange=${() => setParams({ ...params, gLimit: '6.00' })} />
                 Aerobatic (+6.0G / -3.0G)</label>
          <label><input type="radio" name="gpreset" checked=${isCustom}
                        onChange=${() => setParams({ ...params, gLimit: params.gLimit || '0' })} />
                 Custom</label>
        </div>
      </div>
      ${isCustom && html`
        <div class="form-divs flex-col-12">
          <label>Custom G limit</label>
          <input class="inputField" type="number" step="0.01"
                 value=${params.gLimit}
                 onInput=${update('gLimit')} />
        </div>`}
      <div class="form-divs flex-col-12" style=${{ marginTop: '12px' }}>
        Note: These parameters are used to compute best-glide and
        maneuvering speeds at the current weight.
      </div>
      <div class="form-divs flex-col-6"><a href="/">Cancel</a></div>
      <div class="form-divs flex-col-6">
        <button type="submit" class="button">Continue</button>
      </div>
    </form>`;
}

// ---------------------------------------------------------------------
// Decel-instructions step.  Read-only, single Continue button.
// ---------------------------------------------------------------------
function DecelInstructions({ onBack, onContinue }) {
  return html`
    <div>
      Get ready to fly a 1 kt/sec deceleration.<br /><br />
      <b>Instructions:</b><br /><br />
      1. Set your flaps now and do not change them until after you save the calibration.<br />
      2. Fly the entire run at a steady 1 kt/sec deceleration (OnSpeed provides feedback).<br />
      3. Keep the ball in the middle and wings level at all times.<br />
      4. Prioritize pitch smoothness over deceleration rate.<br />
      5. Do not pull up abruptly into the stall. Stall smoothly.<br />
      6. After the stall pitch down for recovery — that ends data recording and
         starts the analysis.<br /><br />
      Ready? Hit Continue and then fly max speed (below Vfe with flaps down) at the
      flap setting you want to calibrate.<br /><br />
      <button type="button" class="button" onClick=${onBack}>Back</button>
      <button type="button" class="button" onClick=${onContinue}>Continue</button>
    </div>`;
}

// ---------------------------------------------------------------------
// Live WebSocket subscription, accumulating samples while recording.
// ---------------------------------------------------------------------
function useLiveSamples({ recording }) {
  const [last, setLast] = useState(null);
  const [status, setStatus] = useState('CONNECTING...');
  const samplesRef = useRef([]);
  const recordingRef = useRef(recording);

  // Keep the ref in sync so the WS handler always sees the latest
  // recording flag without re-subscribing on every change.
  useEffect(() => { recordingRef.current = recording; }, [recording]);

  useEffect(() => {
    if (typeof window === 'undefined') return undefined;
    const meta = document.querySelector('meta[name="onspeed-ws"]');
    const uri = (meta && meta.content)
      ? meta.content
      : `ws://${location.hostname || '192.168.0.1'}:81`;

    let socket = null;
    let closed = false;
    let lastFrameMs = Date.now();

    const open = () => {
      if (closed) return;
      try { socket = new WebSocket(uri); }
      catch (e) { console.error('CalWizard WS open failed:', e); reconnect(); return; }
      socket.onopen = () => setStatus('CONNECTED');
      socket.onclose = () => { setStatus('Reconnecting...'); reconnect(); };
      socket.onerror = () => setStatus('WS error');
      socket.onmessage = (evt) => {
        if (typeof evt.data !== 'string') return;
        let frame;
        try { frame = JSON.parse(evt.data); }
        catch { return; }
        const sample = frameToSample(frame);
        setLast(sample);
        lastFrameMs = Date.now();
        if (recordingRef.current) samplesRef.current.push(sample);
      };
    };
    const reconnect = () => {
      if (closed) return;
      setTimeout(() => { if (!closed) open(); }, 1000);
    };
    open();

    // Stale watchdog — if no frame in STALE_MS, status reflects it.
    const tick = setInterval(() => {
      if (Date.now() - lastFrameMs > STALE_MS) setStatus('NO DATA');
    }, 1000);

    return () => {
      closed = true;
      clearInterval(tick);
      if (socket) try { socket.close(); } catch { /* ignore */ }
    };
  }, []);

  const reset = () => { samplesRef.current = []; };
  const samples = () => samplesRef.current;
  return { last, status, samples, reset };
}

function frameToSample(o) {
  // Map WS field names to the field set the wizard analysis needs.
  // Mirrors javascript_calibration.h's onMessage().  Numbers are
  // captured raw (no client-side smoothingAlpha — the legacy
  // smoothing was a UX nicety that distorted the saved samples; it
  // doesn't appear here).
  return {
    iasKt:        Number(o.IAS) || 0,
    derivedAoaDeg: Number(o.DerivedAOA) || 0,
    coeffP:       Number(o.coeffP) || 0,
    pitchDeg:     Number(o.Pitch) || 0,
    flightPathDeg: Number(o.flightPath) || 0,
    pitchRateDegPerSec: Number(o.PitchRate) || 0,
    decelRateKtPerSec: Number(o.DecelRate) || 0,
    flapsPosDeg:  Number(o.flapsPos) || 0,
    flapIndex:    Number(o.flapIndex) || 0,
  };
}

// ---------------------------------------------------------------------
// Decel-gauge SVG.  Mirrors the legacy /calwiz inline SVG, since the
// shared <DecelGauge> in components/svg/ is laid out for the M5
// 320×240 panel — the wizard's gauge has its own labels and aspect.
// ---------------------------------------------------------------------
function DecelGaugeWizard({ decelRate }) {
  // Legacy formula: needle Y = constrain(56 * smoothDecelRate + 38, -186, 150).
  // Here we drop the smoothing — the WS already emits a smoothed
  // DecelRate; double-smoothing in the JS distorted the readout.
  const dy = Math.max(-186, Math.min(150, 56 * (decelRate || 0) + 38));
  return html`
    <svg version="1.2" xmlns="http://www.w3.org/2000/svg"
         class="cal-graph" aria-label="Deceleration gauge"
         viewBox="0 0 210 350" style=${{ width: '210px', height: '350px' }}>
      <g class="labels y-labels" font-size="13" text-anchor="end">
        <text x="20" y="11">-4</text>
        <text x="20" y="66">-3</text>
        <text x="20" y="122">-2</text>
        <text x="20" y="178">-1</text>
        <text x="20" y="234">0</text>
        <text x="20" y="289">1</text>
        <text x="20" y="345">+2</text>
      </g>
      <g>
        <rect x="30" y="5"   width="160" height="140"
              fill="rgb(255,0,0)" fill-opacity="0.8" />
        <rect x="30" y="145" width="160" height="56"
              fill="rgb(0,255,0)" fill-opacity="0.8" />
        <rect x="30" y="201" width="160" height="140"
              fill="rgb(255,0,0)" fill-opacity="0.8" />
      </g>
      <g transform=${`translate(0, ${dy})`}>
        <rect x="40" y="189" width="145" height="5" fill="black" />
        <path transform="translate(33, 191.5)" d="M1,0 20,-5 20,5 Z"
              stroke="black" stroke-width="2" fill="black" />
      </g>
    </svg>`;
}

// ---------------------------------------------------------------------
// Recording step: gauge + readouts, Record / Stop button, automatic
// stall detection.
// ---------------------------------------------------------------------
function FlyDecelStep({ params, samples, setSamples, onAnalyzed }) {
  const [recording, setRecording] = useState(false);
  const live = useLiveSamples({ recording });

  // Auto-stop on stall recovery: |pitchRate| > 5 AND pitchDeg < 0.
  // This mirrors the legacy `recordData(false)` triggered from
  // onMessage().  Keep the recordingRef + setRecording flow simple —
  // useLiveSamples drives the ingestion; we only watch the latest
  // sample to decide when to stop.
  useEffect(() => {
    if (!recording || !live.last) return;
    if (Math.abs(live.last.pitchRateDegPerSec) > STALL_PITCH_RATE_DEG_PER_SEC
        && live.last.pitchDeg < 0) {
      const captured = live.samples().slice();
      setRecording(false);
      setSamples(captured);
      const result = analyzeDecel(captured, params);
      onAnalyzed(result);
    }
  }, [live.last, recording]);

  const startRecording = () => {
    live.reset();
    setRecording(true);
  };
  const stopRecording = () => {
    const captured = live.samples().slice();
    setRecording(false);
    setSamples(captured);
    const result = analyzeDecel(captured, params);
    onAnalyzed(result);
  };

  const last = live.last || {
    iasKt: 0, decelRateKtPerSec: 0, flapsPosDeg: 0,
  };

  return html`
    <div class="cal-flydecel">
      <div class="cal-flydecel-left">
        <b>Calibration Wizard</b><br /><br />
        ${recording
          ? html`<div>
              Recording until stall is detected...
              <div style=${{ marginTop: '20px', textAlign: 'center' }}>
                <button type="button" class="button" style=${{ width: '220px' }}
                        onClick=${stopRecording}>Stop</button>
              </div>
            </div>`
          : html`<div>
              Decelerate from Vmax (or Vfe). Hit Record when ready.
              <div style=${{ marginTop: '20px', textAlign: 'center' }}>
                <button type="button" class="wifibutton"
                        onClick=${startRecording}>Record</button>
              </div>
            </div>`}
      </div>

      <div class="cal-flydecel-gauge">
        <${DecelGaugeWizard} decelRate=${last.decelRateKtPerSec} />
      </div>

      <div class="cal-flydecel-readouts">
        <div>Flap Pos: ${last.flapsPosDeg ?? 0} deg</div>
        <div>IAS: ${(last.iasKt || 0).toFixed(2)} kts</div>
        <div>DecelRate: ${(last.decelRateKtPerSec || 0).toFixed(1)} kts/s</div>
        <div>Status: ${live.status}</div>
        <div>Captured: ${samples.length} samples</div>
      </div>
    </div>`;
}

// ---------------------------------------------------------------------
// Decel-run analysis — same math the legacy javascript_calibration.h
// ran inline.  Returns { ok, error?, setpoints, fit, samples, chart }.
//
// The setpoints are body angles (degrees), not wing AOA, per CLAUDE.md
// "OnSpeed measures body angle, not wing AOA".  alpha_0 (typically
// negative) is the floor in the lift-equation fit; the percent-lift
// math elsewhere uses (BodyAngle − alpha_0) / (alpha_stall − alpha_0).
// ---------------------------------------------------------------------
export function analyzeDecel(samples, params) {
  if (!samples || samples.length < 50) {
    return { ok: false, error: 'Not enough samples to analyze (need a full run).' };
  }

  // Smoothed IAS / CP — same EMAs the legacy used.
  const sIAS = new Array(samples.length);
  const sCP  = new Array(samples.length);
  sIAS[0] = samples[0].iasKt;
  sCP[0]  = samples[0].coeffP;
  let stallCP = 0, stallIas = 100, stallIdx = 0;
  for (let i = 1; i < samples.length; i++) {
    sIAS[i] = samples[i].iasKt   * 0.98 + sIAS[i - 1] * 0.02;
    sCP[i]  = samples[i].coeffP  * 0.90 + sCP[i  - 1] * 0.10;
    if (sCP[i] > stallCP) {
      stallCP  = sCP[i];
      stallIas = sIAS[i];
      stallIdx = i;
    }
  }
  if (stallCP === 0) {
    return { ok: false, error: 'Stall not detected. Try again — pitch down for stall recovery.' };
  }

  // Verify IAS is decreasing (slope of linear fit on IAS vs index).
  const iasFitData = [];
  for (let i = 0; i <= stallIdx; i++) iasFitData.push([i, samples[i].iasKt]);
  const iasFit = window.regression.polynomial(iasFitData, { order: 1, precision: 2 });
  if (iasFit.equation[0] >= 0) {
    return { ok: false, error: 'Airspeed is increasing during the run. Try again.' };
  }

  // CP-to-AOA fit (used at runtime by firmware).
  const cpData = [];
  for (let i = 0; i <= stallIdx; i++)
    cpData.push([sCP[i], samples[i].derivedAoaDeg]);
  const cpFit = window.regression.polynomial(cpData, { order: 2, precision: 4 });
  const [a2, a1, a0] = cpFit.equation;

  // IAS-to-AOA fit: DerivedAOA = K / IAS^2 + alpha_0.  Linear regression
  // of DerivedAOA vs 1/IAS^2; intercept = alpha_0, slope = K.
  const iasAoaData = [];
  for (let i = 0; i <= stallIdx; i++) {
    const v = samples[i].iasKt;
    if (v > 0) iasAoaData.push([1.0 / (v * v), samples[i].derivedAoaDeg]);
  }
  const iasAoaFit = window.regression.polynomial(iasAoaData, { order: 1, precision: 6 });
  const kFit       = iasAoaFit.equation[0];
  const alpha0     = iasAoaFit.equation[1];
  const alphaStall = kFit / (stallIas * stallIas) + alpha0;

  // Setpoint math.  acVfe overrides LDmaxIAS for flapped runs.
  const flapIndex = samples[stallIdx].flapIndex || 0;
  const acVfe         = parseFloat(params.vfeKt) || 0;
  const acGlimit      = parseFloat(params.gLimit) || 1;
  const acCurrentWt   = parseFloat(params.currentWeightLb) || 0;
  const acGrossWt     = parseFloat(params.grossWeightLb) || 1;
  const acVldmax      = parseFloat(params.bestGlideKt) || 0;
  let ldmaxIAS = (flapIndex === 0)
    ? Math.sqrt(acCurrentWt / acGrossWt) * acVldmax
    : (acVfe > 0 ? acVfe : acVldmax);

  const ldMaxAoa = kFit / (ldmaxIAS * ldmaxIAS) + alpha0;

  const alphaRange = alphaStall - alpha0;
  const naoaFast = 1.0 / (OS_FAST_MULTIPLIER * OS_FAST_MULTIPLIER);
  const naoaSlow = 1.0 / (OS_SLOW_MULTIPLIER * OS_SLOW_MULTIPLIER);
  const onSpeedFastAoa = naoaFast * alphaRange + alpha0;
  const onSpeedSlowAoa = naoaSlow * alphaRange + alpha0;

  const stallWarnIAS = stallIas + STALL_WARN_MARGIN_KT;
  const stallWarnAoa = kFit / (stallWarnIAS * stallWarnIAS) + alpha0;

  const stallAoa = alphaStall;

  const maneuveringIAS = stallIas * Math.sqrt(acGlimit);
  const maneuveringAoa = kFit / (maneuveringIAS * maneuveringIAS) + alpha0;

  return {
    ok: true,
    flapsPos: samples[stallIdx].flapsPosDeg,
    flapIndex,
    stallIas,
    setpoints: {
      ldMaxAoaDeg:       Number(ldMaxAoa.toFixed(2)),
      onSpeedFastAoaDeg: Number(onSpeedFastAoa.toFixed(2)),
      onSpeedSlowAoaDeg: Number(onSpeedSlowAoa.toFixed(2)),
      stallWarnAoaDeg:   Number(stallWarnAoa.toFixed(2)),
      stallAoaDeg:       Number(stallAoa.toFixed(2)),
      maneuveringAoaDeg: Number(maneuveringAoa.toFixed(2)),
    },
    fit: {
      kFit,
      alpha0Deg:     alpha0,
      alphaStallDeg: alphaStall,
      curve0: a2,    // afCoeff[1] target
      curve1: a1,    // afCoeff[2] target
      curve2: a0,    // afCoeff[3] target
      cpToAoaR2:    cpFit.r2,
      iasToAoaR2:   iasAoaFit.r2,
    },
    smoothed: { sIAS, sCP, stallIdx },
  };
}

// ---------------------------------------------------------------------
// Review step — chart, results, save / discard.
// ---------------------------------------------------------------------
function ReviewStep({ result, samples, onSave, onDiscard, saveStatus }) {
  const chartRef = useRef(null);

  // Chartist chart of measured + predicted AOA vs CP.  Effect re-runs
  // when the analysis result changes (new run loaded into review).
  useEffect(() => {
    if (!chartRef.current || !result?.ok) return;
    const { sCP, stallIdx } = result.smoothed;
    const measured = [], predicted = [];
    const [a2, a1, a0] = [result.fit.curve0, result.fit.curve1, result.fit.curve2];
    for (let i = stallIdx; i > 0; i--) {
      const cp = sCP[i];
      measured.push({ x: cp, y: samples[i].derivedAoaDeg });
      predicted.push({ x: cp, y: a2 * cp * cp + a1 * cp + a0 });
    }
    const data = {
      series: [
        { name: 'measured',  data: measured  },
        { name: 'predicted', data: predicted },
      ],
    };
    const opts = {
      showLine: false,
      axisX: { type: window.Chartist.AutoScaleAxis },
      series: {
        measured:  { showPoint: true,  showLine: false },
        predicted: { showPoint: false, showLine: true,
                     lineSmooth: window.Chartist.Interpolation.simple() },
      },
    };
    new window.Chartist.Line(chartRef.current, data, opts);
  }, [result]);

  if (!result || !result.ok) {
    return html`<div>
      <p>Analysis failed: ${result?.error || 'unknown error'}</p>
      <button type="button" class="button" onClick=${onDiscard}>Try again</button>
    </div>`;
  }

  const sp = result.setpoints;
  const f  = result.fit;
  return html`
    <div class="cal-review">
      <b>Calibration Results:</b><br /><br />
      Flap Position: ${result.flapsPos} deg<br />
      Stall Speed: ${result.stallIas.toFixed(2)} kts<br />
      <br />
      <b>Setpoints (body angle, deg):</b><br />
      L/Dmax: ${sp.ldMaxAoaDeg}<br />
      Onspeed Fast: ${sp.onSpeedFastAoaDeg}<br />
      Onspeed Slow: ${sp.onSpeedSlowAoaDeg}<br />
      Stall Warning: ${sp.stallWarnAoaDeg}<br />
      Stall: ${sp.stallAoaDeg}<br />
      Maneuvering: ${sp.maneuveringAoaDeg}<br />
      <br />
      <b>Fit:</b><br />
      Alpha-0 (zero-lift body angle): ${f.alpha0Deg.toFixed(2)} deg<br />
      Alpha-Stall: ${f.alphaStallDeg.toFixed(2)} deg<br />
      K-fit: ${f.kFit.toFixed(2)}<br />
      IAS-to-AOA R²: ${f.iasToAoaR2.toFixed(4)}<br />
      CP-to-AOA R²: ${f.cpToAoaR2.toFixed(4)}<br />
      <br />
      <div class="ct-chart" ref=${chartRef}
           style=${{ height: '300px', display: 'block' }} />
      <br />
      <button type="button" class="button"
              style=${{ backgroundColor: '#42a7f5' }}
              onClick=${onSave}
              disabled=${saveStatus === 'saving'}>
        ${saveStatus === 'saving' ? 'Saving...' : 'Save Calibration'}
      </button>
      <button type="button" class="button" onClick=${onDiscard}>
        Discard / Re-run
      </button>
      ${saveStatus && saveStatus !== 'saving' && html`
        <p class=${saveStatus.startsWith('OK') ? 'cal-ok' : 'cal-warn'}>
          ${saveStatus}
        </p>`}
    </div>`;
}

// ---------------------------------------------------------------------
// Top-level CalWizardPage.
// ---------------------------------------------------------------------
export function CalWizardPage() {
  const [step, setStep] = useState(readStepFromHash());
  const [params, setParams] = useState({
    grossWeightLb:   '',
    currentWeightLb: '',
    bestGlideKt:     '',
    vfeKt:           '',
    gLimit:          '4.40',
  });
  const [stateLoaded, setStateLoaded] = useState(false);
  const [stateError,  setStateError]  = useState(null);
  const [samples,     setSamples]     = useState([]);
  const [result,      setResult]      = useState(null);
  const [saveStatus,  setSaveStatus]  = useState(null);

  // Hash sync — back/forward and refresh both keep the user on the
  // same step (refreshing intro is fine; refreshing flydecel mid-run
  // resets samples — that's correct, because the in-flight samples
  // weren't persisted).
  useEffect(() => {
    const onHash = () => setStep(readStepFromHash());
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, []);

  const goto = (s) => { setStep(s); writeStepToHash(s); };

  // Load starting state from /api/calwiz/state on first mount.
  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const data = await getJson('/api/calwiz/state');
        if (cancelled) return;
        setParams(p => ({
          ...p,
          grossWeightLb:   String(data.aircraft.grossWeightLb || ''),
          bestGlideKt:     String(data.aircraft.bestGlideKt || ''),
          vfeKt:           String(data.aircraft.vfeKt || ''),
          gLimit:          String(data.aircraft.gLimit || '4.40'),
        }));
        setStateLoaded(true);
      } catch (e) {
        if (cancelled) return;
        setStateError(e instanceof ApiError ? e.message : String(e));
      }
    })();
    return () => { cancelled = true; };
  }, []);

  // Save handler — POST to /api/calwiz/save.
  const onSave = async () => {
    if (!result || !result.ok) return;
    setSaveStatus('saving');
    const body = {
      flapsPos:           result.flapsPos,
      LDmaxSetpoint:      result.setpoints.ldMaxAoaDeg,
      OSFastSetpoint:     result.setpoints.onSpeedFastAoaDeg,
      OSSlowSetpoint:     result.setpoints.onSpeedSlowAoaDeg,
      StallWarnSetpoint:  result.setpoints.stallWarnAoaDeg,
      StallSetpoint:      result.setpoints.stallAoaDeg,
      ManeuveringSetpoint: result.setpoints.maneuveringAoaDeg,
      alpha0:             result.fit.alpha0Deg,
      alphaStall:         result.fit.alphaStallDeg,
      K_fit:              result.fit.kFit,
      curve0:             result.fit.curve0,
      curve1:             result.fit.curve1,
      curve2:             result.fit.curve2,
    };
    try {
      const resp = await postJson('/api/calwiz/save', body);
      if (resp.warnings && resp.warnings.length > 0) {
        setSaveStatus('Saved, but: ' + resp.warnings.map(w => w.message).join('; '));
      } else {
        setSaveStatus('OK — calibration saved.');
      }
    } catch (e) {
      const msg = (e instanceof ApiError && e.errors && e.errors.length > 0)
        ? e.errors.map(x => x.message).join('; ')
        : String(e.message || e);
      setSaveStatus('Save failed: ' + msg);
    }
  };

  return html`
    <${PageShell} active="calwiz">
      <div class="cal-wizard" style=${{ maxWidth: '720px', margin: '0 auto' }}>
        <h2>Calibration Wizard</h2>
        ${stateError && html`
          <p class="cal-warn">Could not load saved aircraft params: ${stateError}</p>`}
        ${!stateLoaded && !stateError && html`<p>Loading…</p>`}
        ${step === 'intro' && html`
          <${AircraftParamsForm} params=${params} setParams=${setParams}
                                 onContinue=${() => goto('decel')} />`}
        ${step === 'decel' && html`
          <${DecelInstructions} onBack=${() => goto('intro')}
                                onContinue=${() => goto('flydecel')} />`}
        ${step === 'flydecel' && html`
          <${FlyDecelStep} params=${params}
                           samples=${samples} setSamples=${setSamples}
                           onAnalyzed=${(r) => { setResult(r); goto('review'); }} />`}
        ${step === 'review' && html`
          <${ReviewStep} result=${result} samples=${samples}
                         saveStatus=${saveStatus}
                         onSave=${onSave}
                         onDiscard=${() => { setResult(null); setSaveStatus(null); goto('flydecel'); }} />`}
      </div>
    <//>`;
}
