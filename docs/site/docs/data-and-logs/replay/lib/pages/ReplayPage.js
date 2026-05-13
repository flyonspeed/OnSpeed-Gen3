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
import { detectRotation, downsampleForPlot } from '../replay/syncDetect.js';
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
import { ClipBuilder, buildClipFromPlayhead, buildClipFromMarkers,
         buildClipFromMarkToNextMark, defaultClipLabel, newClipId }
  from '../replay/clipBuilder.js';
import { findDataMarks, logMsToVideoSec } from '../replay/dataMarks.js';
import { useReplayJournal, markKey } from '../replay/journal.js';
import { DataMarkPanel } from '../components/DataMarkPanel.js';
import { reassembleResults } from '../replay/reassemble.js';
import { useReplayPersistence, RecentFilesBanner } from '../replay/persistence.js';
import {
  isFileHandleApiSupported, pickFile, storeHandles, clearHandles,
  requestPermissionForHandles, signatureFromFiles, useFileHandleResume,
  expandMultiChapterHandle, ReplayResumeBanner,
} from '../replay/fileHandles.js';
import { M5Sim } from '../replay/m5sim.js';
import { buildWireFramesFromTask } from '../replay/buildWireFrames.js';
import { PresentationFilter,
         PRESENTATION_LATERAL_TAU_MIN, PRESENTATION_LATERAL_TAU_MAX,
         PRESENTATION_LATERAL_TAU_STEP, defaultLateralTauForLogRate }
  from '../replay/presentationFilter.js';
import { getWasmCore } from '../replay/wasm_core.js';
import {
  EnergyMode, AttitudeMode, IndexerMode, DecelMode, HistoricGMode,
} from '../../../../packages/ui-core/components/svg/m5modes/index.js';
import { HudOverlay }
  from '../../../../packages/ui-core/components/svg/HudOverlay.js';
import {
  detectGoProChapterPattern, groupChapterSiblings, buildChapterTimeline,
  globalToLocal, describeChapterPick,
} from '../replay/chapters.js';

// Mode list indexed by displayType (0..4). The int returned by
// `m5sim.read().displayType` maps directly to the renderer. Ordering
// matches the M5 firmware's `kModeNames` and the IndexerPage's MODES
// — same five modes, same numbering. The page exposes TWO independent
// inset slots (Left + Right); each slot can be off or any of these
// modes. The MP4 export drives the slots via `leftMode` / `rightMode`
// parameters on exportClipAsMp4.
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
//
// Independent left + right inset slots replace the old single-mode +
// "Standard" preset. Each slot persists independently: empty string =
// slot off, "0".."4" = the corresponding M5 mode.
const LEFT_INSET_LS_KEY  = 'replay-inset-left-v1';
const RIGHT_INSET_LS_KEY = 'replay-inset-right-v1';
// Render-side presentation smoothing preset. NOT a firmware mirror —
// purely a viewing aid for 50 Hz log replay where IMU aliasing is
// visible on the slip ball. See presentationFilter.js for rationale.
const M5_SMOOTH_LS_KEY = 'replay-m5-smooth-v1';
// Full-frame HUD layer toggle. Independent of the M5 panel — both
// can be on at the same time. Default OFF (PLAN_HUD_OVERLAY.md PR-1
// is an opt-in design-tuning phase).
const HUD_SHOW_LS_KEY = 'replay-show-hud-v1';

function safeLsGet(key) { try { return localStorage.getItem(key); } catch { return null; } }
function safeLsSet(key, value) { try { localStorage.setItem(key, value); } catch {} }

// Convert a video time (seconds) to a log timestamp (ms since
// power-on) using the takeoff anchor.
function videoToLogMs(videoSec, sync) {
  if (sync == null) return null;
  return sync.logTakeoffMs + (videoSec - sync.videoTakeoffSec) * 1000;
}


