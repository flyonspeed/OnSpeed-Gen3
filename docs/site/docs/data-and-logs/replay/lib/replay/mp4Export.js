// mp4Export.js — public API for the WebCodecs MP4 export pipeline.
//
// The heavy work (Mediabunny demux + VideoDecoder + composite +
// VideoEncoder + Muxer) runs in a Web Worker — see exportWorker.js for
// the implementation. This file is the main-thread shim: it spawns the
// worker, posts the export parameters, services per-frame SVG-string
// requests from the worker, and resolves with the final MP4 blob.
//
// Why a worker:
//   - The Mediabunny demux + WebCodecs decode+encode chain runs at
//     ~30-60 frames/sec wall-clock. On the main thread that blocks
//     UI: button clicks, scroll, even Preact rerenders of the live
//     preview pause until the export finishes. In a worker, the
//     UI stays interactive throughout.
//   - The worker uses WebCodecs' `dequeue` event for back-pressure
//     instead of polling `encodeQueueSize` through `setTimeout(0)` —
//     also lower-latency and one fewer source of jitter.
//
// Why SVG rendering stays on the main thread:
//   - Preact renders into the DOM. Workers have no DOM. The page
//     mounts a hidden detached node and uses the page-supplied
//     `renderOverlaySvg(m5State, displayTypeOverride?)` callback;
//     XMLSerializer turns the resulting SVGElement into a string
//     which postMessage ships to the worker. The worker calls
//     `createImageBitmap(new Blob([svg]))` for the actual rasterise
//     — that's the expensive part, and it's off-thread.
//
// Public API: `exportClipAsMp4`, `exportOverlayOnly`,
// `isMp4ExportSupported`, `isOverlayExportSupported`,
// `clipToVideoWindow`, `expectedFrameCount`, `downloadBlob`,
// `OVERLAY_MODE_IDS`, `OVERLAY_MODE_ORDER`,
// `rotationFromTkhdMatrix`, `MP4_EXPORT_INTERNAL`, `computeBitrate`.
// Same shape as the pre-worker module so callers don't change.

import {
  MSG_EXPORT_CLIP,
  MSG_EXPORT_OVERLAY,
  MSG_SVG_FRAME,
  MSG_CANCEL,
  MSG_SVG_REQUEST,
  MSG_PROGRESS,
  MSG_DONE,
  MSG_ERROR,
} from './exportWorkerProtocol.js';

// Browser feature gate. Worker is required (we always spawn one); both
// VideoEncoder and VideoDecoder are required because the worker uses
// them. OffscreenCanvas + createImageBitmap round out the codec path.
export function isMp4ExportSupported() {
  if (typeof window === 'undefined') return false;
  if (typeof Worker === 'undefined') return false;
  if (!('VideoEncoder' in window)) return false;
  if (!('VideoDecoder' in window)) return false;
  if (!('OffscreenCanvas' in window)) return false;
  return true;
}

export function isMp4AudioExportSupported() {
  if (typeof window === 'undefined') return false;
  if (typeof Blob === 'undefined' || !Blob.prototype.slice) return false;
  return true;
}

const DEFAULT_FRAMERATE = 30;

// Bitrate target as a function of pixel count + framerate. Mirrors the
// worker's identical helper so callers that need bitrate hints (UI
// previews, future log-analysis pages) get the same number either side
// of the worker boundary.
export function computeBitrate(width, height, framerate) {
  const w = Number.isFinite(width)     && width     > 0 ? width     : 1920;
  const h = Number.isFinite(height)    && height    > 0 ? height    : 1080;
  const f = Number.isFinite(framerate) && framerate > 0 ? framerate : 30;
  const pixelsPerSec = w * h * f;
  const bitsPerPixel = (w >= 2560) ? 0.15 : 0.13;
  return Math.round(pixelsPerSec * bitsPerPixel);
}

// Derive the source video's display rotation from its tkhd matrix.
// Used by the smoke tests as a round-trip lock on the rotation algebra;
// the worker reads rotation directly via `track.getRotation()` in the
// production path.
//
// Matrix values arrive either as raw int32 16.16 fixed-point (the ISO
// BMFF wire format) or as already-unpacked floats. Normalize before
// reading.
export function rotationFromTkhdMatrix(matrix) {
  if (!matrix || matrix.length < 9) return 0;
  const maxMag = Math.max(
    Math.abs(matrix[0] || 0),
    Math.abs(matrix[1] || 0),
    Math.abs(matrix[3] || 0),
    Math.abs(matrix[4] || 0),
  );
  const scale = maxMag > 2 ? 1 / 65536 : 1;
  const a = matrix[0] * scale;
  const b = matrix[1] * scale;
  const eps = 0.01;
  if (Math.abs(a - 1) < eps && Math.abs(b)     < eps) return 0;
  if (Math.abs(a)     < eps && Math.abs(b - 1) < eps) return 90;
  if (Math.abs(a + 1) < eps && Math.abs(b)     < eps) return 180;
  if (Math.abs(a)     < eps && Math.abs(b + 1) < eps) return 270;
  return 0;
}

