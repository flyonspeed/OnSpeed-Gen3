// tune_presentation_tau.mjs — offline τ tuner for the replay tool's
// presentation filter.
//
// Question: what τ for the lateral / vertical presentation filter
// makes the OnSpeed slip ball look like what the pilot saw in flight?
//
// Approach: the airplane's primary EFIS (e.g. Dynon SkyView) has its
// own ADAHRS module that internally fuses high-rate IMU and reports
// already-filtered lateral/vertical G via the EFIS serial protocol.
// OnSpeed logs that as `efisLateralG` / `efisVerticalG`. That value
// is what the pilot's PFD slip indicator displays. Treat it as a
// reference signal: find the τ that minimizes RMS error between
// (engine.accelLatSmoothed → presentation EMA at τ) and efisLateralG.
//
// Caveats:
//   - The Dynon's filter is its own black box; we don't know its τ
//     exactly. But the pilot looks at it and accepts it as smooth, so
//     "look like the Dynon" ≈ "look like what the pilot saw".
//   - efisLateralG updates at ~10 Hz; the log captures the freshest
//     value at each 50 Hz row. Adjacent rows often repeat.
//   - We mask out ground portions (iasValid=false) where the slip
//     ball is moot anyway.
//
// Usage:
//   node tools/replay-tuning/tune_presentation_tau.mjs \
//        --log <path-to-csv> \
//        [--cfg <path-to-cfg-xml>]
//
// If --cfg is omitted, uses a minimal default cfg (only flap0
// alpha0/alphaStall matter for the engine's accel-EMA path; aoa
// curve doesn't affect lateralG smoothing).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const REPO_ROOT  = path.resolve(__dirname, '..', '..');

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

const argv = process.argv.slice(2);
function flag(name, def = null) {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : def;
}
const LOG_PATH  = flag('--log');
const CFG_PATH  = flag('--cfg');
const SAMPLE_HZ = parseFloat(flag('--rate', '50'));

if (!LOG_PATH) {
  console.error('usage: node tune_presentation_tau.mjs --log <csv> [--cfg <xml>] [--rate 50]');
  process.exit(2);
}
if (!fs.existsSync(LOG_PATH)) {
  console.error(`FAIL: log not found: ${LOG_PATH}`);
  process.exit(1);
}

// ---------------------------------------------------------------------------
// Load WASM core
// ---------------------------------------------------------------------------

const wasmJs = path.resolve(
  REPO_ROOT, 'software/Libraries/onspeed_core/wasm/dist/onspeed_core.js');
if (!fs.existsSync(wasmJs)) {
  console.error(`FAIL: WASM not built. Run: bash software/Libraries/onspeed_core/wasm/build_wasm.sh`);
  process.exit(1);
}
const factory = (await import(pathToFileURL(wasmJs).href)).default;
const Core = await factory();

// ---------------------------------------------------------------------------
// Parse the CSV log
// ---------------------------------------------------------------------------

console.error(`Reading ${LOG_PATH} ...`);
const text = fs.readFileSync(LOG_PATH, 'utf8');
const lines = text.split('\n');
const header = lines[0].split(',').map(s => s.trim());
const idx = Object.fromEntries(header.map((name, i) => [name, i]));

// Required columns for the engine input. iasValid is optional —
// older logs encode it as "IAS column empty when invalid" rather
// than a separate column. The task's UpdateIasAlive re-derives it.
const REQUIRED = [
  'PfwdSmoothed', 'P45Smoothed', 'PStatic', 'Palt', 'IAS',
  'flapsPos', 'VerticalG', 'LateralG', 'ForwardG', 'RollRate', 'PitchRate', 'YawRate',
  'Pitch', 'Roll',
];
for (const c of REQUIRED) {
  if (!(c in idx)) {
    console.error(`FAIL: missing column ${c} in log header`);
    process.exit(1);
  }
}
// Reference columns for tuning.
const HAS_EFIS_LAT  = 'efisLateralG'  in idx;
const HAS_EFIS_VERT = 'efisVerticalG' in idx;
if (!HAS_EFIS_LAT && !HAS_EFIS_VERT) {
  console.error('FAIL: log has neither efisLateralG nor efisVerticalG; nothing to tune against');
  process.exit(1);
}

