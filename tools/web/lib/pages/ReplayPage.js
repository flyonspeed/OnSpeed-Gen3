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

import { html, useState, useEffect, useRef, useCallback }
  from '../vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';
import { Mode0, Mode1, Mode3 } from '../modes.js';
import { parseLog, findRowAt } from '../replay/parseLog.js';
import { parseConfig } from '../replay/config.js';
import { buildReplay } from '../replay/logReplay.js';
import { detectTakeoff, detectTakeoffWithKind, downsampleForPlot } from '../replay/syncDetect.js';

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

export const ReplayPage = () => {
  const [videoFile, setVideoFile] = useState(null);
  const [videoUrl, setVideoUrl]   = useState(null);
  const [log, setLog]             = useState(null);
  const [logFilename, setLogFilename] = useState('');
  const [cfg, setCfg]             = useState(null);
  const [cfgFilename, setCfgFilename] = useState('');
  const [replay, setReplay]       = useState(null);   // buildReplay() result
  const [sync, setSync]           = useState(null);
  const [videoT, setVideoT]       = useState(0);
  const [modeId, setModeId]       = useState('energy');
  const [overlayVisible, setOverlayVisible] = useState(true);
  const [parseErr, setParseErr]   = useState(null);
  // Anchor kind from detectTakeoffWithKind: 'crosswind' | 'rotation'
  // | 'none'. Used in the status text + timeline label so the pilot
  // knows what event they're syncing against.
  const [anchorKind, setAnchorKind] = useState('none');

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
      const parsed = parseConfig(text);
      if (!parsed.flapsArray.length) throw new Error('no flap detents in config');
      setCfg(parsed);
      setCfgFilename(f.name);
    } catch (err) {
      setParseErr(`Could not parse config: ${err.message}`);
      setCfg(null);
    }
  };

  // Rebuild the replay pipeline whenever the log or config changes.
  // Doing this in an effect (not inline in render) keeps the
  // expensive smooth-accels + lever-sweep precomputation from
  // running every frame.
  useEffect(() => {
    if (!log) { setReplay(null); return; }
    setReplay(buildReplay(log, cfg));
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

  // ---------- Compute current record from log ----------------------

  const rec = (() => {
    if (!log || !replay) return null;
    // When the indexer is paused for re-sync, the overlay freezes at
    // pausedLogMs and ignores the video clock. Pilot scrubs the video
    // to the matching frame and clicks "Attach here".
    if (Number.isFinite(pausedLogMs)) {
      const rowIdx = findRowAt(log, pausedLogMs);
      if (rowIdx < 0) return null;
      return replay.recordAt(rowIdx);
    }
    if (!sync) return null;
    if (!Number.isFinite(sync.videoTakeoffSec) || !Number.isFinite(sync.logTakeoffMs)) return null;
    const tMs = videoToLogMs(videoT, sync);
    if (!Number.isFinite(tMs)) return null;
    const rowIdx = findRowAt(log, tMs);
    if (rowIdx < 0) return null;
    return replay.recordAt(rowIdx);
  })();

  const ModeC = MODES.find(m => m.id === modeId)?.C ?? Mode0;
  const syncReady = sync && Number.isFinite(sync.videoTakeoffSec) && Number.isFinite(sync.logTakeoffMs);

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
              ${cfg.flapsArray.length} flap detents loaded · ${cfgFilename}
            </span>`}
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
                          onLogTakeoffPick=${(tMs) => setSync(prev => ({
                            logTakeoffMs: tMs,
                            videoTakeoffSec: prev?.videoTakeoffSec ?? null,
                          }))}
                          onSeekVideo=${(tSec) => {
                            const v = videoRef.current;
                            if (v) v.currentTime = Math.max(0, tSec);
                          }} />
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
