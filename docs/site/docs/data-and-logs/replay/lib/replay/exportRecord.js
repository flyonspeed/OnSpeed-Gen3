// Export a synced video + indexer overlay to a downloadable WebM.
//
// We compose by drawing each video frame to an offscreen canvas at
// the chosen export resolution, rasterizing the current overlay SVG
// over the video pixels, and feeding the canvas through a
// MediaRecorder. Output is WebM (Chrome/Firefox encode it natively;
// YouTube accepts WebM uploads directly so the pilot doesn't need
// to re-encode).
//
// Why WebM and not MP4: MediaRecorder in browsers can write
// WebM/VP9 reliably across Chrome/Edge/Firefox. MP4/H.264 is
// supported in Safari only and is blocked by policy in Chrome (no
// AVC encode). For Vac's workflow a WebM upload to YouTube works
// fine; if a true MP4 is needed later, a single ffmpeg-wasm pass
// can transcode the output, or the publish-quality Remotion path
// (Phase 5) renders to MP4 server-side.
//
// Frame timing: we drive the render off `requestVideoFrameCallback`
// when available (Chrome, Edge) so each captured frame matches a
// real video frame. The fallback is RAF-paced, which can dropped
// frames during 4K decode but is fine for 1080p output.
//
// Caller passes a `renderOverlay(ctx, scale)` function that
// rasterizes the current overlay onto the canvas. We give the
// caller the scaled canvas dimensions so they can position the
// overlay correctly.

const DEFAULT_BITRATE = 12_000_000;   // 12 Mbps; YouTube's recommended 1080p upload bitrate is ~8-12

// Pick a MediaRecorder MIME the current browser supports.
function pickMime() {
  const candidates = [
    'video/webm;codecs=vp9',
    'video/webm;codecs=vp8',
    'video/webm',
  ];
  for (const m of candidates) {
    if (typeof MediaRecorder !== 'undefined' && MediaRecorder.isTypeSupported(m)) {
      return m;
    }
  }
  return 'video/webm';
}

// Rasterize an SVG element to an Image via a data URL. Asynchronous;
// returns a promise resolving to a loaded HTMLImageElement.
//
// XMLSerializer + data URL is the canonical, taint-safe path. The
// resulting image can be drawImage()d onto a canvas and the canvas
// stays "clean" (no SecurityError on captureStream).
function svgToImage(svgEl) {
  const xml = new XMLSerializer().serializeToString(svgEl);
  const blob = new Blob([xml], { type: 'image/svg+xml;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload  = () => { URL.revokeObjectURL(url); resolve(img); };
    img.onerror = (e) => { URL.revokeObjectURL(url); reject(e); };
    img.src = url;
  });
}