// Pre-allocate column arrays (skip header).
const N = lines.length - 1;
const col = (name, isInt = false) => {
  const arr = isInt ? new Int32Array(N) : new Float32Array(N);
  if (!(name in idx)) return arr;
  const ci = idx[name];
  for (let i = 0; i < N; i++) {
    const tok = (lines[i + 1].split(',')[ci] || '').trim();
    if (tok === '' || tok === '-' || tok === 'XXXX' || tok === 'XXXXX') {
      arr[i] = isInt ? -1 : NaN;
    } else {
      const v = isInt ? parseInt(tok, 10) : parseFloat(tok);
      arr[i] = Number.isFinite(v) ? v : (isInt ? -1 : NaN);
    }
  }
  return arr;
};

console.error(`Parsing ${N} rows...`);
const log = {
  PfwdSmoothed: col('PfwdSmoothed'),
  P45Smoothed:  col('P45Smoothed'),
  PStatic:      col('PStatic'),
  Palt:         col('Palt'),
  IAS:          col('IAS'),
  iasValid:     col('iasValid', true),  // string true/false won't parse; but irrelevant for this analysis
  flapsPos:     col('flapsPos', true),
  VerticalG:    col('VerticalG'),
  LateralG:     col('LateralG'),
  ForwardG:     col('ForwardG'),
  RollRate:     col('RollRate'),
  PitchRate:    col('PitchRate'),
  YawRate:      col('YawRate'),
  Pitch:        col('Pitch'),
  Roll:         col('Roll'),
  FlightPath:   col('FlightPath'),
  VSI:          col('VSI'),
  efisLateralG:  HAS_EFIS_LAT  ? col('efisLateralG')  : null,
  efisVerticalG: HAS_EFIS_VERT ? col('efisVerticalG') : null,
};

// ---------------------------------------------------------------------------
// Build a minimal cfg or load from XML
// ---------------------------------------------------------------------------

let cfg;
if (CFG_PATH) {
  if (!fs.existsSync(CFG_PATH)) {
    console.error(`FAIL: cfg not found: ${CFG_PATH}`);
    process.exit(1);
  }
  const xml = fs.readFileSync(CFG_PATH, 'utf8');
  cfg = Core.parse_config(xml);
  if (cfg.error) {
    console.error(`FAIL: cfg parse: ${cfg.error}`);
    process.exit(1);
  }
} else {
  // Minimal cfg — engine's accel-EMA path doesn't depend on aoa
  // curve / per-flap thresholds, but parse_config still wants
  // structurally-valid XML.
  const minXml = `<CONFIG2>
    <AOA_SMOOTHING>15</AOA_SMOOTHING>
    <PRESSURE_SMOOTHING>15</PRESSURE_SMOOTHING>
    <DATASOURCE>SENSORS</DATASOURCE>
    <FLAP_POSITION>
      <DEGREES>0</DEGREES><POT_VALUE>2048</POT_VALUE>
      <LDMAXAOA>4</LDMAXAOA><ONSPEEDFASTAOA>5</ONSPEEDFASTAOA>
      <ONSPEEDSLOWAOA>6</ONSPEEDSLOWAOA><STALLWARNAOA>8</STALLWARNAOA>
      <STALLAOA>0</STALLAOA><MANAOA>0</MANAOA>
      <ALPHA0>-3</ALPHA0><ALPHASTALL>10</ALPHASTALL>
      <KFIT>0</KFIT>
      <AOA_CURVE><TYPE>1</TYPE><X3>0</X3><X2>0</X2><X1>1</X1><X0>0</X0></AOA_CURVE>
    </FLAP_POSITION>
  </CONFIG2>`;
  cfg = Core.parse_config(minXml);
  if (cfg.error) throw new Error(`min cfg parse: ${cfg.error}`);
}

// ---------------------------------------------------------------------------
// Drive LogReplayTask through every row, capture per-row engine outputs.
// ---------------------------------------------------------------------------

const task = new Core.LogReplayTask(cfg, SAMPLE_HZ, /*flapsRawAdcAvail=*/false);

// We only need the engine's smoothed accel values per row. The task
// uses its lastStep() accessor.
const accelLatSm  = new Float32Array(N).fill(NaN);
const accelVertSm = new Float32Array(N).fill(NaN);
const gOnsetRate  = new Float32Array(N).fill(NaN);

