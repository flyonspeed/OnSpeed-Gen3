// Export a synced video + indexer overlay as an MP4 using WebCodecs.
//
// Pipeline (one clip at a time):
//
//   Video — mp4box demux + VideoDecoder:
//     1. Probe the source File with mp4box.js (head + tail moov probe,
//        same pattern as audio). Pull the video track's codec config
//        (avcC or hvcC) and per-sample byte-range table.
//     2. Filter samples to [clampedStart, clampedEnd], extended
//        backwards to the previous keyframe (decode dependency).
//     3. For each sample: File.slice() → EncodedVideoChunk →
//        VideoDecoder.decode(). Output frames land in a queue, sorted
//        by timestamp (cts → display order).
//     4. For each output slot N (0 to totalFrames-1), pick the decoded
//        frame whose cts is just-at-or-before slot N's target mediaTime.
//        Drive sim to that virtual time, render overlay SVG, composite
//        VideoFrame + overlay onto OffscreenCanvas, encode.
//
//   Audio — mp4box demux, no re-encode:
//     1. Same mp4box file instance — moov already parsed during the
//        video probe.
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
// Why mp4box + slice (not File.arrayBuffer + HTMLVideoElement.play): a
// 17 GB flight video OOM-crashes the browser when fully loaded, and
// HTMLVideoElement playback is capped at ~4× by browser decoder
// throttling regardless of `playbackRate`. mp4box reads only the moov
// box (a few MB) and we fetch each sample's bytes on demand via
// targeted File.slice — VideoDecoder runs as fast as the hardware
// decoder will go (~10-50× realtime on M-series).

import { Muxer, ArrayBufferTarget } from '../vendor/mp4-muxer.js';
import { createFile, DataStream } from '../vendor/mp4box.js';
import { M5Sim } from './m5sim.js';
import { findRowAt } from './parseLog.js';
import { PresentationFilter } from './presentationFilter.js';

