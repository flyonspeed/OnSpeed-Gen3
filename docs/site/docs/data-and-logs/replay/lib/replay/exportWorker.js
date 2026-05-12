// exportWorker.js — Web Worker entry point for the MP4 export pipeline.
//
// Runs the full Mediabunny demux + VideoDecoder + composite +
// VideoEncoder + Muxer chain off the main thread. The main thread
// (mp4Export.js) spawns this worker, posts the export parameters,
// then handles a single back-and-forth message per frame:
//
//   Worker  →  Main:    svg-request   { frameId, m5State, modeId? }
//   Main    →  Worker:  svg-frame     { frameId, svgString }
//
// Main renders the SVG element via the page's `renderOverlaySvg`
// callback (which uses Preact + the M5_MODES catalog mounted into a
// detached DOM node — neither of which work in a Worker), serialises
// it via XMLSerializer, and posts the string back. The Worker then
// rasterises with `createImageBitmap(new Blob([svgString], …))` — an
// off-main-thread codec call — and composites onto its
// OffscreenCanvas.
//
// This is a module Worker. Mediabunny is imported as ESM; m5sim is
// loaded by the m5sim module itself via fetch+eval (it detects the
// missing `document` and switches loaders). PresentationFilter and
// findRowAt are pure JS imports.

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

// ---------------------------------------------------------------------
// Fallback encoder parameters used only when the source's own framerate
// or codec can't be discovered. Match mp4Export.js's main-thread copy
// so the worker emits identical output for identical inputs.
// ---------------------------------------------------------------------
const DEFAULT_FRAMERATE = 30;
const M5_TICK_MS = 50;
const M5_LARGE_JUMP_MS = 5000;

function computeBitrate(width, height, framerate) {
  const w = Number.isFinite(width)     && width     > 0 ? width     : 1920;
  const h = Number.isFinite(height)    && height    > 0 ? height    : 1080;
  const f = Number.isFinite(framerate) && framerate > 0 ? framerate : 30;
  const pixelsPerSec = w * h * f;
  const bitsPerPixel = (w >= 2560) ? 0.15 : 0.13;
  return Math.round(pixelsPerSec * bitsPerPixel);
}

async function pickEncoderConfig({ width, height, bitrate, framerate, sourceCodec }) {
  const w = Math.max(2, Math.floor(width  / 2) * 2);
  const h = Math.max(2, Math.floor(height / 2) * 2);
  const isHevc = sourceCodec === 'hevc';
  const hevcCandidates = [
    { codec: 'hev1.1.6.L153.B0', width: w, height: h, bitrate, framerate, hevc: { format: 'hevc' } },
    { codec: 'hev1.1.6.L150.B0', width: w, height: h, bitrate, framerate, hevc: { format: 'hevc' } },
    { codec: 'hev1.1.6.L120.B0', width: w, height: h, bitrate, framerate, hevc: { format: 'hevc' } },
  ];
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
    `${isHevc ? 'HEVC Main + H.264 High/Baseline' : 'H.264 High/Baseline'}.`);
}

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