console.error(`Running engine through ${N} rows...`);
let countValidEng = 0;
for (let i = 0; i < N; i++) {
  const row = {
    pfwdSmoothed:    log.PfwdSmoothed[i],
    p45Smoothed:     log.P45Smoothed[i],
    pStaticMbar:     log.PStatic[i],
    paltFt:          log.Palt[i],
    iasKt:           log.IAS[i],
    iasValid:        false,                // task derives via UpdateIasAlive
    flapsPos:        log.flapsPos[i] >= 0 ? log.flapsPos[i] : 0,
    flapsRawAdc:     0,
    flapsRawAdcPresent: false,
    imuVerticalG:    log.VerticalG[i],
    imuLateralG:     log.LateralG[i],
    imuForwardG:     log.ForwardG[i],
    imuRollRateDps:  log.RollRate[i],
    imuPitchRateDps: log.PitchRate[i],
    imuYawRateDps:   log.YawRate[i],
    pitchDeg:        log.Pitch[i],
    rollDeg:         log.Roll[i],
    flightPathDeg:   log.FlightPath[i],
    vsiFpm:          log.VSI[i],
    dataMark:        0,
    oatCelsius:      0,
  };
  const bytes = task.processRow(row);
  if (bytes.length === 0) continue;        // synth-path lag at start
  const eng = task.lastStep();
  accelLatSm [i] = eng.accelLatSmoothed;
  accelVertSm[i] = eng.accelVertSmoothed;
  gOnsetRate [i] = eng.gOnsetRate;
  countValidEng++;
}
task.delete();
console.error(`Engine produced ${countValidEng}/${N} valid rows`);

// ---------------------------------------------------------------------------
// τ sweep — for each candidate τ, simulate a presentation EMA at the
// log sample rate, compute RMS error against the EFIS reference.
// ---------------------------------------------------------------------------

// Use the same EMA the live render uses: α = 1 - exp(-dt/τ).
// dt is the per-row interval = 1 / SAMPLE_HZ.
const dt = 1.0 / SAMPLE_HZ;

// Rows to score: skip any row where either signal is NaN (engine lag,
// EFIS not yet present). Also optionally skip ground rows where
// LateralG isn't a meaningful slip indicator. We define "in flight"
// as IAS > 30 kt for at least 10 consecutive rows after takeoff.
function inFlightMask(log) {
  const m = new Uint8Array(N);
  let inFlight = false;
  let consec = 0;
  for (let i = 0; i < N; i++) {
    const ias = log.IAS[i];
    if (Number.isFinite(ias) && ias > 30) {
      consec++;
      if (consec >= 10) inFlight = true;
    } else {
      consec = 0;
    }
    m[i] = inFlight ? 1 : 0;
  }
  return m;
}
const flightMask = inFlightMask(log);
const flightRows = flightMask.reduce((a, b) => a + b, 0);
console.error(`In-flight rows: ${flightRows}`);

// Bias-removed RMS error. Computes the smoothed signal at τ, then
// subtracts the in-flight mean of (smoothed - reference) before RMS-
// ing. This isolates the DYNAMICS match from the constant calibration
// offset between the two sensor pipelines (different IMUs, different
// installation alignment — a ~15 mg lateral bias is unavoidable).
//
// Two-pass: first computes the mean offset over flight rows, then RMS.
function rmsErrorWithTau(tauSec, source, reference) {
  const alpha = (tauSec > 0) ? (1.0 - Math.exp(-dt / tauSec)) : 1.0;
  // Pass 1: smoothed signal AND mean offset.
  const smoothed = new Float32Array(N).fill(NaN);
  let state = NaN;
  let sumDiff = 0, nDiff = 0;
  for (let i = 0; i < N; i++) {
    const x = source[i];
    if (!Number.isFinite(x)) { state = NaN; continue; }
    state = Number.isNaN(state) ? x : (alpha * x + (1 - alpha) * state);
    smoothed[i] = state;
    if (!flightMask[i]) continue;
    const r = reference[i];
    if (!Number.isFinite(r)) continue;
    sumDiff += state - r;
    nDiff++;
  }
  const meanOffset = nDiff > 0 ? sumDiff / nDiff : 0;
  // Pass 2: bias-corrected RMS.
  let sumSq = 0, n = 0;
  for (let i = 0; i < N; i++) {
    if (!flightMask[i]) continue;
    const x = smoothed[i], r = reference[i];
    if (!Number.isFinite(x) || !Number.isFinite(r)) continue;
    const e = (x - meanOffset) - r;
    sumSq += e * e;
    n++;
  }
  return {
    rms: n > 0 ? Math.sqrt(sumSq / n) : NaN,
    bias: meanOffset,
  };
}

