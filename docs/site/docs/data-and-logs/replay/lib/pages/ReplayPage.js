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
//   - Sync state + clip list persist to localStorage keyed by a
//     SHA-256 prefix of the log file's first 10 KB; a reload that
//     re-picks the same log restores both. See replay/persistence.js.
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

// URL-space relative path: see replay-entry.js header for why this is
// 4 `..` and not 7 (production deploys under /latest/; longer walks
// escape that prefix and 404).
import { html, useState, useEffect, useRef, useCallback }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';
import { parseLog, findRowAt, detectLogSampleRate }
  from '../replay/parseLog.js';
import { parseConfigXml } from '../replay/config.js';
import { detectTakeoffWithKind, downsampleForPlot } from '../replay/syncDetect.js';
import { exportOverlayedVideo, downloadBlob } from '../replay/exportRecord.js';
import { findDataMarks, logMsToVideoSec } from '../replay/dataMarks.js';
import { reassembleResults } from '../replay/reassemble.js';
import { M5Sim } from '../replay/m5sim.js';
import { buildWireFramesFromTask } from '../replay/buildWireFrames.js';
import { PresentationFilter, PRESENTATION_PRESETS, defaultPresetForLogRate }
  from '../replay/presentationFilter.js';
import { getWasmCore } from '../replay/wasm_core.js';
import { useReplayPersistence, RecentFilesBanner }
  from '../replay/persistence.js';
import {
  EnergyMode, AttitudeMode, IndexerMode, DecelMode, HistoricGMode,
} from '../../../../packages/ui-core/components/svg/m5modes/index.js';

// Mode list indexed by displayType (0..4). The int returned by
// `m5sim.read().displayType` maps directly to the renderer. Ordering
// matches the M5 firmware's `kModeNames` and the IndexerPage's MODES
// — same five modes, same numbering.
const M5_MODES = [
  { id: 0, label: 'Energy',     C: EnergyMode   },
  { id: 1, label: 'Attitude',   C: AttitudeMode },
  { id: 2, label: 'Indexer',    C: IndexerMode  },
  { id: 3, label: 'Decel',      C: DecelMode    },
  { id: 4, label: 'Historic G', C: HistoricGMode },
];

const M5_MODE_LS_KEY = 'replay-m5-mode-v1';
// Render-side presentation smoothing preset. NOT a firmware mirror —
// purely a viewing aid for 50 Hz log replay where IMU aliasing is
// visible on the slip ball. See presentationFilter.js for rationale.
const M5_SMOOTH_LS_KEY = 'replay-m5-smooth-v1';

// Friendly label for the anchor type the auto-detector picked.
// Pilots usually sync against the first crosswind turn (sharp bank
// step in roll, easy to spot in the video); rotation is the
// fallback when no clear bank is detected.
function anchorKindLabel(kind) {
  if (kind === 'crosswind') return 'crosswind turn';
  if (kind === 'rotation')  return 'rotation';
  return 'anchor';
}

// Local copies of the safe-LS wrappers. The persistence module owns
// its own copy of the same 2-line utility; sharing them through a
// third module isn't worth the indirection given the duplication
// cost. ReplayPage uses these for the small M5_MODE / M5_SMOOTH
// keys; persistence.js uses its own for the sync/clips/recent-files
// keys.
function safeLsGet(key) { try { return localStorage.getItem(key); } catch { return null; } }
function safeLsSet(key, value) { try { localStorage.setItem(key, value); } catch {} }

// Convert a video time (seconds) to a log timestamp (ms since
// power-on) using the takeoff anchor.
function videoToLogMs(videoSec, sync) {
  if (sync == null) return null;
  return sync.logTakeoffMs + (videoSec - sync.videoTakeoffSec) * 1000;
}


