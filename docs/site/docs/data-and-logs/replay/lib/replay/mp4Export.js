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

// Fallback encoder parameters used only when the source's own framerate
// or codec can't be discovered. The export defaults to "match source"
// — same resolution, framerate, codec family, and a bitrate scaled to
// the source's pixel rate.
const DEFAULT_FRAMERATE = 30;

// Bitrate target as a function of pixel count + framerate. YouTube's
// "good upload quality" recommendations: 1080p30 ≈ 8 Mbps, 1440p30 ≈
// 16 Mbps, 2160p30 ≈ 35-45 Mbps. Scale linearly with pixel rate. For
// 4K and up, bump the bits-per-pixel slightly because compression
// efficiency drops at very high resolutions.
export function computeBitrate(width, height, framerate) {
  const w = Number.isFinite(width)     && width     > 0 ? width     : 1920;
  const h = Number.isFinite(height)    && height    > 0 ? height    : 1080;
  const f = Number.isFinite(framerate) && framerate > 0 ? framerate : 30;
  const pixelsPerSec = w * h * f;
  const bitsPerPixel = (w >= 2560) ? 0.15 : 0.13;
  return Math.round(pixelsPerSec * bitsPerPixel);
}

// Pick a VideoEncoder config. When the source is HEVC, probe HEVC
// Main candidates first so the output codec family matches the source
// (avoids transcoding a 4K HEVC to a 1080p AVC by accident). Falls
// back to H.264 High profile, then Baseline as a last resort.
async function pickEncoderConfig({ width, height, bitrate, framerate, sourceCodec }) {
  const w = Math.max(2, Math.floor(width  / 2) * 2);
  const h = Math.max(2, Math.floor(height / 2) * 2);
  const isHevc = !!sourceCodec && /^(hev1|hvc1)\./i.test(sourceCodec);

  // HEVC candidates (in level order — try 4K-capable first if the
  // source is 4K, otherwise lower levels suffice). L153 = up to 4K60,
  // L150 = up to 4K30, L120 = up to 1080p60.
  const hevcCandidates = [
    { codec: 'hev1.1.6.L153.B0', width: w, height: h, bitrate, framerate, hevc: { format: 'hevc' } },
    { codec: 'hev1.1.6.L150.B0', width: w, height: h, bitrate, framerate, hevc: { format: 'hevc' } },
    { codec: 'hev1.1.6.L120.B0', width: w, height: h, bitrate, framerate, hevc: { format: 'hevc' } },
  ];

  // H.264 High profile candidates: avc1.640033 = High @ Level 5.1 (4K),
  // avc1.640028 = High @ Level 4.0 (1080p). Baseline (avc1.42E028) is
  // kept as a last-resort fallback for environments without High
  // encode support.
  const avcCandidates = [
    { codec: 'avc1.640033', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
    { codec: 'avc1.640028', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
    { codec: 'avc1.42E028', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
  ];

  const candidates = isHevc
    ? [...hevcCandidates, ...avcCandidates]
    : [...avcCandidates];

  for (const cfg of candidates) {
    try {
      const r = await VideoEncoder.isConfigSupported(cfg);
      if (r.supported) return r.config;
    } catch (_) { /* try next */ }
  }
  throw new Error(
    `No supported video encoder config at ${w}x${h}. Tried ` +
    `${isHevc ? 'HEVC Main + H.264 High/Baseline' : 'H.264 High/Baseline'}. ` +
    `Your browser has WebCodecs but no usable encode path.`);
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
// Options (v4 — source-faithful):
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
//   outputWidth:       int | null — override the output width to
//                      downscale. null = match source width (default,
//                      source-faithful).
//   bitrate:           int | null — override the encoder target bitrate
//                      in bits/sec. null = compute from output
//                      resolution + framerate using computeBitrate.
//   framerate:         number | null — override the output framerate
//                      (passed to the encoder + muxer as a hint). null
//                      = match source framerate exactly from the per-
//                      sample duration table.
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

  // -------- Resolve source-faithful output parameters ---------------
  // Resolution: default to source dims; if outputWidth is given,
  // downscale aspect-preserving. Round both axes to even numbers
  // (encoder requirement).
  const srcW = videoInfo.width;
  const srcH = videoInfo.height;
  const W = outputWidth
    ? Math.max(2, Math.floor(outputWidth / 2) * 2)
    : Math.max(2, Math.floor(srcW / 2) * 2);
  const H = outputWidth
    ? Math.max(2, Math.floor(W * srcH / srcW / 2) * 2)
    : Math.max(2, Math.floor(srcH / 2) * 2);

  // Framerate: derive from the source sample table (per-sample
  // duration in timescale units → fps). For constant-rate video this
  // is exact (e.g., 30000/1001 for 29.97 fps). When the caller
  // overrides framerate, use that instead.
  const videoSamplesAll = probe.isoFile.getTrackSamplesInfo(
    videoInfo.videoTrack.id);
  if (!videoSamplesAll || videoSamplesAll.length === 0) {
    sim.delete();
    throw new Error('Video track has no samples.');
  }
  const videoTimescale = videoInfo.videoTrack.timescale;
  if (!videoTimescale) {
    sim.delete();
    throw new Error('Video track has no timescale.');
  }
  let resolvedFps;
  if (Number.isFinite(framerate) && framerate > 0) {
    resolvedFps = framerate;
  } else {
    // Use the first sample's duration. mp4box already exposes
    // per-sample durations in track timescale units. For CFR video
    // every sample has identical duration; this yields the exact
    // rational fps (e.g., 30000/1001 ≈ 29.97003).
    const sampleDur = videoSamplesAll[0]?.duration;
    if (sampleDur && sampleDur > 0) {
      resolvedFps = videoTimescale / sampleDur;
    } else if (videoInfo.videoTrack.nb_samples > 0 &&
               videoInfo.videoTrack.duration > 0) {
      // Fallback: average rate from track duration.
      resolvedFps = (videoInfo.videoTrack.nb_samples * videoTimescale) /
                    videoInfo.videoTrack.duration;
    } else {
      resolvedFps = DEFAULT_FRAMERATE;
    }
  }

  // Bitrate: scale to pixel rate unless caller overrides.
  const resolvedBitrate = (Number.isFinite(bitrate) && bitrate > 0)
    ? bitrate
    : computeBitrate(W, H, resolvedFps);

  const canvas = new OffscreenCanvas(W, H);
  const ctx    = canvas.getContext('2d');

  // -------- Pick encoder + muxer codec family -----------------------
  // Probe HEVC first when the source is HEVC. The encoder config
  // result tells us which family won (avc vs hevc); muxer config
  // mirrors it. If HEVC encode fails we fall through to H.264 — the
  // caller learns this from the result codec string.
  const encConfig = await pickEncoderConfig({
    width: W, height: H,
    bitrate: resolvedBitrate,
    framerate: resolvedFps,
    sourceCodec: videoInfo.codec,
  });
  const outputCodecFamily = /^hev1\.|^hvc1\./i.test(encConfig.codec) ? 'hevc' : 'avc';

  // mp4-muxer expects an integer frameRate (it's a hint for the track
  // header). The actual per-frame timing comes from each VideoFrame's
  // timestamp + duration in microseconds, which we set per-sample from
  // the source's exact cts — so 29.97 stays 29.97 regardless of the
  // header value. Round to the nearest integer for the muxer.
  const muxerFrameRate = Math.max(1, Math.round(resolvedFps));

  const target = new ArrayBufferTarget();
  const muxer  = new Muxer({
    target,
    fastStart: 'in-memory',
    video: { codec: outputCodecFamily, width: W, height: H, frameRate: muxerFrameRate },
    ...(muxerAudioCfg ? { audio: muxerAudioCfg } : {}),
    firstTimestampBehavior: 'offset',
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
  // Strategy (source-faithful):
  //   1. Walk the sample table, find the keyframe at-or-before
  //      clampedStart, enumerate samples through clampedEnd. Samples
  //      whose cts falls inside the clip window are flagged `output:
  //      true`; samples earlier than the window (the decode context
  //      back to the anchor keyframe) are flagged `output: false`.
  //   2. Feed each sample's encoded bytes to a VideoDecoder. Output
  //      frames arrive in decode order (which can differ from display
  //      order for B-frame streams).
  //   3. Hold each output frame in a sorted-by-timestamp queue.
  //   4. Walk the output-flagged samples in cts order. For each
  //      output sample: drive sim to that sample's mediaTime, pop
  //      the matching decoded frame, composite overlay, encode the
  //      composite VideoFrame with the source sample's exact
  //      (timestamp, duration) in microseconds.
  //
  // This produces exactly one output frame per source frame in the
  // window — output framerate matches source framerate exactly, no
  // slot dedup or rounding.
  const selectedSamples = selectVideoSamples(
    videoSamplesAll, videoTimescale, clampedStart, clampedEnd);
  if (selectedSamples.length === 0) {
    sim.delete();
    throw new Error('No video samples overlap the clip window.');
  }
  const outputSamples = selectedSamples.filter(s => s.output);
  if (outputSamples.length === 0) {
    sim.delete();
    throw new Error('No video samples overlap the clip window.');
  }
  const totalFrames = outputSamples.length;

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

  // ---------- Output-sample-driven encode loop ----------------------
  // Walks `outputSamples` (every source sample in the clip window) in
  // cts order. For each output sample:
  //   - target timestamp is the sample's own cts (us, anchored at
  //     firstWindowCts), so output framerate matches source exactly.
  //   - drive sim to that mediaTime, render overlay
  //   - pop the decoded frame whose ts matches, composite, encode.
  //
  // Output frame ts/duration come straight from the source sample —
  // no slot rounding, so 29.97 fps source produces 29.97 fps output.
  let feedError = null;
  feedDone.catch((e) => { feedError = e; });

  // Keyframe cadence in output frames. ~1 keyframe per second.
  const keyframeStride = Math.max(1, Math.round(resolvedFps));

  // Per-sample target ts in microseconds, anchored at firstWindowCts.
  function sampleTsUs(s) {
    return Math.round((s.cts - firstWindowCts) * 1_000_000 / videoTimescale);
  }

  // Match a decoded frame to an output sample by timestamp. Decoder
  // emits frames at the same per-sample ts the feeder used; the
  // frameQueue is sorted by ts. Look for the largest ts ≤ targetTs
  // (the at-or-before is required because the encoder's coded ts may
  // round-trip with ±1 us drift through ffmpeg internals).
  let slotsEncoded = 0;
  try {
    for (let i = 0; i < outputSamples.length; i++) {
      if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
      if (decoderError) throw decoderError;
      if (feedError && !(feedError.name === 'AbortError')) throw feedError;
      if (encoderError) throw encoderError;

      const s = outputSamples[i];
      const sampleMediaTime = s.cts / videoTimescale;   // seconds
      const targetTsUs      = sampleTsUs(s);

      // eslint-disable-next-line no-await-in-loop
      let frame = await waitForFrameAtOrBefore(
        frameQueue, targetTsUs, feedDone, signal);

      if (!frame) {
        // Drained queue with nothing usable — should not happen if
        // selectedSamples was constructed correctly. Fall back to
        // black + overlay (don't fail the export for a single frame).
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, W, H);
      }

      // Drive sim to this frame's virtual time (mediaTime in ms).
      const targetVirtMs = Math.max(0, sampleMediaTime * 1000);
      driveSimToVirtMs(sim, simState, targetVirtMs, log, sync, cppWireFrames);

      let rawState = sim.read();
      let m5State  = rawState;
      if (presFilter && rawState) {
        const dtSec = s.duration / videoTimescale;
        const smoothed = presFilter.apply(rawState.LateralG, rawState.VerticalG, dtSec);
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
        console.warn('overlay raster failed for frame', i, e);
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

      // Output frame inherits source sample's exact ts + duration.
      const outTsUs  = targetTsUs;
      const outDurUs = Math.max(1,
        Math.round(s.duration * 1_000_000 / videoTimescale));
      const isKey    = (i % keyframeStride) === 0 || i === 0;
      const outFrame = new VideoFrame(canvas, { timestamp: outTsUs, duration: outDurUs });
      encoder.encode(outFrame, { keyFrame: isKey });
      outFrame.close();

      slotsEncoded++;
      if (onProgress) {
        onProgress({
          frame:      slotsEncoded,
          totalFrames,
          encodedSec: (outTsUs + outDurUs) / 1_000_000,
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

// ---------------------------------------------------------------------
// Overlay-only export: render JUST the M5 mode panel against a
// chroma-key background (no source video involved). Optimised to
// render N modes in one pass — shared sim + presentation filter,
// parallel encoders. Output is one MP4 per mode, ready to drop into
// an NLE for chroma-key compositing over GoPro footage.
// ---------------------------------------------------------------------

// Mode-id string → M5Sim displayType int. The five modes the M5
// firmware supports; matches M5_MODES in ReplayPage.js.
export const OVERLAY_MODE_IDS = Object.freeze({
  'energy':     0,
  'attitude':   1,
  'indexer':    2,
  'decel':      3,
  'historic-g': 4,
});

// Order the UI lists modes in (also the order the M5 firmware
// rotates through). Used for a stable per-pass iteration order and
// for "all 5" UI default.
export const OVERLAY_MODE_ORDER = Object.freeze([
  'indexer',    // 2 — the most-shipped mode; first so it renders quickest
  'attitude',   // 1
  'energy',     // 0
  'decel',      // 3
  'historic-g', // 4
]);

// Default chroma-key color. Purple #A020F0 — picked because it does
// NOT appear in the M5 avionics palette (which uses black, white,
// red, yellow, green-with-yellow-tint #00ff3a, cyan-sky #00fffe,
// brown-ground #954511, magenta #ff00ff for flight-path marker,
// orange #ff8800, and blue #0000ff). Industry-standard chroma-green
// (#00ff00) and chroma-blue (#0000ff) both collide with palette
// entries — NLE chroma-key tolerance keys out the M5's green
// chevrons / blue indicators along with the background. Purple has
// no palette neighbour, so a default tolerance keys cleanly.
//
// Pilots can override via the UI if their overlay context needs a
// different background.
const OVERLAY_DEFAULT_CHROMA = '#A020F0';

// Feature gate. Overlay-only export only needs VideoEncoder +
// OffscreenCanvas — no VideoDecoder (no source video to demux).
export function isOverlayExportSupported() {
  if (typeof window === 'undefined') return false;
  if (!('VideoEncoder' in window)) return false;
  if (!('OffscreenCanvas' in window)) return false;
  return true;
}

// Pick an H.264 encoder config for overlay-only output. Simpler than
// the source-faithful pipeline: no source codec to match, so we go
// straight to H.264 High profile (broadest NLE compat).
async function pickOverlayEncoderConfig({ width, height, bitrate, framerate }) {
  const w = Math.max(2, Math.floor(width  / 2) * 2);
  const h = Math.max(2, Math.floor(height / 2) * 2);
  const candidates = [
    { codec: 'avc1.640033', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
    { codec: 'avc1.640028', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
    { codec: 'avc1.42E028', width: w, height: h, bitrate, framerate, avc: { format: 'avc' } },
  ];
  for (const cfg of candidates) {
    try {
      const r = await VideoEncoder.isConfigSupported(cfg);
      if (r.supported) return r.config;
    } catch (_) { /* try next */ }
  }
  throw new Error(`No supported H.264 encoder config at ${w}x${h}.`);
}

// Composite an overlay SVG image onto a chroma-keyed canvas. The
// overlay fills the canvas (the NLE compositor places it wherever
// the editor wants — there's no "bottom-right of the source video"
// layout decision baked into the file).
//
// Aspect-preserving: the overlay SVG has its own intrinsic aspect
// (the M5 modes are 4:3 by tradition; the canvas is whatever the
// caller picks). We draw the overlay centered, scaled to fill the
// shorter dimension, so a 1080p canvas with a 4:3 SVG paints the
// overlay at 1440×1080 centered — extra width is chroma.
function compositeOverlayOnly(ctx, overlayImg, chromaColor, W, H, intrinsicAspect) {
  ctx.fillStyle = chromaColor;
  ctx.fillRect(0, 0, W, H);
  if (!overlayImg) return;
  // Aspect 4:3 by default; caller can override if the SVG declares
  // a different aspect.
  const aspect = (intrinsicAspect && intrinsicAspect > 0) ? intrinsicAspect : (4 / 3);
  let dw, dh;
  if (W / H > aspect) {
    // Canvas wider than overlay aspect — overlay limited by height.
    dh = H;
    dw = H * aspect;
  } else {
    // Canvas taller than overlay aspect — overlay limited by width.
    dw = W;
    dh = W / aspect;
  }
  const dx = (W - dw) / 2;
  const dy = (H - dh) / 2;
  ctx.drawImage(overlayImg, dx, dy, dw, dh);
}

// One encoder + muxer + canvas, scoped to a single mode. The frame
// loop instantiates one of these per mode requested. Each has its
// own VideoEncoder back-pressure queue; sharing a single encoder
// across modes would serialise the per-mode encode work pointlessly.
class OverlayModeEncoder {
  constructor({ modeId, encConfig, W, H, framerate }) {
    this.modeId = modeId;
    this.W = W;
    this.H = H;
    this.canvas = new OffscreenCanvas(W, H);
    this.ctx    = this.canvas.getContext('2d');
    this.target = new ArrayBufferTarget();
    this.muxer  = new Muxer({
      target: this.target,
      fastStart: 'in-memory',
      video: { codec: 'avc', width: W, height: H, frameRate: framerate },
      firstTimestampBehavior: 'offset',
    });
    this.error = null;
    this.encoder = new VideoEncoder({
      output: (chunk, meta) => {
        try { this.muxer.addVideoChunk(chunk, meta); }
        catch (e) { this.error = e; }
      },
      error: (e) => { this.error = e; },
    });
    this.encoder.configure(encConfig);
    this.framesEncoded = 0;
  }

  async finalize() {
    if (this.error) throw this.error;
    await this.encoder.flush();
    if (this.error) throw this.error;
    this.encoder.close();
    this.muxer.finalize();
    if (!this.target.buffer) throw new Error(`Muxer for "${this.modeId}" produced no output.`);
    return new Blob([this.target.buffer], { type: 'video/mp4' });
  }

  closeOnError() {
    try { if (this.encoder.state !== 'closed') this.encoder.close(); } catch (_) {}
  }
}

// Per-frame timing instrumentation. The caller-facing onProgress
// callback exposes aggregated counts; this object accumulates totals
// for the final report.
function newTimingsBag() {
  return { simMs: 0, renderMs: 0, encodeMs: 0, frames: 0 };
}

// Public API: render overlay-only MP4s for one or more modes.
//
// Options:
//   clip:               { startMs, endMs, label? } — required.
//   sync:               { logTakeoffMs, videoTakeoffSec } — required.
//   log:                parsed log — required.
//   cppWireFrames:      pre-pass output { frames: Uint8Array[] } — required.
//   renderOverlaySvg:   (m5State, displayTypeOverride?) → SVGElement.
//                       The override lets us render a different mode
//                       per encoder while reusing one sim state. The
//                       ReplayPage callback is backwards-compatible:
//                       if it ignores the override, only m5State.displayType
//                       is used (existing source-faithful path).
//   modes:              string[] of mode-id strings from OVERLAY_MODE_IDS.
//                       Defaults to all five in OVERLAY_MODE_ORDER.
//   sourceVideoInfo:    { width, height, frameRate } | null — used as
//                       the default output res/fps if outputWidth /
//                       outputHeight / framerate are omitted. Pass
//                       videoEl.videoWidth/etc. from the page, or null.
//   presentationTau:    same shape as exportClipAsMp4 — mirror live
//                       preview's slip-ball smoothing.
//   backgroundMode:     'chroma' (default) | 'transparent'. Chroma
//                       writes the chromaColor as the canvas background;
//                       transparent only works if an alpha-capable
//                       codec is available (probed at runtime, falls
//                       back to chroma with a console.warn).
//   chromaColor:        CSS color string. Default '#00ff00'.
//   outputWidth/Height: explicit output dims. Default = sourceVideoInfo
//                       dims (or 1920×1080 if no source info given).
//   framerate:          override the encode frame rate. Default = 30.
//   bitrate:            override the encoder bitrate. null = compute
//                       from output dims + fps via computeBitrate.
//   durationSec:        clip duration in seconds. Default = computed
//                       from clip.startMs/endMs.
//   onProgress:         ({ mode, frame, totalFrames, encodedSec, totalSec })
//                       → void. Called once per (mode, frame).
//   signal:             AbortSignal | null.
//
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
  backgroundMode    = 'chroma',
  chromaColor       = OVERLAY_DEFAULT_CHROMA,
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

  // Resolve mode list. Each entry is an { id, displayType } pair.
  const requested = (modes && modes.length > 0) ? modes : OVERLAY_MODE_ORDER;
  const modeList = [];
  for (const m of requested) {
    if (!(m in OVERLAY_MODE_IDS)) {
      throw new Error(`exportOverlayOnly: unknown mode id "${m}". ` +
        `Known: ${Object.keys(OVERLAY_MODE_IDS).join(', ')}.`);
    }
    modeList.push({ id: m, displayType: OVERLAY_MODE_IDS[m] });
  }
  if (modeList.length === 0) {
    throw new Error('exportOverlayOnly: at least one mode required.');
  }

  // Resolve output dims + framerate. The export is video-only and
  // not bound to a source frame timeline, so we pick a regular CFR
  // and stamp frames at integer-rate microsecond offsets.
  const srcW   = sourceVideoInfo?.width;
  const srcH   = sourceVideoInfo?.height;
  const srcFps = sourceVideoInfo?.frameRate;
  let W = outputWidth  || srcW || 1920;
  let H = outputHeight || srcH || 1080;
  W = Math.max(2, Math.floor(W / 2) * 2);
  H = Math.max(2, Math.floor(H / 2) * 2);
  const fps = (Number.isFinite(framerate) && framerate > 0)
    ? framerate
    : (Number.isFinite(srcFps) && srcFps > 0 ? srcFps : DEFAULT_FRAMERATE);
  const muxerFps = Math.max(1, Math.round(fps));
  const resolvedBitrate = (Number.isFinite(bitrate) && bitrate > 0)
    ? bitrate
    : computeBitrate(W, H, fps);

  // Clip duration.
  const clipDurSec = (Number.isFinite(durationSec) && durationSec > 0)
    ? durationSec
    : (clip.endMs - clip.startMs) / 1000;
  if (!Number.isFinite(clipDurSec) || clipDurSec <= 0) {
    throw new Error('exportOverlayOnly: clip has zero or invalid duration.');
  }

  // Background mode resolution. 'transparent' requires an alpha-
  // capable codec; H.264 (the only Chromium WebCodecs encode codec)
  // doesn't carry alpha. We honour the request only if a future
  // browser exposes one; otherwise log + fall back to chroma.
  let effectiveBackground = backgroundMode;
  if (backgroundMode === 'transparent') {
    // No public WebCodecs codec carries alpha in Chromium as of
    // 2026. Probing every imaginable VP9-alpha config wastes time;
    // we'd rather warn loudly and produce a usable chroma file.
    console.warn(
      'exportOverlayOnly: transparent background not supported by ' +
      'available encoders; falling back to chroma key.');
    effectiveBackground = 'chroma';
  }
  const effectiveChroma = (effectiveBackground === 'chroma')
    ? chromaColor : '#000000';

  // ----------- Sim + presentation filter (shared across modes) -----
  const sim = await M5Sim.create();
  if (signal?.aborted) { sim.delete(); throw new DOMException('aborted', 'AbortError'); }
  const simState = { lastVirtMs: 0, lastBoundaryMs: 0 };
  let presFilter = null;
  if (presentationTau &&
      (presentationTau.lateralSec > 0 || presentationTau.verticalSec > 0)) {
    presFilter = new PresentationFilter();
    presFilter.setTau({
      lateralSec:  presentationTau.lateralSec  || 0,
      verticalSec: presentationTau.verticalSec || 0,
    });
  }

  // ----------- Per-mode encoder set --------------------------------
  // Pick the encoder config ONCE (same dims for all modes) then
  // clone it into one encoder + muxer per mode. mp4-muxer's Muxer
  // and WebCodecs' VideoEncoder are both independent instances, so
  // running N in parallel is safe.
  const encConfig = await pickOverlayEncoderConfig({
    width: W, height: H, bitrate: resolvedBitrate, framerate: fps,
  });

  const modeEncoders = new Map(); // modeId → OverlayModeEncoder
  try {
    for (const m of modeList) {
      modeEncoders.set(m.id, new OverlayModeEncoder({
        modeId: m.id, encConfig, W, H, framerate: muxerFps,
      }));
    }

    // ----------- Frame loop ---------------------------------------
    // Walk output slots at CFR (1/fps spacing). Per slot: drive sim
    // once, smooth once, then for each mode render the SVG and
    // encode. svgToImage is the dominant per-frame cost — it does a
    // round-trip through a Blob URL + <img> decode for every mode.
    const totalFrames = Math.max(1, Math.round(clipDurSec * fps));
    const dtUs = Math.round(1_000_000 / fps);
    const dtSec = 1 / fps;
    const keyframeStride = Math.max(1, Math.round(fps));

    // Sim virtual time anchor: clip.startMs in log-time. We use the
    // log timeline directly (not video time) — the overlay only
    // cares about log values, no video sample alignment to honour.
    // Pre-pass build keeps state.lastVirtMs at clip-start as the
    // sim's t=0.
    const simStartVirtMs = 0;
    simState.lastVirtMs    = simStartVirtMs;
    simState.lastBoundaryMs = 0;

    // For each frame slot, virtual ms = (frame / fps) * 1000.
    // driveSimToVirtMs converts this back to a log row via
    // sync — but the sync.videoTakeoffSec is in VIDEO time, and we
    // don't have video here. We build a synthetic sync that maps
    // virtMs directly to log time anchored at clip.startMs.
    const syntheticSync = {
      logTakeoffMs:    clip.startMs,
      videoTakeoffSec: 0,
    };

    const aggregateTimings = newTimingsBag();
    let lastReportedAt = performance.now();

    for (let f = 0; f < totalFrames; f++) {
      if (signal?.aborted) throw new DOMException('aborted', 'AbortError');

      // Check per-mode encoder errors before doing more work.
      for (const enc of modeEncoders.values()) {
        if (enc.error) throw enc.error;
      }

      const virtMs = (f / fps) * 1000;
      const tsUs   = f * dtUs;
      const isKey  = (f % keyframeStride) === 0;

      // -------- Sim drive (once per frame, all modes share) -------
      const t0 = performance.now();
      driveSimToVirtMs(sim, simState, virtMs, log, syntheticSync, cppWireFrames);
      let rawState = sim.read();
      let m5State  = rawState;
      if (presFilter && rawState) {
        const smoothed = presFilter.apply(rawState.LateralG, rawState.VerticalG, dtSec);
        m5State = Object.freeze({
          ...rawState,
          LateralG:  Number.isFinite(smoothed.lateralG)  ? smoothed.lateralG  : rawState.LateralG,
          VerticalG: Number.isFinite(smoothed.verticalG) ? smoothed.verticalG : rawState.VerticalG,
        });
      }
      aggregateTimings.simMs += performance.now() - t0;

      // -------- Per-mode SVG render → composite → encode ---------
      for (const m of modeList) {
        if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
        const enc = modeEncoders.get(m.id);
        if (enc.error) throw enc.error;

        // Encoder back-pressure (per-mode queue).
        while (enc.encoder.encodeQueueSize > 4) {
          if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
          // eslint-disable-next-line no-await-in-loop
          await new Promise(r => setTimeout(r, 0));
        }

        // Render: override displayType so the SVG renders THIS
        // mode regardless of which mode the live sim is in.
        const tR = performance.now();
        let overlayImg = null;
        try {
          // eslint-disable-next-line no-await-in-loop
          const svgEl = renderOverlaySvg(m5State, m.displayType);
          if (svgEl) {
            // eslint-disable-next-line no-await-in-loop
            overlayImg = await svgToImage(svgEl);
          }
        } catch (e) {
          console.warn(`overlay raster failed for ${m.id} frame ${f}:`, e);
        }
        aggregateTimings.renderMs += performance.now() - tR;

        const tC = performance.now();
        compositeOverlayOnly(enc.ctx, overlayImg, effectiveChroma, W, H, 4 / 3);
        const outFrame = new VideoFrame(enc.canvas, {
          timestamp: tsUs, duration: dtUs,
        });
        enc.encoder.encode(outFrame, { keyFrame: isKey });
        outFrame.close();
        aggregateTimings.encodeMs += performance.now() - tC;
        enc.framesEncoded++;

        if (onProgress) {
          onProgress({
            mode:       m.id,
            frame:      f + 1,
            totalFrames,
            encodedSec: (tsUs + dtUs) / 1_000_000,
            totalSec:   clipDurSec,
          });
        }
      }
      aggregateTimings.frames++;

      // Yield to the event loop periodically so the page doesn't
      // freeze. Every ~250 ms of wall-clock work; cheaper than once
      // per frame.
      const now = performance.now();
      if (now - lastReportedAt > 250) {
        lastReportedAt = now;
        // eslint-disable-next-line no-await-in-loop
        await new Promise(r => setTimeout(r, 0));
      }
    }

    // ----------- Finalize all encoders + collect blobs -------------
    const blobs = new Map();
    for (const [id, enc] of modeEncoders) {
      // eslint-disable-next-line no-await-in-loop
      const blob = await enc.finalize();
      blobs.set(id, blob);
    }

    // Stash timings on the returned map for the report. Maps allow
    // an extra symbol-keyed property; we attach via a plain field.
    blobs.__timings = aggregateTimings;
    return blobs;
  } finally {
    // Best-effort teardown.
    for (const enc of modeEncoders.values()) enc.closeOnError();
    try { sim.delete(); } catch (_) {}
  }
}

export const MP4_EXPORT_INTERNAL = Object.freeze({
  M5_TICK_MS,
  M5_LARGE_JUMP_MS,
  DEFAULT_FRAMERATE,
  MP4BOX_HEAD_BYTES,
  MP4BOX_TAIL_BYTES,
  MP4BOX_CHUNK_BYTES,
});
