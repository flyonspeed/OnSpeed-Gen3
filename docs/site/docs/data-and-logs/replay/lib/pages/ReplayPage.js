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

// URL-space relative path: see replay-entry.js header for why this is
// 4 `..` and not 7 (production deploys under /latest/; longer walks
// escape that prefix and 404).
import { html, useState, useEffect, useRef, useCallback, render }
  from '../../../../packages/ui-core/vendor/preact-standalone.js';
import { parseLog, findRowAt, detectLogSampleRate }
  from '../replay/parseLog.js';
import { parseConfigXml } from '../replay/config.js';
import { detectTakeoffWithKind, downsampleForPlot } from '../replay/syncDetect.js';
// downloadBlob lives in mp4Export.js (used by composite + overlay-only
// MP4 exports). The legacy WebM path was removed 2026-05-12 — see PR #533
// description for the rationale (MP4 export is source-faithful and
// audio-passthrough; WebM was a Phase-4 stopgap before the WebCodecs work
// landed).
import {
  exportClipAsMp4, isMp4ExportSupported,
  exportOverlayOnly, isOverlayExportSupported, OVERLAY_MODE_ORDER,
  downloadBlob,
} from '../replay/mp4Export.js';
import { ClipBuilder, buildClipFromPlayhead, buildClipFromMarkers, defaultClipLabel }
  from '../replay/clipBuilder.js';
import { findDataMarks, logMsToVideoSec } from '../replay/dataMarks.js';
import { reassembleResults } from '../replay/reassemble.js';
import { useReplayPersistence, RecentFilesBanner } from '../replay/persistence.js';
import {
  isFileHandleApiSupported, pickFile, storeHandles, clearHandles,
  requestPermissionForHandles, signatureFromFiles, useFileHandleResume,
  ReplayResumeBanner,
} from '../replay/fileHandles.js';
import { M5Sim } from '../replay/m5sim.js';
import { buildWireFramesFromTask } from '../replay/buildWireFrames.js';
import { PresentationFilter, PRESENTATION_PRESETS, defaultPresetForLogRate }
  from '../replay/presentationFilter.js';
import { getWasmCore } from '../replay/wasm_core.js';
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

// Avionics palette tokens used by the offscreen export render
// (mirrors :root in replay.css). Set both on the hidden mount
// node (so the live offscreen render resolves them) AND on each
// rendered <svg> element before XMLSerializer round-trips it
// through an <img> for the MP4 export — once the SVG is parsed
// as an isolated document inside an <img>, the mount-div is no
// longer a CSS ancestor and var() lookups against the page's
// .replay-page scope return empty.
const EXPORT_AVIONICS_VARS = Object.freeze({
  '--bg':           '#111',
  '--ink':          '#eee',
  '--panel-bg':     '#000',
  '--white':        '#ffffff',
  '--green':        '#00ff3a',
  '--yellow':       '#fffd40',
  '--red':          '#ff0018',
  '--grey':         '#888',
  '--dark-grey':    '#6b6d54',
  '--light-grey':   '#aaa',
  '--sky':          '#00fffe',
  '--ground':       '#954511',
  '--magenta':      '#ff00ff',
  '--orange':       '#ff8800',
  '--blue':         '#0000ff',
  '--font-numeric': "'B612', 'Helvetica Neue', Arial, sans-serif",
});

