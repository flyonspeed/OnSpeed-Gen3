// Export a synced video + indexer overlay as an MP4 using WebCodecs.
//
// Pipeline (one clip at a time):
//
//   Video — play-rate harvest:
//     1. Seek <video> to clip start, set playbackRate, call play().
//     2. Each requestVideoFrameCallback fires with the just-painted
//        frame's mediaTime. Map mediaTime → output frame index → log
//        row → wire frame → sim tick.
//     3. Read sim state, run it through PresentationFilter (matches
//        the live preview's slip-ball smoothing), render overlay SVG.
//     4. Composite to OffscreenCanvas, build a VideoFrame, encode.
//     5. When video mediaTime crosses clip end, pause + flush.
//
//   Audio — mp4box demux, no re-encode:
//     1. Probe the source File with mp4box.js, feeding it chunks via
//        File.slice() + appendBuffer until onReady fires (moov parsed).
//     2. Walk the AAC track's sample table for samples in
//        [clampedStart, clampedEnd] — each has byte offset + size + cts.
//     3. For each sample: File.slice(offset, offset+size).arrayBuffer()
//        to pull just that AAC frame's bytes (typically <1 KB each).
//     4. Feed to muxer.addAudioChunkRaw() — no decode + re-encode round
//        trip. First call carries the AudioSpecificConfig in meta.
//
//   Both video chunks and demuxed audio bytes feed the same Muxer;
//   finalize() interleaves them.
//
// Why mp4box + slice (not File.arrayBuffer + decodeAudioData): a 17 GB
// flight video OOM-crashes the browser when fully loaded. mp4box only
// needs the moov box (a few MB), and each AAC sample is fetched via a
// targeted File.slice that reads only that sample's bytes from disk.
//
// Faster-than-realtime: at playbackRate=4, rVFC fires at the source
// video's native frame rate (60 fps) regardless of playback speed,
// each carrying a mediaTime advanced 4× faster than wall clock. A 30 s
// clip encodes in ~7-8 s on M-series Macs, ~12-15 s on a 2020 MBP.
// Compare to the v1 seek-per-frame loop's 60-85 s.

import { Muxer, ArrayBufferTarget } from '../vendor/mp4-muxer.js';
import { createFile } from '../vendor/mp4box.js';
import { M5Sim } from './m5sim.js';
import { findRowAt } from './parseLog.js';
import { PresentationFilter } from './presentationFilter.js';

// Browser feature gate. WebCodecs is Chrome/Edge desktop and partial
// Safari. We check for VideoEncoder, OffscreenCanvas (compositor
// target), and rVFC (frame harvest). Audio is demuxed not encoded,
// so AudioEncoder is no longer required.
export function isMp4ExportSupported() {
  if (typeof window === 'undefined') return false;
  if (!('VideoEncoder' in window)) return false;
  if (!('OffscreenCanvas' in window)) return false;
  if (typeof HTMLVideoElement === 'undefined') return false;
  if (!('requestVideoFrameCallback' in HTMLVideoElement.prototype)) return false;
  return true;
}

// Audio export needs nothing beyond File.slice() (universally
// supported where MP4 export itself runs). Kept as a separate gate so
// the UI can advertise "exports with audio" without parsing the file.
// Note: actual audio output still falls back to silent if the source
// has no AAC track or mp4box can't parse the moov.
export function isMp4AudioExportSupported() {
  if (typeof window === 'undefined') return false;
  if (typeof Blob === 'undefined' || !Blob.prototype.slice) return false;
  return true;
}

// Default encoder parameters. Tuned for social-media MP4s — pilots
// upload to YouTube/Instagram/Twitter where 1080p H.264 is the lowest
// common denominator. 5 Mbps is well within YouTube's "good upload
// quality" range for 1080p30.
const DEFAULT_BITRATE_BPS = 5_000_000;
const DEFAULT_FRAMERATE   = 30;
const DEFAULT_PLAYRATE    = 4;        // 4× source playback during encode

