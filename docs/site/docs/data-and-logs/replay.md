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

<!-- Mount point for the replay app. The replay.html template loads the
     JS/CSS and renders into this div. -->
<div id="replay-app"></div>

<link rel="stylesheet" href="replay.css" />
<script type="module" src="replay-entry.js"></script>
