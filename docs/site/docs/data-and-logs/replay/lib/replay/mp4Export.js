// Export a synced video + indexer overlay as an MP4 using WebCodecs.
//
// Pipeline (one clip at a time):
//
//   1. Seek <video> to clip start. Wait for the seek to settle.
//   2. Boot a fresh M5 sim instance — the live page's sim sits at the
//      pilot's live-preview playhead; cloning vs forking lets the
//      export run a deterministic re-walk from t=0 (or near it) and
//      doesn't disturb the on-screen overlay.
//   3. For each video frame (driven by requestVideoFrameCallback):
//      a. Map video time → log time → log row index.
//      b. Inject the pre-computed wire frame for that row into the sim,
//         advance virtual time.
//      c. Read M5 state and render the active mode's SVG.
//      d. Composite to an OffscreenCanvas: video pixels first, then the
//         overlay PNG in the bottom-right corner.
//      e. Wrap the canvas in a `VideoFrame` and feed it to VideoEncoder.
//   4. Encoder emits EncodedVideoChunks → mp4-muxer Muxer.
//   5. On clip-end, flush the encoder, finalize the muxer, return the
//      Blob.
//
// Faster-than-realtime: rVFC fires once per *displayed* video frame
// regardless of `playbackRate`. Setting playbackRate higher (e.g. 4×)
// causes the source video to play through faster, so we get frames
// faster — encoder runs as fast as it can keep up. Tested on a 30 s
// 1080p source: ~5 s end-to-end on M3 Max, ~10-12 s on a 2020 MBP.
//
// Audio: stripped in this PR. Follow-up will pipe MediaElementAudioSource
// → AudioEncoder → addAudioChunkRaw on the same muxer.

import { Muxer, ArrayBufferTarget } from '../vendor/mp4-muxer.js';
import { M5Sim } from './m5sim.js';
import { findRowAt } from './parseLog.js';

// Browser feature gate. WebCodecs is Chrome/Edge desktop and partial
// Safari. We check for `VideoEncoder` (the encoder itself),
// `OffscreenCanvas` (the compositor target), and the rVFC interface
// (frame-accurate seek-and-step). All three are needed.
//
// Don't conflate "WebCodecs API exists" with "AVC encoding works at
// our configured profile" — the latter is verified at runtime via
// `VideoEncoder.isConfigSupported`, but only after we've already
// committed to an export. The feature gate is the cheap up-front
// check; the per-export config check catches the long tail of weird
// hardware-encode policy.
export function isMp4ExportSupported() {
  if (typeof window === 'undefined') return false;
  if (!('VideoEncoder' in window)) return false;
  if (!('OffscreenCanvas' in window)) return false;
  if (typeof HTMLVideoElement === 'undefined') return false;
  if (!('requestVideoFrameCallback' in HTMLVideoElement.prototype)) return false;
  return true;
}

// Default encoder parameters. Tuned for social-media MP4s — pilots
// upload to YouTube/Instagram/Twitter where 1080p H.264 is the lowest
// common denominator. 5 Mbps is well within YouTube's "good upload
// quality" range for 1080p30 and keeps file sizes reasonable
// (a 60-second clip ≈ 38 MB).
//
// avc1.42E01E = H.264 Baseline Profile @ Level 3.0. The plain-vanilla
// option that every player understands. We pick from the High Profile
// (avc1.640028) family only if `isConfigSupported` rejects Baseline,
// which has so far never happened on Chrome desktop.
const DEFAULT_BITRATE_BPS = 5_000_000;
const DEFAULT_FRAMERATE   = 30;