async function pickEncoderConfig({ width, height, bitrate, framerate }) {
  const w = Math.max(2, Math.floor(width  / 2) * 2);
  const h = Math.max(2, Math.floor(height / 2) * 2);
  const candidates = [
    { codec: 'avc1.42E01F', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
    { codec: 'avc1.42E028', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
    { codec: 'avc1.640028', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
  ];
  for (const cfg of candidates) {
    try {
      const r = await VideoEncoder.isConfigSupported(cfg);
      if (r.supported) return r.config;
    } catch (_) { /* try next */ }
  }
  throw new Error(
    'No supported H.264 encoder config. Tried Baseline 3.1/4.0 and High 4.0. ' +
    'Your browser has WebCodecs but no AVC encode path.');
}

// Rasterize an SVG element to an HTMLImageElement via a Blob URL.
// drawImage(img, x, y, w, h) paints with explicit destination dims so
// SVGs without intrinsic width/height still rasterize at usable size.
function svgToImage(svgEl) {
  const xml  = new XMLSerializer().serializeToString(svgEl);
  const blob = new Blob([xml], { type: 'image/svg+xml;charset=utf-8' });
  const url  = URL.createObjectURL(blob);
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload  = () => { URL.revokeObjectURL(url); resolve(img); };
    img.onerror = (e) => { URL.revokeObjectURL(url); reject(e); };
    img.src = url;
  });
}

// One-shot seek + wait for the next painted frame. Used only for the
// pre-play positioning (we play() through the clip range, not seek per
// frame). Registers rVFC BEFORE setting currentTime so the seek-induced
// composite fires the callback even on paused video.
function seekVideoAndAwaitFrame(videoEl, targetSec, signal = null) {
  return new Promise((resolve, reject) => {
    let settled = false;
    let timeoutId = null;
    const finish = (value, err) => {
      if (settled) return;
      settled = true;
      if (timeoutId !== null) clearTimeout(timeoutId);
      videoEl.removeEventListener('seeked', onSeeked);
      videoEl.removeEventListener('error',  onError);
      if (signal) signal.removeEventListener('abort', onAbort);
      if (err) reject(err); else resolve(value);
    };
    const onSeeked = () => {};
    const onError  = (e) => finish(null, new Error('Video seek failed: ' + (e?.message || e)));
    const onAbort  = () => finish(null, new DOMException('aborted', 'AbortError'));
    if (signal) {
      if (signal.aborted) { reject(new DOMException('aborted', 'AbortError')); return; }
      signal.addEventListener('abort', onAbort, { once: true });
    }
    videoEl.addEventListener('seeked', onSeeked, { once: true });
    videoEl.addEventListener('error',  onError,  { once: true });
    videoEl.requestVideoFrameCallback((_now, meta) => {
      finish(meta?.mediaTime ?? videoEl.currentTime);
    });
    videoEl.currentTime = Math.max(0, targetSec);
    timeoutId = setTimeout(() => finish(videoEl.currentTime), 100);
  });
}

// Composite one frame: video first, overlay PNG in bottom-right.
// Mirrors the live page's .replay-overlay-frame layout.
function compositeFrame(ctx, videoEl, overlayImg, W, H) {
  ctx.drawImage(videoEl, 0, 0, W, H);
  if (overlayImg) {
    const ow = Math.round(W * 0.22);
    const oh = Math.round(ow * 3 / 4);
    const ox = W - Math.round(W * 0.012) - ow;
    const oy = H - Math.round(H * 0.030) - oh;
    ctx.save();
    ctx.shadowColor   = 'rgba(0,0,0,0.7)';
    ctx.shadowBlur    = Math.round(W * 0.006);
    ctx.shadowOffsetY = Math.round(W * 0.0015);
    ctx.drawImage(overlayImg, ox, oy, ow, oh);
    ctx.restore();
  }
}

// ---------------------------------------------------------------------
// Sim driver — same logic as ReplayPage's live driver. Walks 50 ms wire
// boundaries from lastBoundaryMs to targetVirtMs, injects one wire
// frame per boundary, then advances virtual clock to targetVirtMs.
// ---------------------------------------------------------------------
const M5_TICK_MS = 50;
const M5_LARGE_JUMP_MS = 5000;

function driveSimToVirtMs(sim, state, targetVirtMs, log, sync, cppWireFrames) {
  const frames = cppWireFrames && cppWireFrames.frames;
  const injectAt = (virtMs) => {
    if (!frames) return;
    const logMs = sync.logTakeoffMs + (virtMs / 1000 - sync.videoTakeoffSec) * 1000;
    const rowIdx = findRowAt(log, logMs);
    if (rowIdx < 0) return;
    const frameBytes = frames[rowIdx];
    if (!frameBytes) return;
    sim.injectBytes(frameBytes);
  };
  const last = state.lastVirtMs;
  const jump = targetVirtMs - last;
  if (jump < 0 || jump > M5_LARGE_JUMP_MS) {
    injectAt(targetVirtMs);
    sim.advanceTo(targetVirtMs);
    state.lastVirtMs = targetVirtMs;
    state.lastBoundaryMs = Math.floor(targetVirtMs / M5_TICK_MS) * M5_TICK_MS;
    return;
  }
  let lastBoundary = state.lastBoundaryMs;
  let nextBoundary = lastBoundary + M5_TICK_MS;
  while (nextBoundary <= targetVirtMs) {
    injectAt(nextBoundary);
    sim.advanceTo(nextBoundary);
    lastBoundary = nextBoundary;
    nextBoundary += M5_TICK_MS;
  }
  state.lastBoundaryMs = lastBoundary;
  if (targetVirtMs > lastBoundary) sim.advanceTo(targetVirtMs);
  state.lastVirtMs = targetVirtMs;
}

// ---------------------------------------------------------------------
// Audio demux helpers — feed mp4box just enough of the source file to
// parse moov, then pull individual AAC sample byte ranges via slice().
// ---------------------------------------------------------------------

// Constants tuned to the moov-discovery problem.
const MP4BOX_HEAD_BYTES   = 64 * 1024 * 1024;  // Try first 64 MB for fast-start moov.
const MP4BOX_TAIL_BYTES   = 64 * 1024 * 1024;  // If moov isn't there, try last 64 MB.
const MP4BOX_CHUNK_BYTES  =  4 * 1024 * 1024;  // 4 MB per appendBuffer call.

// Feed mp4box chunks of `file` from [start, end) until either
// `isoFile.onReady` has fired or we run out of bytes. Returns true if
// onReady fired. The chunks are not retained — mp4box buffers them
// internally only for the parser's working set.
async function feedRangeUntilReady(isoFile, file, start, end, readyRef, signal) {
  let pos = start;
  while (pos < end) {
    if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
    const sliceEnd = Math.min(end, pos + MP4BOX_CHUNK_BYTES);
    const ab = await file.slice(pos, sliceEnd).arrayBuffer();
    ab.fileStart = pos;
    isoFile.appendBuffer(ab);
    pos = sliceEnd;
    if (readyRef.ready) return true;
  }
  return readyRef.ready;
}

// Probe the source file's audio track via mp4box. Returns:
//   { isoFile, audioTrack, sampleRate, channelCount, codec, dsi }
// or null if no AAC track is found or the moov can't be parsed.
//
// "fast-start" MP4s (GoPro, most phones) have moov near the start;
// "slow-start" MP4s (some editors) have it at the end. We try head
// first (cheap), fall back to tail. We never read the middle (mdat)
// during probe — sample bytes are fetched on demand later.
async function probeAacAudioTrack(file, signal) {
  if (!file || typeof file.slice !== 'function') return null;
  let isoFile;
  try {
    isoFile = createFile();
  } catch (e) {
    console.warn('mp4box createFile failed:', e?.message || e);
    return null;
  }
  const readyRef = { ready: false, info: null };
  isoFile.onReady = (info) => { readyRef.ready = true; readyRef.info = info; };
  isoFile.onError = (msg) => { console.warn('mp4box parse error:', msg); };

  const fileSize = file.size;

  // Try fast-start (head).
  const headEnd = Math.min(fileSize, MP4BOX_HEAD_BYTES);
  let ok = await feedRangeUntilReady(isoFile, file, 0, headEnd, readyRef, signal);

  // If moov is at the tail, try the last 64 MB. mp4box uses
  // fileStart offsets, so it can stitch tail bytes against the head
  // it's already seen.
  if (!ok && fileSize > headEnd) {
    const tailStart = Math.max(headEnd, fileSize - MP4BOX_TAIL_BYTES);
    ok = await feedRangeUntilReady(isoFile, file, tailStart, fileSize, readyRef, signal);
  }

  if (!ok || !readyRef.info) {
    console.warn('mp4box: moov not found within head+tail probe; no audio mux.');
    return null;
  }

  const audioTracks = readyRef.info.audioTracks || [];
  if (audioTracks.length === 0) {
    console.warn('mp4box: source has no audio tracks; emitting silent MP4.');
    return null;
  }
  const audioTrack = audioTracks[0];
  // Codec string looks like "mp4a.40.2" (AAC-LC) or "mp4a.40.5" (HE-AAC), etc.
  if (!audioTrack.codec || !audioTrack.codec.startsWith('mp4a')) {
    console.warn(`mp4box: audio codec "${audioTrack.codec}" is not AAC; emitting silent MP4.`);
    return null;
  }
  const sampleRate   = audioTrack.audio?.sample_rate;
  const channelCount = audioTrack.audio?.channel_count;
  if (!sampleRate || !channelCount) {
    console.warn('mp4box: audio track missing sample_rate/channel_count; emitting silent MP4.');
    return null;
  }

  // Extract the AAC AudioSpecificConfig from the esds box. This goes
  // into EncodedAudioChunk.decoderConfig.description (passed to the
  // muxer via meta on the first addAudioChunkRaw call).
  let dsi = null;
  try {
    const trak    = isoFile.getTrackById(audioTrack.id);
    const stsdEnt = trak?.mdia?.minf?.stbl?.stsd?.entries?.[0];
    const esds    = stsdEnt?.esds;
    // DecoderConfigDescrTag=4, DecSpecificInfoTag=5.
    const dcd     = esds?.esd?.findDescriptor?.(4);
    const dsiDesc = dcd?.findDescriptor?.(5);
    if (dsiDesc && dsiDesc.data) {
      // dsiDesc.data is typically Uint8Array; copy to a stable buffer.
      dsi = new Uint8Array(dsiDesc.data);
    }
  } catch (e) {
    console.warn('mp4box: AudioSpecificConfig extraction failed:', e?.message || e);
  }
  if (!dsi || dsi.length === 0) {
    console.warn('mp4box: missing AudioSpecificConfig; emitting silent MP4.');
    return null;
  }

  return { isoFile, audioTrack, sampleRate, channelCount, codec: audioTrack.codec, dsi };
}

// Feed AAC samples for [clampedStart, clampedEnd] from `file` straight
// into the muxer, no re-encode. Returns { added, skipped, reason }.
async function feedAacSamplesToMuxer({
  muxer, file, probe, clampedStart, clampedEnd, signal,
}) {
  const { isoFile, audioTrack, dsi } = probe;
  const timescale = audioTrack.timescale;
  if (!timescale) return { added: 0, skipped: true, reason: 'no timescale' };

  const samples = isoFile.getTrackSamplesInfo(audioTrack.id);
  if (!samples || samples.length === 0) {
    return { added: 0, skipped: true, reason: 'no samples in track' };
  }

  // Pick samples whose presentation time falls in the clip window.
  // We include any sample that overlaps the window; partial-frame
  // trimming is left to the muxer/decoder (AAC frames are 1024-sample
  // atoms — trimming inside would require re-encoding).
  const startTicks = Math.floor(clampedStart * timescale);
  const endTicks   = Math.ceil (clampedEnd   * timescale);

  // Binary-search style scan: samples are in dts order. We just walk
  // since the sample list is dense and contiguous.
  const inRange = [];
  for (let i = 0; i < samples.length; i++) {
    const s = samples[i];
    if (s.cts + s.duration <= startTicks) continue;
    if (s.cts >= endTicks)                break;
    inRange.push(s);
  }
  if (inRange.length === 0) {
    return { added: 0, skipped: true, reason: 'no samples overlap window' };
  }

  // The output MP4's audio timeline starts at t=0 (the muxer's
  // firstTimestampBehavior is 'offset', so we anchor against the
  // first muxed sample's source cts).
  const firstCts = inRange[0].cts;
  let added = 0;
  let metaSent = false;

  for (let i = 0; i < inRange.length; i++) {
    if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
    const s   = inRange[i];
    const ab  = await file.slice(s.offset, s.offset + s.size).arrayBuffer();
    const buf = new Uint8Array(ab);

    const tsUs  = Math.round((s.cts - firstCts) * 1_000_000 / timescale);
    const durUs = Math.round( s.duration         * 1_000_000 / timescale);

    // All AAC frames are independent (no inter-frame prediction in
    // AAC-LC), so every sample is a 'key' from a seeking standpoint.
    const meta = metaSent ? undefined : { decoderConfig: { description: dsi } };
    muxer.addAudioChunkRaw(buf, 'key', tsUs, durUs, meta);
    metaSent = true;
    added++;
  }
  return { added, skipped: false };
}

// ---------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------
//
// Options (new in v2):
//   sourceFile:        File | Blob — the original video file. Its AAC
//                      track is demuxed via mp4box.js (no in-memory
//                      copy of the file) and remuxed into the output.
//                      If omitted, MP4 is exported silent.
//   presentationTau:   { lateralSec, verticalSec } | null — render-side
//                      smoothing applied to state.LateralG/VerticalG
//                      before SVG render. Mirrors the live preview's
//                      PresentationFilter; null = no smoothing.
//   playbackRate:      4 by default; controls source-video play speed
//                      during harvest. Higher = faster encode but more
//                      sensitive to decoder hiccups.
//
// Returns: Promise<Blob>  — an MP4 ready to download.
export async function exportClipAsMp4({
  videoEl,
  clip,
  sync,
  log,
  cppWireFrames,
  renderOverlaySvg,
  sourceFile      = null,
  presentationTau = null,
  outputWidth     = 1920,
  bitrate         = DEFAULT_BITRATE_BPS,
  framerate       = DEFAULT_FRAMERATE,
  playbackRate    = DEFAULT_PLAYRATE,
  onProgress      = null,
  signal          = null,
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
      'pre-pass first).');
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
                    'invalid video range.');
  }
  const clampedStart = Math.max(0, startVideoSec);
  const clampedEnd   = Math.min(videoEl.duration, endVideoSec);
  if (clampedEnd <= clampedStart) {
    throw new Error('exportClipAsMp4: clip falls outside the loaded video.');
  }
  const totalSec    = clampedEnd - clampedStart;
  const totalFrames = Math.max(1, Math.round(totalSec * framerate));

  // Aspect-preserving output size.
  const aspect = videoEl.videoHeight / videoEl.videoWidth;
  const W = Math.max(2, Math.floor(outputWidth / 2) * 2);
  const H = Math.max(2, Math.floor(W * aspect    / 2) * 2);

  const canvas = new OffscreenCanvas(W, H);
  const ctx    = canvas.getContext('2d');

  const sim = await M5Sim.create();
  if (signal?.aborted) { sim.delete(); throw new DOMException('aborted', 'AbortError'); }
  const simState = { lastVirtMs: 0, lastBoundaryMs: 0 };

  // PresentationFilter — same as live preview's render-side smoothing.
  // Fresh instance per export so it seeds clean from the first frame.
  let presFilter = null;
  if (presentationTau &&
      (presentationTau.lateralSec > 0 || presentationTau.verticalSec > 0)) {
    presFilter = new PresentationFilter();
    presFilter.setTau({
      lateralSec:  presentationTau.lateralSec  || 0,
      verticalSec: presentationTau.verticalSec || 0,
    });
  }

  // ---------- Audio probe (mp4box demux) ------------------------------
  // Parse just the source file's moov box (reading only the first
  // ~64 MB, falling back to the last 64 MB for non-fast-start files).
  // This gives us sample_rate, channel_count, and the AAC
  // AudioSpecificConfig — everything the muxer needs to declare its
  // audio track. Actual sample bytes are pulled later, one frame at
  // a time, via File.slice(). No multi-GB ArrayBuffer ever exists.
  let audioProbe   = null;
  let muxerAudioCfg = null;
  if (sourceFile && isMp4AudioExportSupported()) {
    try {
      audioProbe = await probeAacAudioTrack(sourceFile, signal);
      if (audioProbe) {
        muxerAudioCfg = {
          codec: 'aac',
          sampleRate:       audioProbe.sampleRate,
          numberOfChannels: audioProbe.channelCount,
        };
      }
    } catch (e) {
      if (e?.name === 'AbortError') throw e;
      // eslint-disable-next-line no-console
      console.warn('audio probe skipped:', e?.message || e);
    }
  }

  const target = new ArrayBufferTarget();
  const muxer  = new Muxer({
    target,
    fastStart: 'in-memory',
    video: { codec: 'avc', width: W, height: H, frameRate: framerate },
    ...(muxerAudioCfg ? { audio: muxerAudioCfg } : {}),
    firstTimestampBehavior: 'offset',
  });

  // Pick video encoder config, attach encoder.
  const encConfig = await pickEncoderConfig({
    width: W, height: H, bitrate, framerate,
  });
  let encoderError = null;
  const encoder = new VideoEncoder({
    output: (chunk, meta) => {
      try { muxer.addVideoChunk(chunk, meta); }
      catch (e) { encoderError = e; }
    },
    error: (e) => { encoderError = e; },
  });
  encoder.configure(encConfig);

  // Snapshot user-facing video state for restoration.
  const origRate     = videoEl.playbackRate;
  const origMuted    = videoEl.muted;
  const origPaused   = videoEl.paused;
  const origCurrentT = videoEl.currentTime;

  // ---------- Spawn AAC demux in the background ----------------------
  // Pull each AAC sample's bytes via File.slice() and feed straight
  // into the muxer. No decode + re-encode. Runs in parallel with the
  // video harvest; both feed the same muxer.
  let audioPromise = null;
  if (audioProbe && muxerAudioCfg) {
    audioPromise = feedAacSamplesToMuxer({
      muxer,
      file: sourceFile,
      probe: audioProbe,
      clampedStart,
      clampedEnd,
      signal,
    });
  }

  // ---------- Video harvest loop -------------------------------------
  // Strategy: play() at playbackRate=N. Each rVFC callback fires once
  // per painted frame with mediaTime. We map mediaTime → output frame
  // slot (rounded to the nearest 1/framerate). The first time we see
  // each output slot, we encode that frame. Subsequent rVFC for the
  // same slot are dropped (source video at 60fps × 4× playback = 60
  // mediaTime-frames/sec, which we downsample to 30 output frames/sec
  // by slot dedup).
  //
  // Slot-based dedup is more robust than "encode every rVFC" because
  // playback rate isn't always honored exactly and rVFC can fire
  // late-but-bunched.
  //
  // Termination: when mediaTime ≥ clampedEnd, pause and break the harvest.
  const keyframeInterval = Math.max(1, framerate);
  videoEl.pause();
  videoEl.muted = true;       // we mux audio separately
  await seekVideoAndAwaitFrame(videoEl, clampedStart, signal);

  const harvested = new Set();   // output-slot indices already encoded
  let nextEncodeSlot = 0;
  let lastEncodedFrame = -1;

  const harvestDone = new Promise((resolveHarvest, rejectHarvest) => {
    let stopped = false;
    const stopHarvest = (err) => {
      if (stopped) return;
      stopped = true;
      try { videoEl.pause(); } catch (_) {}
      if (err) rejectHarvest(err); else resolveHarvest();
    };

    const onAbort = () => stopHarvest(new DOMException('aborted', 'AbortError'));
    if (signal) {
      if (signal.aborted) { stopHarvest(new DOMException('aborted', 'AbortError')); return; }
      signal.addEventListener('abort', onAbort, { once: true });
    }

    const onFrame = async (_now, meta) => {
      if (stopped) return;
      try {
        const mediaTime = meta?.mediaTime ?? videoEl.currentTime;

        // Reached or past clip end? Stop.
        if (mediaTime >= clampedEnd) {
          stopHarvest(null);
          return;
        }

        // Encoder back-pressure.
        while (encoder.encodeQueueSize > 4) {
          if (signal?.aborted) { stopHarvest(new DOMException('aborted','AbortError')); return; }
          // eslint-disable-next-line no-await-in-loop
          await new Promise(r => setTimeout(r, 0));
        }
        if (encoderError) { stopHarvest(encoderError); return; }

        // Map mediaTime to an output slot index. We're emitting at
        // framerate fps starting at clampedStart, so slot N has
        // mediaTime ≈ clampedStart + N/framerate.
        const slot = Math.floor((mediaTime - clampedStart) * framerate);
        if (slot < 0)            { videoEl.requestVideoFrameCallback(onFrame); return; }
        if (slot >= totalFrames) { stopHarvest(null); return; }
        if (harvested.has(slot)) { videoEl.requestVideoFrameCallback(onFrame); return; }

        // Fill any skipped slots first (rare — happens if playback
        // rate is too high and rVFC bunches). Re-use the current
        // painted pixels for the gap fillers, which is what the
        // pilot saw too at that mediaTime.
        for (let s = lastEncodedFrame + 1; s <= slot; s++) {
          if (harvested.has(s)) continue;
          // Drive sim to this slot's virtual time.
          const slotMediaTime = clampedStart + s / framerate;
          const targetVirtMs = Math.max(0, slotMediaTime * 1000);
          driveSimToVirtMs(sim, simState, targetVirtMs, log, sync, cppWireFrames);

          let rawState = sim.read();
          let m5State  = rawState;
          if (presFilter && rawState) {
            const dt = 1 / framerate;
            const smoothed = presFilter.apply(rawState.LateralG, rawState.VerticalG, dt);
            m5State = Object.freeze({
              ...rawState,
              LateralG:  Number.isFinite(smoothed.lateralG)  ? smoothed.lateralG  : rawState.LateralG,
              VerticalG: Number.isFinite(smoothed.verticalG) ? smoothed.verticalG : rawState.VerticalG,
            });
          }

          let overlayImg = null;
          try {
            const svgEl = renderOverlaySvg ? renderOverlaySvg(m5State) : null;
            if (svgEl) {
              // eslint-disable-next-line no-await-in-loop
              overlayImg = await svgToImage(svgEl);
            }
          } catch (e) {
            // eslint-disable-next-line no-console
            console.warn('overlay raster failed for slot', s, e);
          }

          compositeFrame(ctx, videoEl, overlayImg, W, H);

          const tsUs    = Math.round(s * 1_000_000 / framerate);
          const durUs   = Math.round(    1_000_000 / framerate);
          const vframe  = new VideoFrame(canvas, { timestamp: tsUs, duration: durUs });
          const isKey   = (s % keyframeInterval) === 0 || s === 0;
          encoder.encode(vframe, { keyFrame: isKey });
          vframe.close();

          harvested.add(s);
          lastEncodedFrame = s;
          nextEncodeSlot = s + 1;
          if (onProgress) {
            onProgress({
              frame: harvested.size,
              totalFrames,
              encodedSec: (s + 1) / framerate,
              totalSec,
            });
          }
        }

        // Re-arm rVFC for the next painted frame.
        if (nextEncodeSlot < totalFrames && !stopped) {
          videoEl.requestVideoFrameCallback(onFrame);
        } else {
          stopHarvest(null);
        }
      } catch (e) {
        stopHarvest(e);
      }
    };

    // Configure playback rate AFTER seek-settle, then play() + arm rVFC.
    videoEl.playbackRate = playbackRate;
    videoEl.requestVideoFrameCallback(onFrame);
    videoEl.play().catch((e) => stopHarvest(e));
  });

  try {
    await harvestDone;
    if (encoderError) throw encoderError;

    // Wait for audio encode (if any). Audio failure shouldn't kill the
    // export; we fall back to silent in that case.
    if (audioPromise) {
      try { await audioPromise; }
      catch (e) {
        // eslint-disable-next-line no-console
        console.warn('audio encode failed, exporting silent:', e);
      }
    }

    await encoder.flush();
    if (encoderError) throw encoderError;
    encoder.close();
    muxer.finalize();

    if (!target.buffer) throw new Error('Muxer produced no output.');
    return new Blob([target.buffer], { type: 'video/mp4' });
  } finally {
    try { videoEl.pause(); } catch (_) {}
    try { encoder.state !== 'closed' && encoder.close(); } catch (_) {}
    try { sim.delete(); } catch (_) {}
    videoEl.playbackRate = origRate;
    videoEl.muted        = origMuted;
    // Restore playhead to where the pilot left it.
    try { videoEl.currentTime = origCurrentT; } catch (_) {}
    if (!origPaused) {
      try { await videoEl.play(); } catch (_) {}
    }
  }
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

// Pure-logic helpers exported for unit testing.
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

export const MP4_EXPORT_INTERNAL = Object.freeze({
  M5_TICK_MS,
  M5_LARGE_JUMP_MS,
  DEFAULT_BITRATE_BPS,
  DEFAULT_FRAMERATE,
  DEFAULT_PLAYRATE,
  MP4BOX_HEAD_BYTES,
  MP4BOX_TAIL_BYTES,
  MP4BOX_CHUNK_BYTES,
});