// Browser feature gate. Both VideoEncoder AND VideoDecoder are now
// required — the export demuxes the source via mp4box.js, decodes
// frames with VideoDecoder, composites the overlay, and re-encodes
// with VideoEncoder. OffscreenCanvas is the compositor target.
export function isMp4ExportSupported() {
  if (typeof window === 'undefined') return false;
  if (!('VideoEncoder' in window)) return false;
  if (!('VideoDecoder' in window)) return false;
  if (!('OffscreenCanvas' in window)) return false;
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

// Composite one frame: video frame first, overlay PNG in bottom-right.
// Mirrors the live page's .replay-overlay-frame layout. `videoSrc` can
// be any drawImage-compatible source (VideoFrame, HTMLVideoElement,
// ImageBitmap, etc).
function compositeFrame(ctx, videoSrc, overlayImg, W, H) {
  if (videoSrc) ctx.drawImage(videoSrc, 0, 0, W, H);
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
// mp4box demux helpers — parse moov, walk per-sample tables for
// audio + video tracks.
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

// Probe the source file's moov via mp4box. Returns the parsed isoFile
// + info, or null if the moov can't be located within head + tail.
//
// "fast-start" MP4s (GoPro, most phones) have moov near the start;
// "slow-start" MP4s (some editors) have it at the end. We try head
// first (cheap), fall back to tail. We never read the middle (mdat)
// during probe — sample bytes are fetched on demand later.
async function probeSourceFile(file, signal) {
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
    console.warn('mp4box: moov not found within head+tail probe.');
    return null;
  }
  return { isoFile, info: readyRef.info };
}

// Extract AAC track info + AudioSpecificConfig. Returns null if no AAC.
function extractAacTrack(isoFile, info) {
  const audioTracks = info.audioTracks || [];
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

  return { audioTrack, sampleRate, channelCount, codec: audioTrack.codec, dsi };
}

// Serialize an avcC or hvcC box to a Uint8Array using its own `write`
// method, then strip the 8-byte box header. The remaining bytes are
// the AVCDecoderConfigurationRecord / HEVCDecoderConfigurationRecord
// — exactly what VideoDecoder.configure() expects as `description`.
function extractDecoderConfigDescription(box) {
  if (!box || typeof box.write !== 'function') return null;
  const ds = new DataStream();
  ds.endianness = DataStream.BIG_ENDIAN;
  try {
    box.write(ds);
  } catch (e) {
    console.warn('mp4box: failed to serialize codec config box:', e?.message || e);
    return null;
  }
  const buf = new Uint8Array(ds.buffer);
  if (buf.length < 8) return null;
  // Standard box header: 4-byte size + 4-byte type = 8 bytes. avcC
  // and hvcC bodies are well under 4 GB, so no 64-bit largesize.
  return buf.subarray(8);
}

// Extract H.264/H.265 video track info + decoder config record.
// Returns null if no video track or codec isn't a supported flavor.
function extractVideoTrack(isoFile, info) {
  const videoTracks = info.videoTracks || [];
  if (videoTracks.length === 0) {
    console.warn('mp4box: source has no video tracks.');
    return null;
  }
  const videoTrack = videoTracks[0];
  const codec = videoTrack.codec || '';
  const isH264 = codec.startsWith('avc1') || codec.startsWith('avc3');
  const isHevc = codec.startsWith('hvc1') || codec.startsWith('hev1');
  if (!isH264 && !isHevc) {
    console.warn(`mp4box: unsupported video codec "${codec}".`);
    return null;
  }
  const width  = videoTrack.video?.width  || videoTrack.track_width;
  const height = videoTrack.video?.height || videoTrack.track_height;
  if (!width || !height) {
    console.warn('mp4box: video track missing width/height.');
    return null;
  }

  // Pull the decoder config record from avcC/hvcC.
  let description = null;
  try {
    const trak    = isoFile.getTrackById(videoTrack.id);
    const stsdEnt = trak?.mdia?.minf?.stbl?.stsd?.entries?.[0];
    const cfgBox  = isH264 ? stsdEnt?.avcC : stsdEnt?.hvcC;
    description   = extractDecoderConfigDescription(cfgBox);
  } catch (e) {
    console.warn('mp4box: decoder config extraction failed:', e?.message || e);
  }
  if (!description || description.length === 0) {
    console.warn('mp4box: missing decoder config record.');
    return null;
  }

  return { videoTrack, codec, width, height, isH264, isHevc, description };
}

// Feed AAC samples for [clampedStart, clampedEnd] from `file` straight
// into the muxer, no re-encode. Returns { added, skipped, reason }.
async function feedAacSamplesToMuxer({
  muxer, file, isoFile, audioInfo, clampedStart, clampedEnd, signal,
}) {
  const { audioTrack, dsi } = audioInfo;
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

// Select the slice of the video sample table needed to decode the
// clip window. Includes:
//   - Every sync sample at or before clampedStart (the closest such
//     keyframe anchors the decode chain).
//   - Every sample between that anchor and the last sample with
//     cts < clampedEnd.
// Each returned sample carries `output: true` if its cts falls in the
// window (its decoded frame will be composited + encoded), or
// `output: false` if it's needed only to advance the decoder context.
function selectVideoSamples(samples, timescale, clampedStart, clampedEnd) {
  if (!samples || samples.length === 0) return [];
  const startTicks = Math.floor(clampedStart * timescale);
  const endTicks   = Math.ceil (clampedEnd   * timescale);

  // Find the latest sync sample with cts <= startTicks. If none,
  // start from sample 0 (which is normally a sync sample anyway).
  let anchorIdx = 0;
  for (let i = 0; i < samples.length; i++) {
    if (samples[i].cts > startTicks) break;
    if (samples[i].is_sync) anchorIdx = i;
  }

  const out = [];
  for (let i = anchorIdx; i < samples.length; i++) {
    const s = samples[i];
    if (s.cts >= endTicks) break;
    const inWindow = (s.cts + s.duration > startTicks) && (s.cts < endTicks);
    out.push({
      offset:   s.offset,
      size:     s.size,
      cts:      s.cts,
      dts:      s.dts,
      duration: s.duration,
      isSync:   !!s.is_sync,
      output:   inWindow,
    });
  }
  return out;
}

// ---------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------
//
// Options (v3 — VideoDecoder pipeline):
//   sourceFile:        File | Blob — REQUIRED. The original video file.
//                      Both video and audio tracks are demuxed via
//                      mp4box.js. No in-memory copy of the file is
//                      created; we File.slice() per-sample.
//   videoEl:           HTMLVideoElement — used ONLY for videoWidth /
//                      videoHeight / duration reads. We never call
//                      play() / seek() on it during export.
//   presentationTau:   { lateralSec, verticalSec } | null — render-side
//                      smoothing applied to state.LateralG/VerticalG
//                      before SVG render. Mirrors the live preview's
//                      PresentationFilter; null = no smoothing.
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
      'demuxes the source via mp4box.js; without it the input video ' +
      'cannot be decoded.');
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

  // ---------- Demux probe (audio + video share one moov parse) -------
  // Parse just the source file's moov box (reading only the first
  // ~64 MB, falling back to the last 64 MB for non-fast-start files).
  // From the parsed moov we pull:
  //   - audio track info + AudioSpecificConfig (for raw AAC mux)
  //   - video track info + AVCDecoderConfigurationRecord (for decode)
  // Actual sample bytes are pulled later, one frame at a time, via
  // File.slice(). No multi-GB ArrayBuffer ever exists.
  const probe = await probeSourceFile(sourceFile, signal);
  if (!probe) {
    sim.delete();
    throw new Error(
      'mp4box could not parse the source file moov box. The file may ' +
      'be truncated, not an ISO BMFF MP4, or have moov beyond head+tail probe.');
  }
  const videoInfo = extractVideoTrack(probe.isoFile, probe.info);
  if (!videoInfo) {
    sim.delete();
    throw new Error(
      'No supported video track in source file. Expected H.264 (avc1/avc3) ' +
      'or HEVC (hvc1/hev1). HEVC requires Chrome with hardware HEVC support.');
  }

  // Audio is optional. If it fails we still produce a silent MP4.
  const audioInfo   = extractAacTrack(probe.isoFile, probe.info);
  const muxerAudioCfg = audioInfo ? {
    codec: 'aac',
    sampleRate:       audioInfo.sampleRate,
    numberOfChannels: audioInfo.channelCount,
  } : null;

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

  // ---------- Spawn AAC demux in the background ----------------------
  // Pull each AAC sample's bytes via File.slice() and feed straight
  // into the muxer. No decode + re-encode. Runs in parallel with the
  // video decode/encode; both feed the same muxer.
  let audioPromise = null;
  if (audioInfo && muxerAudioCfg) {
    audioPromise = feedAacSamplesToMuxer({
      muxer,
      file: sourceFile,
      isoFile: probe.isoFile,
      audioInfo,
      clampedStart,
      clampedEnd,
      signal,
    });
  }

  // ---------- Video decode → composite → encode pipeline -------------
  //
  // Strategy:
  //   1. Walk the sample table, find the keyframe at-or-before
  //      clampedStart, enumerate samples through clampedEnd.
  //   2. Feed each sample's encoded bytes to a VideoDecoder. Output
  //      frames arrive in decode order (which can differ from display
  //      order for B-frame streams).
  //   3. Hold each output frame in a sorted-by-timestamp queue.
  //   4. For each output slot N at clampedStart + N/framerate, pop
  //      the frame whose cts is the largest <= slot target.
  //   5. Drive sim, render overlay, composite, encode.
  //
  // The decoder runs concurrently with composite+encode via a small
  // ring of pending frames. We back-pressure the demux feed when the
  // decode queue gets large.

  const videoTimescale = videoInfo.videoTrack.timescale;
  if (!videoTimescale) {
    sim.delete();
    throw new Error('Video track has no timescale.');
  }
  const videoSamples = probe.isoFile.getTrackSamplesInfo(videoInfo.videoTrack.id);
  if (!videoSamples || videoSamples.length === 0) {
    sim.delete();
    throw new Error('Video track has no samples.');
  }
  const selectedSamples = selectVideoSamples(
    videoSamples, videoTimescale, clampedStart, clampedEnd);
  if (selectedSamples.length === 0) {
    sim.delete();
    throw new Error('No video samples overlap the clip window.');
  }

  // Frame queue: sorted by timestamp (display order). Decoder may
  // emit out of order on B-frame streams; consumer pops in order.
  const frameQueue = [];
  let decoderError = null;

  // Insert keeping sorted-ascending-by-timestamp. Most production
  // flight footage is IPP (no B-frames) so this is amortized O(1)
  // at the tail; even for IBP it's at most a small constant shuffle.
  function insertFrame(frame) {
    const ts = frame.timestamp;
    // Binary search for insert position.
    let lo = 0, hi = frameQueue.length;
    while (lo < hi) {
      const mid = (lo + hi) >>> 1;
      if (frameQueue[mid].timestamp <= ts) lo = mid + 1;
      else hi = mid;
    }
    frameQueue.splice(lo, 0, frame);
  }

  const decoder = new VideoDecoder({
    output: (frame) => {
      if (decoderError) { frame.close(); return; }
      insertFrame(frame);
    },
    error: (e) => { decoderError = e; },
  });

  // Try to configure. If it fails we surface a clear error — most
  // common cause is HEVC on a non-HEVC-capable Chrome.
  const decoderCfg = {
    codec:        videoInfo.codec,
    description:  videoInfo.description,
    codedWidth:   videoInfo.width,
    codedHeight:  videoInfo.height,
  };
  try {
    const support = await VideoDecoder.isConfigSupported(decoderCfg);
    if (!support.supported) {
      sim.delete();
      const what = videoInfo.isHevc ? 'HEVC (h.265)' : `codec "${videoInfo.codec}"`;
      throw new Error(
        `Browser cannot decode ${what}. Try Chrome ≥107 with hardware ` +
        `HEVC support, or transcode the source to H.264 first.`);
    }
  } catch (e) {
    if (e?.message?.startsWith('Browser cannot decode')) throw e;
    sim.delete();
    throw new Error(`VideoDecoder.isConfigSupported failed: ${e?.message || e}`);
  }
  decoder.configure(decoderCfg);

  // Anchor video timestamps at the first sample's cts so the muxer
  // sees a t=0-origin timeline. Use cts, not dts, because we sort
  // output frames by cts (display order).
  const firstWindowCts = selectedSamples.find(s => s.output)?.cts ??
                         selectedSamples[0].cts;

  // ---------- Demux feed task ---------------------------------------
  // Walks selectedSamples, pulls bytes via File.slice, builds
  // EncodedVideoChunks, feeds the decoder. Back-pressures when the
  // decoder is saturated.
  const feedDone = (async () => {
    for (let i = 0; i < selectedSamples.length; i++) {
      if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
      if (decoderError) throw decoderError;

      // Back-pressure on both the decode pipeline (un-started
      // decodes) and the already-decoded frame buffer (each
      // VideoFrame holds GPU/system memory). Cap total in-flight at
      // ~64 frames to keep memory bounded.
      while (decoder.decodeQueueSize + frameQueue.length > 64) {
        if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
        // eslint-disable-next-line no-await-in-loop
        await new Promise(r => setTimeout(r, 1));
      }

      const s = selectedSamples[i];
      // eslint-disable-next-line no-await-in-loop
      const ab = await sourceFile.slice(s.offset, s.offset + s.size).arrayBuffer();
      const bytes = new Uint8Array(ab);

      // Timestamp space: microseconds, anchored at firstWindowCts.
      // Decode-only samples (before the window) get negative ts; that's
      // fine — VideoDecoder accepts any int64, and we filter on cts
      // later when matching to output slots.
      const tsUs  = Math.round((s.cts - firstWindowCts) * 1_000_000 / videoTimescale);
      const durUs = Math.max(1,
        Math.round(s.duration * 1_000_000 / videoTimescale));

      const chunk = new EncodedVideoChunk({
        type:      s.isSync ? 'key' : 'delta',
        timestamp: tsUs,
        duration:  durUs,
        data:      bytes,
      });
      decoder.decode(chunk);
    }
    await decoder.flush();
  })();

  // ---------- Output slot loop --------------------------------------
  // Walks slots 0..totalFrames-1. For each slot, waits for a decoded
  // frame whose timestamp is >= slot target time (or until feed is
  // done and queue can't yield one). The largest frame with
  // timestamp <= target is the "current" frame for that slot.
  //
  // Frames stay in queue until consumed (so a single decoded frame
  // can serve multiple slots if the source frame rate is lower than
  // output frame rate, e.g. 24 fps → 30 fps duplicates some).
  let feedError = null;
  feedDone.catch((e) => { feedError = e; });

  let slotsEncoded = 0;
  try {
    for (let slot = 0; slot < totalFrames; slot++) {
      if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
      if (decoderError) throw decoderError;
      if (feedError && !(feedError.name === 'AbortError')) throw feedError;
      if (encoderError) throw encoderError;

      const slotMediaTime = clampedStart + slot / framerate;
      // Convert to the same timestamp space as decoded frames:
      // microseconds, anchored at firstWindowCts/timescale.
      // Frame ts = (sample.cts - firstWindowCts) * 1e6 / timescale
      //         = (sample_mediaTime - firstWindow_mediaTime) * 1e6.
      const firstWindowSec = firstWindowCts / videoTimescale;
      const slotTsUs = Math.round((slotMediaTime - firstWindowSec) * 1_000_000);

      // Wait until either:
      //   - The queue has a frame with timestamp > slotTsUs (so we
      //     know the largest <= slotTsUs is finalized), OR
      //   - The feed is fully drained (no more frames can arrive).
      // eslint-disable-next-line no-await-in-loop
      let frame = await waitForFrameAtOrBefore(
        frameQueue, slotTsUs, feedDone, signal);

      if (!frame) {
        // Drained queue with nothing usable — should not happen if
        // selectedSamples was constructed correctly. Fall back to
        // black + overlay (don't fail the export for a single slot).
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, W, H);
      }

      // Drive sim to this slot's virtual time.
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
        console.warn('overlay raster failed for slot', slot, e);
      }

      if (frame) compositeFrame(ctx, frame, overlayImg, W, H);
      else if (overlayImg) compositeFrame(ctx, null, overlayImg, W, H);

      // Encoder back-pressure.
      while (encoder.encodeQueueSize > 4) {
        if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
        // eslint-disable-next-line no-await-in-loop
        await new Promise(r => setTimeout(r, 0));
      }
      if (encoderError) throw encoderError;

      const outTsUs  = Math.round(slot * 1_000_000 / framerate);
      const outDurUs = Math.round(       1_000_000 / framerate);
      const isKey    = (slot % Math.max(1, framerate)) === 0 || slot === 0;
      const outFrame = new VideoFrame(canvas, { timestamp: outTsUs, duration: outDurUs });
      encoder.encode(outFrame, { keyFrame: isKey });
      outFrame.close();

      slotsEncoded++;
      if (onProgress) {
        onProgress({
          frame:      slotsEncoded,
          totalFrames,
          encodedSec: slotsEncoded / framerate,
          totalSec,
        });
      }
    }

    // Drain any remaining decoder feed work + audio.
    try { await feedDone; }
    catch (e) {
      if (e?.name === 'AbortError') throw e;
      // Decode errors after we've already collected enough frames
      // are non-fatal — the slot loop already finished.
      console.warn('video demux feed finished with:', e?.message || e);
    }

    if (audioPromise) {
      try { await audioPromise; }
      catch (e) {
        if (e?.name === 'AbortError') throw e;
        // eslint-disable-next-line no-console
        console.warn('audio mux failed, exporting silent:', e);
      }
    }

    await encoder.flush();
    if (encoderError) throw encoderError;
    encoder.close();
    muxer.finalize();

    if (!target.buffer) throw new Error('Muxer produced no output.');
    return new Blob([target.buffer], { type: 'video/mp4' });
  } finally {
    // Close any decoded frames left in queue.
    for (const f of frameQueue) { try { f.close(); } catch (_) {} }
    frameQueue.length = 0;
    try { decoder.state !== 'closed' && decoder.close(); } catch (_) {}
    try { encoder.state !== 'closed' && encoder.close(); } catch (_) {}
    try { sim.delete(); } catch (_) {}
  }
}