// Baseline: source direct (no presentation EMA), bias-corrected.
function baselineRMS(source, reference) {
  let sumDiff = 0, nDiff = 0;
  for (let i = 0; i < N; i++) {
    if (!flightMask[i]) continue;
    const x = source[i], r = reference[i];
    if (!Number.isFinite(x) || !Number.isFinite(r)) continue;
    sumDiff += x - r;
    nDiff++;
  }
  const bias = nDiff > 0 ? sumDiff / nDiff : 0;
  let sumSq = 0, n = 0;
  for (let i = 0; i < N; i++) {
    if (!flightMask[i]) continue;
    const x = source[i], r = reference[i];
    if (!Number.isFinite(x) || !Number.isFinite(r)) continue;
    const e = (x - bias) - r;
    sumSq += e * e;
    n++;
  }
  return {
    rms: n > 0 ? Math.sqrt(sumSq / n) : NaN,
    bias,
  };
}

// Compute σ of the smoothed signal at τ over the in-flight portion.
function sigmaWithTau(tauSec, source) {
  const alpha = (tauSec > 0) ? (1.0 - Math.exp(-dt / tauSec)) : 1.0;
  let state = NaN;
  let sum = 0, sumSq = 0, n = 0;
  for (let i = 0; i < N; i++) {
    const x = source[i];
    if (!Number.isFinite(x)) { state = NaN; continue; }
    state = Number.isNaN(state) ? x : (alpha * x + (1 - alpha) * state);
    if (!flightMask[i]) continue;
    n++;
    sum += state;
    sumSq += state * state;
  }
  if (n === 0) return NaN;
  const mean = sum / n;
  return Math.sqrt(Math.max(0, sumSq / n - mean * mean));
}

function sweep(label, source, reference, tausSec) {
  if (!reference) return;
  console.log(`\n=== ${label} ===`);
  // Reference σ — the target our smoothing should match.
  const refSigma = (() => {
    let n = 0, sum = 0, sumSq = 0;
    for (let i = 0; i < N; i++) {
      if (!flightMask[i] || !Number.isFinite(reference[i])) continue;
      n++; sum += reference[i]; sumSq += reference[i] * reference[i];
    }
    if (n === 0) return NaN;
    const m = sum / n;
    return Math.sqrt(Math.max(0, sumSq / n - m * m));
  })();
  const base = baselineRMS(source, reference);
  console.log(`  reference σ = ${refSigma.toFixed(4)} g  (target for variance-match)`);
  console.log(`  baseline (no smoothing, bias-corrected):  RMS = ${base.rms.toFixed(4)} g  bias = ${base.bias.toFixed(4)} g`);
  let bestRmsTau = 0,    bestRms = base.rms;
  let bestSigmaTau = 0,  bestSigmaErr = Math.abs((-1) - refSigma);
  console.log(`  τ (s)    RMS (g)   σ (g)     |σ - ref|`);
  for (const tau of tausSec) {
    const r = rmsErrorWithTau(tau, source, reference);
    const sig = sigmaWithTau(tau, source);
    const sigErr = Math.abs(sig - refSigma);
    let flag = '';
    if (r.rms < bestRms)         { bestRms = r.rms; bestRmsTau = tau; flag += ' *RMS'; }
    if (sigErr < bestSigmaErr)   { bestSigmaErr = sigErr; bestSigmaTau = tau; flag += ' *σ'; }
    console.log(`  ${tau.toFixed(2).padStart(5)}    ${r.rms.toFixed(4)}   ${sig.toFixed(4)}    ${sigErr.toFixed(4)}${flag}`);
  }
  console.log(`  best τ by RMS         = ${bestRmsTau.toFixed(2)} s  (drives source toward reference, but biased toward LONG τ — captures only low-frequency content)`);
  console.log(`  best τ by σ-match     = ${bestSigmaTau.toFixed(2)} s  (matches reference's variance — recommended for "look-like-flight")`);
}

