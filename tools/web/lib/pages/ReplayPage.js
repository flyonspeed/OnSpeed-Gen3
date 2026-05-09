// Replay / video-overlay page (/replay).
//
// Pilot uploads a flight video (.mp4) and the matching SD-card log
// (.csv). The page time-syncs them by anchoring at takeoff (auto-
// detected in the log; clicked in the video). With the offset
// established, the existing Preact mode renderers (Mode0, Mode1,
// Mode3) are layered over the video at native size, fed live from
// the matching log row at each video frame.
//
// Ships in tools/web/ (dev-server build only — not in firmware).
// Phase 1+2 of the design at <local plans> — sync UI + indexer
// overlay. Phases 3-5 (additional instruments, MP4 export) layer on
// top without changing the data plumbing.
//
// Architecture:
//   - <video> is the master clock. Its currentTime drives every
//     render frame.
//   - The log timestamps are in milliseconds since power-on. We
//     learn the offset from one anchor: takeoff. Pilot clicks
//     "Mark video takeoff" while the video is at the rotation
//     moment; the auto-detector finds the matching log row. The
//     mapping is then linear: log_t_ms = log_takeoff_ms +
//     (video_t_sec - video_takeoff_sec) * 1000.
//   - The overlay re-renders on every video frame via
//     requestVideoFrameCallback (falls back to RAF on Safari).
//   - Sync state persists to localStorage keyed by a hash of the
//     video+log filenames so a reload comes back to the same offset.
//
// Data pipeline (post-WASM Step 2):
//   - parseLog() → columnar typed arrays (unchanged, used by timeline +
//     sync detection + findRowAt).
//   - parseConfigXml() → config object via WASM C++ parser.
//   - LogReplayEngine.create() + pre-pass → results[] array indexed by
//     original log row. Engine runs the same compiled C++ as the firmware,
//     so accel smoothing / synth flap sweep / AOA computation are
//     bit-identical.
//   - computeAnchors() → percent-lift anchor positions for the indexer
//     (pipPctLift, tonesOnPctLift, etc.), also via WASM.
//   - Engine is deleted on new-log-load or component unmount.

import { html, useState, useEffect, useRef, useCallback }
  from '../vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { Mode0, Mode1, Mode3 } from '../modes.js';
import { parseLog, findRowAt, hasFlapsRawAdc, detectLogSampleRate }
  from '../replay/parseLog.js';
import { parseConfigXml } from '../replay/config.js';
import { LogReplayEngine } from '../replay/logReplay.js';
import { computeAnchors } from '../replay/percentLift.js';
import { detectTakeoffWithKind, downsampleForPlot } from '../replay/syncDetect.js';
import { exportOverlayedVideo, downloadBlob } from '../replay/exportRecord.js';
import { findDataMarks, logMsToVideoSec } from '../replay/dataMarks.js';
import { reassembleResults } from '../replay/reassemble.js';

const MODES = [
  { id: 'energy',   label: 'Energy',     C: Mode0 },
  { id: 'attitude', label: 'Attitude',   C: Mode1 },
  { id: 'decel',    label: 'Decel',      C: Mode3 },
];

const SYNC_LS_KEY = 'replay-sync-v1';

// Friendly label for the anchor type the auto-detector picked.
// Pilots usually sync against the first crosswind turn (sharp bank
// step in roll, easy to spot in the video); rotation is the
// fallback when no clear bank is detected.
function anchorKindLabel(kind) {
  if (kind === 'crosswind') return 'crosswind turn';
  if (kind === 'rotation')  return 'rotation';
  return 'anchor';
}

function safeLsGet(key) { try { return localStorage.getItem(key); } catch { return null; } }
function safeLsSet(key, value) { try { localStorage.setItem(key, value); } catch {} }

// Hash a string to a stable short key for localStorage. djb2.
function hashKey(s) {
  let h = 5381;
  for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
  return h.toString(16);
}

// Convert a video time (seconds) to a log timestamp (ms since
// power-on) using the takeoff anchor.
function videoToLogMs(videoSec, sync) {
  if (sync == null) return null;
  return sync.logTakeoffMs + (videoSec - sync.videoTakeoffSec) * 1000;
}

// Build a plain JS row object from the columnar log at index i.
// Maps log column names to the camelCase field names LogReplayEngine.step()
// expects (defined in bindings.cpp's StepInputFromVal).
//
// Fields the engine reads:
//   pfwdSmoothed, p45Smoothed, pStaticMbar, paltFt, iasKt, iasValid,
//   flapsPos, flapsRawAdc, flapsRawAdcPresent,
//   imuVerticalG, imuLateralG, imuForwardG,
//   imuRollRateDps, imuPitchRateDps, imuYawRateDps,
//   pitchDeg, rollDeg, flightPathDeg, vsiFpm, dataMark
//
// PfwdSmoothed and P45Smoothed are canonical always-present columns in
// the OnSpeed log format (written by LogCsv.cpp since the original log
// schema). parseLog.js reads them into typed arrays; we pass them through
// here so the WASM AOA calculator receives real pressure data.
// Pre-PR-#221 logs that lack these columns yield NaN; the engine
// degenerates to aoa=a0 (polynomial constant term) on NaN inputs, which
// is the same behavior as today — no regression on old logs.
function rowObjAt(log, i) {
  const g = (arr) => (arr ? arr[i] : NaN);
  const gi = (arr) => (arr ? arr[i] : 0);
  const iasKt = g(log.IAS);
  // iasValid: engine gates percent-lift at low IAS. Replicate the
  // firmware's bIasAlive flag: valid when IAS is finite and > 0.
  const iasValid = Number.isFinite(iasKt) && iasKt > 0;
  return {
    pfwdSmoothed:    g(log.PfwdSmoothed),
    p45Smoothed:     g(log.P45Smoothed),
    pStaticMbar:     g(log.PStatic),
    paltFt:          g(log.Palt),
    iasKt,
    iasValid,
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
  };
}