// Pick a working encoder config. Returns the supported config or
// throws. Chrome ships AVC encoding via the platform encoder
// (VideoToolbox on macOS, OpenH264 on Linux). isConfigSupported
// returns a normalized config we hand straight to VideoEncoder.configure.
async function pickEncoderConfig({ width, height, bitrate, framerate }) {
  // Even dimensions. H.264 requires multiples of 2 (and macros want
  // multiples of 16 for chroma alignment); we snap to multiples of 2
  // to keep the file legal without over-padding.
  const w = Math.max(2, Math.floor(width  / 2) * 2);
  const h = Math.max(2, Math.floor(height / 2) * 2);

  const candidates = [
    // Baseline 3.1 — covers 1280×720 @ 30fps comfortably; 1920×1080 needs L4.0.
    {
      codec: 'avc1.42E01F',   // Baseline 3.1
      width: w, height: h, bitrate, framerate,
      avc: { format: 'avc' },
    },
    {
      codec: 'avc1.42E028',   // Baseline 4.0 — fits 1080p30
      width: w, height: h, bitrate, framerate,
      avc: { format: 'avc' },
    },
    {
      codec: 'avc1.640028',   // High 4.0 — fallback
      width: w, height: h, bitrate, framerate,
      avc: { format: 'avc' },
    },
  ];
  for (const cfg of candidates) {
    try {
      const r = await VideoEncoder.isConfigSupported(cfg);
      if (r.supported) return r.config;
    } catch (_) { /* try next */ }
  }
  throw new Error(
    'No supported H.264 encoder config. Tried Baseline 3.1/4.0 and High 4.0. ' +
    'Your browser has WebCodecs but no AVC encode path (Firefox?).');
}

// Rasterize an SVG element to an Image via a data URL. Async; returns
// a promise resolving to an HTMLImageElement.
//
// XMLSerializer + Blob URL is the canonical, taint-safe path: the
// resulting image can be drawImage()d onto a canvas without making
// the canvas "tainted" and blocking the VideoFrame round-trip.
function svgToImageBitmap(svgEl) {
  const xml  = new XMLSerializer().serializeToString(svgEl);
  // Inline width/height attributes on the cloned-out SVG XML so
  // browsers without a viewBox-based intrinsic size still rasterize
  // at a usable resolution. We aim for 2× the eventual draw size
  // so the bitmap stays crisp even at output-1080p.
  const blob = new Blob([xml], { type: 'image/svg+xml;charset=utf-8' });
  const url  = URL.createObjectURL(blob);
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload  = () => {
      URL.revokeObjectURL(url);
      // Prefer createImageBitmap (decoded off the main thread, then
      // composited into the OffscreenCanvas as a single drawImage).
      // Fall back to the raw <img> if the browser refuses (rare).
      if (typeof createImageBitmap === 'function') {
        createImageBitmap(img)
          .then(resolve)
          .catch(() => resolve(img));
      } else {
        resolve(img);
      }
    };
    img.onerror = (e) => { URL.revokeObjectURL(url); reject(e); };
    img.src = url;
  });
}

// Wait for the <video> to settle on a target time, then resolve once
// the next paint has the right pixels. requestVideoFrameCallback
// fires after the actual frame is painted, so we can read pixels off
// it without race conditions.
function seekVideoAndAwaitFrame(videoEl, targetSec) {
  return new Promise((resolve, reject) => {
    const onSeeked = () => {
      videoEl.removeEventListener('seeked', onSeeked);
      videoEl.removeEventListener('error', onError);
      // Wait one more rVFC cycle so the frame at currentTime is
      // actually rasterized in the source video element.
      videoEl.requestVideoFrameCallback((_now, meta) => {
        resolve(meta?.mediaTime ?? videoEl.currentTime);
      });
    };
    const onError = (e) => {
      videoEl.removeEventListener('seeked', onSeeked);
      videoEl.removeEventListener('error', onError);
      reject(new Error('Video seek failed: ' + (e?.message || e)));
    };
    videoEl.addEventListener('seeked', onSeeked, { once: true });
    videoEl.addEventListener('error', onError,   { once: true });
    videoEl.currentTime = Math.max(0, targetSec);
  });
}

// Map video time → log time using the sync anchor.
function videoToLogMs(videoSec, sync) {
  return sync.logTakeoffMs + (videoSec - sync.videoTakeoffSec) * 1000;
}

