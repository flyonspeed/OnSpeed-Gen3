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
    Chrome and Firefox tested. Safari may have issues with large WASM modules.
    All processing happens in your browser; no files are uploaded anywhere.

!!! tip "Exporting clips to MP4"
    Once your video and log are loaded and synced, you can mark clip
    ranges and export each one as an MP4 with the M5 indicator burned
    into the corner. Drop a clip from the playhead with "+ 30 s clip"
    or "+ 60 s clip", or click "Mark clip in" → scrub → "Mark clip out"
    for a custom range. Each clip's row has Scrub / Set in here / Set
    out here buttons for tightening the window, an editable label, and
    an Export MP4 button.

    **MP4 export requires Chrome or Edge desktop.** It uses the WebCodecs
    `VideoEncoder` API, which is currently incomplete in Safari and
    missing in Firefox. The Export MP4 button is grayed out on those
    browsers. A legacy WebM export (using `MediaRecorder`) is still
    available for the full video range — clip-level WebM export was
    removed when the MP4 path landed.

    Audio is currently stripped from MP4 exports (visual only). Adding
    AAC audio is a planned follow-up.

<!-- Mount point for the replay app. The replay.html template loads the
     JS/CSS and renders into this div. -->
<div id="replay-app"></div>

<link rel="stylesheet" href="replay.css" />
<script type="module" src="replay-entry.js"></script>