// Build the results array via a WASM LogReplayEngine pre-pass.
// Returns { results, buildRecord, length } where results[i] is the
// ReplayStepResult for log row i, or null if cancelled.
//
// Row alignment is handled by reassembleResults() from lib/replay/reassemble.js.
// See that module for the full semantics of the hasPot fast path vs the
// synth-circular-buffer path (including the N < synthHalfWindowTicks case).
//
// onProgress(fraction) is called with values in [0,1] during the pre-pass.
// isCancelled() is polled between chunks; returns true if the caller has
// cancelled (new log upload, component unmount). Engine is always freed via
// try/finally — no WASM heap leak on cancellation or error.
async function buildResultsFromWasm(log, cfg, onProgress, isCancelled) {
  if (!log || !cfg) return null;

  const sampleRateHz    = detectLogSampleRate(log);
  const hasPot          = hasFlapsRawAdc(log);
  const flapsMin        = cfg.flaps[0].degrees;
  const flapsMax        = cfg.flaps[cfg.flaps.length - 1].degrees;
  const N               = log.Length;

  const engine = await LogReplayEngine.create(cfg, sampleRateHz, hasPot);

  let results;
  try {
    // Pre-pass: feed all rows through the engine in chunks so the UI can
    // remain responsive on long logs (286 k rows × WASM-step ≈ 10 s).
    // Each chunk yields to the event loop so progress paints between chunks.
    const immediates = [];   // results from step(), in order (may contain nulls)
    const CHUNK = 5000;
    for (let start = 0; start < N; start += CHUNK) {
      if (isCancelled && isCancelled()) return null;   // bail early on cancel
      const end = Math.min(start + CHUNK, N);
      for (let i = start; i < end; i++) {
        immediates.push(engine.step(rowObjAt(log, i)));
      }
      if (onProgress) onProgress(end / N);
      // Yield to the event loop so the progress text paints.
      await new Promise(r => setTimeout(r, 0));
    }

    const tail = engine.flush();

    // Align results with original log row indices.
    // reassembleResults handles both paths:
    //   hasPot=true  → 1:1 (fast path, no lag)
    //   hasPot=false → synth path with lag, including N < lag case where
    //                  every immediates[i] is null and tail holds all rows.
    results = reassembleResults(immediates, tail, N, hasPot);
  } finally {
    // Always free the WASM engine — covers happy path, cancellation, and errors.
    engine.delete();
  }

  // Build a closure that maps a log row index to a display record.
  // Computes anchors from the WASM engine result's flapsIndex + flapsRawAdc.
  // Anchors are cheap to compute (single WASM call) and cached per flap index.
  const anchorCache = new Map();

  async function buildRecord(rowIdx) {
    const r = results[rowIdx];
    if (!r) return null;

    // Get or compute anchor positions for the current flap index.
    const cacheKey = `${r.flapsIndex}:${r.flapsRawAdc}`;
    let anchors = anchorCache.get(cacheKey);
    if (!anchors) {
      anchors = await computeAnchors(cfg.flaps, r.flapsIndex, r.flapsRawAdc);
      anchorCache.set(cacheKey, anchors);
    }

    const aoaDeg     = Number.isFinite(r.aoaDeg) ? r.aoaDeg : NaN;
    const aoaIsValid = Number.isFinite(aoaDeg);

    return {
      aoaDeg,
      aoaIsValid,
      derivedAoaDeg:      aoaDeg,
      pitchDeg:           r.pitchDeg,
      rollDeg:            r.rollDeg,
      flightPathDeg:      r.flightPathDeg,
      iasKt:              r.iasKt,
      paltFt:             r.paltFt,
      vsiFpm:             r.kalmanVsiMps != null
                            ? r.kalmanVsiMps * 196.85    // m/s → fpm
                            : NaN,
      // Smoothed accels from WASM (variable-dt rate-adjusted EMA, matches firmware).
      lateralG:           r.accelLatSmoothed,
      verticalG:          r.accelVertSmoothed,
      // Percent lift from WASM (coeffP is in [0, 99.9]).
      percentLift:        r.coeffP,
      // Anchor positions for the indexer needle + pip.
      tonesOnPctLift:         anchors.tonesOnPctLift,
      onSpeedFastPctLift:     anchors.onSpeedFastPctLift,
      onSpeedSlowPctLift:     anchors.onSpeedSlowPctLift,
      stallWarnPctLift:       anchors.stallWarnPctLift,
      pipPctLift:             anchors.pipPctLift,
      flapsDeg:   anchors.flapsDeg,
      flapsMinDeg: flapsMin,
      flapsMaxDeg: flapsMax,
      gOnsetRate:  0,         // not available in log replay
      dataMark:    r.dataMark,
    };
  }

  return { results, buildRecord, length: N };
}