// ---------------------------------------------------------------------
// Composite — same algebra as mp4Export.js (pre-worker) but
// `overlayImg` is now an ImageBitmap (transferable, fast), not an
// HTMLImageElement. drawImage accepts both identically.
// ---------------------------------------------------------------------
function compositeFrame(ctx, videoSrc, overlayImg, W, H, rotationDeg = 0) {
  if (videoSrc) {
    const r = ((rotationDeg % 360) + 360) % 360;
    if (r === 0) {
      ctx.drawImage(videoSrc, 0, 0, W, H);
    } else {
      ctx.save();
      ctx.translate(W / 2, H / 2);
      ctx.rotate(r * Math.PI / 180);
      ctx.drawImage(videoSrc, -W / 2, -H / 2, W, H);
      ctx.restore();
    }
  }
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

function compositeOverlayNative(ctx, overlayImg, background, W, H) {
  ctx.fillStyle = background;
  ctx.fillRect(0, 0, W, H);
  if (!overlayImg) return;
  ctx.drawImage(overlayImg, 0, 0, W, H);
}

// SVG bytes → ImageBitmap. Worker-friendly: no Image() constructor,
// no Blob URL round-trip. The SVG carries its own viewBox so the
// rasteriser scales to canvas dims at composite time.
//
// Some Chrome builds reject SVG bytes without intrinsic dimensions in
// createImageBitmap. Page-side renderOverlaySvg already stamps the
// viewBox; we additionally pass `resizeWidth/Height` matching the
// canvas dest size so the rasterised bitmap is the exact size we'll
// draw at — no per-frame downscale during drawImage.
async function svgStringToBitmap(svgString, destW, destH) {
  if (!svgString) return null;
  const blob = new Blob([svgString], { type: 'image/svg+xml;charset=utf-8' });
  try {
    return await createImageBitmap(blob, {
      resizeWidth:   Math.max(1, Math.round(destW)),
      resizeHeight:  Math.max(1, Math.round(destH)),
      resizeQuality: 'high',
    });
  } catch (e) {
    // Some Safari/Chrome combos reject the resize options on SVGs
    // without intrinsic dims. Retry without the resize.
    try {
      return await createImageBitmap(blob);
    } catch (e2) {
      // eslint-disable-next-line no-console
      console.warn('svgStringToBitmap: createImageBitmap failed:', e2);
      return null;
    }
  }
}

// ---------------------------------------------------------------------
// Sim driver — verbatim copy of mp4Export.js's driveSimToVirtMs. Worker
// runs M5Sim natively because the JS+WASM module now loads inside the
// worker (m5sim.js's loader detects the missing `document` and uses
// fetch+eval).
// ---------------------------------------------------------------------
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
// SVG request/reply bridge — the only point at which the worker
// suspends waiting on the main thread. Sequential per frame for now;
// pipelining N+1 while encoding N is a separate optimisation (the
// SVG round-trip is ~0.5 ms in practice, vs. 5-15 ms for the rest
// of the per-frame work).
// ---------------------------------------------------------------------
let _nextFrameId = 1;
const _pendingSvgReplies = new Map();   // frameId → resolver

function requestSvg(m5State, modeId = null) {
  const frameId = _nextFrameId++;
  // Strip the frozen Float32Array view from m5State. structured-clone
  // would copy it correctly but the per-frame payload is then 1.2 KB
  // and the receiving side has no use for it (gHistory isn't rendered
  // by any per-frame SVG path — only Mode 4 reads it, and that
  // reads off the live state object on the page's own sim, not the
  // export sim's state).
  //
  // Actually: the page-side render callback DOES use the full m5State
  // for Mode 4 (Historic G). So we keep the array. Float32Array
  // structured-clones to a fresh Float32Array; cheap.
  return new Promise((resolve, reject) => {
    _pendingSvgReplies.set(frameId, { resolve, reject });
    self.postMessage({ type: MSG_SVG_REQUEST, frameId, m5State, modeId });
  });
}

function handleSvgFrame(msg) {
  const pending = _pendingSvgReplies.get(msg.frameId);
  if (!pending) return;
  _pendingSvgReplies.delete(msg.frameId);
  pending.resolve(msg.svgString);
}

function rejectAllPendingSvg(reason) {
  for (const { reject } of _pendingSvgReplies.values()) reject(reason);
  _pendingSvgReplies.clear();
}

// ---------------------------------------------------------------------
// Mediabunny demux helpers (copied from mp4Export.js).
// ---------------------------------------------------------------------
function openInput(file) {
  return new Input({
    source: new BlobSource(file),
    formats: ALL_FORMATS,
  });
}

async function readVideoTrackInfo(input) {
  const track = await input.getPrimaryVideoTrack();
  if (!track) return null;
  const codec = await track.getCodec();
  if (codec !== 'avc' && codec !== 'hevc') return null;
  const decoderCfg = await track.getDecoderConfig();
  if (!decoderCfg || !decoderCfg.description) return null;
  const width  = await track.getCodedWidth();
  const height = await track.getCodedHeight();
  if (!width || !height) return null;
  const rotationDeg = await track.getRotation();
  return {
    track,
    codec,
    codecParameterString: decoderCfg.codec,
    width,
    height,
    description: new Uint8Array(decoderCfg.description),
    rotationDeg: rotationDeg || 0,
    decoderCfg,
  };
}

async function readAudioTrackInfo(input) {
  const track = await input.getPrimaryAudioTrack();
  if (!track) return null;
  const codec = await track.getCodec();
  if (codec !== 'aac') return null;
  const decoderCfg = await track.getDecoderConfig();
  if (!decoderCfg || !decoderCfg.description) return null;
  const sampleRate = await track.getSampleRate();
  const channelCount = await track.getNumberOfChannels();
  if (!sampleRate || !channelCount) return null;
  return {
    track,
    codec,
    codecParameterString: decoderCfg.codec,
    sampleRate,
    channelCount,
    description: new Uint8Array(decoderCfg.description),
  };
}

async function feedAacPacketsToOutput({
  audioSource, audioInfo, clampedStart, clampedEnd, signal,
}) {
  const sink = new EncodedPacketSink(audioInfo.track);
  const firstPacket = await sink.getPacket(clampedStart);
  if (!firstPacket) {
    return { added: 0, skipped: true, reason: 'no audio packets at-or-before window start' };
  }
  let firstTs = null;
  let added = 0;
  let metaSent = false;
  for await (const packet of sink.packets(firstPacket)) {
    if (signal?.aborted) throw new DOMException('aborted', 'AbortError');
    if (packet.timestamp >= clampedEnd) break;
    if (packet.timestamp + packet.duration <= clampedStart) continue;
    if (firstTs === null) firstTs = packet.timestamp;
    const reTimed = new EncodedPacket(
      packet.data, packet.type,
      packet.timestamp - firstTs, packet.duration,
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

// WebCodecs `dequeue` event back-pressure. Cleaner than polling
// encodeQueueSize through setTimeout(0): the event fires exactly when
// the queue depth drops below the next-checkpoint threshold.
function waitForEncoderDequeue(encoder, threshold) {
  return new Promise(resolve => {
    if (encoder.encodeQueueSize < threshold) { resolve(); return; }
    const onDequeue = () => {
      if (encoder.encodeQueueSize < threshold) {
        encoder.removeEventListener('dequeue', onDequeue);
        resolve();
      }
    };
    encoder.addEventListener('dequeue', onDequeue);
  });
}

function waitForDecoderDequeue(decoder, threshold) {
  return new Promise(resolve => {
    if (decoder.decodeQueueSize < threshold) { resolve(); return; }
    const onDequeue = () => {
      if (decoder.decodeQueueSize < threshold) {
        decoder.removeEventListener('dequeue', onDequeue);
        resolve();
      }
    };
    decoder.addEventListener('dequeue', onDequeue);
  });
}

// ---------------------------------------------------------------------
// Composite export — equivalent of exportClipAsMp4().
// ---------------------------------------------------------------------
async function runCompositeExport(params, signal) {
  const {
    clip, sync, log, cppWireFrames,
    sourceFile,
    presentationTau = null,
    displayMode = 0,
    outputWidth = null,
    bitrate = null,
    framerate = null,
    videoDurationSec,
  } = params;

  // Map clip times to video seconds.
  const startVideoSec = sync.videoTakeoffSec +
                        (clip.startMs - sync.logTakeoffMs) / 1000;
  const endVideoSec   = sync.videoTakeoffSec +
                        (clip.endMs   - sync.logTakeoffMs) / 1000;
  if (!Number.isFinite(startVideoSec) || !Number.isFinite(endVideoSec) ||
      endVideoSec <= startVideoSec) {
    throw new Error('clip window maps to an empty or invalid video range.');
  }
  const clampedStart = Math.max(0, startVideoSec);
  const clampedEnd   = Math.min(videoDurationSec, endVideoSec);
  if (clampedEnd <= clampedStart) {
    throw new Error('clip falls outside the loaded video.');
  }
  const totalSec = clampedEnd - clampedStart;

  const sim = await M5Sim.create();
  if (signal.aborted) { sim.delete(); throw new DOMException('aborted', 'AbortError'); }
  if (Number.isFinite(displayMode)) {
    try { sim.setMode(displayMode); } catch (_) { /* ignore */ }
  }
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

  const input = openInput(sourceFile);
  const videoInfo = await readVideoTrackInfo(input);
  if (!videoInfo) {
    sim.delete();
    throw new Error('No supported video track in source file.');
  }
  const audioInfo = await readAudioTrackInfo(input);

  const srcW = videoInfo.width;
  const srcH = videoInfo.height;
  const W = outputWidth
    ? Math.max(2, Math.floor(outputWidth / 2) * 2)
    : Math.max(2, Math.floor(srcW / 2) * 2);
  const H = outputWidth
    ? Math.max(2, Math.floor(W * srcH / srcW / 2) * 2)
    : Math.max(2, Math.floor(srcH / 2) * 2);

  const videoSink = new EncodedPacketSink(videoInfo.track);
  const anchorKey =
    (await videoSink.getKeyPacket(clampedStart)) ||
    (await videoSink.getPacket(0));
  if (!anchorKey) {
    sim.delete();
    throw new Error('Video track has no packets.');
  }

  let resolvedFps;
  if (Number.isFinite(framerate) && framerate > 0) {
    resolvedFps = framerate;
  } else if (anchorKey.duration > 0) {
    resolvedFps = 1 / anchorKey.duration;
  } else {
    resolvedFps = DEFAULT_FRAMERATE;
  }
  const resolvedBitrate = (Number.isFinite(bitrate) && bitrate > 0)
    ? bitrate : computeBitrate(W, H, resolvedFps);

  const canvas = new OffscreenCanvas(W, H);
  const ctx = canvas.getContext('2d');

  const encConfig = await pickEncoderConfig({
    width: W, height: H, bitrate: resolvedBitrate, framerate: resolvedFps,
    sourceCodec: videoInfo.codec,
  });
  const outputCodecFamily = /^hev1\.|^hvc1\./i.test(encConfig.codec) ? 'hevc' : 'avc';
  const muxerFrameRate = Math.max(1, Math.round(resolvedFps));

  const output = new Output({
    format: new Mp4OutputFormat({ fastStart: 'in-memory' }),
    target: new BufferTarget(),
  });
  const videoPacketSource = new EncodedVideoPacketSource(outputCodecFamily);
  output.addVideoTrack(videoPacketSource, { rotation: 0, frameRate: muxerFrameRate });

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
      try {
        const packet = EncodedPacket.fromEncodedChunk(chunk);
        const m = firstVideoMetaSent ? undefined : meta;
        firstVideoMetaSent = true;
        videoPacketSource.add(packet, m).catch((e) => { encoderError = e; });
      } catch (e) { encoderError = e; }
    },
    error: (e) => { encoderError = e; },
  });
  encoder.configure(encConfig);

  let audioPromise = null;
  if (audioInfo && audioPacketSource) {
    audioPromise = feedAacPacketsToOutput({
      audioSource: audioPacketSource,
      audioInfo, clampedStart, clampedEnd, signal,
    });
  }

  // Collect selected packets.
  const selectedPackets = [];
  for await (const packet of videoSink.packets(anchorKey)) {
    if (signal.aborted) { sim.delete(); throw new DOMException('aborted', 'AbortError'); }
    if (packet.timestamp >= clampedEnd) break;
    const inWindow = (packet.timestamp + packet.duration > clampedStart) &&
                     (packet.timestamp < clampedEnd);
    selectedPackets.push({
      data: packet.data, type: packet.type,
      timestamp: packet.timestamp, duration: packet.duration,
      output: inWindow,
    });
  }
  if (selectedPackets.length === 0) { sim.delete(); throw new Error('No video samples overlap the clip window.'); }
  const outputSamples = selectedPackets.filter(p => p.output);
  if (outputSamples.length === 0) { sim.delete(); throw new Error('No video samples overlap the clip window.'); }
  const totalFrames = outputSamples.length;

  const frameQueue = [];
  let decoderError = null;
  function insertFrame(frame) {
    const ts = frame.timestamp;
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
      // Drop decode-context frames (pre-window keyframe + leading B/P).
      if (frame.timestamp < 0) { frame.close(); return; }
      insertFrame(frame);
    },
    error: (e) => { decoderError = e; },
  });

  const decoderCfg = {
    codec: videoInfo.codecParameterString,
    description: videoInfo.description,
    codedWidth: videoInfo.width,
    codedHeight: videoInfo.height,
  };
  try {
    const support = await VideoDecoder.isConfigSupported(decoderCfg);
    if (!support.supported) {
      sim.delete();
      const what = videoInfo.codec === 'hevc' ? 'HEVC (h.265)'
                                              : `codec "${videoInfo.codecParameterString}"`;
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

  const firstWindowTs = outputSamples[0].timestamp;
  function toOutTsUs(srcSec) { return Math.round((srcSec - firstWindowTs) * 1_000_000); }
  function toOutDurUs(d)     { return Math.max(1, Math.round(d * 1_000_000)); }

  // Demux feed.
  const feedDone = (async () => {
    for (let i = 0; i < selectedPackets.length; i++) {
      if (signal.aborted) throw new DOMException('aborted', 'AbortError');
      if (decoderError) throw decoderError;
      // Cap in-flight; dequeue-event back-pressure on the decoder
      // side, plus a simple frameQueue cap to bound VideoFrame mem.
      while (decoder.decodeQueueSize + frameQueue.length > 64) {
        if (signal.aborted) throw new DOMException('aborted', 'AbortError');
        // eslint-disable-next-line no-await-in-loop
        await waitForDecoderDequeue(decoder, 32);
        if (frameQueue.length > 32) {
          // The encoder side will drain frameQueue; yield briefly.
          // eslint-disable-next-line no-await-in-loop
          await new Promise(r => setTimeout(r, 1));
        }
      }
      const p = selectedPackets[i];
      const chunk = new EncodedVideoChunk({
        type: p.type,
        timestamp: toOutTsUs(p.timestamp),
        duration: toOutDurUs(p.duration),
        data: p.data,
      });
      decoder.decode(chunk);
    }
    await decoder.flush();
  })();
  let feedError = null;
  feedDone.catch(e => { feedError = e; });

  const keyframeStride = Math.max(1, Math.round(resolvedFps));
  let slotsEncoded = 0;
  try {
    for (let i = 0; i < outputSamples.length; i++) {
      if (signal.aborted) throw new DOMException('aborted', 'AbortError');
      if (decoderError) throw decoderError;
      if (feedError && !(feedError.name === 'AbortError')) throw feedError;
      if (encoderError) throw encoderError;

      const s = outputSamples[i];
      const sampleMediaTime = s.timestamp;
      const targetTsUs = toOutTsUs(s.timestamp);

      // eslint-disable-next-line no-await-in-loop
      let frame = await waitForFrameAtOrBefore(frameQueue, targetTsUs, feedDone, signal);
      if (!frame) {
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, W, H);
      }

      const targetVirtMs = Math.max(0, sampleMediaTime * 1000);
      driveSimToVirtMs(sim, simState, targetVirtMs, log, sync, cppWireFrames);

      let rawState = sim.read();
      let m5State = rawState;
      if (presFilter && rawState) {
        const dtSec = s.duration;
        const smoothed = presFilter.apply(rawState.LateralG, rawState.VerticalG, dtSec);
        m5State = Object.freeze({
          ...rawState,
          LateralG:  Number.isFinite(smoothed.lateralG)  ? smoothed.lateralG  : rawState.LateralG,
          VerticalG: Number.isFinite(smoothed.verticalG) ? smoothed.verticalG : rawState.VerticalG,
        });
      }

      // Request SVG from main thread, rasterise here.
      const overlayW = Math.round(W * 0.22);
      const overlayH = Math.round(overlayW * 3 / 4);
      let overlayBitmap = null;
      try {
        // eslint-disable-next-line no-await-in-loop
        const svgString = await requestSvg(m5State, null);
        if (svgString) {
          // eslint-disable-next-line no-await-in-loop
          overlayBitmap = await svgStringToBitmap(svgString, overlayW, overlayH);
        }
      } catch (e) {
        // eslint-disable-next-line no-console
        console.warn('overlay raster failed for frame', i, e);
      }

      if (frame) compositeFrame(ctx, frame, overlayBitmap, W, H, videoInfo.rotationDeg);
      else if (overlayBitmap) compositeFrame(ctx, null, overlayBitmap, W, H, 0);

      if (frame) { try { frame.close(); } catch (_) {} }
      if (overlayBitmap) { try { overlayBitmap.close(); } catch (_) {} }

      // Encoder back-pressure via dequeue event.
      // eslint-disable-next-line no-await-in-loop
      await waitForEncoderDequeue(encoder, 4);
      if (encoderError) throw encoderError;

      const outTsUs = targetTsUs;
      const outDurUs = toOutDurUs(s.duration);
      const isKey = (i % keyframeStride) === 0 || i === 0;
      const outFrame = new VideoFrame(canvas, { timestamp: outTsUs, duration: outDurUs });
      encoder.encode(outFrame, { keyFrame: isKey });
      outFrame.close();

      slotsEncoded++;
      self.postMessage({
        type: MSG_PROGRESS,
        frame: slotsEncoded,
        totalFrames,
        encodedSec: (outTsUs + outDurUs) / 1_000_000,
        totalSec,
      });
    }

    try { await feedDone; }
    catch (e) {
      if (e?.name === 'AbortError') throw e;
      // eslint-disable-next-line no-console
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
    const buffer = output.target.buffer;
    if (!buffer) throw new Error('Output produced no buffer.');
    return new Blob([buffer], { type: 'video/mp4' });
  } finally {
    for (const f of frameQueue) { try { f.close(); } catch (_) {} }
    frameQueue.length = 0;
    try { decoder.state !== 'closed' && decoder.close(); } catch (_) {}
    try { encoder.state !== 'closed' && encoder.close(); } catch (_) {}
    try { sim.delete(); } catch (_) {}
  }
}

async function waitForFrameAtOrBefore(frameQueue, targetTsUs, feedDone, signal) {
  let feedFinished = false;
  feedDone.then(() => { feedFinished = true; }, () => { feedFinished = true; });
  while (true) {
    if (signal.aborted) throw new DOMException('aborted', 'AbortError');
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
      for (let i = 0; i < chosenIdx; i++) {
        try { frameQueue[i].close(); } catch (_) {}
      }
      frameQueue.splice(0, chosenIdx + 1);
      return chosen;
    }
    // eslint-disable-next-line no-await-in-loop
    await new Promise(r => setTimeout(r, 1));
  }
}