// Wait for the queue to contain a frame whose timestamp is the
// largest <= targetTsUs, AND for that frame's successor (or feed
// drain) to be observed so we know the choice is final. Returns the
// chosen frame (popped from the queue) or null if no frame can serve
// the target (feed drained, nothing at-or-before target).
//
// Frames with timestamp < targetTsUs but not the chosen one are
// closed and discarded — they're earlier than the current slot and
// can't serve later slots either (slots only advance forward).
async function waitForFrameAtOrBefore(frameQueue, targetTsUs, feedDone, signal) {
  let feedFinished = false;
  feedDone.then(() => { feedFinished = true; }, () => { feedFinished = true; });

  while (true) {
    if (signal?.aborted) throw new DOMException('aborted', 'AbortError');

    // Find the chosen frame: largest timestamp <= targetTsUs.
    // We can finalize the choice when:
    //   (a) the queue has a frame with timestamp > targetTsUs
    //       (proving no later-arriving in-order frame can beat the
    //       current best), OR
    //   (b) the feed has fully drained (no more frames can arrive).
    let chosenIdx = -1;
    let hasFuture = false;
    for (let i = 0; i < frameQueue.length; i++) {
      const ts = frameQueue[i].timestamp;
      if (ts <= targetTsUs) chosenIdx = i;
      else { hasFuture = true; break; }
    }

    if (hasFuture || feedFinished) {
      if (chosenIdx < 0) return null;
      const chosen = frameQueue[chosenIdx];
      // Close + drop frames strictly before chosen — they're now
      // unreachable (slots only advance forward).
      for (let i = 0; i < chosenIdx; i++) {
        try { frameQueue[i].close(); } catch (_) {}
      }
      frameQueue.splice(0, chosenIdx + 1);
      return chosen;
    }

    // Queue currently empty or only has candidates <= target;
    // wait for more frames.
    await new Promise(r => setTimeout(r, 1));
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
  MP4BOX_HEAD_BYTES,
  MP4BOX_TAIL_BYTES,
  MP4BOX_CHUNK_BYTES,
});