export const ReplayPage = () => {
  const [videoFile, setVideoFile] = useState(null);
  const [videoUrl, setVideoUrl]   = useState(null);
  const [log, setLog]             = useState(null);
  const [logFilename, setLogFilename] = useState('');
  const [cfg, setCfg]             = useState(null);
  const [cfgFilename, setCfgFilename] = useState('');
  // replayCtx holds { results, buildRecord, length } from buildResultsFromWasm().
  const [replayCtx, setReplayCtx] = useState(null);
  const [sync, setSync]           = useState(null);
  const [videoT, setVideoT]       = useState(0);
  const [modeId, setModeId]       = useState('energy');
  const [overlayVisible, setOverlayVisible] = useState(true);
  const [parseErr, setParseErr]   = useState(null);
  // Building replay pre-pass can take a moment on long logs.
  const [replayBuilding, setReplayBuilding] = useState(false);
  // Pre-pass progress: 0.0–1.0. Updated in 5000-row chunks; drives a
  // progress bar in the toolbar so the pilot sees movement on long logs.
  const [replayProgress, setReplayProgress] = useState(0);
  // Anchor kind from detectTakeoffWithKind: 'crosswind' | 'rotation'
  // | 'none'. Used in the status text + timeline label so the pilot
  // knows what event they're syncing against.
  const [anchorKind, setAnchorKind] = useState('none');

  // Current rendered record (produced async by buildRecord; null while pending).
  const [rec, setRec] = useState(null);

  // Export-to-WebM state. exporting=true while a recording is in
  // progress; exportProgress reflects the % of the source video
  // captured so far; exportHandle is the controller returned by
  // exportOverlayedVideo (call .stop() to end early).
  const [exporting, setExporting]     = useState(false);
  const [exportProgress, setExportProgress] = useState(0);
  const [exportLabel, setExportLabel] = useState('');
  const exportHandleRef = useRef(null);

  // Manual clip list. Each entry is { startMs, endMs, label }.
  // The ClipBuilder UI displays them as a stack of editable rows;
  // "Export all" iterates and produces one WebM per entry.
  const [clips, setClips] = useState([]);

  // Re-sync flow state. When the pilot clicks "Pause indexer" the
  // overlay's log-time freezes at `pausedLogMs`; the video keeps
  // playing/scrubbing freely. When they click "Attach here" we
  // recompute sync so `pausedLogMs` lines up with the current video
  // time. Useful for catching sync drift mid-flight (e.g. video
  // editing chopped out a few seconds and takeoff-anchor sync is
  // 3 s off by the time you reach an interesting maneuver).
  const [pausedLogMs, setPausedLogMs] = useState(null);

  const videoRef    = useRef(null);
  const containerRef = useRef(null);
  const rafIdRef    = useRef(null);

  // ---------- File loaders -----------------------------------------

  const onVideoPick = (e) => {
    const f = e.target.files?.[0];
    if (!f) return;
    if (videoUrl) URL.revokeObjectURL(videoUrl);
    setVideoFile(f);
    setVideoUrl(URL.createObjectURL(f));
  };

  const onLogPick = async (e) => {
    const f = e.target.files?.[0];
    if (!f) return;
    setParseErr(null);
    try {
      const text = await f.text();
      const parsed = parseLog(text);
      if (parsed.Length === 0) throw new Error('no rows');
      setLog(parsed);
      setLogFilename(f.name);
    } catch (err) {
      setParseErr(`Could not parse log: ${err.message}`);
      setLog(null);
    }
  };

  const onCfgPick = async (e) => {
    const f = e.target.files?.[0];
    if (!f) return;
    setParseErr(null);
    try {
      const text = await f.text();
      const parsed = await parseConfigXml(text);
      if (!parsed.flaps || !parsed.flaps.length) throw new Error('no flap detents in config');
      setCfg(parsed);
      setCfgFilename(f.name);
    } catch (err) {
      setParseErr(`Could not parse config: ${err.message}`);
      setCfg(null);
    }
  };

  // Rebuild the replay pipeline whenever the log or config changes.
  // Doing this in an effect (not inline in render) keeps the
  // expensive WASM pre-pass from running every frame.
  //
  // Cancellation: if the user uploads a new log while a pre-pass is in
  // flight (up to ~10 s on 286 k-row logs), the prior build is cancelled
  // via the `cancelled` flag. The engine.delete() inside buildResultsFromWasm's
  // try/finally runs regardless, so the WASM heap never leaks.
  useEffect(() => {
    if (!log || !cfg) {
      setReplayCtx(null);
      setReplayBuilding(false);
      return;
    }

    let cancelled = false;
    setReplayBuilding(true);
    setReplayCtx(null);
    setReplayProgress(0);

    buildResultsFromWasm(
      log, cfg,
      (p) => { if (!cancelled) setReplayProgress(p); },
      () => cancelled,
    ).then(ctx => {
      if (cancelled) return;   // discard results for superseded log
      setReplayCtx(ctx);
      setReplayBuilding(false);
      setReplayProgress(0);
    }).catch(err => {
      if (cancelled) return;
      setParseErr(`Replay engine error: ${err.message}`);
      setReplayBuilding(false);
      setReplayProgress(0);
    });

    return () => { cancelled = true; };
  }, [log, cfg]);

  // ---------- Auto-detect takeoff in the log -----------------------

  useEffect(() => {
    if (!log) return;
    // Try to restore prior sync state before auto-detecting.
    const key = hashKey((videoFile?.name || '') + '|' + logFilename);
    const saved = safeLsGet(SYNC_LS_KEY + ':' + key);
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        if (Number.isFinite(parsed.logTakeoffMs) && Number.isFinite(parsed.videoTakeoffSec)) {
          setSync(parsed);
          return;
        }
      } catch {}
    }
    const { row: tRow, kind } = detectTakeoffWithKind(log);
    setAnchorKind(kind);
    if (tRow >= 0) {
      // Auto-detected anchor; pair it with whatever the pilot's
      // first "mark video anchor" press will be. Until then, sync
      // is null (overlay shows nothing).
      setSync(prev => prev ?? { logTakeoffMs: log.timeStamp[tRow], videoTakeoffSec: null });
    }
  }, [log, videoFile, logFilename]);

  // Persist sync state (only when both anchors are set).
  useEffect(() => {
    if (!sync || !Number.isFinite(sync.videoTakeoffSec) || !Number.isFinite(sync.logTakeoffMs)) return;
    if (!videoFile || !logFilename) return;
    const key = hashKey(videoFile.name + '|' + logFilename);
    safeLsSet(SYNC_LS_KEY + ':' + key, JSON.stringify(sync));
  }, [sync, videoFile, logFilename]);

  // ---------- Video clock ------------------------------------------

  // Drive a render-tick on every video frame. requestVideoFrameCallback
  // is the right primitive (frame-accurate, doesn't fire on a paused
  // video, gives us the true video time). Safari + older browsers
  // fall back to RAF, which is close enough for live preview — for
  // export-quality we'll switch to Remotion (Phase 5).
  useEffect(() => {
    const v = videoRef.current;
    if (!v) return;
    let cancelled = false;
    const useRvfc = typeof v.requestVideoFrameCallback === 'function';

    const tick = (now, meta) => {
      if (cancelled) return;
      // meta?.mediaTime is the canonical video time when rvfc fires;
      // fall back to currentTime for the RAF path.
      const t = (meta && Number.isFinite(meta.mediaTime)) ? meta.mediaTime : v.currentTime;
      setVideoT(t);
      if (useRvfc) v.requestVideoFrameCallback(tick);
      else rafIdRef.current = requestAnimationFrame(() => tick(performance.now()));
    };
    if (useRvfc) v.requestVideoFrameCallback(tick);
    else rafIdRef.current = requestAnimationFrame(() => tick(performance.now()));

    return () => {
      cancelled = true;
      if (rafIdRef.current) cancelAnimationFrame(rafIdRef.current);
    };
  }, [videoUrl]);

  // ---------- Async record resolution (video clock → display record) ---

  // The record lookup is async (computeAnchors is async, cached in
  // replayCtx.buildRecord). We resolve it on each videoT change.
  useEffect(() => {
    if (!log || !replayCtx) { setRec(null); return; }

    let cancelled = false;

    const resolveRec = async () => {
      let rowIdx = -1;

      if (Number.isFinite(pausedLogMs)) {
        rowIdx = findRowAt(log, pausedLogMs);
      } else if (sync &&
                 Number.isFinite(sync.videoTakeoffSec) &&
                 Number.isFinite(sync.logTakeoffMs)) {
        const tMs = videoToLogMs(videoT, sync);
        if (Number.isFinite(tMs)) rowIdx = findRowAt(log, tMs);
      }

      if (rowIdx < 0) { if (!cancelled) setRec(null); return; }

      const record = await replayCtx.buildRecord(rowIdx);
      if (!cancelled) setRec(record);
    };

    resolveRec();
    return () => { cancelled = true; };
  }, [log, replayCtx, sync, videoT, pausedLogMs]);

  // ---------- Anchor-mark handlers ---------------------------------

  const markVideoTakeoff = useCallback(() => {
    const v = videoRef.current;
    if (!v) return;
    setSync(prev => ({
      logTakeoffMs:    prev?.logTakeoffMs ?? null,
      videoTakeoffSec: v.currentTime,
    }));
  }, []);

  const reMarkLogTakeoff = useCallback(() => {
    if (!log) return;
    const { row: tRow, kind } = detectTakeoffWithKind(log);
    setAnchorKind(kind);
    if (tRow >= 0) {
      setSync(prev => ({
        logTakeoffMs:    log.timeStamp[tRow],
        videoTakeoffSec: prev?.videoTakeoffSec ?? null,
      }));
    }
  }, [log]);

  const clearSync = useCallback(() => setSync(null), []);

  // Sync readiness (both anchors set). Hoisted up here from its
  // earlier render-time computation site because the export
  // callback below depends on it.
  const syncReady = sync &&
    Number.isFinite(sync.videoTakeoffSec) &&
    Number.isFinite(sync.logTakeoffMs);

  // ---------- Export flow ------------------------------------------
  //
  // The general export takes a [startSec, endSec] window. Phase-4's
  // "export the whole thing from playhead" becomes a special case
  // (startSec = playhead, endSec = null → duration). DataMarks and
  // ClipBuilder rows call this with explicit windows.
  //
  // Returns the finished Blob (or null on error). Caller decides
  // whether to download it directly or queue it.
  const exportRange = useCallback(async ({ startSec, endSec, label }) => {
    const v = videoRef.current;
    if (!v) return null;

    // Clamp the window to the video's actual range. A data-mark
    // whose log time maps to a negative video time (mark dropped
    // before the camera started recording) or past the end (mark
    // dropped after the camera stopped) would otherwise either
    // produce a 0-byte file or hang. Refuse those windows loudly.
    if (Number.isFinite(startSec)) {
      if (startSec < 0 || startSec >= v.duration) {
        setParseErr(`Export skipped: ${label || 'clip'} starts ` +
                    `outside the video (${startSec.toFixed(1)} s; ` +
                    `video is 0 to ${v.duration.toFixed(1)} s).`);
        return null;
      }
    }
    if (Number.isFinite(endSec) && Number.isFinite(startSec) && endSec <= startSec) {
      setParseErr(`Export skipped: ${label || 'clip'} window is empty ` +
                  `(start ${startSec.toFixed(1)} s, end ${endSec.toFixed(1)} s).`);
      return null;
    }

    setExporting(true);
    setExportProgress(0);
    setExportLabel(label || '');
    try {
      const handle = await exportOverlayedVideo({
        videoEl: v,
        getOverlaySvg: () => document.querySelector('.replay-overlay-frame > svg'),
        startSec, endSec,
        outputWidth: 1920,
        bitrate: 12_000_000,
        onProgress: ({ videoSec, startSec: s, endSec: e }) => {
          if (e > s) setExportProgress((videoSec - s) / (e - s));
        },
      });
      exportHandleRef.current = handle;
      return await handle.finished;
    } catch (err) {
      setParseErr('Export failed: ' + (err?.message || err));
      return null;
    } finally {
      setExporting(false);
      setExportLabel('');
      exportHandleRef.current = null;
    }
  }, []);

  // Phase-4 button: export the whole video from current playhead.
  const startFullExport = useCallback(async () => {
    const v = videoRef.current;
    if (!v || !syncReady) return;
    const blob = await exportRange({
      startSec: v.currentTime,
      endSec:   null,
      label:    'full export',
    });
    if (!blob) return;
    const base = (videoFile?.name || 'flight').replace(/\.[^.]+$/, '');
    downloadBlob(blob, `${base}_with_overlay.webm`);
  }, [syncReady, videoFile, exportRange]);

  // DataMark / ClipBuilder export: single window with a custom
  // filename suffix. Used by the "Clip 30s" buttons and the
  // ClipBuilder list's per-row Export.
  const exportClip = useCallback(async ({ startSec, endSec, label, filenameSuffix }) => {
    const blob = await exportRange({ startSec, endSec, label });
    if (!blob) return;
    const base = (videoFile?.name || 'flight').replace(/\.[^.]+$/, '');
    downloadBlob(blob, `${base}_${filenameSuffix}.webm`);
  }, [exportRange, videoFile]);

  const stopExport = useCallback(() => {
    if (exportHandleRef.current) exportHandleRef.current.stop();
  }, []);

  // Re-sync flow:
  //   1. pauseIndexer() — freeze the indexer at the current
  //      video-mapped log time so it stops following video frames.
  //   2. Pilot scrubs the video to the moment that visually matches
  //      the frozen overlay state (e.g. wheels touching the runway).
  //   3. attachHere() — anchor the frozen log time to the now-current
  //      video time, replacing any stale takeoff sync.
  // Useful for catching the typical 2-3 s drift after video-edit
  // cuts that the takeoff anchor can't detect.
  const pauseIndexer = useCallback(() => {
    if (!sync) return;
    if (!Number.isFinite(sync.videoTakeoffSec) || !Number.isFinite(sync.logTakeoffMs)) return;
    const tMs = videoToLogMs(videoT, sync);
    if (!Number.isFinite(tMs)) return;
    setPausedLogMs(tMs);
  }, [sync, videoT]);

  const attachHere = useCallback(() => {
    const v = videoRef.current;
    if (!v || !Number.isFinite(pausedLogMs)) return;
    setSync({
      logTakeoffMs:    pausedLogMs,
      videoTakeoffSec: v.currentTime,
    });
    setPausedLogMs(null);
  }, [pausedLogMs]);

  const cancelPause = useCallback(() => setPausedLogMs(null), []);

  const ModeC = MODES.find(m => m.id === modeId)?.C ?? Mode0;
  // syncReady is computed up at the top so the export callback can
  // reference it; reuse here in the layout.

  // Compute the list of data-mark events once per log change.
  // Cheap (one pass over a 1 MB Int32Array); inline rather than
  // useMemo since Preact-standalone's hook surface is small.
  const marks = log ? findDataMarks(log) : [];

  // Helper for the data-mark panel: jump video to a mark.
  const jumpToMark = (markLogMs) => {
    const v = videoRef.current;
    if (!v) return;
    const tSec = logMsToVideoSec(markLogMs, sync);
    if (Number.isFinite(tSec)) v.currentTime = Math.max(0, tSec);
  };

  // Export an N-second clip starting at a mark's video time.
  const exportClipFromMark = (mark, durationSec) => {
    const startSec = logMsToVideoSec(mark.logTimeMs, sync);
    if (!Number.isFinite(startSec)) return;
    const endSec = Math.min(videoRef.current?.duration || startSec + durationSec,
                            startSec + durationSec);
    return exportClip({
      startSec,
      endSec,
      label: `mark ${mark.label}`,
      filenameSuffix: `mark${mark.label}_${durationSec}s`,
    });
  };

  // Add the current playhead as a clip start, with a default
  // 30-second window. Pilot can edit either edge in the row.
  const addClipFromPlayhead = (durationSec = 30) => {
    const v = videoRef.current;
    if (!v) return;
    const startVideoSec = v.currentTime;
    if (!sync) return;
    const startMs = sync.logTakeoffMs + (startVideoSec - sync.videoTakeoffSec) * 1000;
    const endMs   = startMs + durationSec * 1000;
    const label   = `clip ${String(clips.length + 1).padStart(2, '0')}`;
    setClips(prev => [...prev, { startMs, endMs, label }]);
  };

  // Export every clip in the list as N separate WebMs in sequence.
  const exportAllClips = async () => {
    for (let i = 0; i < clips.length; i++) {
      const c = clips[i];
      const startSec = logMsToVideoSec(c.startMs, sync);
      const endSec   = logMsToVideoSec(c.endMs,   sync);
      if (!Number.isFinite(startSec) || !Number.isFinite(endSec)) continue;
      // Sequential await: each MediaRecorder run produces its own
      // Blob and download before the next one starts.
      // eslint-disable-next-line no-await-in-loop
      await exportClip({
        startSec, endSec,
        label: c.label,
        filenameSuffix: c.label.replace(/[^a-z0-9_-]/gi, '_'),
      });
    }
  };

  // ---------- Layout -----------------------------------------------

  return html`
    <${PageShell} title="Replay">
      <div class="replay-page">
        <header class="replay-toolbar">
          <label class="replay-file">
            <span>Video</span>
            <input type="file" accept="video/*,.mp4,.mov,.webm" onChange=${onVideoPick} />
          </label>
          <label class="replay-file">
            <span>Log</span>
            <input type="file" accept=".csv,text/csv" onChange=${onLogPick} />
          </label>
          <label class="replay-file">
            <span>Config</span>
            <input type="file" accept=".cfg,.xml,text/xml" onChange=${onCfgPick} />
          </label>
          ${cfg && html`
            <span class="replay-status">
              ${cfg.flaps.length} flap detents loaded · ${cfgFilename}
            </span>`}
          ${replayBuilding && html`
            <span class="replay-status">
              building replay… ${replayProgress > 0
                ? Math.round(replayProgress * 100) + '%'
                : ''}
            </span>
            <progress class="replay-progress" max="1" value=${replayProgress}></progress>`}
          ${parseErr && html`<span class="replay-error">${parseErr}</span>`}
        </header>

        <div class="replay-stage" ref=${containerRef}>
          ${videoUrl ? html`
            <video
              ref=${videoRef}
              src=${videoUrl}
              controls
              playsInline
              class="replay-video"
            />
          ` : html`<div class="replay-placeholder">Drop a flight video and an SD-log CSV to get started.</div>`}

          ${overlayVisible && rec && html`
            <div class="replay-overlay">
              <div class="replay-overlay-frame ${Number.isFinite(pausedLogMs) ? 'paused' : ''}">
                <${ModeC} r=${rec} stale=${false} />
              </div>
            </div>
          `}
        </div>

        <footer class="replay-controls">
          <div class="replay-control-row">
            <span class="replay-label">Mode</span>
            ${MODES.map(m => html`
              <button
                class=${m.id === modeId ? 'replay-mode-btn active' : 'replay-mode-btn'}
                onClick=${() => setModeId(m.id)}>${m.label}</button>
            `)}
            <span class="replay-spacer"></span>
            <label class="replay-toggle">
              <input type="checkbox" checked=${overlayVisible}
                     onChange=${e => setOverlayVisible(e.target.checked)} />
              Show overlay
            </label>
            ${exporting
              ? html`
                  <span class="replay-status">
                    exporting${exportLabel ? ` · ${exportLabel}` : ''}
                  </span>
                  <progress class="replay-progress"
                            max="1" value=${exportProgress}></progress>
                  <span class="replay-status">${Math.round(exportProgress * 100)}%</span>
                  <button class="replay-btn-ghost" onClick=${stopExport}>
                    Stop
                  </button>`
              : html`
                  <button class="replay-btn-primary" onClick=${startFullExport}
                          disabled=${!syncReady || !videoUrl}>
                    Export WebM
                  </button>`}
          </div>

          <div class="replay-control-row">
            <button class="replay-btn-primary" onClick=${markVideoTakeoff} disabled=${!videoUrl}>
              Mark video anchor
            </button>
            <button class="replay-btn" onClick=${reMarkLogTakeoff} disabled=${!log}>
              Re-detect log anchor
            </button>
            <button class="replay-btn-ghost" onClick=${clearSync} disabled=${!sync}>
              Clear sync
            </button>
            <span class="replay-spacer"></span>
            <span class="replay-status">
              ${Number.isFinite(pausedLogMs)
                ? `indexer paused at log ${(pausedLogMs / 1000).toFixed(2)}s · scrub video and click Attach`
                : syncReady
                  ? `synced · video ${sync.videoTakeoffSec.toFixed(2)}s ↔ log ${(sync.logTakeoffMs / 1000).toFixed(2)}s ` +
                    `(${anchorKindLabel(anchorKind)})`
                  : sync
                    ? `log ${anchorKindLabel(anchorKind)} at ${(sync.logTakeoffMs / 1000).toFixed(2)}s — set video anchor`
                    : 'load files to begin'}
            </span>
          </div>

          <div class="replay-control-row">
            ${!Number.isFinite(pausedLogMs)
              ? html`<button class="replay-btn" onClick=${pauseIndexer}
                              disabled=${!syncReady}>Pause indexer for re-sync</button>`
              : html`
                  <button class="replay-btn-primary" onClick=${attachHere}>
                    Attach here
                  </button>
                  <button class="replay-btn-ghost" onClick=${cancelPause}>
                    Cancel
                  </button>`}
            <span class="replay-spacer"></span>
            ${Number.isFinite(pausedLogMs) && html`
              <span class="replay-status replay-status-attention">
                scrub the video to where the overlay matches, then click Attach
              </span>`}
          </div>

          <${LogTimeline} log=${log} sync=${sync}
                          videoT=${videoT}
                          anchorLabel=${anchorKindLabel(anchorKind)}
                          marks=${marks}
                          onLogTakeoffPick=${(tMs) => setSync(prev => ({
                            logTakeoffMs: tMs,
                            videoTakeoffSec: prev?.videoTakeoffSec ?? null,
                          }))}
                          onSeekVideo=${(tSec) => {
                            const v = videoRef.current;
                            if (v) v.currentTime = Math.max(0, tSec);
                          }} />

          ${marks.length > 0 && html`
            <${DataMarkPanel}
                marks=${marks}
                sync=${sync}
                disabled=${exporting || !syncReady}
                videoDuration=${videoRef.current?.duration}
                onJump=${jumpToMark}
                onClip=${exportClipFromMark} />`}

          <${ClipBuilderPanel}
              clips=${clips}
              sync=${sync}
              disabled=${exporting || !syncReady}
              onAdd=${addClipFromPlayhead}
              onRemove=${(i) => setClips(prev => prev.filter((_, j) => j !== i))}
              onExport=${(c) => exportClip({
                startSec: logMsToVideoSec(c.startMs, sync),
                endSec:   logMsToVideoSec(c.endMs, sync),
                label: c.label,
                filenameSuffix: c.label.replace(/[^a-z0-9_-]/gi, '_'),
              })}
              onExportAll=${exportAllClips} />
        </footer>
      </div>
    <//>`;
};