// ---------------------------------------------------------------------
// Overlay-only export — equivalent of exportOverlayOnly().
// ---------------------------------------------------------------------
const OVERLAY_MODE_IDS = Object.freeze({
  'energy': 0, 'attitude': 1, 'indexer': 2, 'decel': 3, 'historic-g': 4,
});
const OVERLAY_MODE_ORDER = Object.freeze([
  'indexer', 'attitude', 'energy', 'decel', 'historic-g',
]);

class OverlayModeEncoder {
  constructor({ modeId, encConfig, W, H, framerate }) {
    this.modeId = modeId;
    this.W = W; this.H = H;
    this.canvas = new OffscreenCanvas(W, H);
    this.ctx = this.canvas.getContext('2d');
    this.output = new Output({
      format: new Mp4OutputFormat({ fastStart: 'in-memory' }),
      target: new BufferTarget(),
    });
    this.videoSource = new EncodedVideoPacketSource('avc');
    this.output.addVideoTrack(this.videoSource, { rotation: 0, frameRate: framerate });
    this.error = null;
    let firstMetaSent = false;
    this.encoder = new VideoEncoder({
      output: (chunk, meta) => {
        try {
          const packet = EncodedPacket.fromEncodedChunk(chunk);
          const m = firstMetaSent ? undefined : meta;
          firstMetaSent = true;
          this.videoSource.add(packet, m).catch(e => { this.error = e; });
        } catch (e) { this.error = e; }
      },
      error: (e) => { this.error = e; },
    });
    this.encoder.configure(encConfig);
    this.framesEncoded = 0;
  }
  async start() { await this.output.start(); }
  async finalize() {
    if (this.error) throw this.error;
    await this.encoder.flush();
    if (this.error) throw this.error;
    this.encoder.close();
    await this.output.finalize();
    const buffer = this.output.target.buffer;
    if (!buffer) throw new Error(`Output for "${this.modeId}" produced no buffer.`);
    return new Blob([buffer], { type: 'video/mp4' });
  }
  closeOnError() {
    try { if (this.encoder.state !== 'closed') this.encoder.close(); } catch (_) {}
  }
}