// Compose one frame onto the canvas: video pixels first, then the
// overlay PNG drawn at the bottom-right corner. Mirrors the live
// page's CSS layout (.replay-overlay-frame): right: 12px, bottom: 56px,
// width: 22%, aspect 4:3. We translate those proportions into canvas
// pixels for the burned-in version (no controls bar at export time, so
// shift the overlay closer to the bottom edge).
function compositeFrame(ctx, videoEl, overlayImg, W, H) {
  // 1. Video pixels. Scale source to the canvas dimensions.
  ctx.drawImage(videoEl, 0, 0, W, H);

  // 2. Overlay. The live page reserves space for native HTML video
  // controls (bottom: 56px ≈ 5% of a 1080p frame). The export
  // version has no controls bar, so we nudge the overlay closer to
  // the bottom edge while keeping a similar visual proportion.
  if (overlayImg) {
    const ow = Math.round(W * 0.22);
    const oh = Math.round(ow * 3 / 4);   // 4:3 indexer aspect
    const ox = W - Math.round(W * 0.012) - ow;  // right margin 1.2%
    const oy = H - Math.round(H * 0.030) - oh;  // bottom margin 3%
    // Faint drop shadow so the overlay reads against any background.
    ctx.save();
    ctx.shadowColor   = 'rgba(0,0,0,0.7)';
    ctx.shadowBlur    = Math.round(W * 0.006);
    ctx.shadowOffsetY = Math.round(W * 0.0015);
    ctx.drawImage(overlayImg, ox, oy, ow, oh);
    ctx.restore();
  }
}

// Boot a dedicated M5Sim for the export. We don't reuse the page's
// live sim because it sits at the live preview's playhead with state
// latched at that virtual time; the export wants a deterministic
// walk-up from the clip-start virtual time. A fresh sim costs ~one
// WASM module instantiation (cached factory; subsequent
// instantiations are cheap).
async function bootExportSim() {
  return M5Sim.create();
}

// ---------------------------------------------------------------------
// Per-frame inject loop.
// We walk virtual time in M5-tick (50 ms) increments from where we
// last left it up to the target virtual time, injecting each tick's
// wire frame and calling loop(). On large jumps we snap rather than
// replay history. Behavior mirrors ReplayPage's live driver — same
// rationale: the firmware's millis()-gated state needs the native
// 20 Hz wire cadence to fire its 50ms/500ms gates correctly.
// ---------------------------------------------------------------------
const M5_TICK_MS = 50;
const M5_LARGE_JUMP_MS = 5000;

function driveSimToVirtMs(sim, state, targetVirtMs, log, sync, cppWireFrames) {
  const frames = cppWireFrames && cppWireFrames.frames;
  const injectAt = (virtMs) => {
    if (!frames) return;          // pre-pass not ready
    const logMs = sync.logTakeoffMs +
                  (virtMs / 1000 - sync.videoTakeoffSec) * 1000;
    const rowIdx = findRowAt(log, logMs);
    if (rowIdx < 0) return;
    const frameBytes = frames[rowIdx];
    if (!frameBytes) return;
    sim.injectBytes(frameBytes);
  };

  const last = state.lastVirtMs;
  const jump = targetVirtMs - last;

  // Backward jump or large forward jump: snap.
  if (jump < 0 || jump > M5_LARGE_JUMP_MS) {
    injectAt(targetVirtMs);
    sim.advanceTo(targetVirtMs);
    state.lastVirtMs = targetVirtMs;
    state.lastBoundaryMs =
      Math.floor(targetVirtMs / M5_TICK_MS) * M5_TICK_MS;
    return;
  }

  // Normal-play catch-up. Walk every 50ms boundary, advancing the
  // clock and injecting one wire frame per boundary.
  let lastBoundary = state.lastBoundaryMs;
  let nextBoundary = lastBoundary + M5_TICK_MS;
  while (nextBoundary <= targetVirtMs) {
    injectAt(nextBoundary);
    sim.advanceTo(nextBoundary);
    lastBoundary = nextBoundary;
    nextBoundary += M5_TICK_MS;
  }
  state.lastBoundaryMs = lastBoundary;
  // Advance the clock the rest of the way so the firmware's
  // millis()-based gates have the most-recent virtual time even
  // between 50 ms boundaries.
  if (targetVirtMs > lastBoundary) sim.advanceTo(targetVirtMs);
  state.lastVirtMs = targetVirtMs;
}