export const ReplayPage = () => {
  const [videoFile, setVideoFile] = useState(null);
  const [videoUrl, setVideoUrl]   = useState(null);
  const [log, setLog]             = useState(null);
  // logFile / cfgFile are the raw File objects (separate from the
  // parsed log/cfg objects). Persistence keys off logFile content,
  // not the parsed log struct.
  const [logFile, setLogFile]     = useState(null);
  const [logFilename, setLogFilename] = useState('');
  const [cfg, setCfg]             = useState(null);
  const [cfgFile, setCfgFile]     = useState(null);
  const [cfgFilename, setCfgFilename] = useState('');
  const [sync, setSync]           = useState(null);
  const [videoT, setVideoT]       = useState(0);
  const [overlayVisible, setOverlayVisible] = useState(true);
  const [parseErr, setParseErr]   = useState(null);
  // Anchor kind from detectTakeoffWithKind: 'crosswind' | 'rotation'
  // | 'none'. Used in the status text + timeline label so the pilot
  // knows what event they're syncing against.
  const [anchorKind, setAnchorKind] = useState('none');

  const [m5ModeId, setM5ModeId] = useState(() => {
    const s = safeLsGet(M5_MODE_LS_KEY);
    const n = s == null ? 0 : parseInt(s, 10);
    return Number.isFinite(n) && n >= 0 && n <= 4 ? n : 0;
  });
  // Render-side smoothing preset. 'off' = byte-faithful (wire values
  // drive the SVG directly). The other presets emulate the missing
  // 208 Hz averaging that 50 Hz log replay can't reproduce; see
  // presentationFilter.js for the structural rationale.
  //
  // Initial value: a previously-saved user choice if any, else null
  // (the load-time effect below picks a rate-appropriate default
  // once the log loads).
  const [m5SmoothPreset, setM5SmoothPreset] = useState(() => {
    const s = safeLsGet(M5_SMOOTH_LS_KEY);
    return PRESENTATION_PRESETS.find(p => p.id === s) ? s : null;
  });
  // Pre-computed wire frames per log row from the C++ LogReplayTask.
  // Shape: { frames: Uint8Array[], engineResults: object[] }.
  // engineResults is kept around for ?debug=1; not consumed by the
  // production rendering path (the firmware sim reads bytes only).
  const [cppWireFrames, setCppWireFrames] = useState(null);
  const [cppBuilding, setCppBuilding] = useState(false);
  const [cppProgress, setCppProgress] = useState(0);
  // The latest frozen state object from M5Sim.read(). Updated each
  // frame; the renderer reads it as a prop.
  const [m5State, setM5State] = useState(null);
  // Lazy-loaded sim instance. Created on first toggle-on, reused
  // across frames. The Core WASM module is also lazy-loaded.
  const m5SimRef = useRef(null);
  const m5CoreRef = useRef(null);
  // Track the previous virtual-time advance so backwards scrubs reset
  // the sim (avoids the firmware's millis() comparisons getting stuck
  // when the clock goes backwards).
  const m5LastVirtualMsRef = useRef(0);
  // Separate from virtual-time tracking: the highest 50ms-boundary
  // wire injection that we've already done. Per-frame effect injects
  // at every NEW 50ms boundary crossed by videoT advancing — even if
  // any single frame's delta is sub-50ms (at 60 fps it always is).
  // Reset to 0 on sim init / backward scrub / preset change.
  const m5LastInjectBoundaryMsRef = useRef(0);
  // Mirror m5ModeId into a ref so the async re-init `.then()` callback
  // (below) reads the current mode rather than the value captured by
  // the effect closure at fire time. The effect that updates this ref
  // runs on every m5ModeId change.
  const m5ModeIdRef = useRef(m5ModeId);

  // Render-side presentation filter (NOT a firmware mirror).
  // Applies after sim.read(); attenuates 50 Hz log aliasing before
  // SVG renderers consume state.LateralG / state.VerticalG. τ is
  // driven by m5SmoothPreset.
  const m5RenderFilterRef = useRef(null);
  // Last-frame timestamp for the render filter's continuous-time α
  // computation. videoT in seconds.
  const m5LastFilterVideoTRef = useRef(null);
  // Bump to force a sim re-init. The init effect's dep is
  // Bumping m5SimReinitNonce drops the current sim and rebuilds it.
  // Needed after backward virtual-time jumps because the firmware's
  // millis()-gated state (loopTime, numbersUpdateTime, gHistory cursor)
  // latches HIGH values that won't fire again until virtual time
  // exceeds them — leaving displayIAS / displayPalt /
  // displayPercentLift stuck at the last-seen-future values.
  const [m5SimReinitNonce, setM5SimReinitNonce] = useState(0);

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

  // Persistence: stores sync + clips per log-content digest in
  // localStorage so a reload restores both when the pilot re-picks
  // the same log. Also drives the "last session" banner that
  // suggests re-picking the prior session's files. See
  // replay/persistence.js for the storage contract.
  const persistence = useReplayPersistence({ logFile });

  // ---------- File loaders -----------------------------------------

  const onVideoPick = (e) => {
    const f = e.target.files?.[0];
    if (!f) return;
    if (videoUrl) URL.revokeObjectURL(videoUrl);
    setVideoFile(f);
    setVideoUrl(URL.createObjectURL(f));
    persistence.notifyFilePicked('video', f);
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
      setLogFile(f);
      setLogFilename(f.name);
      persistence.notifyFilePicked('log', f);
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
      setCfgFile(f);
      setCfgFilename(f.name);
      persistence.notifyFilePicked('cfg', f);
    } catch (err) {
      setParseErr(`Could not parse config: ${err.message}`);
      setCfg(null);
    }
  };

  // ---------- Auto-detect takeoff in the log -----------------------

  // Auto-detect takeoff + restore persisted sync.
  // Guards on persistence.digestReady so we don't auto-detect when a
  // stored sync for this exact log is already in localStorage. The
  // digest is async (10 KB read + SHA-256); during that gap we don't
  // touch sync, which avoids racing the auto-detector against the
  // restored value.
  useEffect(() => {
    if (!log) return;
    if (!persistence.digestReady) return;
    if (persistence.storedSync) {
      setSync(persistence.storedSync);
      setAnchorKind(persistence.storedSync.anchorKind || 'none');
      return;
    }
    const { row: tRow, kind } = detectTakeoffWithKind(log);
    setAnchorKind(kind);
    if (tRow >= 0) {
      // Auto-detected anchor; pair it with whatever the pilot's
      // first "mark video anchor" press will be. Until then, sync
      // is null (overlay shows nothing).
      setSync(prev => prev ?? { logTakeoffMs: log.timeStamp[tRow], videoTakeoffSec: null });
    }
  }, [log, persistence.digestReady, persistence.storedSync]);

  // Persist sync state to the log-content-keyed localStorage entry.
  // persistence.storeSync rejects partial sync (one anchor null), so
  // we don't bother gating here.
  useEffect(() => {
    persistence.storeSync(sync);
  }, [sync, persistence.storeSync]);

  // Restore persisted clips when the log digest resolves. Only seed
  // if the in-memory clip list is empty — never clobber clips the
  // user added before the digest came back.
  useEffect(() => {
    if (!persistence.digestReady) return;
    if (!persistence.storedClips || !persistence.storedClips.length) return;
    setClips(prev => prev.length === 0 ? persistence.storedClips : prev);
  }, [persistence.digestReady, persistence.storedClips]);

  // Persist clips on every change.
  useEffect(() => {
    persistence.storeClips(clips);
  }, [clips, persistence.storeClips]);

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

  // ---------- M5 sim init / teardown -----------------------------------

  // Lazy-load the M5 sim and onspeed_core WASM on mount. The sim
  // survives across renders; it's torn down + recreated when the
  // reinit-nonce bumps (used by backward-scrub recovery — firmware
  // millis-gated state otherwise latches at values from the prior
  // playhead position).
  useEffect(() => {
    if (m5SimRef.current) {
      try { m5SimRef.current.delete(); } catch (_) {}
      m5SimRef.current = null;
    }
    m5LastVirtualMsRef.current = 0;
    m5LastInjectBoundaryMsRef.current = 0;

    let cancelled = false;
    (async () => {
      try {
        const [sim, core] = await Promise.all([
          M5Sim.create(),
          getWasmCore(),
        ]);
        if (cancelled) {
          // Race: component unmounted mid-load.
          sim.delete();
          return;
        }
        m5SimRef.current = sim;
        m5CoreRef.current = core;
        // Initialize the render-side filter. The ?-effect below
        // retunes it as the smooth preset settles (rate-aware
        // default + user changes).
        const filter = new PresentationFilter();
        if (m5SmoothPreset) {
          const preset = PRESENTATION_PRESETS.find(p => p.id === m5SmoothPreset)
                         || PRESENTATION_PRESETS[0];
          filter.setTau({
            lateralSec:  preset.lateralSec,
            verticalSec: preset.verticalSec,
          });
        }
        m5RenderFilterRef.current = filter;
        m5LastFilterVideoTRef.current = null;
        // Hydrate displayType from the persisted choice so the first
        // frame after init renders the right mode.
        sim.setMode(m5ModeId);
      } catch (err) {
        if (!cancelled) setParseErr(`M5 sim load error: ${err.message}`);
      }
    })();

    return () => { cancelled = true; };
  }, [m5SimReinitNonce]);

  // Pick a rate-appropriate default smoothing preset once the log
  // loads, IF the user hasn't set one (m5SmoothPreset === null on a
  // fresh session with no LS value). 50 Hz logs default to the Gen2
  // 2.5 s preset (compensates for the missing 208 Hz averaging the
  // log doesn't capture). 208 Hz logs default to Off.
  useEffect(() => {
    if (m5SmoothPreset !== null || !log) return;
    const sampleRateHz = detectLogSampleRate(log);
    setM5SmoothPreset(defaultPresetForLogRate(sampleRateHz));
  }, [log, m5SmoothPreset]);

  // Persist the smoothing preset; also retune the live filter when
  // the user changes it. Reset filter state so the next frame seeds
  // fresh — a step-change in τ otherwise looks like a glitch in the
  // first frame after the change.
  useEffect(() => {
    if (m5SmoothPreset === null) return;        // wait for default-pick
    safeLsSet(M5_SMOOTH_LS_KEY, m5SmoothPreset);
    if (m5RenderFilterRef.current) {
      const preset = PRESENTATION_PRESETS.find(p => p.id === m5SmoothPreset)
                     || PRESENTATION_PRESETS[0];
      m5RenderFilterRef.current.setTau({
        lateralSec:  preset.lateralSec,
        verticalSec: preset.verticalSec,
      });
      m5RenderFilterRef.current.reset();
      m5LastFilterVideoTRef.current = null;
    }
  }, [m5SmoothPreset]);

  // C++-engine pre-pass. Runs once per (log, cfg) pair while
  // M5-accurate is on, and survives mode toggle JS↔C++ — clicking
  // back to C++ reuses the cached frames, no rebuild. Invalidate
  // only when log or cfg changes. Skipping when M5-accurate is off
  // saves the cost on JS-only legacy-rec sessions.
  useEffect(() => {
    if (!log || !cfg) {
      setCppWireFrames(null);
      setCppBuilding(false);
      return;
    }
    let cancelled = false;
    setCppBuilding(true);
    setCppWireFrames(null);
    setCppProgress(0);
    buildWireFramesFromTask(
      log, cfg,
      (p) => { if (!cancelled) setCppProgress(p); },
      () => cancelled,
    ).then(frames => {
      if (cancelled) return;
      setCppWireFrames(frames);
      setCppBuilding(false);
      setCppProgress(0);
    }).catch(err => {
      if (cancelled) return;
      console.error('LogReplayTask pre-pass failed:', err);
      setCppBuilding(false);
      setCppProgress(0);
    });
    return () => { cancelled = true; };
  }, [log, cfg]);

  // Persist the mode.
  useEffect(() => {
    safeLsSet(M5_MODE_LS_KEY, String(m5ModeId));
    if (m5SimRef.current) {
      m5SimRef.current.setMode(m5ModeId);
      // Refresh React state immediately so the SVG re-renders the new
      // mode without waiting for a videoT tick. Important on pause:
      // no per-frame effect runs to refresh state otherwise.
      setM5State(m5SimRef.current.read());
    }
  }, [m5ModeId]);

  // Mirror m5ModeId into m5ModeIdRef so the async re-init .then()
  // (in the per-frame effect below) reads the current mode rather
  // than a stale closure value.
  useEffect(() => {
    m5ModeIdRef.current = m5ModeId;
  }, [m5ModeId]);

  // Per-frame M5 sim driver: tick the M5 firmware's virtual clock at
  // its native 20 Hz wire cadence, injecting a wire frame and calling
  // loop() once per 50 ms tick. Multiple ticks may fire per video
  // frame (60 fps video → ~3 ticks per frame at 1× playback).
  //
  // Why 50 ms ticks instead of "advance virtMs to videoT*1000 in one
  // step": the M5's internal render-rate gate is `millis() > loopTime
  // + 50`. If we slam virtMs from t=1500.000 to t=1500.016 in one
  // call, the firmware's gates fire once on a single advanceTo and
  // see `Slip` from one specific log row — but every frame we'd inject
  // a wire frame from a NEW log row. SerialRead computes Slip
  // synchronously on each byte injection (not gated by millis), so
  // Slip would update at video-frame rate (60 Hz) while the M5 hardware
  // updates it at 20 Hz. That's why the ball jittered.
  //
  // The fix: drive virtual time in 50 ms steps, injecting one wire
  // frame per step, calling loop() per step. The firmware sees its
  // own native 20 Hz cadence. gHistory fills naturally as we step
  // forward through time; the 2 Hz numbers snapshot fires every 10
  // ticks; the wire-rate Slip computation runs once per tick.
  //
  // Tick the M5 firmware's virtual clock at its native 20 Hz wire
  // cadence: each tick injects one wire frame and advances time across
  // the 50 ms boundary. Per-video-frame this is 1-2 ticks at 60 fps,
  // matching what the M5 hardware sees on real wire.
  //
  // We deliberately render React state only on tick boundaries, so the
  // SVG refreshes at 20 Hz max regardless of video framerate. This is
  // what keeps the slip ball from jittering: the firmware's `Slip =
  // int(-LateralG * 34/0.04)` runs synchronously per inject, but the
  // visible state only refreshes when a tick fires.
  //
  // Backward scrubs: snap last to target so the M5's gates don't wedge
  // on a backward-running clock. gHistory + filter state will be
  // partially stale after a backward scrub (the firmware retains the
  // pre-scrub history); for v0 this is acceptable. A future fix
  // re-warms by replaying ~60 s forward, but the catch-up burst was
  // visually disruptive enough that we strip it for now.
  // Tick at the M5's wire cadence (20 Hz) when we're catching up to
  // continuous play. On large jumps (first sync, scrub forward, scrub
  // backward), SNAP virtual time to target without replaying history:
  // inject one wire frame for the target log row, advance time, render.
  // Filter state (gHistory, EMAs) is partially stale after a snap, but
  // visible state matches the new video position immediately.
  const M5_TICK_MS = 50;
  const M5_LARGE_JUMP_MS = 5000;   // > this triggers a snap, not a tick chain.

  // ---------- Diagnostic mode --------------------------------------
  //
  // ?debug=1 turns on JSON-line logging once per ~500 ms with the
  // raw log values, both pipelines' wire-encoded frames (decoded
  // via parse_display_frame), and the M5 sim's read() snapshot.
  // Lets the user copy a few lines to compare paths.
  const debugMode = (typeof window !== 'undefined') &&
                    new URLSearchParams(window.location.search).get('debug') === '1';
  const debugLastLogMsRef = useRef(0);

  useEffect(() => {
    const sim = m5SimRef.current;
    const core = m5CoreRef.current;
    if (!sim || !core || !log || !cfg) { setM5State(null); return; }
    if (!cppWireFrames) return;   // wait for C++ pre-pass
    if (!sync ||
        !Number.isFinite(sync.videoTakeoffSec) ||
        !Number.isFinite(sync.logTakeoffMs)) return;

    const targetVirtMs = Number.isFinite(pausedLogMs)
      ? Math.max(0, pausedLogMs)
      : Math.max(0, videoT * 1000);
    const last = m5LastVirtualMsRef.current;
    const jump = targetVirtMs - last;

    // Helper: inject the pre-computed wire frame for the log row
    // mapped from a virtual timestamp. Single-source path: bytes come
    // from the C++ LogReplayTask pre-pass.
    const injectAt = (virtMs) => {
      const tickLogMs = Number.isFinite(pausedLogMs)
        ? pausedLogMs
        : sync.logTakeoffMs + (virtMs / 1000 - sync.videoTakeoffSec) * 1000;
      const rowIdx = findRowAt(log, tickLogMs);
      if (rowIdx < 0) return;
      const frameBytes = cppWireFrames.frames[rowIdx];
      if (!frameBytes) return;               // synth-path lag for this row
      sim.injectBytes(frameBytes);
    };

    // Apply the render-side smoothing filter to a sim.read() snapshot
    // and return a frozen object with replaced LateralG / VerticalG.
    // SVG components consume these fields directly — Slip (the
    // clamped firmware-display integer) stays untouched and is not
    // used for slip-ball positioning anymore. Smoothing operates in
    // continuous-G space at all magnitudes, so saturation past
    // ±0.116g doesn't dead-zone the filter.
    const renderSmooth = (state) => {
      const filter = m5RenderFilterRef.current;
      if (!filter || !state) return state;
      const lastT = m5LastFilterVideoTRef.current;
      const dt = (Number.isFinite(lastT) && videoT > lastT)
                  ? (videoT - lastT) : (1 / 60);
      m5LastFilterVideoTRef.current = videoT;
      const smoothed = filter.apply(state.LateralG, state.VerticalG, dt);
      return Object.freeze({
        ...state,
        LateralG:  Number.isFinite(smoothed.lateralG)
                     ? smoothed.lateralG : state.LateralG,
        VerticalG: Number.isFinite(smoothed.verticalG)
                     ? smoothed.verticalG : state.VerticalG,
      });
    };

    // Backward jump: the firmware's millis()-gated state (loopTime,
    // numbersUpdateTime, gHistory cursor) latched HIGH values from
    // the prior playback position. Setting virtual time backward
    // leaves those gates with stale futures — displayIAS, displayPalt,
    // displayPercentLift, and the gHistory sample cursor all stay
    // frozen until forward virtual time catches back up. Re-init the
    // sim from scratch so all those internal gates restart at 0.
    if (jump < 0) {
      if (m5RenderFilterRef.current) {
        m5RenderFilterRef.current.reset();
        m5LastFilterVideoTRef.current = null;
      }
      // Bumping the nonce triggers the sim-init effect to drop the
      // old sim and rebuild. The next per-frame effect tick picks up
      // the fresh sim. State display catches up within ~one video
      // frame; nothing user-visible during the gap because the prior
      // setM5State value sticks until then.
      setM5SimReinitNonce(n => n + 1);
      return;
    }

    // Large forward jump: snap. Don't replay history — it would
    // produce a multi-second visual scramble. The firmware gates
    // tolerate a forward jump fine (millis() jumps past the latched
    // numbersUpdateTime+500 → gate fires).
    if (jump > M5_LARGE_JUMP_MS) {
      if (m5RenderFilterRef.current) {
        m5RenderFilterRef.current.reset();
        m5LastFilterVideoTRef.current = null;
      }
      injectAt(targetVirtMs);
      sim.advanceTo(targetVirtMs);
      m5LastVirtualMsRef.current = targetVirtMs;
      // Realign the inject-boundary tracker so subsequent ticks
      // resume at the next 50ms boundary past where we snapped.
      m5LastInjectBoundaryMsRef.current =
        Math.floor(targetVirtMs / M5_TICK_MS) * M5_TICK_MS;
      setM5State(renderSmooth(sim.read()));
      return;
    }

    // Normal-play catch-up. Two cadences in play:
    //  - WIRE injection happens on 50 ms boundaries (M5 wire rate is
    //    20 Hz; injecting more often would over-feed the SerialRead
    //    parser with duplicate frames).
    //  - VIRTUAL CLOCK must track every video frame so the firmware's
    //    millis()-based gates (loopTime + numbersUpdateTime) fire on
    //    their own internal cadence.
    //
    // We track the two cadences with separate refs so a single video
    // frame's sub-50ms advance doesn't stall the inject loop — earlier
    // logic compared `last + 50 <= target` per-frame, and since target
    // only advances ~16 ms per video frame, that condition was never
    // true and the while loop ran zero times forever.
    //
    // New logic: walk boundary-by-boundary from the LAST INJECTED
    // boundary to (or past) targetVirtMs, regardless of per-frame
    // delta. Then advance the virtual clock the rest of the way.
    let lastBoundary = m5LastInjectBoundaryMsRef.current;
    let nextBoundary = lastBoundary + M5_TICK_MS;
    let ticks = 0;
    while (nextBoundary <= targetVirtMs) {
      injectAt(nextBoundary);
      sim.advanceTo(nextBoundary);     // fires loop() at the boundary
      lastBoundary = nextBoundary;
      nextBoundary += M5_TICK_MS;
      ticks++;
    }
    m5LastInjectBoundaryMsRef.current = lastBoundary;

    // Advance virtual time the rest of the way to target so the
    // firmware's millis()-based gates have the most recent clock
    // even between 50 ms boundaries.
    if (targetVirtMs > lastBoundary) {
      sim.advanceTo(targetVirtMs);
    }
    m5LastVirtualMsRef.current = targetVirtMs;

    // Always refresh visible state per video frame.
    setM5State(renderSmooth(sim.read()));

    // --- Diagnostic logging (?debug=1) -----------------------------
    if (debugMode && core && typeof core.parse_display_frame === 'function') {
      const now = performance.now();
      if (now - debugLastLogMsRef.current >= 500) {
        debugLastLogMsRef.current = now;
        const logMs = Number.isFinite(pausedLogMs)
          ? pausedLogMs
          : sync.logTakeoffMs + (videoT - sync.videoTakeoffSec) * 1000;
        const rowIdx = findRowAt(log, logMs);
        const fnum = (n, k = 2) => Number.isFinite(n) ? +n.toFixed(k) : null;
        const safeDecode = (bytes) => {
          if (!bytes || bytes.length === 0) return null;
          try { return core.parse_display_frame(bytes); }
          catch (_) { return null; }
        };
        const sliceFrame = (f) => f && {
          ias:    fnum(f.iasKt, 1),
          iasV:   f.iasIsValid,
          alt:    fnum(f.paltFt, 0),
          pct:    fnum(f.percentLiftPct, 1),
          pip:    f.pipPctLift,
          tonesOn:f.tonesOnPctLift,
          fast:   f.onSpeedFastPctLift,
          slow:   f.onSpeedSlowPctLift,
          warn:   f.stallWarnPctLift,
          flapsDeg: f.flapsDeg,
          fmin:   f.flapsMinDeg,
          fmax:   f.flapsMaxDeg,
          pitch:  fnum(f.pitchDeg, 1),
          roll:   fnum(f.rollDeg, 1),
          latG:   fnum(f.lateralG, 3),
          vertG:  fnum(f.verticalG, 2),
          gOnset: fnum(f.gOnsetRate, 2),
          fpa:    fnum(f.flightPathDeg, 1),
        };
        // Production wire frame from the C++ task pre-pass.
        const cppFrame = (cppWireFrames && rowIdx >= 0)
          ? safeDecode(cppWireFrames.frames[rowIdx]) : null;
        // C++ engine's ReplayStepResult for this row — independent
        // of which engine arm is currently driving the visible sim.
        const cppEng = (cppWireFrames && rowIdx >= 0)
          ? cppWireFrames.engineResults[rowIdx] : null;
        const cppEngSlice = cppEng ? {
          aoa:    fnum(cppEng.aoaDeg, 2),
          coeffP: fnum(cppEng.coeffP, 3),
          iasV:   cppEng.iasValid,
          flapsPos: cppEng.flapsPos,        // <- what the engine received
          flapsIdx: cppEng.flapsIndex,      // <- what ResolveFlapIndex_ returned
          accelLatSm: fnum(cppEng.accelLatSmoothed, 3),
          accelVertSm: fnum(cppEng.accelVertSmoothed, 2),
          gOnsetRate: fnum(cppEng.gOnsetRate, 2),
          flapsRawAdc: cppEng.flapsRawAdc,
        } : null;

        const m5 = m5SimRef.current && typeof m5SimRef.current.read === 'function'
          ? m5SimRef.current.read() : null;
        // Field names mirror m5sim.read() output (see m5sim.js:200-247).
        // displayXxx are the firmware's 2 Hz number snapshots (what the
        // SVG actually shows in the corner numerics); the bare names
        // are wire-rate live values driving chevrons/ball.
        const m5Slice = m5 ? {
          // What the SVG number-text renders (these are stuck if the
          // firmware's render gate isn't firing).
          dIAS:    fnum(m5.displayIAS, 1),
          dPalt:   fnum(m5.displayPalt, 0),
          dPitch:  fnum(m5.displayPitch, 1),
          dPctL:   fnum(m5.displayPercentLift, 1),
          dVertG:  fnum(m5.displayVerticalG, 2),
          // Wire-rate live values.
          IAS:     fnum(m5.IAS, 1),
          Palt:    fnum(m5.Palt, 0),
          Pitch:   fnum(m5.Pitch, 1),
          Roll:    fnum(m5.Roll, 1),
          PctL:    fnum(m5.PercentLift, 1),
          IasV:    m5.IasIsValid,
          // Anchors (drive indexer chevrons / pip).
          TOn:     m5.TonesOnPctLift,
          Pip:     m5.PipPctLift,
          Fast:    m5.OnSpeedFastPctLift,
          Slow:    m5.OnSpeedSlowPctLift,
          Warn:    m5.StallWarnPctLift,
          FPos:    m5.FlapPos,
          FMin:    m5.FlapsMinDeg,
          FMax:    m5.FlapsMaxDeg,
          Slip:    m5.Slip,
          gOR:     fnum(m5.gOnsetRate, 2),
        } : null;

        // Raw log row (a slice of the columnar arrays at rowIdx).
        const raw = (rowIdx >= 0) ? {
          IAS:        fnum(log.IAS?.[rowIdx], 1),
          Palt:       fnum(log.Palt?.[rowIdx], 0),
          PfwdSm:     fnum(log.PfwdSmoothed?.[rowIdx], 3),
          P45Sm:      fnum(log.P45Smoothed?.[rowIdx], 3),
          flapsPos:   log.flapsPos?.[rowIdx],
          flapsRawAdc:log.flapsRawADC?.[rowIdx],
          Pitch:      fnum(log.Pitch?.[rowIdx], 1),
          Roll:       fnum(log.Roll?.[rowIdx], 1),
          LateralG:   fnum(log.LateralG?.[rowIdx], 3),
          VerticalG:  fnum(log.VerticalG?.[rowIdx], 2),
          AOA:        fnum(log.AngleofAttack?.[rowIdx], 2),
          // Dynon ADAHRS-filtered values (10 Hz nominal). Different
          // sensor + different filter pipeline than OnSpeed's IMU.
          // Useful as a tuning reference for the presentation filter:
          // efisLateralG is what the airplane's primary EFIS slip
          // indicator showed the pilot.
          efisLatG:   fnum(log.efisLateralG?.[rowIdx], 3),
          efisVertG:  fnum(log.efisVerticalG?.[rowIdx], 2),
        } : null;

        // eslint-disable-next-line no-console
        console.log('REPLAY_DBG ' + JSON.stringify({
          videoT: fnum(videoT, 2),
          rowIdx,
          mode: m5ModeId,
          smooth: m5SmoothPreset,
          raw,
          cppEng: cppEngSlice,
          cppFrame: sliceFrame(cppFrame),
          m5: m5Slice,
        }));
      }
    }
  }, [log, cfg, sync, videoT, pausedLogMs, m5ModeId,
      m5SmoothPreset, cppWireFrames, debugMode]);

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
      <div class="replay-page">
        <header class="replay-toolbar">
          <${RecentFilesBanner} info=${persistence.bannerInfo}
                                 onDismiss=${persistence.dismissBanner} />
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
          ${cppBuilding && html`
            <span class="replay-status">
              building replay… ${cppProgress > 0
                ? Math.round(cppProgress * 100) + '%'
                : ''}
            </span>
            <progress class="replay-progress" max="1" value=${cppProgress}></progress>`}
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

          ${overlayVisible && m5State && html`
            <div class="replay-overlay">
              <div class="replay-overlay-frame ${Number.isFinite(pausedLogMs) ? 'paused' : ''}">
                ${(() => {
                  const M = M5_MODES.find(m => m.id === m5State.displayType);
                  const C = M ? M.C : EnergyMode;
                  return html`<${C} state=${m5State} stale=${false} />`;
                })()}
              </div>
            </div>
          `}
        </div>

        <footer class="replay-controls">
          <div class="replay-control-row">
            <span class="replay-label">Mode</span>
            ${M5_MODES.map(m => html`
                <button
                  class=${m.id === m5ModeId ? 'replay-mode-btn active' : 'replay-mode-btn'}
                  onClick=${() => setM5ModeId(m.id)}>${m.label}</button>
              `)}
            <span class="replay-spacer"></span>
            <span class="replay-toggle"
                  title="Render-side smoothing for the slip ball. Not firmware-faithful — a viewing aid for 50 Hz logs whose IMU samples carry aliased noise the airplane never showed at 208 Hz.">
              Smooth:
              ${PRESENTATION_PRESETS.map(p => html`
                <label style="margin-left:0.4em;">
                  <input type="radio" name="m5-smooth" value=${p.id}
                         checked=${m5SmoothPreset === p.id}
                         onChange=${() => setM5SmoothPreset(p.id)} />
                  ${p.label}
                </label>
              `)}
              ${cppBuilding
                ? html`<small>(pre-pass ${Math.round(cppProgress * 100)}%)</small>`
                : ''}
            </span>
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
      </div>`;
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