// ---------- Log timeline plot ---------------------------------------

// Renders IAS over time as a stripchart, with a vertical cursor at
// the current video-mapped log time and a draggable handle for the
// log-takeoff anchor.
//
// Click semantics:
//   - Plain click  → seek the video to the matching log time. The
//     primary action once sync is established. Re-uses whatever
//     sync state the page has so the cursor jumps to where you
//     clicked, video and overlay following along.
//   - Shift-click  → set the log-takeoff anchor at that point.
//     Manual override of the auto-detected takeoff row, useful
//     when the auto-detector picks the wrong climbout (e.g. a
//     pattern flight with multiple liftoffs).
//   - When sync isn't ready yet (no video-takeoff anchor set),
//     plain click falls back to the shift-click behavior so the
//     timeline is still useful for first-time setup.
const LogTimeline = ({ log, sync, videoT, anchorLabel = 'anchor',
                       marks = [],
                       onLogTakeoffPick, onSeekVideo }) => {
  const W = 1100, H = 80;
  const PAD = 4;

  if (!log) return html`<div class="replay-timeline empty"></div>`;

  // Downsample once per log change. useMemo would be ideal but
  // Preact-standalone's hook surface is small; just compute on each
  // render — log data is read-only once parsed and the downsample is
  // ~1 ms.
  const ds = downsampleForPlot(log, 1500);
  if (ds.tMs.length < 2) return html`<div class="replay-timeline empty"></div>`;

  const tMin = ds.tMs[0];
  const tMax = ds.tMs[ds.tMs.length - 1];
  const tSpan = Math.max(1, tMax - tMin);

  // IAS scale: 0 to max-seen, with a small headroom.
  let iasMax = 0;
  for (const v of ds.iasKt) if (Number.isFinite(v) && v > iasMax) iasMax = v;
  iasMax = Math.max(80, iasMax * 1.05);

  const xOf = (tMs) => PAD + (tMs - tMin) / tSpan * (W - 2 * PAD);
  const yOf = (ias) => H - PAD - (Number.isFinite(ias) ? (ias / iasMax) * (H - 2 * PAD) : 0);

  // Build path d-string for IAS line.
  let d = '';
  for (let i = 0; i < ds.tMs.length; i++) {
    const x = xOf(ds.tMs[i]);
    const y = yOf(ds.iasKt[i]);
    d += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ' ' + y.toFixed(1) + ' ';
  }

  const syncReady = sync &&
    Number.isFinite(sync.logTakeoffMs) &&
    Number.isFinite(sync.videoTakeoffSec);

  // Click handler. The svg viewBox is in CSS pixels via
  // `width: 100%; height: ${H}px` + `preserveAspectRatio: none`,
  // so e.clientX → x in viewBox units needs the bounding rect
  // scale ratio.
  const onSvgClick = (e) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const xPx  = e.clientX - rect.left;
    const xVB  = xPx / rect.width * W;     // viewBox-space x
    const frac = (xVB - PAD) / (W - 2 * PAD);
    const tMs  = tMin + Math.max(0, Math.min(1, frac)) * tSpan;

    if (e.shiftKey || !syncReady) {
      // Manual override (or first-time setup): place the
      // log-takeoff anchor here.
      onLogTakeoffPick(tMs);
      return;
    }
    // Default: seek the video to the matching moment, leaving
    // the takeoff anchor untouched.
    const videoT = sync.videoTakeoffSec + (tMs - sync.logTakeoffMs) / 1000;
    onSeekVideo(videoT);
  };

  // Cursor at current video-mapped log time.
  const cursorTMs = syncReady
    ? sync.logTakeoffMs + (videoT - sync.videoTakeoffSec) * 1000
    : null;

  return html`
    <div class="replay-timeline">
      <svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none"
           onClick=${onSvgClick}
           style="width: 100%; height: ${H}px; cursor: crosshair;">
        <rect x="0" y="0" width=${W} height=${H} fill="#0e1418" />
        <path d=${d} fill="none" stroke="#5cd6ff" stroke-width="1" />
        ${marks.map(m => {
          if (m.logTimeMs < tMin || m.logTimeMs > tMax) return null;
          const x = xOf(m.logTimeMs).toFixed(1);
          return html`
            <line x1=${x} y1="0" x2=${x} y2=${H}
                  stroke="#7dd3fc" stroke-width="1" stroke-opacity="0.55" />
            <text x=${(parseFloat(x) + 2).toFixed(1)} y=${(H - 4).toFixed(1)}
                  fill="#7dd3fc" font-size="10" font-family="monospace">
              ${m.label}
            </text>`;
        })}
        ${sync && Number.isFinite(sync.logTakeoffMs) && html`
          <line x1=${xOf(sync.logTakeoffMs).toFixed(1)} y1="0"
                x2=${xOf(sync.logTakeoffMs).toFixed(1)} y2=${H}
                stroke="#ffcc00" stroke-width="2" />
          <text x=${(xOf(sync.logTakeoffMs) + 4).toFixed(1)} y="14"
                fill="#ffcc00" font-size="11" font-family="monospace">
            ${anchorLabel}
          </text>`}
        ${cursorTMs != null && cursorTMs >= tMin && cursorTMs <= tMax && html`
          <line x1=${xOf(cursorTMs).toFixed(1)} y1="0"
                x2=${xOf(cursorTMs).toFixed(1)} y2=${H}
                stroke="#ff5577" stroke-width="1" stroke-dasharray="3 2" />`}
      </svg>
      <div class="replay-timeline-hint">${syncReady
        ? 'click to seek the video · shift+click to override the log-takeoff anchor'
        : 'click anywhere on the IAS trace to set the log-takeoff anchor'}</div>
    </div>`;
};