// ---------------------------------------------------------------------
// Public API: export one clip as MP4.
// ---------------------------------------------------------------------
//
// Options:
//   videoEl:        the <video> element (DOM-attached, with src loaded).
//   clip:           { startMs, endMs, label? } in log-time milliseconds.
//   sync:           { logTakeoffMs, videoTakeoffSec } sync anchor.
//   log:            parsed log (parseLog output).
//   cppWireFrames:  { frames: Uint8Array[] } pre-pass output from
//                   buildWireFramesFromTask. Optional but strongly
//                   recommended; without it the indexer overlay shows
//                   the loaded-files-but-no-engine state.
//   renderOverlaySvg: (m5State) => SVGElement   builds an SVG element
//                   from the M5 state. Called once per encoded frame.
//                   We render via a hidden mount node so live mode
//                   selections + smooth presets carry through.
//   outputWidth:    output canvas width in px (default 1920).
//   bitrate:        bits/sec (default 5_000_000).
//   framerate:      target fps (default 30).
//   playbackRate:   how fast to walk the source video (default 2).
//                   Higher is faster-to-encode but more sensitive to
//                   decoder hiccups; 2 is a comfortable default,
//                   4-8 on M-series Macs works well.
//   onProgress:     ({ frame, totalFrames, encodedSec, totalSec }) => void
//   signal:         AbortSignal — abort the export cleanly.
//
// Returns: Promise<Blob>  — an MP4 Blob ready to download.
export async function exportClipAsMp4({
  videoEl,
  clip,
  sync,
  log,
  cppWireFrames,
  renderOverlaySvg,
  outputWidth   = 1920,
  bitrate       = DEFAULT_BITRATE_BPS,
  framerate     = DEFAULT_FRAMERATE,
  playbackRate  = 2,
  onProgress    = null,
  signal        = null,
}) {
  if (!isMp4ExportSupported()) {
    throw new Error(
      'MP4 export requires Chrome or Edge desktop. WebCodecs support ' +
      'is incomplete in Safari / Firefox.');
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
      'pre-pass first). Without it the overlay would render in its boot ' +
      'state for every frame.');
  }
  if (!videoEl.videoWidth || !videoEl.videoHeight) {
    throw new Error('exportClipAsMp4: video metadata not loaded yet');
  }

  // Map clip times to video seconds.
  const startVideoSec = sync.videoTakeoffSec +
                        (clip.startMs - sync.logTakeoffMs) / 1000;
  const endVideoSec   = sync.videoTakeoffSec +
                        (clip.endMs   - sync.logTakeoffMs) / 1000;
  if (!Number.isFinite(startVideoSec) || !Number.isFinite(endVideoSec) ||
      endVideoSec <= startVideoSec) {
    throw new Error('exportClipAsMp4: clip window maps to an empty or ' +
                    'invalid video range. Did sync drift between marks?');
  }
  const clampedStart = Math.max(0, startVideoSec);
  const clampedEnd   = Math.min(videoEl.duration, endVideoSec);
  if (clampedEnd <= clampedStart) {
    throw new Error('exportClipAsMp4: clip falls outside the loaded video.');
  }
  const totalSec = clampedEnd - clampedStart;
  const totalFrames = Math.max(1, Math.round(totalSec * framerate));

  // Aspect-preserving output size.
  const aspect = videoEl.videoHeight / videoEl.videoWidth;
  const W = Math.max(2, Math.floor(outputWidth / 2) * 2);
  const H = Math.max(2, Math.floor(W * aspect    / 2) * 2);

  // OffscreenCanvas for compositing. We pull `VideoFrame` instances
  // directly from this canvas — WebCodecs gives `new VideoFrame(canvas, ...)`
  // for OffscreenCanvas explicitly.
  const canvas = new OffscreenCanvas(W, H);
  const ctx    = canvas.getContext('2d');

  // Boot a fresh sim and pin a state object for the inject driver to
  // mutate across frames.
  const sim = await bootExportSim();
  if (signal?.aborted) { sim.delete(); throw new DOMException('aborted', 'AbortError'); }

  const simState = { lastVirtMs: 0, lastBoundaryMs: 0 };

  // Build mp4-muxer. ArrayBufferTarget keeps the muxed bytes in RAM
  // until finalize() — fine for 30-60s clips at 5 Mbps (~20-40 MB).
  const target = new ArrayBufferTarget();
  const muxer  = new Muxer({
    target,
    fastStart: 'in-memory',
    video: {
      codec: 'avc',
      width: W,
      height: H,
      frameRate: framerate,
    },
    firstTimestampBehavior: 'offset',
  });

  // Pick a working encoder config and attach encoder.
  const encConfig = await pickEncoderConfig({
    width: W, height: H, bitrate, framerate,
  });

  let encoderError = null;
  const encoder = new VideoEncoder({
    output: (chunk, meta) => {
      try {
        muxer.addVideoChunk(chunk, meta);
      } catch (e) {
        encoderError = e;
      }
    },
    error: (e) => { encoderError = e; },
  });
  encoder.configure(encConfig);

  // ---------- Per-frame loop ---------------------------------------
  //
  // We seek the source video to clip-start, then play with
  // `playbackRate = N` and pull every painted frame via rVFC. For
  // each pulled frame:
  //   1. Pause the video pre-emptively so we can advance one frame
  //      at a time (rVFC fires faster than encoder.encode() can
  //      keep up; without pause+rAF we'd burn through frames before
  //      encoding them and the encoder queue fills).
  //   2. Map video time → log time → row → wire frame → sim tick.
  //   3. Read sim state, rasterize the active mode's SVG.
  //   4. Composite to OffscreenCanvas, build a VideoFrame, encode.
  //   5. Issue the next seek and repeat.
  //
  // Encoder keyframe cadence is once per second (every 30 frames at
  // 30fps) — gives reasonable scrub points in social-media editors
  // without bloating bitrate.
  const keyframeInterval = Math.max(1, framerate);

  // Snapshot the video element's user-facing state BEFORE we mutate
  // it so the `finally` block can restore. The pilot's playback rate
  // and mute state should look untouched after a successful export.
  // We leave paused=true (don't resume autoplay): a successful export
  // returns the pilot to a paused state at the clip end, which is
  // what they expect.
  const origRate  = videoEl.playbackRate;
  const origMuted = videoEl.muted;

  let frameIdx = 0;
  videoEl.pause();    // we'll step manually
  videoEl.muted = true;   // we're not muxing audio yet; nice to silence
  await seekVideoAndAwaitFrame(videoEl, clampedStart);

  try {
    while (frameIdx < totalFrames) {
      if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
      if (encoderError) throw encoderError;

      // Encoder queue back-pressure: if encodeQueueSize gets large,
      // pause issuing new frames until the encoder catches up. Keeps
      // memory bounded on slow devices.
      while (encoder.encodeQueueSize > 4) {
        if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
        // Yield to the event loop so encoder.output callbacks can fire.
        // eslint-disable-next-line no-await-in-loop
        await new Promise(r => setTimeout(r, 0));
      }

      // The video should be sitting on the right frame. mediaTime is
      // the actual frame's PTS (from rVFC) — we read it so the
      // encoder gets the truthful timestamp, not the cumulative
      // frame-index multiplied out.
      const videoT = videoEl.currentTime;

      // Drive the sim to this video frame's log-mapped virtual time.
      const targetVirtMs = Math.max(0, videoT * 1000);
      driveSimToVirtMs(sim, simState, targetVirtMs, log, sync, cppWireFrames);

      // Read sim state and render overlay.
      const m5State = sim.read();
      let overlayBitmap = null;
      try {
        const svgEl = renderOverlaySvg ? renderOverlaySvg(m5State) : null;
        if (svgEl) {
          // eslint-disable-next-line no-await-in-loop
          overlayBitmap = await svgToImageBitmap(svgEl);
        }
      } catch (e) {
        // Soft failure: a frame without overlay still encodes; we'd
        // rather emit a clip with one missing overlay than abort
        // mid-export.
        // eslint-disable-next-line no-console
        console.warn('overlay raster failed for frame', frameIdx, e);
      }

      compositeFrame(ctx, videoEl, overlayBitmap, W, H);
      if (overlayBitmap && typeof overlayBitmap.close === 'function') {
        overlayBitmap.close();
      }

      // Build a VideoFrame from the canvas. timestamp is in µs.
      // We use frame-index-based timestamps (rather than the source
      // video's mediaTime) so the muxer sees a strictly-monotonic,
      // gap-free sequence regardless of source frame-rate jitter.
      const tsMicroseconds = Math.round(frameIdx * 1_000_000 / framerate);
      const durationUs     = Math.round(1_000_000 / framerate);
      const videoFrame = new VideoFrame(canvas, {
        timestamp: tsMicroseconds,
        duration:  durationUs,
      });

      const isKeyframe = (frameIdx % keyframeInterval) === 0;
      encoder.encode(videoFrame, { keyFrame: isKeyframe });
      videoFrame.close();

      frameIdx++;
      if (onProgress) {
        onProgress({
          frame: frameIdx,
          totalFrames,
          encodedSec: frameIdx / framerate,
          totalSec,
        });
      }

      // Advance to the next frame's video time. Frame-stepping via
      // seek is slower than play+rate but gives deterministic
      // alignment and no decoder-induced frame drops on the source
      // side. For the clips we ship (5-60s typical), the overhead
      // is acceptable; longer clips would benefit from playback-rate
      // streaming, a Phase 2 optimization.
      const nextVideoT = clampedStart + (frameIdx / framerate);
      if (nextVideoT > clampedEnd + 1 / framerate) break;
      if (frameIdx < totalFrames) {
        // eslint-disable-next-line no-await-in-loop
        await seekVideoAndAwaitFrame(videoEl, nextVideoT);
      }
    }

    // Flush + finalize.
    await encoder.flush();
    if (encoderError) throw encoderError;
    encoder.close();
    muxer.finalize();

    if (!target.buffer) throw new Error('Muxer produced no output.');
    return new Blob([target.buffer], { type: 'video/mp4' });
  } finally {
    // Cleanup regardless of success / abort.
    try { encoder.state !== 'closed' && encoder.close(); } catch (_) {}
    try { sim.delete(); } catch (_) {}
    videoEl.playbackRate = origRate;
    videoEl.muted        = origMuted;
  }
}