// Pure helper: clip [startMs, endMs] in log time → [start, end] in video
// seconds, clamped to the video duration. Returns null if the clip
// falls entirely outside the video window or is inverted.
export function clipToVideoWindow(clip, sync, videoDurationSec) {
  if (!clip || !sync) return null;
  if (!Number.isFinite(sync.logTakeoffMs) ||
      !Number.isFinite(sync.videoTakeoffSec)) return null;
  if (!Number.isFinite(clip.startMs) || !Number.isFinite(clip.endMs)) return null;
  if (clip.endMs <= clip.startMs) return null;
  const startVideoSec = sync.videoTakeoffSec + (clip.startMs - sync.logTakeoffMs) / 1000;
  const endVideoSec   = sync.videoTakeoffSec + (clip.endMs   - sync.logTakeoffMs) / 1000;
  const start = Math.max(0, startVideoSec);
  const end   = Number.isFinite(videoDurationSec) ? Math.min(videoDurationSec, endVideoSec) : endVideoSec;
  if (end <= start) return null;
  return { startVideoSec: start, endVideoSec: end };
}

export function expectedFrameCount(clipDurationSec, frameRate) {
  if (!Number.isFinite(clipDurationSec) || clipDurationSec <= 0) return 0;
  if (!Number.isFinite(frameRate) || frameRate <= 0) return 0;
  return Math.max(1, Math.round(clipDurationSec * frameRate));
}

// Trigger a browser download for a Blob.
export function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 60_000);
}

// ---------------------------------------------------------------------
// Worker bridge — spawn the worker, wire up the SVG-request bridge,
// drive an export to completion.
// ---------------------------------------------------------------------

// The worker is a module worker; mediabunny is imported as ESM and
// m5sim's loader switches to fetch+eval when it sees no `document`.
// new URL(..., import.meta.url) resolves to the docs-site origin so
// the browser fetches the worker source from the same place as the
// page's own JS.
function spawnWorker() {
  return new Worker(new URL('./exportWorker.js', import.meta.url), {
    type: 'module',
  });
}