// τ grid: dense low end, sparser high end. Extended to large τ to
// find the true optimum on lateral, where signal is noisy enough that
// even 5 s wasn't a clear plateau on first run.
const tausLat = [
  0.1, 0.2, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0,
  6.0, 7.5, 10.0, 12.5, 15.0, 20.0, 30.0,
];
const tausVert = [
  0.1, 0.2, 0.3, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5, 3.0, 4.0, 5.0,
];

sweep('Lateral G',  accelLatSm,  log.efisLateralG,  tausLat);
sweep('Vertical G', accelVertSm, log.efisVerticalG, tausVert);

// ---------------------------------------------------------------------------
// Also report the standard deviation of each signal (independent of
// reference) so we can see how noisy our engine output is vs the EFIS
// reference. This is a sanity check for the structural-aliasing
// hypothesis.
// ---------------------------------------------------------------------------

function stats(label, arr) {
  let n = 0, sum = 0, sumSq = 0;
  for (let i = 0; i < N; i++) {
    if (!flightMask[i] || !Number.isFinite(arr[i])) continue;
    n++;
    sum += arr[i];
    sumSq += arr[i] * arr[i];
  }
  if (n === 0) return console.log(`  ${label}: no data`);
  const mean = sum / n;
  const variance = sumSq / n - mean * mean;
  const sd = Math.sqrt(Math.max(0, variance));
  console.log(`  ${label.padEnd(28)} mean=${mean.toFixed(4)}  σ=${sd.toFixed(4)}  (n=${n})`);
}

console.log('\n=== Signal statistics over in-flight portion ===');
stats('raw IMU LateralG',         log.LateralG);
stats('engine accelLatSmoothed',  accelLatSm);
stats('efisLateralG (ref)',       log.efisLateralG);
stats('raw IMU VerticalG',        log.VerticalG);
stats('engine accelVertSmoothed', accelVertSm);
stats('efisVerticalG (ref)',      log.efisVerticalG);
stats('engine gOnsetRate (g/s)',  gOnsetRate);

// ---------------------------------------------------------------------------
// gOnsetRate: no EFIS reference (Dynon doesn't emit derivative).
// Sweep τ on a render-side EMA and report σ + 99th-percentile |value|
// at each τ so we can pick a τ that gives a visually-acceptable signal.
// ---------------------------------------------------------------------------

function pctile(arr, p) {
  const vals = [];
  for (let i = 0; i < N; i++) {
    if (flightMask[i] && Number.isFinite(arr[i])) vals.push(Math.abs(arr[i]));
  }
  if (vals.length === 0) return NaN;
  vals.sort((a, b) => a - b);
  const idx = Math.min(vals.length - 1, Math.floor(vals.length * p));
  return vals[idx];
}

console.log('\n=== gOnsetRate τ sweep (no EFIS ref; absolute stats) ===');
const baseSigma = sigmaWithTau(0, gOnsetRate);
const basePct99 = pctile(gOnsetRate, 0.99);
console.log(`  baseline (no EMA): σ=${baseSigma.toFixed(3)} g/s  p99|val|=${basePct99.toFixed(3)} g/s`);
console.log(`  τ (s)    σ (g/s)   p99|val|`);
for (const tau of [0.1, 0.2, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 5.0]) {
  const sig = sigmaWithTau(tau, gOnsetRate);
  // p99 via the EMA. Reuse rmsErrorWithTau's smoothing pass.
  const alpha = (tau > 0) ? (1.0 - Math.exp(-dt / tau)) : 1.0;
  let state = NaN;
  const smoothed = new Float32Array(N).fill(NaN);
  for (let i = 0; i < N; i++) {
    const x = gOnsetRate[i];
    if (!Number.isFinite(x)) { state = NaN; continue; }
    state = Number.isNaN(state) ? x : (alpha * x + (1 - alpha) * state);
    smoothed[i] = state;
  }
  const p99 = pctile(smoothed, 0.99);
  console.log(`  ${tau.toFixed(2).padStart(5)}    ${sig.toFixed(3)}     ${p99.toFixed(3)}`);
}
console.log(`  (target: visually-acceptable σ ≈ 0.1–0.3 g/s; p99 ≈ 0.5–1.0 g/s)`);

console.log('\nDone.');