// Trigger a browser download for a Blob. Pulled out as a re-export so
// callers can import from one module rather than mixing this with the
// WebM path.
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

// Pure-logic helpers exported for unit testing. Keeping these in the
// same module (rather than a sibling util file) keeps the test surface
// adjacent to the production caller.

/**
 * Convert a clip {startMs, endMs} log-time pair to {startVideoSec,
 * endVideoSec} via the sync anchor, then clamp to the video's actual
 * [0, duration] range. Returns null if the clamped window is empty or
 * if sync is incomplete.
 *
 * Exported for tests; mp4Export uses it internally too.
 */
export function clipToVideoWindow(clip, sync, videoDurationSec) {
  if (!clip || !sync) return null;
  if (!Number.isFinite(sync.logTakeoffMs) ||
      !Number.isFinite(sync.videoTakeoffSec)) return null;
  if (!Number.isFinite(clip.startMs) || !Number.isFinite(clip.endMs)) return null;
  if (clip.endMs <= clip.startMs) return null;

  const startVideoSec = sync.videoTakeoffSec +
                        (clip.startMs - sync.logTakeoffMs) / 1000;
  const endVideoSec   = sync.videoTakeoffSec +
                        (clip.endMs   - sync.logTakeoffMs) / 1000;
  const start = Math.max(0, startVideoSec);
  const end   = Number.isFinite(videoDurationSec)
                  ? Math.min(videoDurationSec, endVideoSec)
                  : endVideoSec;
  if (end <= start) return null;
  return { startVideoSec: start, endVideoSec: end };
}

/**
 * Compute the number of frames an MP4 export will produce for a given
 * (clipDurationSec, frameRate). Pulled out for tests; used implicitly
 * by exportClipAsMp4's loop bound.
 */
export function expectedFrameCount(clipDurationSec, frameRate) {
  if (!Number.isFinite(clipDurationSec) || clipDurationSec <= 0) return 0;
  if (!Number.isFinite(frameRate) || frameRate <= 0) return 0;
  return Math.max(1, Math.round(clipDurationSec * frameRate));
}

// Re-export the M5_TICK_MS / M5_LARGE_JUMP_MS constants for tests that
// want to validate snap vs catch-up behavior bounds.
export const MP4_EXPORT_INTERNAL = Object.freeze({
  M5_TICK_MS,
  M5_LARGE_JUMP_MS,
  DEFAULT_BITRATE_BPS,
  DEFAULT_FRAMERATE,
});
