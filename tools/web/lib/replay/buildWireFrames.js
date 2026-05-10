// buildWireFrames.js — pre-pass a parsed log through the LogReplayTask
// and return a per-row array of 77-byte wire frames.
//
// The single-source C++ pipeline (issue #514) handles iasAlive
// hysteresis, engine smoothing, anchors, percent-lift, and wire
// encoding internally. JS callers just feed parsed LogRow shapes in
// row order and inject the resulting bytes into the M5 sim.
//
// This is the C++-engine arm of the M5-accurate path's A/B toggle.
// The JS-engine arm uses replayCtx.results[] + buildWireFrame from
// wireBridge.js. Both paths feed sim.injectBytes — the M5 firmware
// decode is unchanged.

import { LogReplayTask } from './logReplayTask.js';
import { reassembleResults } from './reassemble.js';
import { detectLogSampleRate, hasFlapsRawAdc } from './parseLog.js';

// Build a plain JS row object from the columnar log at index i.
// Mirrors rowObjAt in ReplayPage.js minus the iasValid threading —
// the C++ task IGNORES row.iasValid and re-derives it hysteretically
// (closes Bug 1 from the trial-run retro by construction).
function rowAt(log, i) {
  const g  = (arr) => (arr ? arr[i] : NaN);
  const gi = (arr) => (arr ? arr[i] : 0);
  return {
    pfwdSmoothed:    g(log.PfwdSmoothed),
    p45Smoothed:     g(log.P45Smoothed),
    pStaticMbar:     g(log.PStatic),
    paltFt:          g(log.Palt),
    iasKt:           g(log.IAS),
    iasValid:        false,                  // task overrides via UpdateIasAlive
    flapsPos:        gi(log.flapsPos),
    flapsRawAdc:     gi(log.flapsRawADC),
    flapsRawAdcPresent: !!(log.flapsRawADC),
    imuVerticalG:    g(log.VerticalG),
    imuLateralG:     g(log.LateralG),
    imuForwardG:     g(log.ForwardG),
    imuRollRateDps:  g(log.RollRate),
    imuPitchRateDps: g(log.PitchRate),
    imuYawRateDps:   g(log.YawRate),
    pitchDeg:        g(log.Pitch),
    rollDeg:         g(log.Roll),
    flightPathDeg:   g(log.FlightPath),
    vsiFpm:          g(log.VSI),
    dataMark:        gi(log.DataMark),
    oatCelsius:      g(log.OAT),
  };
}

// Pre-pass the log through a fresh LogReplayTask and return a
// per-row array of Uint8Arrays. The result has length = log.Length;
// each entry is either a 77-byte frame or null (synth-path lag).
//
// Synth-path lag handling: the task returns empty arrays during the
// first ~kSynthHalfWindowTicks rows and at end-of-log returns the
// trailing rows via flush(). reassembleResults aligns these with
// original log indices so we can index by row directly.
export async function buildWireFramesFromTask(log, cfg, onProgress, isCancelled) {
  if (!log || !cfg) return null;

  const sampleRateHz = detectLogSampleRate(log);
  const hasPot       = hasFlapsRawAdc(log);
  const N            = log.Length;

  const task = await LogReplayTask.create(cfg, sampleRateHz, hasPot);

  let frames;
  try {
    const immediates = [];
    const CHUNK = 5000;
    for (let start = 0; start < N; start += CHUNK) {
      if (isCancelled && isCancelled()) return null;
      const end = Math.min(start + CHUNK, N);
      for (let i = start; i < end; i++) {
        const bytes = task.processRow(rowAt(log, i));
        // Empty Uint8Array (length 0) means the engine is in lag.
        immediates.push(bytes.length > 0 ? bytes : null);
      }
      if (onProgress) onProgress(end / N);
      await new Promise(r => setTimeout(r, 0));
    }
    const tail = task.flush();   // Array<Uint8Array>; non-77-length impossible
    frames = reassembleResults(immediates, tail, N, hasPot);
  } finally {
    task.delete();
  }

  return frames;
}