// Sync + clips persistence lives in replay/persistence.js
// (content-keyed by log digest). These are simpler prefs keyed by
// a fixed string.
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
  // Raw File handle, separate from logFilename. Used to compute the
  // content-keyed digest for sync + clip persistence (see
  // useReplayPersistence in ../replay/persistence.js).
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


  // Manual clip list. Each entry is { startMs, endMs, label }.
  // The ClipBuilder UI displays them as a stack of editable rows;
  // "Export all" iterates and produces one MP4 per entry.
  const [clips, setClips] = useState([]);

  // Persistence: stores sync + clips per log-content digest in
  // localStorage so a reload restores both when the pilot re-picks
  // the same log. Also drives the "last session" banner that
  // suggests re-picking the prior session's files. See
  // replay/persistence.js for the storage contract.
  const persistence = useReplayPersistence({ logFile });

  // File handles for FileSystemAccess-supported browsers (Chrome /
  // Edge desktop). Held in refs because they're not Preact state —
  // we only consult them when writing to IDB on a new full set, or
  // when the resume banner re-grants permission and reads files.
  // Falsy on Firefox / Safari (those fall through to <input>).
  const videoHandleRef = useRef(null);
  const logHandleRef = useRef(null);
  const cfgHandleRef = useRef(null);

  // Resume-on-reload state. Reads IDB for handles matching the
  // previously-recorded recent-files signature. resumeReady fires
  // only when the FSA API is supported AND a matching record exists.
  const fileHandleResume = useFileHandleResume({
    recentFilesSig: persistence.recentFilesSig,
  });

  // Live mirrors of the three File objects. Used by persistHandles
  // (which fires inside a state-setter and can't see the latest React
  // state). Updated alongside setVideoFile/setLogFile/setCfgFile.
  const videoFileRef = useRef(null);
  const logFileRef = useRef(null);
  const cfgFileRef = useRef(null);

  // Persist the current handle bundle whenever both video + log
  // handles exist. Mirrors persistence.notifyFilePicked's video+log
  // gating. The cfg handle is optional (null if pilot hasn't picked
  // one). Signature key derives from the live file metadata so
  // the next reload's resume lookup matches.
  //
  // Reads from refs (not React state) so it sees writes from the
  // current event handler synchronously — state-setter timing would
  // otherwise miss the just-picked file.
  const persistHandlesIfReady = useCallback(() => {
    if (!isFileHandleApiSupported()) return;
    const v = videoHandleRef.current;
    const l = logHandleRef.current;
    if (!v || !l) return;
    const sig = signatureFromFiles({
      video: videoFileRef.current,
      log: logFileRef.current,
      cfg: cfgFileRef.current,
    });
    if (!sig) return;
    storeHandles(sig, {
      video: v,
      log: l,
      cfg: cfgHandleRef.current || null,
    }).catch(() => { /* best-effort, surfaced via console only */ });
  }, []);

  // Mark-in flow: when the pilot clicks "Mark clip in" we stash the
  // current video time; the next "Mark clip out" click completes the
  // clip. Cleared on cancel / completion.
  const [pendingInVideoSec, setPendingInVideoSec] = useState(null);

  // MP4-export state. exportingClipIdx is the index of the currently-
  // exporting clip (so its row in the ClipBuilder can show progress
  // in place of the Export button); null when idle. The AbortController
  // lets the Cancel button stop the encoder cleanly.
  const [exportingClipIdx, setExportingClipIdx] = useState(null);
  const [mp4ExportProgress, setMp4ExportProgress] = useState(0);
  const [mp4ExportLabel, setMp4ExportLabel] = useState('');
  const mp4AbortRef = useRef(null);
  // Feature-detect WebCodecs once at mount. Capture as a state value
  // so we re-render the gate the first time the env settles.
  const [mp4Available] = useState(() => isMp4ExportSupported());

  // Overlay-only export state. Tracks the batch (5-mode pass) progress
  // — totalFrames is per-mode, modesDone counts modes finalized so
  // the UI can show "indexer 47% · attitude pending · ..." in a
  // single status line. Separate from mp4ExportProgress so the two
  // export paths don't contend over the same progress widget.
  const [overlayExporting, setOverlayExporting] = useState(false);
  const [overlayCurrentMode, setOverlayCurrentMode] = useState(null);
  const [overlayProgress, setOverlayProgress] = useState(0);
  const overlayAbortRef = useRef(null);
  const [overlayAvailable] = useState(() => isOverlayExportSupported());

  // Which M5 modes to include in the overlay export. The default is
  // ['indexer'] — the most-used mode and the one Vac asks for first.
  // Pilots tick additional modes via checkboxes in the ClipBuilder UI.
  // Persisted across reloads via localStorage so the choice sticks.
  const [selectedOverlayModes, setSelectedOverlayModes] = useState(() => {
    const s = safeLsGet('replay-overlay-modes-v1');
    if (!s) return ['indexer'];
    try {
      const parsed = JSON.parse(s);
      if (Array.isArray(parsed) && parsed.every(m => OVERLAY_MODE_ORDER.includes(m))) {
        return parsed.length > 0 ? parsed : ['indexer'];
      }
    } catch { /* fall through to default */ }
    return ['indexer'];
  });
  useEffect(() => {
    safeLsSet('replay-overlay-modes-v1', JSON.stringify(selectedOverlayModes));
  }, [selectedOverlayModes]);

  // Overlay size — fraction of source video width, or 'native' for the
  // M5's 320×240 pixel grid. NLEs (especially iMovie) handle drag-on-
  // top sanely when the overlay is already at a useful display size;
  // 0.2 (20% of a 4K width = 768×576) is the iMovie sweet spot.
  // Persisted so the choice sticks.
  const [overlaySize, setOverlaySize] = useState(() => {
    const s = safeLsGet('replay-overlay-size-v1');
    const valid = ['native', '0.2', '0.3', '0.5'];
    return valid.includes(s) ? s : '0.2';
  });
  useEffect(() => {
    safeLsSet('replay-overlay-size-v1', overlaySize);
  }, [overlaySize]);

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
  // Tracks whether an MP4 export is in progress. The live-preview
  // rVFC chain reads this each tick and suspends its self-
  // re-registration loop while true: the export pipeline registers
  // its own rVFC against the same <video> element, and on a paused
  // video only one composite fires per seek — letting the live tick
  // grab it would steal the frame the export is waiting for.
  // Resumed on the next videoUrl-effect run by the explicit kick at
  // the end of `exportClipMp4`.
  const mp4ExportingRef = useRef(false);

  // ---------- File loaders -----------------------------------------
  //
  // Each slot has an `apply*File(file, handle)` helper that does the
  // state mutations + persistence-notify work. The handle is optional
  // (null on Firefox/Safari, or in the <input> fallback path). The
  // helpers are called from three places:
  //   1. `<input type="file">` onChange events  (handle = null)
  //   2. FSA `showOpenFilePicker` buttons       (handle = FSFileHandle)
  //   3. Resume-banner re-grant path            (handle from IDB)

  const applyVideoFile = useCallback((f, handle) => {
    if (videoUrl) URL.revokeObjectURL(videoUrl);
    setVideoFile(f);
    setVideoUrl(URL.createObjectURL(f));
    videoFileRef.current = f;
    persistence.notifyFilePicked('video', f);
    videoHandleRef.current = handle || null;
    if (handle) persistHandlesIfReady();
  }, [videoUrl, persistence, persistHandlesIfReady]);

  const applyLogFile = useCallback(async (f, handle) => {
    setParseErr(null);
    try {
      const text = await f.text();
      const parsed = parseLog(text);
      if (parsed.Length === 0) throw new Error('no rows');
      // Synchronously detach the persistence hook from the prior
      // log's digest before any state-driven persist effect fires.
      // Without this the persist-on-change effects for clips/sync
      // (which run after the same render that batches the resets
      // below) would write the new []/null state into the PRIOR
      // log's localStorage key, since the storeClips/storeSync
      // callbacks captured the old logDigest. beginLogSwap clears
      // the synchronous ref those writers consult.
      persistence.beginLogSwap();
      setLog(parsed);
      setLogFile(f);
      setLogFilename(f.name);
      setClips([]);
      setSync(null);
      setAnchorKind('none');
      setPausedLogMs(null);
      setPendingInVideoSec(null);
      logFileRef.current = f;
      persistence.notifyFilePicked('log', f);
      if (handle) {
        logHandleRef.current = handle;
        persistHandlesIfReady();
      }
      return true;
    } catch (err) {
      setParseErr(`Could not parse log: ${err.message}`);
      setLog(null);
      return false;
    }
  }, [persistence, persistHandlesIfReady]);

  const applyCfgFile = useCallback(async (f, handle) => {
    setParseErr(null);
    try {
      const text = await f.text();
      const parsed = await parseConfigXml(text);
      if (!parsed.flaps || !parsed.flaps.length) throw new Error('no flap detents in config');
      setCfg(parsed);
      setCfgFile(f);
      setCfgFilename(f.name);
      cfgFileRef.current = f;
      persistence.notifyFilePicked('cfg', f);
      if (handle) {
        cfgHandleRef.current = handle;
        persistHandlesIfReady();
      }
      return true;
    } catch (err) {
      setParseErr(`Could not parse config: ${err.message}`);
      setCfg(null);
      return false;
    }
  }, [persistence, persistHandlesIfReady]);

  // <input type="file"> onChange handlers — used on Firefox / Safari
  // and as a fallback if the FSA picker fails. The handle ref stays
  // null in this path so no IDB write happens; resume banner won't
  // appear next reload (correct behavior on unsupported browsers).
  const onVideoPick = (e) => {
    const f = e.target.files?.[0];
    if (!f) return;
    applyVideoFile(f, null);
  };

  const onLogPick = async (e) => {
    const f = e.target.files?.[0];
    if (!f) return;
    await applyLogFile(f);
  };

  const onCfgPick = async (e) => {
    const f = e.target.files?.[0];
    if (!f) return;
    await applyCfgFile(f, null);
  };

  // FSA picker handlers — used on Chrome / Edge. Each opens the OS
  // file dialog via showOpenFilePicker, retrieves a FileSystemFileHandle
  // for re-grant on next reload, then delegates to the same apply*
  // helpers the <input> path uses.
  const fsaSupported = useState(() => isFileHandleApiSupported())[0];

  const pickVideoViaFsa = useCallback(async () => {
    try {
      const r = await pickFile('video');
      if (r) applyVideoFile(r.file, r.handle);
    } catch (err) {
      setParseErr(`Could not open video: ${err.message}`);
    }
  }, [applyVideoFile]);

  const pickLogViaFsa = useCallback(async () => {
    try {
      const r = await pickFile('log');
      if (r) await applyLogFile(r.file, r.handle);
    } catch (err) {
      setParseErr(`Could not open log: ${err.message}`);
    }
  }, [applyLogFile]);

  const pickCfgViaFsa = useCallback(async () => {
    try {
      const r = await pickFile('cfg');
      if (r) await applyCfgFile(r.file, r.handle);
    } catch (err) {
      setParseErr(`Could not open config: ${err.message}`);
    }
  }, [applyCfgFile]);

  // Resume-banner click handler. MUST run the permission request
  // synchronously in the same user-gesture tick — no awaits before
  // requestPermissionForHandles or browsers reject the prompt.
  // After grant, getFile() each handle and re-feed the apply* helpers.
  const onResumeClick = useCallback(async () => {
    const handles = fileHandleResume.availableHandles;
    if (!handles) return;
    // Permission request first — same gesture. Browsers batch the
    // prompts when called sequentially in this tick.
    const granted = await requestPermissionForHandles(handles);
    if (!granted) {
      setParseErr('Permission denied for one or more files. Pick them manually.');
      return;
    }
    try {
      // Read the three files in parallel.
      const [vFile, lFile, cFile] = await Promise.all([
        handles.video.getFile(),
        handles.log.getFile(),
        handles.cfg ? handles.cfg.getFile() : Promise.resolve(null),
      ]);
      applyVideoFile(vFile, handles.video);
      const logOk = await applyLogFile(lFile, handles.log);
      if (cFile && logOk) await applyCfgFile(cFile, handles.cfg);
      fileHandleResume.markUsed();
    } catch (err) {
      setParseErr(`Resume failed: ${err.message}`);
    }
  }, [fileHandleResume, applyVideoFile, applyLogFile, applyCfgFile]);

  const onResumeDismiss = useCallback(() => {
    fileHandleResume.dismiss();
  }, [fileHandleResume]);

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
      // is null (overlay shows nothing). Carry anchorKind inside the
      // sync object so it round-trips through localStorage (the
      // persistence layer only stores fields it sees on sync).
      setSync(prev => prev ?? { logTakeoffMs: log.timeStamp[tRow], videoTakeoffSec: null, anchorKind: kind });
    }
  }, [log, persistence.digestReady, persistence.storedSync]);

  // Persist sync on every change. Gated on digestReady so writes
  // don't fire while the digest is being recomputed between log
  // swaps (in that window storeSync's logDigest still refers to the
  // PREVIOUS log, so a write would corrupt the prior log's sync key).
  useEffect(() => {
    if (!persistence.digestReady) return;
    persistence.storeSync(sync);
  }, [sync, persistence.digestReady, persistence.storeSync]);

  // Restore persisted clips when the log digest resolves. Only seed
  // if the in-memory clip list is empty — never clobber clips the
  // user added before the digest came back.
  useEffect(() => {
    if (!persistence.digestReady) return;
    if (!persistence.storedClips || !persistence.storedClips.length) return;
    setClips(prev => prev.length === 0 ? persistence.storedClips : prev);
  }, [persistence.digestReady, persistence.storedClips]);

  // Persist clips on every change. Gated on digestReady — see the
  // matching note on the sync persist effect above. Without this
  // gate, picking a new log corrupts the previous log's clips key
  // before the new digest resolves.
  useEffect(() => {
    if (!persistence.digestReady) return;
    persistence.storeClips(clips);
  }, [clips, persistence.digestReady, persistence.storeClips]);

  // ---------- Video clock ------------------------------------------

  // Drive a render-tick on every video frame. requestVideoFrameCallback
  // is the right primitive (frame-accurate, doesn't fire on a paused
  // video, gives us the true video time). Safari + older browsers
  // fall back to RAF, which is close enough for live preview — for
  // export-quality we'll switch to Remotion (Phase 5).
  // Bumping this nonce restarts the live-preview rVFC chain. We use
  // it to resume the chain after an MP4 export finishes — the chain
  // self-suspends mid-export (see comment on mp4ExportingRef).
  const [livePreviewNonce, setLivePreviewNonce] = useState(0);
  useEffect(() => {
    const v = videoRef.current;
    if (!v) return;
    let cancelled = false;
    const useRvfc = typeof v.requestVideoFrameCallback === 'function';

    const tick = (now, meta) => {
      if (cancelled) return;
      // Suspend during MP4 export: the export pipeline drives the
      // same video element with its own rVFC, and competing callbacks
      // on a paused video starve the export. Re-armed by bumping
      // livePreviewNonce when the export completes.
      if (mp4ExportingRef.current) return;
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
  }, [videoUrl, livePreviewNonce]);

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
        // frame after init renders the right mode. Read from the ref
        // so the async then-callback sees the CURRENT mode rather
        // than the value captured at effect-fire time — the mode can
        // change while the WASM module loads (toggle pressed during
        // a sim-reinit-nonce bump), and using a stale closure value
        // would render the wrong mode for the first frame.
        sim.setMode(m5ModeIdRef.current);
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
      anchorKind:      prev?.anchorKind ?? anchorKind,
    }));
  }, [anchorKind]);

  const reMarkLogTakeoff = useCallback(() => {
    if (!log) return;
    const { row: tRow, kind } = detectTakeoffWithKind(log);
    setAnchorKind(kind);
    if (tRow >= 0) {
      setSync(prev => ({
        logTakeoffMs:    log.timeStamp[tRow],
        videoTakeoffSec: prev?.videoTakeoffSec ?? null,
        anchorKind:      kind,
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
      anchorKind,
    });
    setPausedLogMs(null);
  }, [pausedLogMs, anchorKind]);

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

  // Add an N-second clip starting at a DataMark. Same behavior as
  // "+ N s clip from playhead" — the clip lands in the clip list,
  // pilot edits / exports per-row from there. Does NOT auto-export
  // (the legacy WebM path used to; that was wrong + WebM is gone).
  const addClipFromMark = (mark, durationSec) => {
    const v = videoRef.current;
    if (!v) return;
    const startSec = logMsToVideoSec(mark.logTimeMs, sync);
    if (!Number.isFinite(startSec)) return;
    const label = `mark ${mark.label} +${durationSec}s`;
    const clip = buildClipFromPlayhead(startSec, durationSec, sync, label);
    if (clip) setClips(prev => [...prev, clip]);
  };

  // Add the current playhead as a clip start, with a default
  // 30-second window. Pilot can edit either edge in the row.
  const addClipFromPlayhead = (durationSec = 30) => {
    const v = videoRef.current;
    if (!v) return;
    const label = defaultClipLabel(clips.length);
    const clip = buildClipFromPlayhead(v.currentTime, durationSec, sync, label);
    if (clip) setClips(prev => [...prev, clip]);
  };

  // Mark-in / mark-out flow. The first click stashes the current
  // video time as the pending in-point. The second click reads the
  // current video time as the out-point and appends a clip.
  const markClipIn = useCallback(() => {
    const v = videoRef.current;
    if (!v) return;
    setPendingInVideoSec(v.currentTime);
  }, []);

  const markClipOut = useCallback(() => {
    const v = videoRef.current;
    if (!v || pendingInVideoSec == null) return;
    const label = defaultClipLabel(clips.length);
    const clip = buildClipFromMarkers(pendingInVideoSec, v.currentTime, sync, label);
    if (clip) {
      setClips(prev => [...prev, clip]);
      setPendingInVideoSec(null);
    }
  }, [pendingInVideoSec, clips, sync]);

  const cancelClipMark = useCallback(() => setPendingInVideoSec(null), []);

  // Scrub the live video to a particular time (in seconds). Used by
  // ClipBuilder rows.
  const scrubVideoTo = useCallback((videoSec) => {
    const v = videoRef.current;
    if (!v || !Number.isFinite(videoSec)) return;
    v.currentTime = Math.max(0, Math.min(v.duration || videoSec, videoSec));
  }, []);

  // Render the export's current-frame overlay SVG.
  //
  // The export driver in mp4Export.js runs its own M5Sim independent
  // of the page's live sim — the page's live sim is pinned to whatever
  // playhead position the pilot left it at, while the export needs a
  // deterministic per-frame state derived from the clip-start virtual
  // time. So the export passes in the per-frame m5State and asks us
  // to produce an SVG element from it.
  //
  // We render through Preact into a hidden mount node whose SVG child
  // we then hand back. The render is synchronous (Preact-standalone
  // rAF-batches by default; we toggle a synchronous mode by mounting
  // into a detached node with no scheduler). The mount survives
  // across export-frames so the SVG element identity is stable and
  // the XMLSerializer round-trip stays cheap.
  // Hidden offscreen mount where the export pipeline renders the
  // per-frame overlay SVG. Created once on mount; torn down on
  // unmount.
  //
  // The avionics CSS variables (--panel-bg, --white, --green, ...)
  // are inline-set on the mount node so the SVG components in
  // packages/ui-core/ resolve them the same way they do inside
  // .replay-page. Without this, every fill resolves to transparent
  // and the burned-in overlay looks like a wireframe.
  const exportOverlayMountRef = useRef(null);
  useEffect(() => {
    if (typeof document === 'undefined') return;
    const div = document.createElement('div');
    div.setAttribute('data-replay-export-overlay', '');
    div.style.cssText = 'position:absolute;left:-99999px;top:0;width:640px;height:480px;visibility:hidden;pointer-events:none;';
    for (const [k, v] of Object.entries(EXPORT_AVIONICS_VARS)) {
      div.style.setProperty(k, v);
    }
    document.body.appendChild(div);
    exportOverlayMountRef.current = div;
    return () => {
      if (div.parentNode) div.parentNode.removeChild(div);
      exportOverlayMountRef.current = null;
    };
  }, []);

  // renderOverlaySvg signature: (m5State, displayTypeOverride?) → SVGElement.
  // The override path is used by the overlay-only export, which iterates
  // all five modes per frame; it spreads m5State with displayType
  // overridden so the SVG renders THAT mode regardless of which mode
  // the live sim is in. Without an override, m5State.displayType wins
  // (the source-composite export path).
  const renderOverlayForExport = useCallback((m5State, displayTypeOverride) => {
    const mount = exportOverlayMountRef.current;
    if (!mount || !m5State) return null;
    const targetMode = Number.isFinite(displayTypeOverride)
      ? displayTypeOverride : m5State.displayType;
    const M = M5_MODES.find(m => m.id === targetMode) || M5_MODES[0];
    const C = M.C;
    // For the mode-override path the SVG component still reads
    // state.displayType (some modes branch on it internally), so
    // synthesize a state with the override stamped on.
    const stateForRender = (targetMode === m5State.displayType)
      ? m5State
      : Object.freeze({ ...m5State, displayType: targetMode });
    m5State = stateForRender;
    // Render synchronously via Preact's render into our detached
    // mount. Subsequent calls reuse the same DOM tree (diff path),
    // which is much cheaper than a fresh mount per frame.
    render(html`<${C} state=${m5State} stale=${false} />`, mount);
    const svg = mount.querySelector('svg');
    if (!svg) return null;
    // Inline the avionics palette onto the SVG element itself.
    // The mount-div carries the same vars so the live offscreen
    // render resolves them, but the export pipeline serializes
    // the SVG via XMLSerializer and loads it into an <img>. That
    // <img> parses the SVG as an isolated document where the
    // mount-div is no longer a CSS ancestor, so any var() in the
    // SVG's own style attribute (e.g. `background: var(--panel-bg)`
    // on the <svg> root) resolves to nothing → transparent
    // background. Setting the vars on the SVG element makes them
    // available within the isolated document.
    for (const [k, v] of Object.entries(EXPORT_AVIONICS_VARS)) {
      svg.style.setProperty(k, v);
    }
    return svg;
  }, []);

  // Export one clip as MP4. Returns a Blob promise; the page also
  // wires progress + abort state.
  const exportClipMp4 = useCallback(async (clip, idx) => {
    const v = videoRef.current;
    if (!v) return null;
    if (!syncReady) {
      setParseErr('Export failed: sync anchor not set.');
      return null;
    }
    if (!cppWireFrames) {
      setParseErr('Export failed: replay engine pre-pass not complete yet.');
      return null;
    }
    if (!mp4Available) {
      setParseErr('Export requires Chrome or Edge desktop. WebCodecs ' +
                  'support is incomplete in Safari / Firefox.');
      return null;
    }
    const controller = new AbortController();
    mp4AbortRef.current = controller;
    setExportingClipIdx(idx);
    setMp4ExportProgress(0);
    setMp4ExportLabel(clip.label || `clip ${idx + 1}`);
    setParseErr(null);
    // Suspend the live-preview rVFC chain. See mp4ExportingRef comment;
    // resumed in the finally{} below by bumping livePreviewNonce.
    mp4ExportingRef.current = true;

    // Mirror the live preview's render-side smoothing in the export.
    // Without this the slip ball jitters at 50 Hz log replay aliasing —
    // looks structurally different from what the pilot sees on the
    // live page. See presentationFilter.js for the τ rationale.
    const preset = PRESENTATION_PRESETS.find(p => p.id === m5SmoothPreset)
                   || null;
    const presentationTau = preset
      ? { lateralSec: preset.lateralSec, verticalSec: preset.verticalSec }
      : null;

    try {
      const blob = await exportClipAsMp4({
        videoEl:        v,
        clip,
        sync,
        log,
        cppWireFrames,
        renderOverlaySvg: renderOverlayForExport,
        // Source file for AAC audio decode. Without it the MP4 is silent.
        sourceFile:    videoFile,
        // Match the live preview's slip-ball smoothing.
        presentationTau,
        // Match the live preview's M5 mode (Energy/Attitude/Indexer/...).
        // Without this the fresh export-sim defaults to mode 0 (Energy)
        // regardless of what the page is showing.
        displayMode:   m5ModeId,
        // outputWidth omitted: export defaults to source resolution +
        // source framerate + source codec family for a "source video
        // with overlay added" result.
        onProgress: ({ frame, totalFrames }) => {
          if (totalFrames > 0) setMp4ExportProgress(frame / totalFrames);
        },
        signal: controller.signal,
      });
      return blob;
    } catch (err) {
      if (err?.name === 'AbortError') {
        // Cancel is a normal exit; surface a short status, not a
        // red banner.
        setParseErr(null);
      } else {
        setParseErr('MP4 export failed: ' + (err?.message || err));
      }
      return null;
    } finally {
      mp4AbortRef.current = null;
      setExportingClipIdx(null);
      setMp4ExportProgress(0);
      setMp4ExportLabel('');
      // Resume the live-preview rVFC chain.
      mp4ExportingRef.current = false;
      setLivePreviewNonce(n => n + 1);
    }
  }, [syncReady, sync, log, cppWireFrames, mp4Available, renderOverlayForExport,
      videoFile, m5SmoothPreset, m5ModeId]);

  const exportClipMp4AndDownload = useCallback(async (clip, idx) => {
    const blob = await exportClipMp4(clip, idx);
    if (!blob) return;
    const base = (videoFile?.name || 'flight').replace(/\.[^.]+$/, '');
    const suffix = (clip.label || `clip${idx + 1}`).replace(/[^a-z0-9_-]/gi, '_');
    downloadBlob(blob, `${base}_${suffix}.mp4`);
  }, [exportClipMp4, videoFile]);

  // Track batch-cancel separately from per-export cancel so a Cancel
  // click during "Export all" stops the whole sequence, not just the
  // current clip.
  const batchCancelledRef = useRef(false);
  const exportAllClipsMp4 = useCallback(async () => {
    batchCancelledRef.current = false;
    for (let i = 0; i < clips.length; i++) {
      if (batchCancelledRef.current) break;
      // eslint-disable-next-line no-await-in-loop
      const blob = await exportClipMp4(clips[i], i);
      if (blob) {
        const base = (videoFile?.name || 'flight').replace(/\.[^.]+$/, '');
        const suffix = (clips[i].label || `clip${i + 1}`)
                         .replace(/[^a-z0-9_-]/gi, '_');
        downloadBlob(blob, `${base}_${suffix}.mp4`);
      } else if (batchCancelledRef.current) {
        // Cancelled mid-clip — fall through and exit the loop.
        break;
      }
      // If the export returned null for any other reason (parse error,
      // engine pre-pass not ready), bail out of the batch rather than
      // bombarding the user with serialised error banners.
      if (!blob && !batchCancelledRef.current) break;
    }
    batchCancelledRef.current = false;
  }, [clips, exportClipMp4, videoFile]);

  const cancelMp4Export = useCallback(() => {
    // Mark batch-cancelled so a running "Export all" stops after the
    // current clip aborts. Single-clip exports also abort cleanly.
    batchCancelledRef.current = true;
    if (mp4AbortRef.current) mp4AbortRef.current.abort();
  }, []);

  // Export overlay-only MP4s — one per M5 mode, NLE-ready with a
  // chroma-key background. Single pass through the sim, parallel
  // encoders. Pilots composite onto GoPro footage in iMovie / Final
  // Cut / Premiere by adding a chroma-key effect.
  const exportOverlaysForClip = useCallback(async (clip, idx) => {
    if (!syncReady) {
      setParseErr('Overlay export failed: sync anchor not set.');
      return null;
    }
    if (!cppWireFrames) {
      setParseErr('Overlay export failed: replay engine pre-pass not complete yet.');
      return null;
    }
    if (!overlayAvailable) {
      setParseErr('Overlay export requires Chrome or Edge desktop. WebCodecs ' +
                  'support is incomplete in Safari / Firefox.');
      return null;
    }
    const preset = PRESENTATION_PRESETS.find(p => p.id === m5SmoothPreset) || null;
    const presentationTau = preset
      ? { lateralSec: preset.lateralSec, verticalSec: preset.verticalSec }
      : null;

    // Resolve output dimensions. 'native' = M5 panel pixel grid
    // (320×240, the export module's default when outputWidth is null).
    // Numeric fractions = that proportion of the source video's width,
    // preserving the M5 panel's 4:3 aspect. Falls back to native if
    // the source resolution isn't known yet.
    const v = videoRef.current;
    const srcW = v && v.videoWidth > 0 ? v.videoWidth : 0;
    let overlayW = null;
    let overlayH = null;
    if (overlaySize !== 'native' && srcW > 0) {
      const frac = parseFloat(overlaySize);
      if (Number.isFinite(frac) && frac > 0) {
        // Round to multiples of 2 for the encoder; keep 4:3 aspect
        // (M5 panel is 320×240 = 4:3).
        overlayW = Math.max(2, Math.round(srcW * frac / 2) * 2);
        overlayH = Math.max(2, Math.round(overlayW * 3 / 4 / 2) * 2);
      }
    }

    const controller = new AbortController();
    overlayAbortRef.current = controller;
    setOverlayExporting(true);
    setOverlayCurrentMode(null);
    setOverlayProgress(0);
    setParseErr(null);

    try {
      // Native-dimensions export: output is the M5 panel as a video at
      // its native 320×240 pixel grid against the M5's own black panel
      // background. Vac drops the file into his NLE, positions and
      // scales the M5 widget wherever he wants on top of his footage.
      // No chroma key, no padding, no transparency — the output is
      // just "what the M5 displays" frame-by-frame.
      //
      // Mode picker: selectedOverlayModes filters the export to only
      // the modes the user checked. Falls back to indexer-only if none
      // are selected (defensive — UI shouldn't allow that state).
      const requestedModes = selectedOverlayModes && selectedOverlayModes.length > 0
        ? selectedOverlayModes
        : ['indexer'];
      const blobs = await exportOverlayOnly({
        clip,
        sync,
        log,
        cppWireFrames,
        renderOverlaySvg: renderOverlayForExport,
        modes:           requestedModes,
        presentationTau,
        // null/null falls through to M5 native 320×240; numeric values
        // pre-scale the overlay to a fraction of source-video width so
        // NLEs that auto-scale drop-on-top layers (iMovie's biggest
        // gotcha) put the overlay at a sensible size automatically.
        outputWidth:  overlayW,
        outputHeight: overlayH,
        // framerate, bitrate, background default to sensible values
        // (30fps, ~150 kbps@native scaling up with resolution, #000).
        onProgress: ({ mode, frame, totalFrames, modeCount }) => {
          // Aggregate report (no `mode`): multi-mode batch — show a
          // count label, not a per-mode name flickering 5 times/frame.
          // Per-mode report: single-mode export, label = the mode.
          if (mode !== undefined) {
            setOverlayCurrentMode(mode);
          } else if (modeCount > 0) {
            setOverlayCurrentMode(
              modeCount === 1
                ? (requestedModes[0] || 'overlay')
                : `${modeCount} modes`);
          }
          if (totalFrames > 0) setOverlayProgress(frame / totalFrames);
        },
        signal: controller.signal,
      });
      // Trigger one download per mode. Filename pattern:
      //   <video-basename>_<clip-label>_<mode>.mp4
      const base = (videoFile?.name || 'flight').replace(/\.[^.]+$/, '');
      const suffix = (clip.label || `clip${(idx ?? 0) + 1}`)
                       .replace(/[^a-z0-9_-]/gi, '_');
      for (const [modeId, blob] of blobs) {
        downloadBlob(blob, `${base}_${suffix}_${modeId}.mp4`);
      }
      return blobs;
    } catch (err) {
      if (err?.name === 'AbortError') {
        setParseErr(null);
      } else {
        setParseErr('Overlay export failed: ' + (err?.message || err));
      }
      return null;
    } finally {
      overlayAbortRef.current = null;
      setOverlayExporting(false);
      setOverlayCurrentMode(null);
      setOverlayProgress(0);
    }
  }, [syncReady, sync, log, cppWireFrames, overlayAvailable,
      renderOverlayForExport, videoFile, m5SmoothPreset,
      selectedOverlayModes, overlaySize]);

  const cancelOverlayExport = useCallback(() => {
    if (overlayAbortRef.current) overlayAbortRef.current.abort();
  }, []);

  // Export the overlay for the ENTIRE log, no source video. Useful when:
  //   (a) the GoPro started recording mid-flight and the pilot wants
  //       overlay coverage of the boot-up / taxi / pre-roll log time;
  //   (b) the pilot wants one big overlay MP4 to drop on his original
  //       source footage in iMovie/FCP without having to mark a clip.
  // Synthesizes a full-log clip {startMs, endMs} and calls the existing
  // overlay-only path. No source video is read (overlay path doesn't
  // touch source). Output is 320×240 (native) by default; the size
  // selector still applies if the user picked a fraction-of-source.
  const exportFullLogOverlay = useCallback(async () => {
    if (!log || !cppWireFrames) {
      setParseErr('Full overlay export needs a loaded log + completed replay engine pre-pass.');
      return null;
    }
    if (!overlayAvailable) {
      setParseErr('Overlay export requires Chrome or Edge desktop. WebCodecs ' +
                  'support is incomplete in Safari / Firefox.');
      return null;
    }
    // Build a synthetic clip spanning the entire log. The overlay-only
    // path reads clip.startMs and clip.endMs in log-time; no sync
    // anchor is consulted, so this works even when sync is null.
    const startMs = log.timeStamp[0];
    const endMs   = log.timeStamp[log.timeStamp.length - 1];
    if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs) {
      setParseErr('Full overlay export skipped: log has no usable timestamp range.');
      return null;
    }
    const clip = {
      startMs,
      endMs,
      label: 'full-log',
    };
    // Reuse the per-clip overlay export. exportOverlaysForClip already
    // handles cancel, progress, mode selection, size selector — we just
    // hand it a clip with a longer window.
    return await exportOverlaysForClip(clip, null);
  }, [log, cppWireFrames, overlayAvailable, exportOverlaysForClip]);

  // ---------- Layout -----------------------------------------------

  return html`
      <div class="replay-page">
        ${fileHandleResume.resumeReady
          ? html`<${ReplayResumeBanner}
                    info=${persistence.rawBannerInfo}
                    onResume=${onResumeClick}
                    onDismiss=${onResumeDismiss} />`
          : html`<${RecentFilesBanner} info=${persistence.bannerInfo}
                                       onDismiss=${persistence.dismissBanner} />`}
        <header class="replay-toolbar">
          ${fsaSupported ? html`
            <label class="replay-file">
              <span>Video</span>
              <button class="replay-file-btn" type="button"
                      onClick=${pickVideoViaFsa}
                      title=${videoFile ? videoFile.name : 'Open video'}>
                ${videoFile ? videoFile.name : 'Open video…'}
              </button>
            </label>
            <label class="replay-file">
              <span>Log</span>
              <button class="replay-file-btn" type="button"
                      onClick=${pickLogViaFsa}
                      title=${logFilename || 'Open log'}>
                ${logFilename || 'Open log…'}
              </button>
            </label>
            <label class="replay-file">
              <span>Config</span>
              <button class="replay-file-btn" type="button"
                      onClick=${pickCfgViaFsa}
                      title=${cfgFilename || 'Open config'}>
                ${cfgFilename || 'Open config…'}
              </button>
            </label>
          ` : html`
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
          `}
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
            ${exportingClipIdx != null
              ? html`
                  <span class="replay-status">
                    MP4 export${mp4ExportLabel ? ` · ${mp4ExportLabel}` : ''}
                  </span>
                  <progress class="replay-progress"
                            max="1" value=${mp4ExportProgress}></progress>
                  <span class="replay-status">
                    ${Math.round(mp4ExportProgress * 100)}%
                  </span>
                  <button class="replay-btn-ghost" onClick=${cancelMp4Export}>
                    Cancel
                  </button>`
              : html`
                  <button class="replay-btn" onClick=${exportFullLogOverlay}
                          disabled=${!log || !cppWireFrames || overlayExporting}
                          title="Render the overlay for the entire log (no source video). Drop on top of your GoPro footage in iMovie / Final Cut.">
                    Full overlay export
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
                            anchorKind:      prev?.anchorKind ?? anchorKind,
                          }))}
                          onSeekVideo=${(tSec) => {
                            const v = videoRef.current;
                            if (v) v.currentTime = Math.max(0, tSec);
                          }} />

          ${marks.length > 0 && html`
            <${DataMarkPanel}
                marks=${marks}
                sync=${sync}
                disabled=${exportingClipIdx != null || !syncReady}
                videoDuration=${videoRef.current?.duration}
                onJump=${jumpToMark}
                onClip=${addClipFromMark} />`}

          <${ClipBuilder}
              clips=${clips}
              setClips=${setClips}
              sync=${sync}
              syncReady=${syncReady}
              videoEl=${videoRef}
              disabled=${exportingClipIdx != null}
              exportingClipIdx=${exportingClipIdx}
              exportProgress=${mp4ExportProgress}
              exportLabel=${mp4ExportLabel}
              mp4Available=${mp4Available}
              mp4UnavailableTooltip=${'Export requires Chrome or Edge ' +
                'desktop. WebCodecs support is incomplete in Safari/Firefox.'}
              pendingInVideoSec=${pendingInVideoSec}
              onMarkIn=${markClipIn}
              onMarkOut=${markClipOut}
              onCancelMark=${cancelClipMark}
              onAddQuick=${addClipFromPlayhead}
              onScrubTo=${scrubVideoTo}
              onExport=${exportClipMp4AndDownload}
              onExportAll=${exportAllClipsMp4}
              onCancel=${cancelMp4Export}
              onExportOverlays=${exportOverlaysForClip}
              onCancelOverlays=${cancelOverlayExport}
              overlayExporting=${overlayExporting}
              overlayCurrentMode=${overlayCurrentMode}
              overlayProgress=${overlayProgress}
              overlayAvailable=${overlayAvailable}
              selectedOverlayModes=${selectedOverlayModes}
              onChangeOverlayModes=${setSelectedOverlayModes}
              overlayModeOrder=${OVERLAY_MODE_ORDER}
              overlaySize=${overlaySize}
              onChangeOverlaySize=${setOverlaySize} />
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