export const ReplayPage = () => {
  // Chapter-aware state. For non-GoPro single picks, videoFiles is
  // `[file]` and videoTimeline is null — the page falls back to its
  // legacy single-file behaviour (videoEl.duration drives all the
  // time math). For a GoPro multi-chapter pick, videoFiles is the
  // sorted chapter list and videoTimeline carries the {chapters,
  // totalDurationSec} that lets `globalSec` be computed across the
  // whole recording. `videoFile` (singular) is the active chapter
  // displayed in the <video> element — derived below.
  const [videoFiles, setVideoFiles] = useState([]);
  const [videoTimeline, setVideoTimeline] = useState(null);
  const [activeChapterIndex, setActiveChapterIndex] = useState(0);
  const [videoUrl, setVideoUrl]   = useState(null);
  const videoFile = videoFiles[activeChapterIndex] || null;
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
  // HUD layer toggle. Persisted to localStorage so a reload remembers
  // the pilot's preference. Default false: PR-1 is design-tuning, and
  // pilots who don't want the HUD shouldn't see it on first load.
  const [showHud, setShowHud] = useState(() => {
    return safeLsGet(HUD_SHOW_LS_KEY) === '1';
  });
  useEffect(() => { safeLsSet(HUD_SHOW_LS_KEY, showHud ? '1' : '0'); },
            [showHud]);
  const [parseErr, setParseErr]   = useState(null);
  // True while the resume path is mid-load (auto-resume on mount, or
  // banner click). Surfaces a "Resuming…" status pill so the page
  // doesn't look blank during the three getFile()/parse calls.
  const [resuming, setResuming]   = useState(false);

  // Two independent inset slots. Each slot's value is null (off) or an
  // M5 mode id (0..4). Persisted via per-slot LS keys: empty string =
  // off, decimal digit = mode id. Default: left off, right Energy.
  const parseSlot = (raw, fallback) => {
    if (raw === null || raw === undefined) return fallback;
    if (raw === '') return null;
    const n = parseInt(raw, 10);
    const validIds = M5_MODES.map(m => m.id);
    return Number.isFinite(n) && validIds.includes(n) ? n : fallback;
  };
  const [leftInsetMode, setLeftInsetModeRaw]   = useState(() =>
    parseSlot(safeLsGet(LEFT_INSET_LS_KEY), null));
  const [rightInsetMode, setRightInsetModeRaw] = useState(() =>
    parseSlot(safeLsGet(RIGHT_INSET_LS_KEY), 0));
  const setLeftInsetMode = useCallback((m) => {
    setLeftInsetModeRaw(m);
    safeLsSet(LEFT_INSET_LS_KEY, m == null ? '' : String(m));
  }, []);
  const setRightInsetMode = useCallback((m) => {
    setRightInsetModeRaw(m);
    safeLsSet(RIGHT_INSET_LS_KEY, m == null ? '' : String(m));
  }, []);
  // The sim itself runs in exactly one mode at a time (its internal
  // mode controls displayType + which datapoints get computed). When
  // both slots are off, default to Energy so the sim still ticks; when
  // either slot wants a mode, mirror that to the sim. The right slot
  // wins when both are set, since it's the historical primary (mode
  // picker default). The sim's displayType is overridden per-render
  // when the active slot mode differs from the sim's mode.
  const activeSimMode = rightInsetMode != null ? rightInsetMode
                      : leftInsetMode  != null ? leftInsetMode
                      : 0;
  // Render-side smoothing preset. 'off' = byte-faithful (wire values
  // drive the SVG directly). The other presets emulate the missing
  // 208 Hz averaging that 50 Hz log replay can't reproduce; see
  // presentationFilter.js for the structural rationale.
  //
  // Initial value: a previously-saved user choice if any, else null
  // (the load-time effect below picks a rate-appropriate default
  // once the log loads).
  // Lateral τ (slip ball smoothing) as a free-form number in seconds.
  // `null` = pilot hasn't picked one yet; the load-time effect below
  // chooses a rate-appropriate default once the log lands.
  const [m5SmoothLateralTau, setM5SmoothLateralTau] = useState(() => {
    const s = safeLsGet(M5_SMOOTH_LS_KEY);
    const v = parseFloat(s);
    if (Number.isFinite(v) &&
        v >= PRESENTATION_LATERAL_TAU_MIN &&
        v <= PRESENTATION_LATERAL_TAU_MAX) {
      return v;
    }
    return null;
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
  // Mirror activeSimMode into a ref so the async re-init `.then()`
  // callback (below) reads the current mode rather than the value
  // captured by the effect closure at fire time. The effect that
  // updates this ref runs on every slot-mode change.
  const m5ModeIdRef = useRef(activeSimMode);

  // Render-side presentation filter (NOT a firmware mirror).
  // Applies after sim.read(); attenuates 50 Hz log aliasing before
  // SVG renderers consume state.LateralG / state.VerticalG. τ is
  // driven by m5SmoothLateralTau (lateral) — vertical stays 0.
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

  // Per-log annotation overlay (DataMark names + notes). Keyed by the
  // same log-content digest as sync/clip persistence. Additive on top
  // of the parser-derived `marks` array — the journal layer never
  // creates marks, only annotates them.
  const journal = useReplayJournal({ logHash: persistence.logDigest });

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
    // Multi-chapter sessions: `videoFileRef.current` is chapter 0,
    // so the IDB key uses chapter 0's {name, size, lastModified}.
    // Two unrelated GoPro sessions whose chapter 0 has identical
    // metadata would overwrite each other; same limitation as the
    // single-chapter path (and same as the legacy file-input flow).
    // The Resume banner's persistence-key surface keeps the same
    // shape so this stays in lockstep with the recent-files banner.
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

  // Standard (ADI + Energy) layout used to be a checkbox in the
  // ClipBuilder, then a single MODE_STANDARD entry in the mode picker.
  // It's now expressed as two independent inset slots (Left + Right) —
  // each can be off or any M5 mode. The export composes the same way:
  // leftMode/rightMode params on exportClipAsMp4. The dead
  // `replay-standard-clip-overlay-v1` and `replay-m5-mode-v1` LS keys
  // from the prior shapes are intentionally left in place — clearing
  // them here would risk touching localStorage during render, and the
  // keys are dormant. A future tidy can prune them via a one-shot
  // migration.

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
  // Live mirrors of videoTimeline + activeChapterIndex consumed by the
  // rVFC tick (the tick captures these via ref so a chapter swap mid-
  // tick is observed immediately, not after the next render).
  const videoTimelineRef = useRef(null);
  const activeChapterIndexRef = useRef(0);
  // While a chapter is being swapped (URL change + loadedmetadata
  // round-trip), we want to seek to a specific local time once the
  // new source is loaded. Pending swap target lives here; the
  // loadedmetadata handler reads and clears it.
  const pendingChapterSeekRef = useRef(null);
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

  // Apply a video pick. Accepts either a single File (legacy) or an
  // array of Files (multi-chapter). When `files.length > 1` we group
  // GoPro siblings, build the timeline, and start playback at chapter
  // 0. If grouping produces zero matches we fall back to treating the
  // first file as a single standalone video.
  //
  // The `handle` argument is the FSA handle for the user-picked file
  // (single chapter). For multi-chapter picks the caller passes a
  // `chapterHandles` array via the third slot and the parent
  // `directoryHandle` via the fourth. The directory handle is what
  // gets persisted to IDB for multi-chapter resume — re-walking the
  // directory recovers the chapter set in one permission prompt
  // (instead of three prompts per chapter).
  const applyVideoFiles = useCallback(async (filesArg, handle, chapterHandles, directoryHandle) => {
    if (videoUrl) URL.revokeObjectURL(videoUrl);
    const filesIn = Array.isArray(filesArg) ? filesArg : (filesArg ? [filesArg] : []);
    if (filesIn.length === 0) {
      setVideoFiles([]);
      setVideoTimeline(null);
      setActiveChapterIndex(0);
      setVideoUrl(null);
      videoFileRef.current = null;
      videoHandleRef.current = null;
      return;
    }
    let chaptersList = [];
    if (filesIn.length > 1) {
      chaptersList = groupChapterSiblings(filesIn);
    }
    let timeline = null;
    let activeFiles = filesIn;
    if (chaptersList.length > 1) {
      try {
        timeline = await buildChapterTimeline(chaptersList);
        activeFiles = timeline.chapters.map(c => c.file);
      } catch (err) {
        // Probe failure (corrupt chapter, codec issue). Fall back to
        // single-chapter behaviour with the first file; pilot can
        // re-pick if they want multi-chapter.
        setParseErr(`Multi-chapter pick failed: ${err.message}. Loaded first file only.`);
        timeline = null;
        activeFiles = [filesIn[0]];
      }
    }
    setVideoFiles(activeFiles);
    setVideoTimeline(timeline);
    setActiveChapterIndex(0);
    const first = activeFiles[0];
    // For non-chapter picks, set videoUrl directly here — the chapter
    // swap effect short-circuits on null timeline. For multi-chapter
    // picks, clear videoUrl first; the chapter-swap effect creates
    // the URL for the active chapter once the new timeline lands.
    if (!timeline) {
      setVideoUrl(URL.createObjectURL(first));
    } else {
      setVideoUrl(null);
    }
    videoFileRef.current = first;
    persistence.notifyFilePicked('video', first);
    // Multi-chapter sessions store the directory handle + the ordered
    // chapter filename list so Resume can re-walk the directory with a
    // single permission prompt. Single-chapter sessions store the bare
    // file handle (existing PR #533 shape).
    if (timeline && directoryHandle && chapterHandles && chapterHandles.length > 1) {
      videoHandleRef.current = {
        kind: 'multi-chapter',
        directoryHandle,
        chapterNames: activeFiles.map(f => f.name),
      };
    } else {
      videoHandleRef.current = handle ||
        (chapterHandles && chapterHandles[0]) || null;
    }
    if (handle || chapterHandles || directoryHandle) persistHandlesIfReady();
  }, [videoUrl, persistence, persistHandlesIfReady]);

  // Compat wrapper for the existing single-file callsites (resume
  // banner, etc.). Equivalent to applyVideoFiles([f], handle).
  const applyVideoFile = useCallback((f, handle) => {
    applyVideoFiles(f ? [f] : [], handle, null);
  }, [applyVideoFiles]);

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
    const fileList = e.target.files;
    if (!fileList || fileList.length === 0) return;
    const files = Array.from(fileList);
    applyVideoFiles(files, null, null);
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

  // Pick a video via the File System Access picker. When the pilot
  // picks a GoPro first-chapter file (GOPR####.MP4), immediately
  // open the directory picker so we can pull in the sibling
  // continuation chapters (GP01...GP09####.MP4) into one virtual
  // timeline.
  //
  // If the directory pick is denied or unavailable (or no siblings
  // match), the single chapter the pilot picked still works
  // standalone — same as a non-GoPro pick. A banner explains the
  // fallback path.
  const pickVideoViaFsa = useCallback(async () => {
    try {
      const r = await pickFile('video');
      if (!r) return;
      const pattern = detectGoProChapterPattern(r.file?.name || '');
      if (!pattern || typeof window === 'undefined' ||
          typeof window.showDirectoryPicker !== 'function') {
        applyVideoFile(r.file, r.handle);
        return;
      }
      // Try the directory picker. The pilot may dismiss it; treat
      // dismissal the same as "use just this chapter".
      let dirHandle;
      try {
        dirHandle = await window.showDirectoryPicker();
      } catch (dirErr) {
        if (dirErr && dirErr.name === 'AbortError') {
          applyVideoFile(r.file, r.handle);
          setParseErr(
            `Picked ${r.file.name} only. Re-pick using the multi-select ` +
            `flow to load all chapters together.`);
          return;
        }
        throw dirErr;
      }
      // Walk the directory and collect every GoPro chapter sibling
      // matching the same seq as the picked file.
      const siblings = [];
      const siblingHandles = [];
      for await (const [name, entry] of dirHandle.entries()) {
        if (entry.kind !== 'file') continue;
        const m = detectGoProChapterPattern(name);
        if (!m || m.seq !== pattern.seq) continue;
        try {
          const f = await entry.getFile();
          siblings.push(f);
          siblingHandles.push(entry);
        } catch (_) { /* unreadable file: skip */ }
      }
      if (siblings.length <= 1) {
        // Directory had no extra chapters — fall back to single-file.
        applyVideoFile(r.file, r.handle);
        return;
      }
      // Sort siblings + handles together by chapterIndex so the
      // resulting list stays aligned. The grouping in
      // applyVideoFiles will re-sort by chapterIndex too, but doing
      // it here keeps the handle indices in lockstep.
      const indexed = siblings.map((f, i) => ({
        file: f,
        handle: siblingHandles[i],
        idx: detectGoProChapterPattern(f.name).chapterIndex,
      }));
      indexed.sort((a, b) => a.idx - b.idx);
      const sortedFiles = indexed.map(x => x.file);
      const sortedHandles = indexed.map(x => x.handle);
      await applyVideoFiles(sortedFiles, sortedHandles[0], sortedHandles, dirHandle);
    } catch (err) {
      setParseErr(`Could not open video: ${err.message}`);
    }
  }, [applyVideoFile, applyVideoFiles]);

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

  // Shared resume load body. Reads the three files via the (already-
  // granted) FSA handles and re-feeds them through the apply* helpers.
  // Called by both onResumeClick (user-gesture banner click, runs
  // requestPermissionForHandles first) and the auto-resume effect (page
  // load, queryPermission already reported 'granted'). `getFile()` may
  // still fail if a file was deleted between sessions — the catch
  // surfaces the error and leaves the banner up for a manual retry.
  const performResumeLoad = useCallback(async (handles) => {
    setResuming(true);
    try {
      const isMultiChapter = handles.video && handles.video.kind === 'multi-chapter';
      // Log + cfg are always single-file handles; video may be either.
      const [lFile, cFile] = await Promise.all([
        handles.log.getFile(),
        handles.cfg ? handles.cfg.getFile() : Promise.resolve(null),
      ]);
      if (isMultiChapter) {
        // Permission on the directory handle was already granted (by
        // requestPermissionForHandles or queryPermissionForHandles).
        // Subsequent entries() iteration and entry.getFile() reads on
        // an already-granted directory do NOT need a fresh user-gesture
        // token, so awaiting the walk here is safe even though it
        // crosses several async hops.
        const expanded = await expandMultiChapterHandle(handles.video);
        if (!expanded || expanded.files.length === 0) {
          throw new Error('multi-chapter directory is empty or chapters moved');
        }
        if (expanded.files.length < handles.video.chapterNames.length) {
          setParseErr(
            `Resumed with ${expanded.files.length} of ` +
            `${handles.video.chapterNames.length} chapters — some files moved ` +
            `or were renamed.`);
        }
        await applyVideoFiles(
          expanded.files, expanded.handles[0], expanded.handles, expanded.directoryHandle);
      } else {
        const vFile = await handles.video.getFile();
        applyVideoFile(vFile, handles.video);
      }
      const logOk = await applyLogFile(lFile, handles.log);
      if (cFile && logOk) await applyCfgFile(cFile, handles.cfg);
      fileHandleResume.markUsed();
    } catch (err) {
      setParseErr(`Resume failed: ${err.message}`);
    } finally {
      setResuming(false);
    }
  }, [fileHandleResume, applyVideoFile, applyVideoFiles, applyLogFile, applyCfgFile]);

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
      // Collapse the banner so the pilot only sees one signal — the
      // error message. Leaving the banner up alongside the error
      // reads as "Resume failed but click here to try again" when in
      // fact the browser already denied without a prompt and another
      // click won't help.
      fileHandleResume.dismiss();
      setParseErr('Permission denied for one or more files. Pick them manually.');
      return;
    }
    await performResumeLoad(handles);
  }, [fileHandleResume, performResumeLoad]);

  const onResumeDismiss = useCallback(() => {
    fileHandleResume.dismiss();
  }, [fileHandleResume]);

  // Auto-resume: when the hook reports queryPermission is still
  // 'granted' on every stored handle (typical within-browser-session
  // reload), skip the banner click and load the files at mount. The ref
  // pins this to fire exactly once per (signature, handles) pair so a
  // re-render after markUsed() doesn't retrigger the load. Pilots who
  // dismissed the recent-files banner last session won't see the
  // recent-files banner; we also suppress auto-resume in that case via
  // the `persistence.bannerInfo` gate so the page stays as the pilot
  // left it (manual re-pick).
  // Auto-resume gates only on autoResumeReady (handles present AND
  // queryPermission returned 'granted' on all of them). The
  // recent-files banner dismissal is a separate signal — pilots
  // dismiss the banner to declutter the page, NOT to opt out of
  // silent reload. Treating them as one gate strands users who
  // dismissed the banner once and then never see their files
  // come back automatically.
  const autoResumedSigRef = useRef('');
  useEffect(() => {
    if (!fileHandleResume.autoResumeReady) return;
    const handles = fileHandleResume.availableHandles;
    if (!handles) return;
    if (autoResumedSigRef.current === persistence.recentFilesSig) return;
    autoResumedSigRef.current = persistence.recentFilesSig;
    performResumeLoad(handles);
  }, [fileHandleResume.autoResumeReady, fileHandleResume.availableHandles,
      persistence.recentFilesSig, performResumeLoad]);

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
      return;
    }
    const tRow = detectRotation(log);
    if (tRow >= 0) {
      // Auto-detected anchor; pair it with whatever the pilot's
      // first "mark video anchor" press will be. Until then, sync
      // is null (overlay shows nothing).
      setSync(prev => prev ?? { logTakeoffMs: log.timeStamp[tRow], videoTakeoffSec: null });
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
  //
  // Clips persisted before Phase 3 lack `id`; backfill one so the
  // journal-annotation layer has a stable key to address them. The
  // backfilled id is fresh on each load (no UUID is recoverable from
  // a label/timestamps alone), which is fine — annotations will simply
  // start empty for legacy clips, matching the existing behavior.
  useEffect(() => {
    if (!persistence.digestReady) return;
    if (!persistence.storedClips || !persistence.storedClips.length) return;
    setClips(prev => {
      if (prev.length !== 0) return prev;
      return persistence.storedClips.map(c =>
        c && typeof c.id === 'string' && c.id ? c : { ...c, id: newClipId() });
    });
  }, [persistence.digestReady, persistence.storedClips]);

  // Persist clips on every change. Gated on digestReady — see the
  // matching note on the sync persist effect above. Without this
  // gate, picking a new log corrupts the previous log's clips key
  // before the new digest resolves.
  useEffect(() => {
    if (!persistence.digestReady) return;
    persistence.storeClips(clips);
  }, [clips, persistence.digestReady, persistence.storeClips]);

  // ---------- Chapter timeline refs + swap effect ------------------

  // Mirror videoTimeline + activeChapterIndex into refs so the rVFC
  // tick reads the latest values without effect re-runs.
  useEffect(() => {
    videoTimelineRef.current = videoTimeline;
  }, [videoTimeline]);
  useEffect(() => {
    activeChapterIndexRef.current = activeChapterIndex;
  }, [activeChapterIndex]);

  // Swap the <video> src when the active chapter changes. Revokes
  // the previous object URL, creates a fresh one for the new chapter,
  // and on `loadedmetadata` resumes playback at local-time 0 (or at
  // a stored seek target from a cross-chapter scrub). videoFileRef
  // and persistence stay anchored to chapter 0 (the "primary" file)
  // so log digests / banners don't churn on chapter swaps.
  //
  // Pre-condition: videoTimeline is non-null. For non-chapter pickups
  // applyVideoFiles sets videoUrl directly and this effect doesn't
  // fire because videoTimeline stays null.
  useEffect(() => {
    if (!videoTimeline) return;
    const chapter = videoTimeline.chapters[activeChapterIndex];
    if (!chapter) return;
    if (videoUrl) URL.revokeObjectURL(videoUrl);
    const url = URL.createObjectURL(chapter.file);
    setVideoUrl(url);
    // No state-write of videoFile — keep the active <video>-element
    // file accessible via videoFiles[activeChapterIndex] (above).
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [videoTimeline, activeChapterIndex]);

  // When the <video> element's source completes loading, if a
  // chapter-swap requested a specific local-time seek, apply it now.
  // Otherwise the new chapter starts at local-time 0 — matching the
  // auto-advance flow on chapter rollover.
  useEffect(() => {
    const v = videoRef.current;
    if (!v) return;
    const onMeta = () => {
      const pending = pendingChapterSeekRef.current;
      if (pending != null && Number.isFinite(pending)) {
        v.currentTime = Math.max(0, pending);
        pendingChapterSeekRef.current = null;
      }
    };
    v.addEventListener('loadedmetadata', onMeta);
    return () => v.removeEventListener('loadedmetadata', onMeta);
  }, [videoUrl]);

  // Seek the live video to a global timeline-second. Handles cross-
  // chapter scrubs by swapping the active chapter then queueing a
  // local-time seek for the loadedmetadata handler above. For single-
  // chapter / legacy playback, falls back to a direct currentTime
  // assign.
  const seekToGlobalSec = useCallback((globalSec) => {
    const v = videoRef.current;
    if (!v || !Number.isFinite(globalSec)) return;
    const tl = videoTimelineRef.current;
    if (!tl) {
      v.currentTime = Math.max(0, globalSec);
      return;
    }
    const { chapterIndex, localSec } = globalToLocal(tl, globalSec);
    if (chapterIndex === activeChapterIndexRef.current) {
      v.currentTime = Math.max(0, localSec);
    } else {
      pendingChapterSeekRef.current = localSec;
      activeChapterIndexRef.current = chapterIndex;
      setActiveChapterIndex(chapterIndex);
    }
  }, []);

  // Convert the active <video> element's local currentTime to a
  // global-timeline second. Used by every callsite that previously
  // read videoRef.current.currentTime — anything that records a
  // "where is the playhead now" answer must use globalSec.
  const currentGlobalSec = useCallback(() => {
    const v = videoRef.current;
    if (!v) return 0;
    const tl = videoTimelineRef.current;
    const idx = activeChapterIndexRef.current;
    if (tl && tl.chapters[idx]) {
      return tl.chapters[idx].startSec + v.currentTime;
    }
    return v.currentTime;
  }, []);

  // Frame-step keyboard scrub (closes #63). Pilots routinely click off
  // the <video> element (timeline plot, clip rows, notes textarea) and
  // lose the browser's native Space-to-play binding; this handler runs
  // at the document level so the keys work regardless of focus. Typed
  // input in <input>/<textarea>/[contenteditable] passes through —
  // arrow keys / space in the smoothing slider, clip labels, and
  // DataMark notes textarea keep their normal behavior.
  //
  // Step size derives from the active video's frame rate, detected
  // from rVFC mediaTime deltas (see fps detection in the rVFC tick
  // above). Defaults to 30 fps when unknown (paused-on-load, RAF
  // fallback, or pre-warmup) — matches the GoPro Hero default. Seeks
  // route through `seekToGlobalSec` so cross-chapter back-steps swap
  // to the previous chapter's tail correctly.
  //
  // Bindings:
  //   Space           toggle play/pause
  //   ←  / ,          back one frame
  //   →  / .          forward one frame
  //   Shift+←/Shift+→ back/forward ten frames
  //
  // Test plan (manual, no unit test — handler touches DOM + refs):
  //   1. Load any video, click the timeline plot, press ←/→: video
  //      steps one frame.
  //   2. Press Space: toggles play/pause.
  //   3. Click into DataMark notes textarea, press ←: caret moves
  //      left, video does NOT seek.
  //   4. Press Shift+→ ten times in a row while playing: video pauses
  //      first, then jumps ~100 frames.
  //   5. On a multi-chapter pick, scrub to local-time ~0.05 s on
  //      chapter 2 and press ←: chapter swaps to chapter 1's tail.
  useEffect(() => {
    const onKey = (e) => {
      // Don't hijack typing in inputs / textareas / contenteditable.
      if (e.target && typeof e.target.closest === 'function' &&
          e.target.closest('input, textarea, [contenteditable]')) {
        return;
      }
      const v = videoRef.current;
      if (!v) return;
      // Use e.code (not e.key) so Shift+, doesn't shift the binding
      // from `,` (Comma) to `<` (Less).
      const code = e.code;
      if (code === 'Space') {
        e.preventDefault();
        if (v.paused) v.play().catch(() => {});
        else v.pause();
        return;
      }
      let direction = 0;
      if (code === 'ArrowLeft' || code === 'Comma') direction = -1;
      else if (code === 'ArrowRight' || code === 'Period') direction = 1;
      else return;
      e.preventDefault();
      // Pause on first step. A play loop swallows seeks otherwise.
      if (!v.paused) v.pause();
      const fps = v._detectedFps && Number.isFinite(v._detectedFps) && v._detectedFps > 0
        ? v._detectedFps : 30;
      const frames = e.shiftKey ? 10 : 1;
      const dtSec = (direction * frames) / fps;
      const tl = videoTimelineRef.current;
      const cur = currentGlobalSec();
      const max = tl ? tl.totalDurationSec : (Number.isFinite(v.duration) ? v.duration : Infinity);
      const target = Math.max(0, Math.min(max, cur + dtSec));
      seekToGlobalSec(target);
    };
    // Capture phase so we intercept arrow keys BEFORE the native <video
    // controls> sees them — otherwise the browser's built-in 5-second
    // seek-by-arrow eats the keystroke and we never get a chance to
    // frame-step. The input-passthrough check above still lets typing
    // in <input>/<textarea>/[contenteditable] reach the native handler
    // unchanged. Pilots can click anywhere — including the video —
    // and the arrow keys still frame-step rather than 5-second-skip.
    document.addEventListener('keydown', onKey, { capture: true });
    return () => document.removeEventListener('keydown', onKey, { capture: true });
  }, [seekToGlobalSec, currentGlobalSec]);

  // Toolbar label for the video pick. Single-chapter (or non-chapter
  // pick): just the filename. Multi-chapter: "GOPR0314.MP4 + 3 chapters
  // (Hh Mm Ss)".
  const videoChapterLabel = videoTimeline
    ? describeChapterPick(videoTimeline, videoFiles[0]?.name || '')
    : (videoFile ? videoFile.name : '');

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
    // rVFC fps detection: track mediaTime deltas between consecutive
    // frame callbacks while the video is playing. After ~10 samples
    // settle on the median delta and stash 1/dt as `_detectedFps` on
    // the element for the frame-step keybindings to read. Falls back
    // to 30 fps if the video never plays / RAF path.
    const fpsSamples = [];
    v._detectedFps = null;
    v._lastMediaTimeForFps = null;

    const tick = (now, meta) => {
      if (cancelled) return;
      // Suspend during MP4 export: the export pipeline drives the
      // same video element with its own rVFC, and competing callbacks
      // on a paused video starve the export. Re-armed by bumping
      // livePreviewNonce when the export completes.
      if (mp4ExportingRef.current) return;
      // meta?.mediaTime is the canonical local video time (within
      // the active chapter) when rVFC fires; fall back to currentTime
      // for the RAF path. For non-chapter playback (no timeline),
      // global time equals local time.
      const local = (meta && Number.isFinite(meta.mediaTime)) ? meta.mediaTime : v.currentTime;
      // Sample frame rate from rVFC metadata. Only valid while
      // playing and against the *previous* media time we observed;
      // a seek discontinuity shows up as a large delta and is
      // discarded by the [0.005, 0.1] band.
      if (meta && Number.isFinite(meta.mediaTime) && v._lastMediaTimeForFps != null) {
        const dt = meta.mediaTime - v._lastMediaTimeForFps;
        if (dt > 0.005 && dt < 0.1) {
          fpsSamples.push(dt);
          if (fpsSamples.length >= 10 && v._detectedFps == null) {
            const sorted = fpsSamples.slice().sort((a, b) => a - b);
            const median = sorted[Math.floor(sorted.length / 2)];
            v._detectedFps = 1 / median;
          }
        }
      }
      if (meta && Number.isFinite(meta.mediaTime)) v._lastMediaTimeForFps = meta.mediaTime;
      const tl = videoTimelineRef.current;
      const idx = activeChapterIndexRef.current;
      // Auto-advance at chapter boundary: when local time reaches
      // the active chapter's duration and the video is playing,
      // queue a swap to the next chapter. The swap effect (below)
      // handles the actual src change. We skip the videoT update
      // for this tick — a stale rVFC callback firing one more time
      // on the old <video> element would otherwise produce a brief
      // global-time spike at the boundary.
      let advanced = false;
      if (tl && tl.chapters[idx] && !v.paused) {
        const chapter = tl.chapters[idx];
        if (local >= chapter.durationSec - 0.05 &&
            idx < tl.chapters.length - 1) {
          activeChapterIndexRef.current = idx + 1;
          setActiveChapterIndex(idx + 1);
          advanced = true;
        }
      }
      if (!advanced) {
        const globalT = tl && tl.chapters[idx]
          ? tl.chapters[idx].startSec + local
          : local;
        setVideoT(globalT);
      }
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
        // retunes it as the slider settles (rate-aware default +
        // user drag).
        const filter = new PresentationFilter();
        if (Number.isFinite(m5SmoothLateralTau)) {
          filter.setTau({
            lateralSec:  m5SmoothLateralTau,
            verticalSec: 0,
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

  // Pick a rate-appropriate default lateral τ once the log loads, IF
  // the user hasn't set one (m5SmoothLateralTau === null on a fresh
  // session with no LS value). 50 Hz logs default to ~0.75 s
  // (compensates for missing 208 Hz averaging); 208 Hz logs default
  // to 0 (firmware EMA already runs at ~76 ms).
  useEffect(() => {
    if (m5SmoothLateralTau !== null || !log) return;
    const sampleRateHz = detectLogSampleRate(log);
    setM5SmoothLateralTau(defaultLateralTauForLogRate(sampleRateHz));
  }, [log, m5SmoothLateralTau]);

  // Persist the slider value; also retune the live filter when the
  // user drags it. Reset filter state so the next frame seeds fresh —
  // a step-change in τ otherwise looks like a glitch in the first
  // frame after the change.
  useEffect(() => {
    if (m5SmoothLateralTau === null) return;     // wait for default-pick
    safeLsSet(M5_SMOOTH_LS_KEY, String(m5SmoothLateralTau));
    if (m5RenderFilterRef.current) {
      m5RenderFilterRef.current.setTau({
        lateralSec:  m5SmoothLateralTau,
        verticalSec: 0,
      });
      m5RenderFilterRef.current.reset();
      m5LastFilterVideoTRef.current = null;
    }
  }, [m5SmoothLateralTau]);

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

  // Drive the sim's internal mode from whichever slot is active. The
  // sim has one displayType at a time; slot rendering overrides it
  // per-panel where the slot mode differs from activeSimMode.
  useEffect(() => {
    if (m5SimRef.current) {
      m5SimRef.current.setMode(activeSimMode);
      // Refresh React state immediately so the SVG re-renders the new
      // mode without waiting for a videoT tick. Important on pause:
      // no per-frame effect runs to refresh state otherwise.
      setM5State(m5SimRef.current.read());
    }
  }, [activeSimMode]);

  // Mirror activeSimMode into m5ModeIdRef so the async re-init .then()
  // (in the per-frame effect below) reads the current mode rather
  // than a stale closure value.
  useEffect(() => {
    m5ModeIdRef.current = activeSimMode;
  }, [activeSimMode]);

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
    // Don't clear m5State here (issue #540). Aggressive backward scrubs
    // bump m5SimReinitNonce, which sets m5SimRef.current = null while
    // the WASM reloads (~50–100 ms). Calling setM5State(null) in that
    // window unmounts the overlay via the `m5State && ...` render gate
    // and produces visible flicker on rapid scrub reversals. Initial
    // mount sets m5State = null directly, so first-load gating is
    // unaffected; here we hold the last-good state until the new sim
    // is ready to render fresh data.
    if (!sim || !core || !log || !cfg) return;
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
          mode: activeSimMode,
          leftSlot: leftInsetMode,
          rightSlot: rightInsetMode,
          smoothLateralTau: m5SmoothLateralTau,
          raw,
          cppEng: cppEngSlice,
          cppFrame: sliceFrame(cppFrame),
          m5: m5Slice,
        }));
      }
    }
  }, [log, cfg, sync, videoT, pausedLogMs, activeSimMode,
      leftInsetMode, rightInsetMode,
      m5SmoothLateralTau, cppWireFrames, debugMode]);

  // ---------- Anchor-mark handlers ---------------------------------

  const markVideoTakeoff = useCallback(() => {
    const v = videoRef.current;
    if (!v) return;
    // Anchor at the GLOBAL timeline position (chapter offset +
    // currentTime). For single-file playback this collapses to
    // currentTime — backward-compatible with persisted sync from
    // before multi-chapter landed.
    setSync(prev => ({
      logTakeoffMs:    prev?.logTakeoffMs ?? null,
      videoTakeoffSec: currentGlobalSec(),
    }));
  }, [currentGlobalSec]);

  const reMarkLogTakeoff = useCallback(() => {
    if (!log) return;
    const tRow = detectRotation(log);
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
      videoTakeoffSec: currentGlobalSec(),
    });
    setPausedLogMs(null);
  }, [pausedLogMs, currentGlobalSec]);

  const cancelPause = useCallback(() => setPausedLogMs(null), []);

  // syncReady is computed up at the top so the export callback can
  // reference it; reuse here in the layout.

  // Compute the list of data-mark events once per log change.
  // Cheap (one pass over a 1 MB Int32Array); inline rather than
  // useMemo since Preact-standalone's hook surface is small.
  const marks = log ? findDataMarks(log) : [];

  // Helper for the data-mark panel: jump video to a mark.
  const jumpToMark = (markLogMs) => {
    const tSec = logMsToVideoSec(markLogMs, sync);
    if (Number.isFinite(tSec)) seekToGlobalSec(tSec);
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

  // Add a clip spanning this DataMark to the next one. Uses log times
  // directly — no sync round-trip needed. The DataMark panel computes
  // `nextMark` from the sorted marks array, so the caller knows up
  // front whether a next-mark exists (button is disabled otherwise).
  //
  // Label preference: if the pilot has annotated either mark with a
  // name, use the names (so "Slow flight → first stall" beats
  // "mark 17 → mark 18"). Falls back to the firmware values when no
  // names are set. The clip's name field is editable after creation,
  // so this is just a smart default.
  const addClipFromMarkToNextMark = (thisMark, nextMark) => {
    if (!thisMark || !nextMark) return;
    const annA = journal.markAnnotations?.[markKey(thisMark.value, thisMark.logTimeMs)];
    const annB = journal.markAnnotations?.[markKey(nextMark.value, nextMark.logTimeMs)];
    const nameA = (annA?.name || '').trim();
    const nameB = (annB?.name || '').trim();
    const labelA = nameA || `mark ${thisMark.label}`;
    const labelB = nameB || `mark ${nextMark.label}`;
    const label = `${labelA} → ${labelB}`;
    const clip = buildClipFromMarkToNextMark(thisMark, nextMark, label);
    if (clip) setClips(prev => [...prev, clip]);
  };

  // Add the current playhead as a clip start, with a default
  // 30-second window. Pilot can edit either edge in the row.
  const addClipFromPlayhead = (durationSec = 30) => {
    const v = videoRef.current;
    if (!v) return;
    const label = defaultClipLabel(clips.length);
    const clip = buildClipFromPlayhead(currentGlobalSec(), durationSec, sync, label);
    if (clip) setClips(prev => [...prev, clip]);
  };

  // Mark-in / mark-out flow. The first click stashes the current
  // global video time as the pending in-point. The second click
  // reads the global video time as the out-point and appends a clip.
  const markClipIn = useCallback(() => {
    const v = videoRef.current;
    if (!v) return;
    setPendingInVideoSec(currentGlobalSec());
  }, [currentGlobalSec]);

  const markClipOut = useCallback(() => {
    const v = videoRef.current;
    if (!v || pendingInVideoSec == null) return;
    const label = defaultClipLabel(clips.length);
    const clip = buildClipFromMarkers(pendingInVideoSec, currentGlobalSec(), sync, label);
    if (clip) {
      setClips(prev => [...prev, clip]);
      setPendingInVideoSec(null);
    }
  }, [pendingInVideoSec, clips, sync, currentGlobalSec]);

  const cancelClipMark = useCallback(() => setPendingInVideoSec(null), []);

  // Scrub the live video to a particular global timeline second.
  // Cross-chapter scrubs swap the active chapter automatically.
  const scrubVideoTo = useCallback((globalSec) => {
    if (!Number.isFinite(globalSec)) return;
    seekToGlobalSec(globalSec);
  }, [seekToGlobalSec]);

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

  // Separate mount for the full-frame HUD overlay during export. The
  // HUD's SVG is 1920x1080 (vs. the 320x240 mode-panel SVG), so we
  // can't reuse the same offscreen div without growing it to HUD
  // dimensions and having that cost on every export render.
  const exportHudMountRef = useRef(null);
  useEffect(() => {
    if (typeof document === 'undefined') return;
    const div = document.createElement('div');
    div.setAttribute('data-replay-export-hud', '');
    div.style.cssText = 'position:absolute;left:-99999px;top:0;width:1920px;height:1080px;visibility:hidden;pointer-events:none;';
    for (const [k, v] of Object.entries(EXPORT_AVIONICS_VARS)) {
      div.style.setProperty(k, v);
    }
    document.body.appendChild(div);
    exportHudMountRef.current = div;
    return () => {
      if (div.parentNode) div.parentNode.removeChild(div);
      exportHudMountRef.current = null;
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

  // renderHudSvg signature: (m5State, rowIdx?) → SVGElement.
  // Mirrors renderOverlayForExport but renders the full-frame HUD
  // overlay (HudOverlay) instead of the mode-specific M5 panel.
  // The rowIdx (optional) lets the HUD pull `efisMagHeading` from
  // the parsed log so the export shows the same MH the live preview
  // does. When `showHud` is false this is left null and the export
  // path skips HUD rendering entirely.
  const renderHudForExport = useCallback((m5State, rowIdx) => {
    const mount = exportHudMountRef.current;
    if (!mount || !m5State) return null;
    let mh = null;
    if (log && Number.isFinite(rowIdx) && rowIdx >= 0 && log.efisMagHeading) {
      const v = log.efisMagHeading[rowIdx];
      if (Number.isFinite(v) && v >= 0) mh = v;
    }
    render(html`<${HudOverlay} state=${m5State}
                                pitchOffsetDeg=${journal.hudPitchOffsetDeg ?? 0}
                                magneticHeading=${mh} />`, mount);
    const svg = mount.querySelector('svg');
    if (!svg) return null;
    for (const [k, v] of Object.entries(EXPORT_AVIONICS_VARS)) {
      svg.style.setProperty(k, v);
    }
    return svg;
  }, [log, journal.hudPitchOffsetDeg]);

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
    const presentationTau = Number.isFinite(m5SmoothLateralTau) && m5SmoothLateralTau > 0
      ? { lateralSec: m5SmoothLateralTau, verticalSec: 0 }
      : null;

    try {
      const blob = await exportClipAsMp4({
        videoEl:        v,
        clip,
        sync,
        log,
        cppWireFrames,
        renderOverlaySvg: renderOverlayForExport,
        // HUD overlay — only rasterized into the export when the
        // toolbar's Show HUD toggle is on. When off, the export
        // path skips HUD rendering entirely.
        renderHudSvg: showHud ? renderHudForExport : null,
        // Multi-chapter takes priority: when a chapter timeline is set,
        // the exporter stitches across boundaries into one MP4. Source
        // file alone still works for legacy single-file picks.
        videoTimeline:  videoTimeline,
        sourceFile:    videoFile,
        // Match the live preview's slip-ball smoothing.
        presentationTau,
        // Two independent inset slots. Each can be null (slot off) or
        // an M5 mode id (0..4). The export renders whichever slots are
        // set, mirroring the live preview's slot layout.
        leftMode:  leftInsetMode,
        rightMode: rightInsetMode,
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
      renderHudForExport, showHud,
      videoFile, videoTimeline, m5SmoothLateralTau,
      leftInsetMode, rightInsetMode]);

  const exportClipMp4AndDownload = useCallback(async (clip, idx) => {
    const blob = await exportClipMp4(clip, idx);
    if (!blob) return;
    const base = (videoFiles[0]?.name || 'flight').replace(/\.[^.]+$/, '');
    const suffix = (clip.label || `clip${idx + 1}`).replace(/[^a-z0-9_-]/gi, '_');
    downloadBlob(blob, `${base}_${suffix}.mp4`);
  }, [exportClipMp4, videoFiles]);

  // Export the entire loaded video (single or multi-chapter) as a single
  // MP4 with the current HUD + inset configuration. Synthesises a clip
  // spanning [0, totalDuration] in video seconds, maps that back into
  // clip startMs/endMs via the live sync anchor, and reuses
  // exportClipMp4 — the same composite path used for per-clip exports.
  const exportSortieMp4 = useCallback(async () => {
    const v = videoRef.current;
    if (!v || !syncReady || !sync || !log || !cppWireFrames || !mp4Available) {
      return;
    }
    const duration = videoTimeline?.totalDurationSec ?? v.duration;
    if (!Number.isFinite(duration) || duration <= 0) return;
    // videoSec -> logMs: logMs = sync.logTakeoffMs + (videoSec - sync.videoTakeoffSec) * 1000
    const startMs = sync.logTakeoffMs - sync.videoTakeoffSec * 1000;
    const endMs   = sync.logTakeoffMs + (duration - sync.videoTakeoffSec) * 1000;
    const sortieClip = {
      id:      'sortie',
      label:   'sortie',
      startMs,
      endMs,
    };
    // Sentinel idx -1 marks this as the sortie export so existing
    // per-clip UI keyed on `exportingClipIdx === i` ignores it. The
    // toolbar reads exportingClipIdx != null for the cancel/progress
    // panel which is what we want.
    const blob = await exportClipMp4(sortieClip, -1);
    if (!blob) return;
    const base = (videoFiles[0]?.name || 'flight').replace(/\.[^.]+$/, '');
    downloadBlob(blob, `${base}_sortie.mp4`);
  }, [exportClipMp4, syncReady, sync, log, cppWireFrames, mp4Available,
      videoTimeline, videoFiles]);

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
        const base = (videoFiles[0]?.name || 'flight').replace(/\.[^.]+$/, '');
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
  }, [clips, exportClipMp4, videoFiles]);

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
    const presentationTau = Number.isFinite(m5SmoothLateralTau) && m5SmoothLateralTau > 0
      ? { lateralSec: m5SmoothLateralTau, verticalSec: 0 }
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
      const base = (videoFiles[0]?.name || 'flight').replace(/\.[^.]+$/, '');
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
      renderOverlayForExport, videoFiles, m5SmoothLateralTau,
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
        ${resuming
          ? null
          : (fileHandleResume.resumeReady
              ? html`<${ReplayResumeBanner}
                        info=${persistence.rawBannerInfo}
                        onResume=${onResumeClick}
                        onDismiss=${onResumeDismiss} />`
              : html`<${RecentFilesBanner} info=${persistence.bannerInfo}
                                           onDismiss=${persistence.dismissBanner} />`)}
        <header class="replay-toolbar">
          ${fsaSupported ? html`
            <label class="replay-file">
              <span>Video</span>
              <button class="replay-file-btn" type="button"
                      onClick=${pickVideoViaFsa}
                      title=${videoChapterLabel || 'Open video'}>
                ${videoChapterLabel || 'Open video…'}
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
              <input type="file" accept="video/*,.mp4,.mov,.webm"
                     multiple
                     onChange=${onVideoPick} />
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
          ${resuming && html`
            <span class="replay-status">Resuming…</span>`}
          ${cfg && html`
            <span class="replay-status">
              ${cfg.flaps.length} flap detents loaded · ${cfgFilename}
            </span>`}
          ${videoTimeline && videoTimeline.chapters.length > 1 && html`
            <span class="replay-status">
              Chapter ${activeChapterIndex + 1} of ${videoTimeline.chapters.length}
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
            ${videoTimeline && videoTimeline.chapters.length > 1 && html`
              <div class="replay-global-clock"
                   title="Flight time across the full multi-chapter timeline. The native <video> controls below show the chapter-local time; the data-marks panel uses this global clock.">
                Flight ${formatHms(videoT)} / ${formatHms(videoTimeline.totalDurationSec)}
                · Chapter ${activeChapterIndex + 1}/${videoTimeline.chapters.length}
              </div>`}
            <video
              ref=${videoRef}
              src=${videoUrl}
              controls
              playsInline
              class="replay-video"
            />
          ` : html`<div class="replay-placeholder">
              <span>Drop a flight video and an SD-log CSV to get started.</span>
              <a class="replay-help-link"
                 href="./getting-started/"
                 target="_blank"
                 rel="noopener">How does this work?</a>
            </div>`}

          ${showHud && m5State && (() => {
            // Resolve the current log row from the same video↔log
            // mapping the indexer uses, so the live HUD's MH box
            // reads the same efisMagHeading the export does.
            const tMs = Number.isFinite(pausedLogMs)
              ? pausedLogMs
              : (syncReady ? videoToLogMs(videoT, sync) : null);
            const liveRow = (log && Number.isFinite(tMs))
              ? findRowAt(log, tMs) : -1;
            const liveMh = (liveRow >= 0 && log && log.efisMagHeading)
              ? log.efisMagHeading[liveRow] : null;
            return html`
              <div class="replay-hud">
                <${HudOverlay} state=${m5State}
                                pitchOffsetDeg=${journal.hudPitchOffsetDeg ?? 0}
                                magneticHeading=${Number.isFinite(liveMh) && liveMh >= 0 ? liveMh : null} />
              </div>`;
          })()}

          ${overlayVisible && m5State && (() => {
            // Render one panel per active slot. Each slot may carry a
            // different mode from the sim's current displayType, so
            // stamp displayType into the state copy for the slot's
            // component (matches the export-side rendering convention).
            const slotPanel = (modeId, side) => {
              if (modeId == null) return null;
              const M = M5_MODES.find(m => m.id === modeId);
              const C = (M && M.C) ? M.C : EnergyMode;
              const stateForSlot = (modeId === m5State.displayType)
                ? m5State
                : { ...m5State, displayType: modeId };
              const pausedCls = Number.isFinite(pausedLogMs) ? 'paused' : '';
              return html`
                <div class=${`replay-overlay-frame replay-overlay-${side} ${pausedCls}`}>
                  <${C} state=${stateForSlot} stale=${false} />
                </div>`;
            };
            const left  = slotPanel(leftInsetMode,  'left');
            const right = slotPanel(rightInsetMode, 'right');
            if (!left && !right) return null;
            return html`
              <div class="replay-overlay">
                ${left}
                ${right}
              </div>`;
          })()}
        </div>

        <footer class="replay-controls">
          <div class="replay-control-row">
            <label class="replay-inset-select">
              Left
              <select value=${leftInsetMode == null ? '' : String(leftInsetMode)}
                      onChange=${e => setLeftInsetMode(e.target.value === '' ? null : Number(e.target.value))}>
                <option value="">Off</option>
                ${M5_MODES.map(m => html`<option value=${String(m.id)}>${m.label}</option>`)}
              </select>
            </label>
            <label class="replay-inset-select">
              Right
              <select value=${rightInsetMode == null ? '' : String(rightInsetMode)}
                      onChange=${e => setRightInsetMode(e.target.value === '' ? null : Number(e.target.value))}>
                <option value="">Off</option>
                ${M5_MODES.map(m => html`<option value=${String(m.id)}>${m.label}</option>`)}
              </select>
            </label>
            <span class="replay-spacer"></span>
            <span class="replay-toggle"
                  title="Slip-ball EMA time constant (s). 0 = firmware-faithful, ~0.25 = VN-300, ~0.75 = SkyView.">
              Ball smoothing:
              <input type="range"
                     min=${PRESENTATION_LATERAL_TAU_MIN}
                     max=${PRESENTATION_LATERAL_TAU_MAX}
                     step=${PRESENTATION_LATERAL_TAU_STEP}
                     value=${m5SmoothLateralTau ?? 0}
                     style="margin-left:0.4em; vertical-align:middle; width:140px;"
                     onInput=${e => setM5SmoothLateralTau(parseFloat(e.target.value))} />
              <small style="margin-left:0.4em; font-variant-numeric:tabular-nums;">
                ${(m5SmoothLateralTau ?? 0).toFixed(2)} s
              </small>
              ${cppBuilding
                ? html`<small style="margin-left:0.4em;">(pre-pass ${Math.round(cppProgress * 100)}%)</small>`
                : ''}
            </span>
            <label class="replay-toggle">
              <input type="checkbox" checked=${overlayVisible}
                     onChange=${e => setOverlayVisible(e.target.checked)} />
              Show overlay
            </label>
            <label class="replay-toggle"
                   title="Full-frame HUD: scaled-up attitude indicator + IAS/MH/PALT boxes + VVI trend + slip ball. Independent of the M5 inset slots — they can all be on.">
              <input type="checkbox" checked=${showHud}
                     onChange=${e => setShowHud(e.target.checked)} />
              Show HUD
            </label>
            ${showHud && html`
              <label class="replay-pitch-offset"
                     title="Per-flight HUD pitch-ladder offset to compensate for camera mount misalignment. Persisted per-log.">
                HUD pitch offset
                <input type="range" min="-20" max="20" step="0.1"
                       value=${journal.hudPitchOffsetDeg ?? 0}
                       onInput=${e => journal.setHudPitchOffsetDeg(parseFloat(e.target.value))} />
                <span>${(journal.hudPitchOffsetDeg ?? 0).toFixed(1)}°</span>
              </label>`}
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
                  ? `synced · video ${sync.videoTakeoffSec.toFixed(2)}s ↔ log ${(sync.logTakeoffMs / 1000).toFixed(2)}s (sync)`
                  : sync
                    ? `log sync at ${(sync.logTakeoffMs / 1000).toFixed(2)}s — set video anchor`
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
                          anchorLabel="sync"
                          marks=${marks}
                          clips=${clips}
                          clipAnnotations=${journal.clipAnnotations}
                          markAnnotations=${journal.markAnnotations}
                          onLogTakeoffPick=${(tMs) => setSync(prev => ({
                            logTakeoffMs: tMs,
                            videoTakeoffSec: prev?.videoTakeoffSec ?? null,
                          }))}
                          onSeekVideo=${(tSec) => seekToGlobalSec(tSec)} />

          ${marks.length > 0 && html`
            <${DataMarkPanel}
                marks=${marks}
                sync=${sync}
                disabled=${exportingClipIdx != null || !syncReady}
                videoDuration=${videoTimeline?.totalDurationSec ?? videoRef.current?.duration}
                markAnnotations=${journal.markAnnotations}
                onJump=${jumpToMark}
                onClip=${addClipFromMark}
                onClipToNext=${addClipFromMarkToNextMark}
                onPatchAnnotation=${journal.upsertMarkAnnotation} />`}

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
              onExportSortie=${exportSortieMp4}
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
              onChangeOverlaySize=${setOverlaySize}
              getCurrentVideoSec=${currentGlobalSec}
              clipAnnotations=${journal.clipAnnotations}
              onPatchClipAnnotation=${journal.upsertClipAnnotation}
              marks=${marks}
              markAnnotations=${journal.markAnnotations}
              onJumpToMark=${jumpToMark} />
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
//
// TODO(timeline-zoom): pilots want to zoom into a slice of the IAS
// stripchart with a minimap overview, video-editor style. Deferred to
// a follow-up PR — see local-plans/ when started.
const LogTimeline = ({ log, sync, videoT, anchorLabel = 'anchor',
                       marks = [],
                       clips = [], clipAnnotations = {}, markAnnotations = {},
                       onLogTakeoffPick, onSeekVideo }) => {
  const W = 1100;
  const PAD = 4;
  // Top lane reserved for clip spans; mark ticks shift down by this
  // amount so the two layers don't overlap. The total SVG height is the
  // original 80 px IAS chart plus the clip lane stacked on top, so the
  // clip rects (y=0..14) sit above the IAS trace rather than sharing a
  // canvas with it.
  const CLIP_LANE_H = 16;
  // Hide a mark's text label when its left neighbor is within this
  // many pixels — on long flights with many marks the labels mash
  // together (e.g. "0708090910 11 12") and become unreadable. The
  // tick line and <title> tooltip still render in both cases, so
  // hovering surfaces the label.
  const MIN_LABEL_PX = 28;
  const H = 80 + CLIP_LANE_H;

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
        ${clips.map(c => {
          if (!Number.isFinite(c.startMs) || !Number.isFinite(c.endMs)) return null;
          if (c.endMs < tMin || c.startMs > tMax) return null;
          const x0 = xOf(Math.max(c.startMs, tMin));
          const x1 = xOf(Math.min(c.endMs, tMax));
          const w  = Math.max(1, x1 - x0);
          const ann = c.id ? clipAnnotations[c.id] : null;
          const label = (ann && ann.label) ? ann.label : (c.label || '');
          return html`
            <g>
              <rect class="replay-timeline-clip"
                    x=${x0.toFixed(1)} y="0"
                    width=${w.toFixed(1)} height="14">
                <title>${label}</title>
              </rect>
              <text class="replay-timeline-clip-label"
                    x=${(x0 + 3).toFixed(1)} y="10">
                ${label}
              </text>
            </g>`;
        })}
        ${(() => {
          // Sort by time and pre-compute showLabel so we can decide
          // per-mark whether its <text> would crowd a left neighbor.
          // .map() alone can't carry state across iterations cleanly.
          const sorted = marks
            .filter(m => m.logTimeMs >= tMin && m.logTimeMs <= tMax)
            .slice()
            .sort((a, b) => a.logTimeMs - b.logTimeMs);
          let lastShownX = -Infinity;
          return sorted.map(m => {
            const xNum = xOf(m.logTimeMs);
            const x = xNum.toFixed(1);
            const ann = markAnnotations
              ? markAnnotations[String(m.value) + ':' + String(m.logTimeMs)]
              : null;
            const titleText = m.label + (ann && ann.name ? ' — ' + ann.name : '');
            const showLabel = xNum - lastShownX >= MIN_LABEL_PX;
            if (showLabel) lastShownX = xNum;
            return html`
              <line x1=${x} y1=${CLIP_LANE_H} x2=${x} y2=${H}
                    stroke="#7dd3fc" stroke-width="1" stroke-opacity="0.55">
                <title>${titleText}</title>
              </line>
              ${showLabel && html`
                <text x=${(xNum + 2).toFixed(1)} y=${(H - 4).toFixed(1)}
                      fill="#7dd3fc" font-size="10" font-family="monospace">
                  ${m.label}
                  <title>${titleText}</title>
                </text>`}`;
          });
        })()}
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
