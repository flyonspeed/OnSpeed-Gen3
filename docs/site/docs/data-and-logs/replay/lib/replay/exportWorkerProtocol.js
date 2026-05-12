// exportWorkerProtocol.js — message-type constants shared between
// mp4Export.js (main thread) and exportWorker.js.
//
// The Worker runs the full Mediabunny demux + VideoDecoder + composite
// + VideoEncoder + Muxer pipeline. SVG rendering CANNOT move into the
// Worker: Preact + the M5_MODES catalog mounts into a detached DOM
// node on the page, and Workers have no DOM. To bridge that, the
// Worker requests one SVG-string per frame; the main thread renders
// the SVG element via the page-supplied callback, serializes it, and
// posts the XML string back. The Worker rasterises the string with
// createImageBitmap and composites.
//
// All messages carry a `type` discriminator. `frameId` is a monotonic
// counter used to match svg-request ↔ svg-frame round trips.

// Main → Worker.
export const MSG_EXPORT_CLIP    = 'export-clip';      // start composite export
export const MSG_EXPORT_OVERLAY = 'export-overlay';   // start overlay-only export
export const MSG_SVG_FRAME      = 'svg-frame';        // svg string requested by worker
export const MSG_CANCEL         = 'cancel';           // abort current export

// Worker → Main.
export const MSG_SVG_REQUEST    = 'svg-request';      // worker asking for next SVG
export const MSG_PROGRESS       = 'progress';         // export progress
export const MSG_MODE_BLOB      = 'mode-blob';        // overlay-only: per-mode result
export const MSG_DONE           = 'done';             // final blob (composite) or
                                                      //   final batch (overlay-only)
export const MSG_ERROR          = 'error';            // export failed

// Sentinel `frameId` for the synthetic "no SVG element this frame"
// reply. Worker treats svg-frame with svgString === null as "nothing
// to composite" and falls back to black + no overlay.
export const SVG_FRAME_EMPTY = null;
