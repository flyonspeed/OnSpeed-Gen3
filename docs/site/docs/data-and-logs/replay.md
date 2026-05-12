---
title: Video Replay Tool
description: Overlay a synced OnSpeed indicator onto cockpit video from your SD log
---

# Video Replay Tool

Drop your cockpit video, SD-card log, and OnSpeed config to play back what
the M5 indicator showed during the flight. The indicator is the actual
OnSpeed M5 display firmware compiled to WebAssembly — what you see is what
the panel showed in the cockpit.

!!! note "Browser support"
    Use Chrome or Edge desktop. The export pipeline uses WebCodecs, which
    Firefox does not yet implement. Safari can run the live preview but
    may struggle with very large WASM modules. All processing happens in
    your browser — no files are uploaded anywhere.

## Picking a mode

Five M5 panels render from the same flight data: Energy, Attitude,
Indexer, Decel, and Historic G. Click a mode button under the video
to switch the live overlay. The choice is remembered across reloads.

## Sync

Pick a video and a log; the tool auto-detects takeoff in the log and
annotates the timeline. Click "Mark video anchor" at takeoff (or the
matching maneuver) and the two clocks lock. Anchors and clip lists are
saved per-log: re-pick the same log file later and your previous sync
and clips come back.

If sync drifts mid-flight (video edits chopped a few seconds out),
click "Pause indexer" at a recognizable moment, scrub the video to the
correct frame, and click "Attach here" to re-anchor.

## Exporting clips

Mark a window — "+ 30 s clip" / "+ 60 s clip" from the playhead, or
"Mark clip in" → scrub → "Mark clip out" for a custom range. Each
clip's row carries Scrub / Set in / Set out buttons for tightening
the window plus an editable label.

There are two export paths.

### Composite MP4 (video + overlay burned in)

Click **Export MP4**. Output matches the source: same codec
(H.264 / HEVC), same dimensions, same framerate, same nominal bitrate.
Audio is AAC-passthrough — the source audio packets go through the
muxer unchanged, no re-encode. Source rotation is honored, so an
upside-down GoPro clip exports upright.

### Overlay-only MP4s (just the M5 panel)

Click **Overlays · NLE**. The tool renders one transparent-ish MP4 per
M5 mode you tick (Energy, Attitude, Indexer, Decel, Historic G), each
sized either 320×240 native or 20% / 30% / 50% of the source width.
Drop the resulting files onto a secondary track in iMovie / Premiere /
DaVinci on top of your existing footage. Overlay-only exports are
silent by design.

## Output naming

Composite MP4s are named after the clip label. Overlay-only exports
get one MP4 per mode, suffixed by mode name. Both drop into your
browser's default download folder.

<!-- Mount point for the replay app. The replay.html template loads the
     JS/CSS and renders into this div. -->
<div id="replay-app"></div>

<link rel="stylesheet" href="replay.css" />
<script type="module" src="replay-entry.js"></script>