async function runOverlayExport(params, signal) {
  const {
    clip, sync, log, cppWireFrames,
    modes = null,
    sourceVideoInfo = null,
    presentationTau = null,
    background = '#000',
    outputWidth = null,
    outputHeight = null,
    framerate = null,
    bitrate = null,
    durationSec = null,
  } = params;

  const requested = (modes && modes.length > 0) ? modes : OVERLAY_MODE_ORDER;
  const modeList = [];
  for (const m of requested) {
    if (!(m in OVERLAY_MODE_IDS)) {
      throw new Error(`exportOverlayOnly: unknown mode id "${m}".`);
    }
    modeList.push({ id: m, displayType: OVERLAY_MODE_IDS[m] });
  }
  if (modeList.length === 0) throw new Error('exportOverlayOnly: at least one mode required.');

  const srcFps = sourceVideoInfo?.frameRate;
  let W = outputWidth || M5_PANEL_W;
  let H = outputHeight || M5_PANEL_H;
  W = Math.max(2, Math.floor(W / 2) * 2);
  H = Math.max(2, Math.floor(H / 2) * 2);
  const fps = (Number.isFinite(framerate) && framerate > 0)
    ? framerate
    : (Number.isFinite(srcFps) && srcFps > 0 ? srcFps : DEFAULT_FRAMERATE);
  const muxerFps = Math.max(1, Math.round(fps));
  const resolvedBitrate = (Number.isFinite(bitrate) && bitrate > 0)
    ? bitrate : computeBitrate(W, H, fps);

  const clipDurSec = (Number.isFinite(durationSec) && durationSec > 0)
    ? durationSec : (clip.endMs - clip.startMs) / 1000;
  if (!Number.isFinite(clipDurSec) || clipDurSec <= 0) {
    throw new Error('exportOverlayOnly: clip has zero or invalid duration.');
  }

  const sim = await M5Sim.create();
  if (signal.aborted) { sim.delete(); throw new DOMException('aborted', 'AbortError'); }
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

  const encConfig = await pickOverlayEncoderConfig({
    width: W, height: H, bitrate: resolvedBitrate, framerate: fps,
  });

  const modeEncoders = new Map();
  try {
    for (const m of modeList) {
      const me = new OverlayModeEncoder({
        modeId: m.id, encConfig, W, H, framerate: muxerFps,
      });
      // eslint-disable-next-line no-await-in-loop
      await me.start();
      modeEncoders.set(m.id, me);
    }

    const totalFrames = Math.max(1, Math.round(clipDurSec * fps));
    const dtUs = Math.round(1_000_000 / fps);
    const dtSec = 1 / fps;
    const keyframeStride = Math.max(1, Math.round(fps));

    simState.lastVirtMs = 0;
    simState.lastBoundaryMs = 0;
    const syntheticSync = {
      logTakeoffMs: clip.startMs,
      videoTakeoffSec: 0,
    };

    for (let f = 0; f < totalFrames; f++) {
      if (signal.aborted) throw new DOMException('aborted', 'AbortError');
      for (const enc of modeEncoders.values()) {
        if (enc.error) throw enc.error;
      }

      const virtMs = (f / fps) * 1000;
      const tsUs = f * dtUs;
      const isKey = (f % keyframeStride) === 0;

      driveSimToVirtMs(sim, simState, virtMs, log, syntheticSync, cppWireFrames);
      let rawState = sim.read();
      let m5State = rawState;
      if (presFilter && rawState) {
        const smoothed = presFilter.apply(rawState.LateralG, rawState.VerticalG, dtSec);
        m5State = Object.freeze({
          ...rawState,
          LateralG:  Number.isFinite(smoothed.lateralG)  ? smoothed.lateralG  : rawState.LateralG,
          VerticalG: Number.isFinite(smoothed.verticalG) ? smoothed.verticalG : rawState.VerticalG,
        });
      }

      for (const m of modeList) {
        if (signal.aborted) throw new DOMException('aborted', 'AbortError');
        const enc = modeEncoders.get(m.id);
        if (enc.error) throw enc.error;

        // eslint-disable-next-line no-await-in-loop
        await waitForEncoderDequeue(enc.encoder, 4);

        let overlayBitmap = null;
        try {
          // eslint-disable-next-line no-await-in-loop
          const svgString = await requestSvg(m5State, m.displayType);
          if (svgString) {
            // eslint-disable-next-line no-await-in-loop
            overlayBitmap = await svgStringToBitmap(svgString, W, H);
          }
        } catch (e) {
          // eslint-disable-next-line no-console
          console.warn(`overlay raster failed for ${m.id} frame ${f}:`, e);
        }

        compositeOverlayNative(enc.ctx, overlayBitmap, background, W, H);
        if (overlayBitmap) { try { overlayBitmap.close(); } catch (_) {} }
        const outFrame = new VideoFrame(enc.canvas, { timestamp: tsUs, duration: dtUs });
        enc.encoder.encode(outFrame, { keyFrame: isKey });
        outFrame.close();
        enc.framesEncoded++;

        self.postMessage({
          type: MSG_PROGRESS,
          mode: m.id,
          frame: f + 1,
          totalFrames,
          encodedSec: (tsUs + dtUs) / 1_000_000,
          totalSec: clipDurSec,
        });
      }
    }

    const blobs = new Map();
    for (const [id, enc] of modeEncoders) {
      // eslint-disable-next-line no-await-in-loop
      const blob = await enc.finalize();
      blobs.set(id, blob);
    }
    return blobs;
  } finally {
    for (const enc of modeEncoders.values()) enc.closeOnError();
    try { sim.delete(); } catch (_) {}
  }
}