// ---------- DataMarkPanel ------------------------------------------

// Lists every DataMark transition the pilot dropped during the
// flight. Each row shows the mark's ordinal label + log time + the
// mapped video time (when sync is established), plus action buttons:
//   - Jump:    seek the video to the mark's video time
//   - Clip Ns: export an N-second WebM starting at the mark
const DataMarkPanel = ({ marks, sync, disabled, videoDuration,
                         onJump, onClip }) => {
  if (!marks || marks.length === 0) return null;
  // A mark is "in range" when its mapped video time falls within
  // the loaded video. Common reasons a mark falls out of range:
  //   - The mark was dropped before the camera started recording
  //     (video time goes negative).
  //   - The mark was dropped after the camera stopped (video time
  //     past videoDuration).
  // Out-of-range rows render with disabled action buttons and a
  // "no video" hint instead of a video timestamp.
  return html`
    <div class="replay-marks">
      <div class="replay-marks-header">
        <span class="replay-label">Data marks</span>
        <span class="replay-status">${marks.length}</span>
      </div>
      <div class="replay-marks-list">
        ${marks.map(m => {
          const videoSec = logMsToVideoSec(m.logTimeMs, sync);
          const dur = Number.isFinite(videoDuration) ? videoDuration : Infinity;
          const inRange = Number.isFinite(videoSec) &&
                          videoSec >= 0 && videoSec < dur;
          const tStr = inRange
            ? `video ${formatHms(videoSec)}`
            : (Number.isFinite(videoSec) ? 'outside video' : 'no sync');
          return html`
            <div class="replay-mark-row">
              <span class="replay-mark-label">${m.label}</span>
              <span class="replay-mark-time">log ${formatHms(m.logTimeMs / 1000)} · ${tStr}</span>
              <span class="replay-spacer"></span>
              <button class="replay-btn" disabled=${disabled || !inRange}
                      onClick=${() => onJump(m.logTimeMs)}>Jump</button>
              <button class="replay-btn" disabled=${disabled || !inRange}
                      onClick=${() => onClip(m, 30)}>Clip 30 s</button>
              <button class="replay-btn" disabled=${disabled || !inRange}
                      onClick=${() => onClip(m, 60)}>Clip 60 s</button>
            </div>`;
        })}
      </div>
    </div>`;
};

