// Export a synced video + indexer overlay as an MP4 using WebCodecs.
//
// Pipeline (one clip at a time):
//
//   Video — Mediabunny demux + VideoDecoder:
//     1. Open the source File with `new Input({ source: new BlobSource(file),
//        formats: ALL_FORMATS })`. Mediabunny streams reads natively
//        — no head/tail probe budget; the moov can sit anywhere in
//        the file.
//     2. From the primary video track, pull codec config (avcC or
//        hvcC bytes, via getDecoderConfig()) and rotation
//        (getRotation()).
//     3. Find the keyframe at-or-before clampedStart via
//        `videoSink.getKeyPacket(clampedStart)`, then walk forward
//        with `videoSink.packets(startPacket)` (decode order).
//        Packets with cts in [clampedStart, clampedEnd] are flagged
//        `output: true`; earlier packets (the decode dependency
//        chain back to the anchor key) are flagged `output: false`.
//     4. For each packet: wrap as `EncodedVideoChunk` →
//        `VideoDecoder.decode()`. Output frames land in a queue,
//        sorted by timestamp (cts → display order).
//     5. For each output sample (in cts order): drive sim to that
//        sample's mediaTime, render overlay SVG, composite
//        VideoFrame + overlay onto OffscreenCanvas, encode with the
//        source's exact (timestamp, duration).
//
//   Audio — Mediabunny demux, no re-encode:
//     1. Same Input — both tracks share one underlying source.
//     2. Find the audio packet at-or-before clampedStart, walk
//        forward via `audioSink.packets()` until ts >= clampedEnd.
//     3. For each packet: build a Mediabunny `EncodedPacket` with
//        timestamp re-anchored at the first muxed packet's source ts
//        (so the output MP4 starts at t=0), push to the audio
//        EncodedAudioPacketSource. First push carries the
//        AudioSpecificConfig DSI in meta.decoderConfig.description.
//
//   Both video and audio share a single Mediabunny Output. After
//   `finalize()`, `output.target.buffer` is the complete MP4 as an
//   ArrayBuffer.

import {
  ALL_FORMATS,
  BlobSource,
  BufferTarget,
  EncodedAudioPacketSource,
  EncodedPacket,
  EncodedPacketSink,
  EncodedVideoPacketSource,
  Input,
  Mp4OutputFormat,
  Output,
} from '../vendor/mediabunny.min.mjs';
import { M5_PANEL_W, M5_PANEL_H }
  from '../../../../packages/ui-core/core/geometry.js';
import { M5Sim } from './m5sim.js';
import { findRowAt } from './parseLog.js';
import { PresentationFilter } from './presentationFilter.js';

// Browser feature gate. Both VideoEncoder AND VideoDecoder are now
// required — the export demuxes the source via Mediabunny, decodes
// frames with VideoDecoder, composites the overlay, and re-encodes
// with VideoEncoder. OffscreenCanvas is the compositor target.
export function isMp4ExportSupported() {
  if (typeof window === 'undefined') return false;
  if (!('VideoEncoder' in window)) return false;
  if (!('VideoDecoder' in window)) return false;
  if (!('OffscreenCanvas' in window)) return false;
  return true;
}

// Audio export needs nothing beyond Blob.slice() (universally
// supported where MP4 export itself runs). Kept as a separate gate
// so the UI can advertise "exports with audio" without parsing the
// file. Note: actual audio output still falls back to silent if the
// source has no AAC track or Mediabunny can't parse the container.
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
  // Mediabunny's getCodec() returns the codec family name ('hevc', 'avc',
  // 'vp9', etc) — not a full parameter string. Match that family here.
  const isHevc = sourceCodec === 'hevc';

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

// Bottom-corner overlay placement. position='right' is the default
// single-overlay layout (the live page's .replay-overlay-frame
// position). position='left' mirrors X across the centerline for the
// "standard" two-panel export — same width, same Y, same margins, just
// pinned to the opposite edge. Returns { x, y, w, h } in canvas pixels.
function overlayPlacement(W, H, position) {
  const w = Math.round(W * 0.22);
  const h = Math.round(w * 3 / 4);
  const y = H - Math.round(H * 0.030) - h;
  const x = position === 'left'
    ? Math.round(W * 0.012)
    : W - Math.round(W * 0.012) - w;
  return { x, y, w, h };
}