// Drive a single export end-to-end. `startMsg` is the seed message
// (export-clip or export-overlay) with all params. `renderOverlaySvg`
// is the page callback; we call it on each svg-request, XMLSerialize
// the result, and post the string back.
//
// Returns a Promise that resolves with the worker's `done` payload
// (already converted to Blob form on the main side).
function driveExport({ startMsg, transferList, renderOverlaySvg, onProgress, signal }) {
  return new Promise((resolve, reject) => {
    const worker = spawnWorker();
    let settled = false;
    const finish = (fn, val) => {
      if (settled) return;
      settled = true;
      try { worker.terminate(); } catch (_) {}
      fn(val);
    };

    // Wire abort → worker cancel. The worker's runCompositeExport /
    // runOverlayExport check signal.aborted in every loop step and
    // throw AbortError; the worker then posts MSG_ERROR with
    // aborted=true. We translate that to a DOMException on the main
    // side to match the pre-worker contract.
    let abortListener = null;
    if (signal) {
      if (signal.aborted) {
        try { worker.terminate(); } catch (_) {}
        reject(new DOMException('aborted', 'AbortError'));
        return;
      }
      abortListener = () => {
        try { worker.postMessage({ type: MSG_CANCEL }); } catch (_) {}
      };
      signal.addEventListener('abort', abortListener);
    }
    const cleanupAbort = () => {
      if (signal && abortListener) signal.removeEventListener('abort', abortListener);
    };

    worker.addEventListener('message', (ev) => {
      const msg = ev.data;
      if (!msg || !msg.type) return;

      if (msg.type === MSG_SVG_REQUEST) {
        // Page-side render. Use try/catch; on failure post null so the
        // worker falls through to "no overlay this frame" rather than
        // wedging the pipeline.
        let svgString = null;
        try {
          const svgEl = renderOverlaySvg
            ? renderOverlaySvg(msg.m5State,
                Number.isFinite(msg.modeId) ? msg.modeId : undefined)
            : null;
          if (svgEl) {
            svgString = new XMLSerializer().serializeToString(svgEl);
          }
        } catch (e) {
          // eslint-disable-next-line no-console
          console.warn('renderOverlaySvg failed for frame', msg.frameId, e);
        }
        worker.postMessage({
          type: MSG_SVG_FRAME, frameId: msg.frameId, svgString,
        });
        return;
      }

      if (msg.type === MSG_PROGRESS) {
        if (onProgress) onProgress(msg);
        return;
      }

      if (msg.type === MSG_DONE) {
        cleanupAbort();
        if (msg.entries) {
          // Overlay-only: rebuild the Map<modeId, Blob>.
          const out = new Map();
          for (const [id, buf] of msg.entries) {
            out.set(id, new Blob([buf], { type: msg.mime || 'video/mp4' }));
          }
          finish(resolve, out);
        } else {
          // Composite: single buffer → single Blob.
          const blob = new Blob([msg.buffer], { type: msg.mime || 'video/mp4' });
          finish(resolve, blob);
        }
        return;
      }

      if (msg.type === MSG_ERROR) {
        cleanupAbort();
        if (msg.aborted) {
          finish(reject, new DOMException('aborted', 'AbortError'));
        } else {
          finish(reject, new Error(msg.error || 'export failed'));
        }
        return;
      }
    });

    worker.addEventListener('error', (e) => {
      cleanupAbort();
      finish(reject, new Error(
        `export worker crashed: ${e?.message || 'unknown error'}`));
    });
    worker.addEventListener('messageerror', () => {
      cleanupAbort();
      finish(reject, new Error('export worker: messageerror (un-cloneable payload).'));
    });

    // Kick off.
    try {
      worker.postMessage(startMsg, transferList || []);
    } catch (e) {
      cleanupAbort();
      finish(reject, new Error(`failed to post export params to worker: ${e?.message || e}`));
    }
  });
}

// ---------------------------------------------------------------------
// Public API — composite export.
// ---------------------------------------------------------------------
//
// Options (unchanged from the pre-worker version):
//   sourceFile:      File | Blob — REQUIRED. The original video file.
//   videoEl:         HTMLVideoElement — used ONLY for videoWidth /
//                    videoHeight / duration reads.
//   clip:            { startMs, endMs, label? }
//   sync:            { logTakeoffMs, videoTakeoffSec }
//   log:             parsed log
//   cppWireFrames:   pre-pass output { frames: Uint8Array[] }
//   renderOverlaySvg: (m5State, displayTypeOverride?) → SVGElement.
//                    Same shape as before — the page callback is
//                    unchanged. The shim XMLSerializes the result and
//                    ships the string to the worker.
//   presentationTau: { lateralSec, verticalSec } | null
//   displayMode:     int 0-4 (M5 mode id)
//   outputWidth:     int | null — null = match source
//   bitrate:         int | null
//   framerate:       int | null
//   onProgress:      ({ frame, totalFrames, encodedSec, totalSec }) → void
//   signal:          AbortSignal | null
//
// Returns: Promise<Blob>
export async function exportClipAsMp4({
  videoEl,
  clip,
  sync,
  log,
  cppWireFrames,
  renderOverlaySvg,
  sourceFile      = null,
  presentationTau = null,
  displayMode     = 0,
  outputWidth     = null,
  bitrate         = null,
  framerate       = null,
  onProgress      = null,
  signal          = null,
}) {
  if (!isMp4ExportSupported()) {
    throw new Error(
      'MP4 export requires Chrome or Edge desktop. WebCodecs ' +
      '(VideoEncoder + VideoDecoder) is incomplete in Safari / Firefox.');
  }
  if (!videoEl)        throw new Error('exportClipAsMp4: videoEl required');
  if (!clip)           throw new Error('exportClipAsMp4: clip required');
  if (!sync || !Number.isFinite(sync.logTakeoffMs) ||
      !Number.isFinite(sync.videoTakeoffSec)) {
    throw new Error('exportClipAsMp4: complete sync anchor required ' +
                    '(set both video and log anchors before exporting).');
  }
  if (!log)            throw new Error('exportClipAsMp4: log required');
  if (!cppWireFrames || !cppWireFrames.frames) {
    throw new Error(
      'exportClipAsMp4: cppWireFrames required (build the replay engine ' +
      'pre-pass first).');
  }
  if (!videoEl.videoWidth || !videoEl.videoHeight) {
    throw new Error('exportClipAsMp4: video metadata not loaded yet');
  }
  if (!sourceFile || typeof sourceFile.slice !== 'function') {
    throw new Error(
      'exportClipAsMp4: sourceFile (File/Blob) is required. The export ' +
      'demuxes the source via Mediabunny; without it the input video ' +
      'cannot be decoded.');
  }

  const params = {
    clip, sync, log, cppWireFrames,
    sourceFile,
    presentationTau,
    displayMode,
    outputWidth, bitrate, framerate,
    videoDurationSec: videoEl.duration,
  };
  const startMsg = { type: MSG_EXPORT_CLIP, params };
  return await driveExport({
    startMsg,
    renderOverlaySvg,
    onProgress: onProgress
      ? (m) => onProgress({
          frame: m.frame, totalFrames: m.totalFrames,
          encodedSec: m.encodedSec, totalSec: m.totalSec,
        })
      : null,
    signal,
  });
}