// Export config:
//   {
//     videoEl:        HTMLVideoElement (the source <video>),
//     getOverlaySvg:  () -> SVGElement | null  (current frame's
//                     mode SVG; called per video frame so the
//                     overlay state is current),
//     startSec:       optional float (seconds). If set, video seeks
//                     here before recording starts. Defaults to
//                     the videoEl's current playhead.
//     endSec:         optional float (seconds). If set, recording
//                     stops when video crosses this time. Defaults
//                     to videoEl.duration (i.e. record to the end).
//     overlayInset:   { right, bottom, widthPct } — px from the
//                     edges in canvas-space, percentage of canvas
//                     width for the overlay box.
//     outputWidth:    int (e.g. 1920); output canvas width.
//                     Height derived from video aspect ratio.
//     bitrate:        int bits/sec (default 12 Mbps)
//     onProgress:     ({ encodedSec, videoSec, startSec, endSec }) => void
//   }
//
// Returns: { stop() -> Promise<Blob>, finished: Promise<Blob> }.
export async function exportOverlayedVideo(opts) {
  const {
    videoEl, getOverlaySvg,
    startSec = null,
    endSec   = null,
    overlayInset = { right: 24, bottom: 110, widthPct: 0.22 },
    outputWidth = 1920,
    bitrate = DEFAULT_BITRATE,
    onProgress = null,
  } = opts;

  if (!videoEl) throw new Error('exportOverlayedVideo: no video element');
  if (!videoEl.videoWidth || !videoEl.videoHeight) {
    throw new Error('exportOverlayedVideo: video metadata not loaded yet');
  }

  // Compute output resolution preserving aspect.
  const aspect = videoEl.videoHeight / videoEl.videoWidth;
  const W = outputWidth;
  const H = Math.round(W * aspect);

  // Offscreen canvas. We use a real DOM canvas (not OffscreenCanvas)
  // because MediaRecorder + captureStream is more interoperable on
  // a regular canvas across the browsers we care about.
  const canvas = document.createElement('canvas');
  canvas.width  = W;
  canvas.height = H;
  const ctx = canvas.getContext('2d');

  const stream = canvas.captureStream(0);   // 0 = manual frame advance
  const track  = stream.getVideoTracks()[0];

  // Add the audio track from the video element. The video has its
  // own audio that we want preserved in the export.
  let audioStream = null;
  if (videoEl.captureStream) {
    try {
      audioStream = videoEl.captureStream();
      const audioTrack = audioStream.getAudioTracks()[0];
      if (audioTrack) stream.addTrack(audioTrack);
    } catch (e) {
      console.warn('exportOverlayedVideo: audio capture failed:', e);
    }
  }

  const mimeType = pickMime();
  const recorder = new MediaRecorder(stream, {
    mimeType,
    videoBitsPerSecond: bitrate,
  });

  const chunks = [];
  recorder.ondataavailable = (e) => {
    if (e.data && e.data.size > 0) chunks.push(e.data);
  };

  // Async frame driver. Plays the video at native speed, captures
  // a canvas frame each time the video paints. Stop when we hit
  // the end-of-window (or duration) or the caller calls stop().
  let stopped = false;
  const effectiveEnd = Number.isFinite(endSec) ? endSec : videoEl.duration;
  let lastVideoTime = videoEl.currentTime;

  function rasterizeOverlay(scaleX, scaleY) {
    const svg = getOverlaySvg && getOverlaySvg();
    if (!svg) return Promise.resolve(null);
    return svgToImage(svg);
  }

  async function captureFrame() {
    // 1. video pixels
    ctx.drawImage(videoEl, 0, 0, W, H);

    // 2. overlay
    try {
      const img = await rasterizeOverlay(W / videoEl.videoWidth, H / videoEl.videoHeight);
      if (img) {
        const ow = Math.round(W * overlayInset.widthPct);
        const oh = Math.round(ow * 3 / 4);    // 4:3 indexer aspect
        const ox = W - overlayInset.right * (W / videoEl.clientWidth || 1) - ow;
        const oy = H - overlayInset.bottom * (H / videoEl.clientHeight || 1) - oh;
        ctx.drawImage(img, ox, oy, ow, oh);
      }
    } catch (e) {
      console.warn('overlay raster failed:', e);
    }

    // 3. push the frame to the MediaRecorder track
    if (track.requestFrame) track.requestFrame();
  }

  function tick(_now, meta) {
    if (stopped) return;
    captureFrame().then(() => {
      if (onProgress) {
        onProgress({
          encodedSec: videoEl.currentTime - lastVideoTime,
          videoSec:   videoEl.currentTime,
          startSec:   Number.isFinite(startSec) ? startSec : 0,
          endSec:     effectiveEnd,
        });
      }
      // Stop at end of clip window (or end of video, or natural end).
      if (videoEl.currentTime >= effectiveEnd - 0.02 ||
          videoEl.currentTime >= videoEl.duration - 0.05 ||
          videoEl.ended) {
        finish();
        return;
      }
      if (videoEl.requestVideoFrameCallback) {
        videoEl.requestVideoFrameCallback(tick);
      } else {
        requestAnimationFrame(() => tick(performance.now()));
      }
    });
  }

  let finishResolve, finishReject;
  const finished = new Promise((resolve, reject) => {
    finishResolve = resolve;
    finishReject = reject;
  });

  function finish() {
    if (stopped) return;
    stopped = true;
    try {
      recorder.stop();
      videoEl.pause();
    } catch (e) { /* ignore */ }
  }

  recorder.onstop = () => {
    const blob = new Blob(chunks, { type: mimeType });
    finishResolve(blob);
  };
  recorder.onerror = (e) => finishReject(e?.error || e);

  // Seek to the clip start (if provided) and wait for the seek
  // to settle before kicking the recorder. Without the wait,
  // MediaRecorder captures the pre-seek frame as its first frame.
  if (Number.isFinite(startSec) && Math.abs(videoEl.currentTime - startSec) > 0.05) {
    await new Promise(resolve => {
      const onSeeked = () => {
        videoEl.removeEventListener('seeked', onSeeked);
        resolve();
      };
      videoEl.addEventListener('seeked', onSeeked, { once: true });
      videoEl.currentTime = startSec;
    });
  }
  lastVideoTime = videoEl.currentTime;

  recorder.start(1000);   // 1 s slice for ondataavailable
  if (videoEl.requestVideoFrameCallback) {
    videoEl.requestVideoFrameCallback(tick);
  } else {
    requestAnimationFrame(() => tick(performance.now()));
  }
  try {
    await videoEl.play();
  } catch (e) {
    finish();
    throw new Error('Could not start video playback: ' + (e?.message || e));
  }

  return {
    stop: () => { finish(); return finished; },
    finished,
  };
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