// Composite one frame: video frame first (rotated to upright orientation
// if the source has a display-matrix rotation, e.g. GoPro -180°), then
// each overlay drawn at its requested corner. Overlays are always drawn
// AFTER the rotation so they sit upright on screen regardless of source
// orientation. Mirrors the live page's .replay-overlay-frame layout.
// `videoSrc` can be any drawImage-compatible source (VideoFrame,
// HTMLVideoElement, ImageBitmap, etc). `rotationDeg` is the angle to
// rotate the source pixels by — 0 / 90 / 180 / 270 (or -90 / -180).
// `overlays` is an array of { img, position } entries; falsy `img`
// entries are skipped. Empty array = source-only frame.
function compositeFrame(ctx, videoSrc, overlays, W, H, rotationDeg = 0) {
  if (videoSrc) {
    const r = ((rotationDeg % 360) + 360) % 360;
    if (r === 0) {
      ctx.drawImage(videoSrc, 0, 0, W, H);
    } else {
      ctx.save();
      // Rotate around the canvas center so the rotated image fills the
      // same WxH bounds. For 90/270 the source's natural aspect would
      // be sideways; we still target WxH so the encoder gets a normal
      // landscape frame. (For literal sideways video we'd want to
      // swap W/H at the encoder, but that's a future concern.)
      ctx.translate(W / 2, H / 2);
      ctx.rotate(r * Math.PI / 180);
      ctx.drawImage(videoSrc, -W / 2, -H / 2, W, H);
      ctx.restore();
    }
  }
  if (!overlays || overlays.length === 0) return;
  ctx.save();
  ctx.shadowColor   = 'rgba(0,0,0,0.7)';
  ctx.shadowBlur    = Math.round(W * 0.006);
  ctx.shadowOffsetY = Math.round(W * 0.0015);
  for (const ov of overlays) {
    if (!ov || !ov.img) continue;
    const { x, y, w, h } = overlayPlacement(W, H, ov.position);
    ctx.drawImage(ov.img, x, y, w, h);
  }
  ctx.restore();
}

