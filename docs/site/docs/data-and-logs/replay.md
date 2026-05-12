---
title: Video Replay Tool
description: Overlay a synced OnSpeed indicator onto cockpit video from your SD log
---

# Video Replay Tool

Drop your cockpit video, SD-card log, and OnSpeed config to play back what
the M5 indicator showed during the flight. The indicator is the actual
OnSpeed M5 display firmware compiled to WebAssembly — what you see is what
the panel showed in the cockpit.

!!! note "Session persistence"
    Sync anchors and clip lists persist across reloads, keyed by the
    log file's content (a SHA-256 prefix of the first 10 KB). Re-pick
    the same log on reload and the takeoff anchor + clip times come
    back automatically. The browser doesn't let pages auto-load files,
    so you still have to re-pick the video, log, and config each
    session — a banner reminds you which files belonged to the
    previous session.

!!! note "Browser support"
    Chrome and Firefox tested. Safari may have issues with large WASM modules.
    All processing happens in your browser; no files are uploaded anywhere.

<!-- Mount point for the replay app. The replay.html template loads the
     JS/CSS and renders into this div. -->
<div id="replay-app"></div>

<link rel="stylesheet" href="replay.css" />
<script src="replay-bundle.js"></script>