// ---------------------------------------------------------------------
// Worker message dispatch.
// ---------------------------------------------------------------------
let _currentAbort = null;

self.addEventListener('message', async (ev) => {
  const msg = ev.data;
  if (!msg || !msg.type) return;

  if (msg.type === MSG_SVG_FRAME) {
    handleSvgFrame(msg);
    return;
  }
  if (msg.type === MSG_CANCEL) {
    if (_currentAbort) _currentAbort.abort();
    return;
  }
  if (msg.type === MSG_EXPORT_CLIP) {
    _currentAbort = new AbortController();
    try {
      const blob = await runCompositeExport(msg.params, _currentAbort.signal);
      // Convert to ArrayBuffer for cheap transfer (a copy of the
      // Blob's bytes does NOT happen — Blob → arrayBuffer() returns
      // a backing buffer the worker already owns).
      const buf = await blob.arrayBuffer();
      self.postMessage({ type: MSG_DONE, buffer: buf, mime: 'video/mp4' }, [buf]);
    } catch (e) {
      if (e?.name === 'AbortError') {
        self.postMessage({ type: MSG_ERROR, error: 'AbortError', aborted: true });
      } else {
        self.postMessage({ type: MSG_ERROR, error: e?.message || String(e) });
      }
      rejectAllPendingSvg(e);
    } finally {
      _currentAbort = null;
    }
    return;
  }
  if (msg.type === MSG_EXPORT_OVERLAY) {
    _currentAbort = new AbortController();
    try {
      const blobs = await runOverlayExport(msg.params, _currentAbort.signal);
      // Convert each blob to ArrayBuffer + transfer.
      const entries = [];
      const transferList = [];
      for (const [id, blob] of blobs) {
        // eslint-disable-next-line no-await-in-loop
        const buf = await blob.arrayBuffer();
        entries.push([id, buf]);
        transferList.push(buf);
      }
      self.postMessage({ type: MSG_DONE, entries, mime: 'video/mp4' }, transferList);
    } catch (e) {
      if (e?.name === 'AbortError') {
        self.postMessage({ type: MSG_ERROR, error: 'AbortError', aborted: true });
      } else {
        self.postMessage({ type: MSG_ERROR, error: e?.message || String(e) });
      }
      rejectAllPendingSvg(e);
    } finally {
      _currentAbort = null;
    }
    return;
  }
});