// Derive the source video's display rotation from its tkhd matrix.
// mp4 tkhd matrices are 9-element fixed-point arrays in row-major
// order: [a, b, u, c, d, v, x, y, w]. For rotation θ the 2x2 (a,b,c,d)
// block holds [cosθ, sinθ, -sinθ, cosθ] in 16.16 fixed-point — so
// dividing by 65536 recovers the float values. We probe the standard
// orientations (0/90/180/270) and return the matching angle, or 0 if
// the matrix is identity / unrecognized.
//
// The live export path reads rotation directly via Mediabunny's
// `track.getRotation()` (which returns 0/90/180/270 having already
// inspected the matrix internally). This helper is retained for
// diagnostics and tests — round-tripping canonical matrices locks
// the algebra against regressions in either path.
//
// Matrix values arrive either as raw int32 16.16 fixed-point (the
// ISO BMFF wire format) or as already-unpacked floats (some
// upstream parsers do the conversion eagerly). Normalize before
// reading.
function rotationFromTkhdMatrix(matrix) {
  if (!matrix || matrix.length < 9) return 0;
  // Detect fixed-point vs already-unpacked floats. At a 90°/270°
  // rotation the (0,0) entry is 0, so we sniff the largest magnitude
  // across the 2x2 rotation block (entries 0,1,3,4) — at least one of
  // those is ±1 in float form, ±65536 in fixed-point.
  const maxMag = Math.max(
    Math.abs(matrix[0] || 0),
    Math.abs(matrix[1] || 0),
    Math.abs(matrix[3] || 0),
    Math.abs(matrix[4] || 0),
  );
  const scale = maxMag > 2 ? 1 / 65536 : 1;
  const a = matrix[0] * scale;
  const b = matrix[1] * scale;
  // c = matrix[3] * scale, d = matrix[4] * scale — not needed since
  // a/b alone disambiguate the four cardinal rotations.
  const eps = 0.01;
  if (Math.abs(a - 1) < eps && Math.abs(b)     < eps) return 0;
  if (Math.abs(a)     < eps && Math.abs(b - 1) < eps) return 90;
  if (Math.abs(a + 1) < eps && Math.abs(b)     < eps) return 180;
  if (Math.abs(a)     < eps && Math.abs(b + 1) < eps) return 270;
  return 0;
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
// Mediabunny demux helpers — open the source, pick out primary tracks,
// pull decoder configs + rotation.
// ---------------------------------------------------------------------

// Open `file` (File | Blob) as a Mediabunny Input. ALL_FORMATS lets
// the library sniff the container — we only ship MP4 today but the
// docs-site replay page may grow MOV / MKV support later, and the
// extra format detectors weigh nothing at runtime.
function openInput(file) {
  return new Input({
    source: new BlobSource(file),
    formats: ALL_FORMATS,
  });
}

// Pull the primary video track + its decoder config + rotation +
// (estimated) framerate from the first packet's duration. Returns
// null if the source has no decodable video.
async function readVideoTrackInfo(input) {
  const track = await input.getPrimaryVideoTrack();
  if (!track) {
    console.warn('mediabunny: source has no video track.');
    return null;
  }
  const codec = await track.getCodec();           // 'avc' | 'hevc' | ...
  if (codec !== 'avc' && codec !== 'hevc') {
    console.warn(`mediabunny: unsupported video codec "${codec}".`);
    return null;
  }
  const decoderCfg = await track.getDecoderConfig();
  if (!decoderCfg || !decoderCfg.description) {
    console.warn('mediabunny: video track missing decoder config.');
    return null;
  }
  const width  = await track.getCodedWidth();
  const height = await track.getCodedHeight();
  if (!width || !height) {
    console.warn('mediabunny: video track missing width/height.');
    return null;
  }
  const rotationDeg = await track.getRotation();   // 0 | 90 | 180 | 270

  return {
    track,
    codec,                                          // 'avc' | 'hevc'
    codecParameterString: decoderCfg.codec,         // e.g. 'avc1.640033'
    width,
    height,
    description: new Uint8Array(decoderCfg.description),
    rotationDeg: rotationDeg || 0,
    decoderCfg,
  };
}

// Pull the primary audio track if it's AAC. Returns null if no AAC
// track — caller falls back to silent MP4.
async function readAudioTrackInfo(input) {
  const track = await input.getPrimaryAudioTrack();
  if (!track) {
    console.warn('mediabunny: source has no audio track; emitting silent MP4.');
    return null;
  }
  const codec = await track.getCodec();
  if (codec !== 'aac') {
    console.warn(`mediabunny: audio codec "${codec}" is not AAC; emitting silent MP4.`);
    return null;
  }
  const decoderCfg = await track.getDecoderConfig();
  if (!decoderCfg) {
    console.warn('mediabunny: AAC track has no decoder config; emitting silent MP4.');
    return null;
  }
  if (!decoderCfg.description) {
    console.warn('mediabunny: AAC track missing AudioSpecificConfig; emitting silent MP4.');
    return null;
  }
  const sampleRate = await track.getSampleRate();
  const channelCount = await track.getNumberOfChannels();
  if (!sampleRate || !channelCount) {
    console.warn('mediabunny: AAC track missing sample_rate/channel_count; emitting silent MP4.');
    return null;
  }
  return {
    track,
    codec,                                            // 'aac'
    codecParameterString: decoderCfg.codec,           // 'mp4a.40.2' etc.
    sampleRate,
    channelCount,
    description: new Uint8Array(decoderCfg.description),
  };
}

// Pull every AAC packet whose presentation time overlaps [clampedStart,
// clampedEnd] and push it through `audioSource` unchanged. Returns
// { added, skipped, reason }.
//
// First push carries decoderConfig (codec, channels, sampleRate,
// description). Subsequent pushes have no meta.
async function feedAacPacketsToOutput({
  audioSource, videoTrack: _vt, audioInfo, clampedStart, clampedEnd, signal,
}) {
  const sink = new EncodedPacketSink(audioInfo.track);

  // Find the first packet at-or-before clampedStart. AAC has no
  // inter-frame prediction (each ADTS frame is independent), so any
  // packet at-or-before the window start is a valid decoder anchor
  // AND a usable mux input. We include any packet whose time range
  // overlaps the window.
  let firstPacket = await sink.getPacket(clampedStart);
  if (!firstPacket && clampedStart < 1.0) {
    // Edge case: clip starts at t=0 (or near it) but the AAC track's
    // first packet is at a small positive offset (typical: ~0.046s).
    // getPacket(0) returns null in that case. Walk forward to find
    // the actual first audio packet rather than producing a silent
    // export.
    // eslint-disable-next-line no-restricted-syntax
    for await (const p of sink.packets()) {
      firstPacket = p;
      break;
    }
  }
  if (!firstPacket) {
    return { added: 0, skipped: true, reason: 'no audio packets at-or-before window start' };
  }

  // Iterate forward. Anchor output timestamps at the first muxed
  // packet's source timestamp so the output MP4's audio timeline
  // starts at t=0.
  let firstTs = null;
  let added = 0;
  let metaSent = false;

  for await (const packet of sink.packets(firstPacket)) {
    if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
    // Stop at-or-after clampedEnd. Use packet.timestamp (cts) — AAC
    // packets are short (1024 / sampleRate ≈ 23ms at 44.1kHz) so
    // including a few past the end if any straddle the boundary is
    // cheap and avoids cutting mid-frame.
    if (packet.timestamp >= clampedEnd) break;
    // Skip packets entirely before the window. AAC packets are
    // independently decodable, so we don't need pre-window decode
    // context (unlike video).
    if (packet.timestamp + packet.duration <= clampedStart) continue;

    if (firstTs === null) firstTs = packet.timestamp;
    const reTimed = new EncodedPacket(
      packet.data,
      packet.type,                           // always 'key' for AAC
      packet.timestamp - firstTs,            // re-anchor at t=0
      packet.duration,
    );

    const meta = metaSent ? undefined : {
      decoderConfig: {
        codec:            audioInfo.codecParameterString,
        sampleRate:       audioInfo.sampleRate,
        numberOfChannels: audioInfo.channelCount,
        description:      audioInfo.description,
      },
    };
    // eslint-disable-next-line no-await-in-loop
    await audioSource.add(reTimed, meta);
    metaSent = true;
    added++;
  }

  if (added === 0) {
    return { added: 0, skipped: true, reason: 'no audio packets overlap window' };
  }
  return { added, skipped: false };
}

// ---------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------
//
// Options (v4 — source-faithful):
//   sourceFile:        File | Blob — REQUIRED. The original video file.
//                      Mediabunny streams reads from it; no in-memory
//                      copy is created.
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
//                      = match source framerate exactly from the first
//                      packet's duration.
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
  // Which M5 mode the burned-in overlay should render. Integer 0-4
  // matching the firmware's kModeNames / M5_MODES. Default 0 (Energy)
  // matches a fresh M5Sim's default but is almost always wrong for
  // export — the page should pass the live preview's current mode so
  // the export matches what the pilot sees on screen.
  displayMode     = 0,
  // "Standard" layout: render BOTH Attitude (ADI, displayType 1) and
  // Energy (displayType 0) burned into the source frame — ADI in the
  // bottom-left, Energy in the bottom-right (same Y, X mirrored across
  // the source centerline). When true, `displayMode` is ignored.
  standardOverlay = false,
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
  // Set the display mode on the fresh sim so state.displayType matches
  // the live preview's mode. Without this, every export rendered the
  // sim's default mode (0 = Energy) regardless of what the page showed.
  if (Number.isFinite(displayMode)) {
    try { sim.setMode(displayMode); } catch (_) { /* sim may not yet support */ }
  }
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

  // ---------- Demux probe (audio + video share one Input) -----------
  // Mediabunny opens the source with streaming reads — no head/tail
  // budget, no in-memory copy. Both track lookups go through the same
  // Input instance.
  const input = openInput(sourceFile);

  let videoInfo;
  try {
    videoInfo = await readVideoTrackInfo(input);
  } catch (e) {
    sim.delete();
    throw new Error(`mediabunny: failed to open source video track: ${e?.message || e}`);
  }
  if (!videoInfo) {
    sim.delete();
    throw new Error(
      'No supported video track in source file. Expected H.264 (avc1/avc3) ' +
      'or HEVC (hvc1/hev1). HEVC requires Chrome with hardware HEVC support.');
  }

  // Audio is optional. If it fails we still produce a silent MP4.
  const audioInfo = await readAudioTrackInfo(input);

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

  // Framerate: derive from the source's first packet duration. For
  // CFR video this is exact (e.g., 0.0333666... = 30000/1001 for
  // 29.97 fps). Mediabunny normalizes packet timing to floating-point
  // seconds; 1/duration recovers fps with enough precision for the
  // muxer's frameRate hint and the encoder config.
  const videoSink = new EncodedPacketSink(videoInfo.track);

  // Find the keyframe at-or-before clampedStart — this anchors the
  // decode chain. If none exists (clampedStart is before the first
  // key), pull the first packet of the track (which is always a key
  // in well-formed MP4).
  const anchorKey =
    (await videoSink.getKeyPacket(clampedStart)) ||
    (await videoSink.getPacket(0));
  if (!anchorKey) {
    sim.delete();
    throw new Error('Video track has no packets.');
  }

  // First packet duration → exact fps for CFR video.
  let resolvedFps;
  if (Number.isFinite(framerate) && framerate > 0) {
    resolvedFps = framerate;
  } else if (anchorKey.duration > 0) {
    resolvedFps = 1 / anchorKey.duration;
  } else {
    resolvedFps = DEFAULT_FRAMERATE;
  }

  // Bitrate: scale to pixel rate unless caller overrides.
  const resolvedBitrate = (Number.isFinite(bitrate) && bitrate > 0)
    ? bitrate
    : computeBitrate(W, H, resolvedFps);

  const canvas = new OffscreenCanvas(W, H);
  const ctx    = canvas.getContext('2d');

  // -------- Pick encoder config + output codec family ---------------
  // Probe HEVC first when the source is HEVC. The encoder config
  // result tells us which family won (avc vs hevc); the Mediabunny
  // packet source mirrors it.
  const encConfig = await pickEncoderConfig({
    width: W, height: H,
    bitrate: resolvedBitrate,
    framerate: resolvedFps,
    sourceCodec: videoInfo.codec,
  });
  const outputCodecFamily = /^hev1\.|^hvc1\./i.test(encConfig.codec) ? 'hevc' : 'avc';

  // Mediabunny's frameRate option is a hint passed to the track
  // header. Actual per-frame timing comes from each EncodedPacket's
  // timestamp + duration in seconds — so 29.97 stays 29.97 regardless
  // of the header value. Round to the nearest integer for the muxer.
  const muxerFrameRate = Math.max(1, Math.round(resolvedFps));

  // -------- Build Mediabunny Output ---------------------------------
  // Single Output for video + (optional) audio. Both share one
  // BufferTarget; finalize() resolves to ArrayBuffer.
  //
  // Rotation: we rotate pixels at composite time (compositeFrame) so
  // the output frames are upright. Pass rotation: 0 so the muxer
  // writes an identity tkhd matrix — players don't apply any
  // additional rotation. (Honoring the source matrix at composite-
  // time is required because we draw the overlay AFTER the rotation,
  // and we want the overlay to sit upright on screen.)
  const output = new Output({
    format: new Mp4OutputFormat({ fastStart: 'in-memory' }),
    target: new BufferTarget(),
  });
  // Set true once output.finalize() resolves. The finally-block uses
  // this to decide whether to call output.cancel() on teardown: cancel
  // logs a benign "already finalized" warning post-finalize, so we
  // only cancel when we're bailing out mid-stream.
  let outputFinalized = false;

  const videoPacketSource = new EncodedVideoPacketSource(outputCodecFamily);
  output.addVideoTrack(videoPacketSource, {
    rotation:  0,
    frameRate: muxerFrameRate,
  });

  let audioPacketSource = null;
  if (audioInfo) {
    audioPacketSource = new EncodedAudioPacketSource('aac');
    output.addAudioTrack(audioPacketSource);
  }
  await output.start();

  let encoderError = null;
  let firstVideoMetaSent = false;
  const encoder = new VideoEncoder({
    output: (chunk, meta) => {
      // Wrap the EncodedVideoChunk as a Mediabunny EncodedPacket and
      // push to the video source. First chunk carries decoderConfig
      // (codec, codedWidth, codedHeight, description) — the muxer
      // needs the AVCDecoderConfigurationRecord / HEVCDecoderConfigurationRecord
      // bytes in description to write the correct avcC / hvcC box.
      try {
        const packet = EncodedPacket.fromEncodedChunk(chunk);
        const m = firstVideoMetaSent ? undefined : meta;
        firstVideoMetaSent = true;
        // Don't await — VideoEncoder.output is a sync callback. The
        // packet source queues internally; back-pressure is handled
        // via encoder.encodeQueueSize in the main loop.
        videoPacketSource.add(packet, m).catch((e) => {
          encoderError = e;
        });
      } catch (e) {
        encoderError = e;
      }
    },
    error: (e) => { encoderError = e; },
  });
  encoder.configure(encConfig);

  // ---------- Spawn AAC demux in the background ----------------------
  // Pull each AAC packet via Mediabunny's EncodedPacketSink and push
  // straight into the output's audio source. No decode + re-encode.
  // Runs in parallel with the video decode/encode; both feed the
  // same Output.
  let audioPromise = null;
  if (audioInfo && audioPacketSource) {
    audioPromise = feedAacPacketsToOutput({
      audioSource: audioPacketSource,
      audioInfo,
      clampedStart,
      clampedEnd,
      signal,
    });
  }

  // ---------- Video decode → composite → encode pipeline -------------
  //
  // Strategy (source-faithful):
  //   1. Walk packets in decode order starting from the keyframe at-
  //      or-before clampedStart. Stop when packet.timestamp >=
  //      clampedEnd. Packets whose timestamp falls inside the clip
  //      window are flagged `output: true`; earlier packets (the
  //      decode context back to the anchor keyframe) are flagged
  //      `output: false`.
  //   2. Feed each packet's encoded bytes to a VideoDecoder. Output
  //      frames arrive in decode order (which can differ from display
  //      order for B-frame streams).
  //   3. Hold each output frame in a sorted-by-timestamp queue.
  //   4. Walk the output-flagged packets in cts order. For each
  //      output packet: drive sim to that packet's mediaTime, pop
  //      the matching decoded frame, composite overlay, encode the
  //      composite VideoFrame with the source packet's exact
  //      (timestamp, duration) in microseconds.

  // Collect the selected packet sequence (in decode order). This
  // materializes lightweight {data, type, timestamp, duration, output}
  // descriptors — the raw Uint8Array stays in memory only between
  // demux and decoder.decode().
  const selectedPackets = [];
  for await (const packet of videoSink.packets(anchorKey)) {
    if (signal?.aborted) {
      sim.delete();
      throw new DOMException('aborted', 'AbortError');
    }
    if (packet.timestamp >= clampedEnd) break;
    const inWindow = (packet.timestamp + packet.duration > clampedStart) &&
                     (packet.timestamp < clampedEnd);
    selectedPackets.push({
      data:      packet.data,
      type:      packet.type,
      timestamp: packet.timestamp,     // seconds
      duration:  packet.duration,      // seconds
      output:    inWindow,
    });
  }
  if (selectedPackets.length === 0) {
    sim.delete();
    throw new Error('No video samples overlap the clip window.');
  }
  const outputSamples = selectedPackets.filter(p => p.output);
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
      // Drop decode-context frames (pre-window keyframe + leading
      // B/P frames before clampedStart). Their job — providing
      // decode state for forward-dependent in-window frames — is
      // done as soon as the decoder consumed them. Keeping them in
      // the frameQueue would let waitForFrameAtOrBefore pick a
      // pre-window frame for the first output slot (target ts = 0)
      // and produce a "video starts at takeoff, not at clip start"
      // bug. Their `timestamp` is negative (anchored at the first
      // output packet's cts via toOutTsUs).
      if (frame.timestamp < 0) {
        frame.close();
        return;
      }
      insertFrame(frame);
    },
    error: (e) => { decoderError = e; },
  });

  // Try to configure. If it fails we surface a clear error — most
  // common cause is HEVC on a non-HEVC-capable Chrome.
  const decoderCfg = {
    codec:        videoInfo.codecParameterString,
    description:  videoInfo.description,
    codedWidth:   videoInfo.width,
    codedHeight:  videoInfo.height,
  };
  try {
    const support = await VideoDecoder.isConfigSupported(decoderCfg);
    if (!support.supported) {
      sim.delete();
      const what = videoInfo.codec === 'hevc' ? 'HEVC (h.265)' :
                   `codec "${videoInfo.codecParameterString}"`;
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

  // Anchor video timestamps at the first output sample's cts so the
  // muxer sees a t=0-origin timeline. Use cts (presentation), not
  // decode order, because we sort output frames by cts.
  const firstWindowTs = outputSamples[0].timestamp;

  // Convert a source-time (seconds) to output-time (microseconds),
  // anchored at firstWindowTs. Decode-only packets (before the
  // window) get negative ts; that's fine — VideoDecoder accepts any
  // int64, and we filter on cts later when matching to output slots.
  function toOutTsUs(srcSec) {
    return Math.round((srcSec - firstWindowTs) * 1_000_000);
  }
  function toOutDurUs(srcDurSec) {
    return Math.max(1, Math.round(srcDurSec * 1_000_000));
  }

  // ---------- Demux feed task ---------------------------------------
  // Walks selectedPackets and feeds the decoder. Back-pressures when
  // the decoder is saturated.
  const feedDone = (async () => {
    for (let i = 0; i < selectedPackets.length; i++) {
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

      const p = selectedPackets[i];
      const chunk = new EncodedVideoChunk({
        type:      p.type,                        // 'key' | 'delta'
        timestamp: toOutTsUs(p.timestamp),
        duration:  toOutDurUs(p.duration),
        data:      p.data,
      });
      decoder.decode(chunk);
      // Release the encoded bytes reference now that the decoder has
      // consumed (and internally copied) them. The descriptor's
      // remaining {type,timestamp,duration,output} fields are still
      // needed for slot-matching; only `data` is heavy. For a 30s
      // clip at 50 Mbps that's ~900 packets × ~180 KB ≈ 150 MB of
      // pinned encoded-bytes that this null-out releases.
      selectedPackets[i].data = null;
    }
    await decoder.flush();
  })();

  // ---------- Output-sample-driven encode loop ----------------------
  // Walks `outputSamples` (every source packet in the clip window) in
  // cts order. For each output sample:
  //   - target timestamp is the sample's own cts (us, anchored at
  //     firstWindowTs), so output framerate matches source exactly.
  //   - drive sim to that mediaTime, render overlay
  //   - pop the decoded frame whose ts matches, composite, encode.
  let feedError = null;
  feedDone.catch((e) => { feedError = e; });

  // Keyframe cadence in output frames. ~1 keyframe per second.
  const keyframeStride = Math.max(1, Math.round(resolvedFps));

  // Match a decoded frame to an output sample by timestamp. Decoder
  // emits frames at the same per-sample ts the feeder used; the
  // frameQueue is sorted by ts. Look for the largest ts ≤ targetTs.
  let slotsEncoded = 0;
  try {
    for (let i = 0; i < outputSamples.length; i++) {
      if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
      if (decoderError) throw decoderError;
      if (feedError && !(feedError.name === 'AbortError')) throw feedError;
      if (encoderError) throw encoderError;

      const s = outputSamples[i];
      const sampleMediaTime = s.timestamp;            // seconds
      const targetTsUs      = toOutTsUs(s.timestamp);

      // eslint-disable-next-line no-await-in-loop
      let frame = await waitForFrameAtOrBefore(
        frameQueue, targetTsUs, feedDone, signal);

      if (!frame) {
        // Drained queue with nothing usable — should not happen if
        // selectedPackets was constructed correctly. Fall back to
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
        const dtSec = s.duration;
        const smoothed = presFilter.apply(rawState.LateralG, rawState.VerticalG, dtSec);
        m5State = Object.freeze({
          ...rawState,
          LateralG:  Number.isFinite(smoothed.lateralG)  ? smoothed.lateralG  : rawState.LateralG,
          VerticalG: Number.isFinite(smoothed.verticalG) ? smoothed.verticalG : rawState.VerticalG,
        });
      }

      // Build the per-frame overlay list. Standard layout: ADI on
      // the left, Energy on the right (same Y, mirrored X). Single
      // mode: one overlay pinned to the right corner (legacy layout).
      // Two separate svgToImage calls — each mode reads its own
      // displayType branches inside the SVG, so a single render with
      // an overridden state object isn't equivalent.
      const overlays = [];
      const renderOne = async (displayType, position) => {
        try {
          const svgEl = renderOverlaySvg ? renderOverlaySvg(m5State, displayType) : null;
          if (svgEl) {
            // eslint-disable-next-line no-await-in-loop
            const img = await svgToImage(svgEl);
            if (img) overlays.push({ img, position });
          }
        } catch (e) {
          // eslint-disable-next-line no-console
          console.warn('overlay raster failed for frame', i, e);
        }
      };
      if (standardOverlay) {
        await renderOne(1, 'left');   // ADI / Attitude
        await renderOne(0, 'right');  // Energy
      } else {
        await renderOne(undefined, 'right'); // single mode, sim's displayType wins
      }

      if (frame) compositeFrame(ctx, frame, overlays, W, H, videoInfo.rotationDeg);
      else if (overlays.length > 0) compositeFrame(ctx, null, overlays, W, H, 0);

      // Close the source VideoFrame as soon as we've drawn it. Holds
      // GPU/system memory; leaving it for GC triggers WebCodecs'
      // "VideoFrame garbage collected without being closed" warning.
      if (frame) { try { frame.close(); } catch (_) {} }

      // Encoder back-pressure.
      while (encoder.encodeQueueSize > 4) {
        if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
        // eslint-disable-next-line no-await-in-loop
        await new Promise(r => setTimeout(r, 0));
      }
      if (encoderError) throw encoderError;

      // Output frame inherits source sample's exact ts + duration.
      const outTsUs  = targetTsUs;
      const outDurUs = toOutDurUs(s.duration);
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

    await output.finalize();
    outputFinalized = true;

    const buffer = output.target.buffer;
    if (!buffer) throw new Error('Output produced no buffer.');
    return new Blob([buffer], { type: 'video/mp4' });
  } finally {
    // Close any decoded frames left in queue.
    for (const f of frameQueue) { try { f.close(); } catch (_) {} }
    frameQueue.length = 0;
    try { decoder.state !== 'closed' && decoder.close(); } catch (_) {}
    try { encoder.state !== 'closed' && encoder.close(); } catch (_) {}
    // If we exit before finalize() completes, ask Mediabunny to tear
    // down the Output (closes encoders/writer, frees internal state).
    // Skip on the happy path — calling cancel() after finalize() logs
    // a benign "already finalized" warning. Wrapped in try/catch so a
    // teardown error can't shadow the real exception.
    if (!outputFinalized) {
      try { await output.cancel(); } catch (_) {}
    }
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

// Order the UI lists modes in — matches the M5 firmware's BtnB-cycle
// order (kModeNames[5]) and the live page's mode-selector radio row.
// Pilots already have muscle memory for "Energy first" from the
// hardware display, so matching that here keeps the workflow obvious.
export const OVERLAY_MODE_ORDER = Object.freeze([
  'energy',     // 0
  'attitude',   // 1
  'indexer',    // 2
  'decel',      // 3
  'historic-g', // 4
]);

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

// Composite the overlay SVG into the entire canvas. In native-dimensions
// mode (default), the canvas IS the M5 panel — same 320×240 pixel grid
// the hardware draws to, same black background that the avionics SVG
// renders against. There's no padding, no chroma key, no "video to
// composite onto" — the output IS the M5 panel as a video.
//
// Vac drops this into his NLE on top of his GoPro footage, scales /
// positions to taste. iMovie, Final Cut, Premiere, Resolve all treat
// the black panel correctly — it sits over the underlying video like
// a real cockpit instrument.
//
// `background` is the canvas fill before the overlay draws. Default
// '#000' matches the M5's --panel-bg. Set to a chroma color if the
// caller wants the legacy chroma-key fallback.
function compositeOverlayNative(ctx, overlayImg, background, W, H) {
  ctx.fillStyle = background;
  ctx.fillRect(0, 0, W, H);
  if (!overlayImg) return;
  // Overlay SVG viewBox is 0 0 M5_PANEL_W M5_PANEL_H (320×240). When
  // canvas dims match the panel exactly, this draws at 1:1; when the
  // canvas is scaled (e.g. user opted into 1080p), the SVG scales
  // proportionally — the aspect matches, so no letterboxing.
  ctx.drawImage(overlayImg, 0, 0, W, H);
}

// One encoder + Mediabunny output + canvas, scoped to a single mode.
// The frame loop instantiates one of these per mode requested. Each
// has its own VideoEncoder back-pressure queue; sharing a single
// encoder across modes would serialise the per-mode encode work
// pointlessly.
class OverlayModeEncoder {
  constructor({ modeId, encConfig, W, H, framerate }) {
    this.modeId = modeId;
    this.W = W;
    this.H = H;
    this.canvas = new OffscreenCanvas(W, H);
    this.ctx    = this.canvas.getContext('2d');
    this.output = new Output({
      format: new Mp4OutputFormat({ fastStart: 'in-memory' }),
      target: new BufferTarget(),
    });
    this.videoSource = new EncodedVideoPacketSource('avc');
    this.output.addVideoTrack(this.videoSource, {
      rotation:  0,
      frameRate: framerate,
    });
    this.error = null;
    let firstMetaSent = false;
    this.encoder = new VideoEncoder({
      output: (chunk, meta) => {
        try {
          const packet = EncodedPacket.fromEncodedChunk(chunk);
          const m = firstMetaSent ? undefined : meta;
          firstMetaSent = true;
          this.videoSource.add(packet, m).catch((e) => { this.error = e; });
        } catch (e) { this.error = e; }
      },
      error: (e) => { this.error = e; },
    });
    this.encoder.configure(encConfig);
    this.framesEncoded = 0;
    this.started = false;
    // Set true once output.finalize() resolves. Used by closeOnError
    // to skip cancel() on already-finalized outputs (which would log
    // a benign but noisy "already finalized" warning).
    this.finalized = false;
  }

  async start() {
    await this.output.start();
    this.started = true;
  }

  async finalize() {
    if (this.error) throw this.error;
    await this.encoder.flush();
    if (this.error) throw this.error;
    this.encoder.close();
    await this.output.finalize();
    this.finalized = true;
    const buffer = this.output.target.buffer;
    if (!buffer) throw new Error(`Output for "${this.modeId}" produced no buffer.`);
    return new Blob([buffer], { type: 'video/mp4' });
  }

  async closeOnError() {
    try { if (this.encoder.state !== 'closed') this.encoder.close(); } catch (_) {}
    // Only cancel the Output if we're bailing out before finalize.
    // Post-finalize cancel() logs a benign warning. Wrapped so a
    // teardown error can't shadow the original failure.
    if (!this.finalized) {
      try { await this.output.cancel(); } catch (_) {}
    }
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
//   background:         CSS color string for the canvas background.
//                       Default '#000' — matches the M5's own panel
//                       background. The output IS the M5 panel as a
//                       video; Vac composites it on top of his footage
//                       in his NLE. Pass a chroma color (e.g.
//                       '#00ff00') if you specifically want chroma-key.
//   outputWidth/Height: explicit output dims. Default = M5 panel native
//                       dimensions (320×240). The avionics SVG renders
//                       to exactly this pixel grid (it's the M5
//                       hardware's screen size), so the default is
//                       lossless 1:1 — no upscale, no downscale.
//                       Override to render bigger (e.g. 1920×1440)
//                       for callers that want pre-scaled output.
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

  // Resolve output dims + framerate. The default is the M5 panel's
  // native pixel grid (320×240) — same as the M5 hardware screen, same
  // as the avionics SVG viewBox. Rendered at 1:1, the output IS the
  // M5 panel as a video. Caller can override (e.g. for 1080p prescaled
  // output) but most workflows want native: Vac scales in the NLE.
  const srcFps = sourceVideoInfo?.frameRate;
  let W = outputWidth  || M5_PANEL_W;
  let H = outputHeight || M5_PANEL_H;
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
  // clone it into one encoder + Output per mode. Mediabunny's Output
  // and WebCodecs' VideoEncoder are both independent instances, so
  // running N in parallel is safe.
  const encConfig = await pickOverlayEncoderConfig({
    width: W, height: H, bitrate: resolvedBitrate, framerate: fps,
  });

  const modeEncoders = new Map(); // modeId → OverlayModeEncoder
  try {
    for (const m of modeList) {
      const me = new OverlayModeEncoder({
        modeId: m.id, encConfig, W, H, framerate: muxerFps,
      });
      // eslint-disable-next-line no-await-in-loop
      await me.start();
      modeEncoders.set(m.id, me);
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
        compositeOverlayNative(enc.ctx, overlayImg, background, W, H);
        const outFrame = new VideoFrame(enc.canvas, {
          timestamp: tsUs, duration: dtUs,
        });
        enc.encoder.encode(outFrame, { keyFrame: isKey });
        outFrame.close();
        aggregateTimings.encodeMs += performance.now() - tC;
        enc.framesEncoded++;

      }

      // Report progress once per frame (not once per mode). Aggregate
      // across modes so the bar advances smoothly instead of flashing
      // 5 times per frame. `framesEncoded` counts encoded *output*
      // slots (one per frame, regardless of how many modes are
      // simultaneously encoding). `totalFrames` is also per-mode, so
      // the ratio is the same for every mode — no mode-switch flicker.
      if (onProgress) {
        onProgress({
          // No `mode` field: this is a multi-mode aggregate report.
          // The page-side label can show "exporting N modes" instead
          // of cycling through individual mode IDs.
          frame:      f + 1,
          totalFrames,
          modeCount:  modeList.length,
          encodedSec: ((f + 1) * dtUs) / 1_000_000,
          totalSec:   clipDurSec,
        });
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
    // Best-effort teardown. closeOnError is async (it awaits
    // output.cancel()), so we await all of them in parallel before
    // tearing the sim down. Wrapping in a single catch so one
    // encoder's failure doesn't prevent the others from cleaning up.
    try {
      await Promise.all(
        Array.from(modeEncoders.values()).map(enc => enc.closeOnError()),
      );
    } catch (_) {}
    try { sim.delete(); } catch (_) {}
  }
}

// Exported for tests + diagnostics. The rotation helper in particular
// is non-trivial enough (fixed-point vs float, sign-disambiguation
// across four cardinal angles) that round-tripping a few canonical
// matrices belongs in the test suite. Mediabunny exposes the result
// directly via `track.getRotation()` in production code, but the
// helper round-trips the same algebra against canonical matrices in
// the smoke tests to lock the rotation semantics.
export { rotationFromTkhdMatrix };

export const MP4_EXPORT_INTERNAL = Object.freeze({
  M5_TICK_MS,
  M5_LARGE_JUMP_MS,
  DEFAULT_FRAMERATE,
});