// ---------- ClipBuilderPanel ---------------------------------------

// User-defined clip ranges. Each row stores a {startMs, endMs,
// label} tuple in log-time; we display + edit in seconds. The
// expected workflow: scrub the video to a clip start, click "Add
// 30 s clip", then optionally drag the right edge in the timeline
// (future) or click Edit to type new times.
const ClipBuilderPanel = ({ clips, sync, disabled, onAdd, onRemove, onExport, onExportAll }) => {
  return html`
    <div class="replay-clips">
      <div class="replay-clips-header">
        <span class="replay-label">Clips</span>
        <span class="replay-status">${clips.length}</span>
        <span class="replay-spacer"></span>
        <button class="replay-btn" disabled=${disabled || !sync}
                onClick=${() => onAdd(30)}>+ 30 s clip from playhead</button>
        <button class="replay-btn" disabled=${disabled || !sync}
                onClick=${() => onAdd(60)}>+ 60 s clip from playhead</button>
        ${clips.length > 0 && html`
          <button class="replay-btn-primary" disabled=${disabled}
                  onClick=${onExportAll}>Export all clips</button>`}
      </div>
      ${clips.length === 0
        ? html`<div class="replay-clips-empty">no clips yet — scrub video, click "+ 30 s clip"</div>`
        : html`<div class="replay-clips-list">
            ${clips.map((c, i) => {
              const startSec = logMsToVideoSec(c.startMs, sync);
              const endSec   = logMsToVideoSec(c.endMs,   sync);
              const span = (c.endMs - c.startMs) / 1000;
              return html`
                <div class="replay-clip-row">
                  <span class="replay-mark-label">${c.label}</span>
                  <span class="replay-mark-time">
                    ${Number.isFinite(startSec) ? formatHms(startSec) : '—'}
                    → ${Number.isFinite(endSec) ? formatHms(endSec) : '—'}
                    · ${span.toFixed(1)} s
                  </span>
                  <span class="replay-spacer"></span>
                  <button class="replay-btn" disabled=${disabled}
                          onClick=${() => onExport(c)}>Export</button>
                  <button class="replay-btn-ghost" disabled=${disabled}
                          onClick=${() => onRemove(i)}>×</button>
                </div>`;
            })}
          </div>`}
    </div>`;
};

// Format a duration in seconds as H:MM:SS or M:SS.
function formatHms(sec) {
  if (!Number.isFinite(sec)) return '—';
  const total = Math.floor(sec);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  if (h > 0) return `${h}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
  return `${m}:${String(s).padStart(2,'0')}`;
}