// ---------------------------------------------------------------------
// Public API — overlay-only export.
// ---------------------------------------------------------------------

export const OVERLAY_MODE_IDS = Object.freeze({
  'energy':     0,
  'attitude':   1,
  'indexer':    2,
  'decel':      3,
  'historic-g': 4,
});

export const OVERLAY_MODE_ORDER = Object.freeze([
  'indexer',
  'attitude',
  'energy',
  'decel',
  'historic-g',
]);

export function isOverlayExportSupported() {
  if (typeof window === 'undefined') return false;
  if (typeof Worker === 'undefined') return false;
  if (!('VideoEncoder' in window)) return false;
  if (!('OffscreenCanvas' in window)) return false;
  return true;
}

// Returns: Promise<Map<modeId, Blob>>.
export async function exportOverlayOnly({
  clip,
  sync,
  log,
  cppWireFrames,
  renderOverlaySvg,
  modes             = null,
  sourceVideoInfo   = null,
  presentationTau   = null,
  background        = '#000',
  outputWidth       = null,
  outputHeight      = null,
  framerate         = null,
  bitrate           = null,
  durationSec       = null,
  onProgress        = null,
  signal            = null,
} = {}) {
  if (!isOverlayExportSupported()) {
    throw new Error(
      'Overlay-only export requires Chrome or Edge desktop. WebCodecs ' +
      'VideoEncoder + OffscreenCanvas needed.');
  }
  if (!clip) throw new Error('exportOverlayOnly: clip required');
  if (!sync || !Number.isFinite(sync.logTakeoffMs) ||
      !Number.isFinite(sync.videoTakeoffSec)) {
    throw new Error('exportOverlayOnly: complete sync anchor required.');
  }
  if (!log) throw new Error('exportOverlayOnly: log required');
  if (!cppWireFrames || !cppWireFrames.frames) {
    throw new Error('exportOverlayOnly: cppWireFrames required.');
  }
  if (!renderOverlaySvg) {
    throw new Error('exportOverlayOnly: renderOverlaySvg callback required.');
  }

  // Validate modes up front so callers see the same error message they
  // saw pre-worker.
  const requested = (modes && modes.length > 0) ? modes : OVERLAY_MODE_ORDER;
  for (const m of requested) {
    if (!(m in OVERLAY_MODE_IDS)) {
      throw new Error(`exportOverlayOnly: unknown mode id "${m}". ` +
        `Known: ${Object.keys(OVERLAY_MODE_IDS).join(', ')}.`);
    }
  }

  const params = {
    clip, sync, log, cppWireFrames,
    modes: requested,
    sourceVideoInfo,
    presentationTau,
    background,
    outputWidth, outputHeight,
    framerate, bitrate, durationSec,
  };
  const startMsg = { type: MSG_EXPORT_OVERLAY, params };
  return await driveExport({
    startMsg,
    renderOverlaySvg,
    onProgress: onProgress
      ? (m) => onProgress({
          mode: m.mode, frame: m.frame, totalFrames: m.totalFrames,
          encodedSec: m.encodedSec, totalSec: m.totalSec,
        })
      : null,
    signal,
  });
}

// ---------------------------------------------------------------------
// Diagnostics export — pre-worker callers (and the smoke tests) read
// these constants directly. Keep the shape so the smoke test passes
// without a touch.
// ---------------------------------------------------------------------
export const MP4_EXPORT_INTERNAL = Object.freeze({
  DEFAULT_FRAMERATE,
});
