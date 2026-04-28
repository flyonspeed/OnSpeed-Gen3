// Indexer tab — embeds the M5 sim WebAssembly module and pipes the
// firmware's binary `#1` display-serial frames into it via a WebSocket
// connection on port 81.  A pilot can prop a tablet on the yoke during
// flight and see the same indexer the M5 hardware draws.
//
// Layout: full-viewport pixelated canvas centered on screen, with three
// touch-target buttons mirroring the M5 hardware's BtnA/B/C (DIM, MODE,
// BRIGHT).  Buttons synthesize the SDL keyboard events emscripten's
// HTML5 backend translates into the lgfx::Panel_sdl GPIO-key mapping
// the firmware already binds.  No firmware change needed.
//
// The status pill (top-right) mirrors WebSocket connection state.  On
// disconnect, the firmware's NO DATA overlay (300 ms freshness gate)
// fires automatically — no separate fallback needed here.
//
// Wake lock keeps the tablet's screen on.  Layout works in either
// orientation — the canvas centers in the available space above the
// button row.
//
// The nav row at the top mirrors the rest of the web UI's menu so the
// pilot can navigate back to LiveView, config, etc. from a phone or
// tablet.  We pull the nav <ul> inline and link /css/main.css for its
// styling; we skip the logo block (~94 px tall) to keep the indexer
// canvas as large as possible on portrait phones.

const char htmlIndexer[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<meta charset="utf-8"/>
<title>OnSpeed Indexer</title>
<link rel="stylesheet" href="/css/main.css">
<style>
  html, body {
    margin: 0;
    padding: 0;
    height: 100%;
    background: #000;
    color: #ddd;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    overflow: hidden;
    overscroll-behavior: none;
    touch-action: manipulation;
  }
  /* Nav row — pinned to top, height matches the .nav-row height below. */
  .nav-row {
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 46px;
    z-index: 50;
  }
  /* Status pill — top-right corner, below the nav row. */
  #status {
    position: fixed; top: 54px; right: 12px;
    font-family: monospace; font-size: 12px;
    color: #ddd;
    background: rgba(0,0,0,0.6);
    border: 1px solid #555;
    border-radius: 12px;
    padding: 4px 10px;
    z-index: 100;
  }
  #status.ok    { border-color: #1d6; color: #afc; }
  #status.stale { border-color: #d92; color: #fc6; }
  #status.bad   { border-color: #d33; color: #fcc; }
  /* Canvas — fills viewport between the nav row and the button row,
     integer-multiple scaling preserves crisp pixel edges. JS sets
     width/height in pixels to match 320:240 aspect on whatever's
     available. */
  #stage {
    position: absolute;
    top: 46px; left: 0; right: 0; bottom: 84px;
    display: flex; align-items: center; justify-content: center;
  }
  #canvas {
    background: #000;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    outline: none;
    display: block;
  }
  /* Button row — bottom of viewport, three big touch targets. */
  .controls {
    position: absolute;
    left: 0; right: 0; bottom: 0;
    height: 84px;
    display: flex; gap: 8px;
    padding: 8px;
    background: #111;
    border-top: 1px solid #333;
    box-sizing: border-box;
  }
  .controls button {
    flex: 1;
    background: #2a2a2a;
    color: #eee;
    border: 1px solid #555;
    border-radius: 8px;
    font-size: 18px;
    font-weight: bold;
    letter-spacing: 1px;
    -webkit-tap-highlight-color: transparent;
    user-select: none;
    cursor: pointer;
  }
  .controls button:active { background: #4a9eff; color: #000; }
</style>
</head>
<body>

<!-- Nav row — same items and styling as szHtmlHeader's menu so the
     pilot can return to LiveView, config pages, etc.  Logo and version
     readout are intentionally omitted; this view is meant to maximize
     the indexer canvas on a phone in portrait. -->
<div class="nav-row">
<ul>
    <li><a href="/">Home</a></li>
    <li class="dropdown">
        <a href="javascript:void(0)" class="dropbtn">Tools</a>
        <div class="dropdown-content">
            <a href="logs">Log Files</a>
            <a href="format">Format SD Card</a>
            <a href="upgrade">Firmware Upgrade</a>
            <a href="reboot">Reboot System</a>
        </div>
    </li>
    <li class="dropdown">
        <a href="javascript:void(0)" class="dropbtn">Settings</a>
        <div class="dropdown-content">
            <a href="aoaconfig">System Configuration</a>
            <a href="sensorconfig">Sensor Calibration</a>
            <a href="calwiz">AOA Calibration Wizard</a>
        </div>
    </li>
    <li><a href="live">LiveView</a></li>
    <li><a href="indexer">Indexer</a></li>
</ul>
</div>

<div id="status">connecting&hellip;</div>

<div id="stage">
  <canvas id="canvas" width="320" height="240" tabindex="0"></canvas>
</div>

<div class="controls">
  <button id="btnDim"    aria-label="Dim brightness">DIM</button>
  <button id="btnMode"   aria-label="Cycle display mode">MODE</button>
  <button id="btnBright" aria-label="Brightness up">BRIGHT</button>
</div>

<script>
(function() {
  var statusEl = document.getElementById('status');
  var canvas   = document.getElementById('canvas');

  // ----- Canvas sizing — fit viewport, integer multiples preferred ----
  // Capped at 4× (1280×960) so the indexer doesn't swallow a desktop
  // monitor; tablets and phones almost always land below the cap.
  var MAX_SCALE = 4;
  function resizeCanvas() {
    var stage = document.getElementById('stage');
    var avail = stage.getBoundingClientRect();
    // 320:240 = 4:3.  Pick an integer scale that fits both dimensions.
    var fit = Math.min(avail.width / 320, avail.height / 240);
    var scale = Math.max(1, Math.floor(fit));
    // If integer scale leaves a lot of slack, allow a non-integer scale
    // so the canvas fills more of the viewport.  Slight blur is OK.
    if (fit - scale > 0.4) scale = fit;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    canvas.style.width  = (320 * scale) + 'px';
    canvas.style.height = (240 * scale) + 'px';
  }
  window.addEventListener('resize', resizeCanvas);
  setTimeout(resizeCanvas, 0);

  // ----- Status pill ---------------------------------------------------
  function setStatus(text, cls) {
    statusEl.textContent = text;
    statusEl.className = cls || '';
  }

  // ----- Wake lock — keep screen alive in flight -----------------------
  var wakeLock = null;
  function requestWakeLock() {
    if (!('wakeLock' in navigator)) return;
    navigator.wakeLock.request('screen').then(function(lock) {
      wakeLock = lock;
      lock.addEventListener('release', function() { wakeLock = null; });
    }).catch(function() { /* ignore — best-effort */ });
  }
  document.addEventListener('visibilitychange', function() {
    if (document.visibilityState === 'visible' && !wakeLock) requestWakeLock();
  });
  requestWakeLock();

  // ----- Button -> SDL keyboard event bridge ---------------------------
  // emscripten's SDL2 backend listens for keydown events on the canvas
  // (or document); lgfx::Panel_sdl::setup() binds Left=GPIO39 (BtnA),
  // Down=GPIO38 (BtnB), Right=GPIO37 (BtnC).  Synthesizing these key
  // events drives M5.BtnA/B/C.wasPressed() in the firmware unchanged.
  function dispatchKey(keyCode, code) {
    canvas.focus();
    canvas.dispatchEvent(new KeyboardEvent('keydown', {
      keyCode: keyCode, which: keyCode, code: code, key: code, bubbles: true
    }));
    setTimeout(function() {
      canvas.dispatchEvent(new KeyboardEvent('keyup', {
        keyCode: keyCode, which: keyCode, code: code, key: code, bubbles: true
      }));
    }, 30);
  }
  document.getElementById('btnDim')   .addEventListener('click', function() { dispatchKey(37, 'ArrowLeft');  });
  document.getElementById('btnMode')  .addEventListener('click', function() { dispatchKey(40, 'ArrowDown');  });
  document.getElementById('btnBright').addEventListener('click', function() { dispatchKey(39, 'ArrowRight'); });

  // ----- Module bootstrap ----------------------------------------------
  // Defined globally so /sim/index.js (emscripten loader) picks it up.
  window.Module = {
    canvas: canvas,
    print:    function(t) { console.log('[wasm]', t); },
    printErr: function(t) { console.warn('[wasm]', t); },
    setStatus: function(t) { /* swallow internal emscripten status */ },
    onRuntimeInitialized: function() { startWebSocket(); }
  };

  // ----- WebSocket bridge — pipe binary #1 frames into _inject_serial_byte
  var ws = null;
  var reconnectDelay = 250;          // ms; exponential backoff capped at 8s
  var lastFrameMs = 0;
  function startWebSocket() {
    var inject;
    try {
      inject = Module.cwrap('inject_serial_byte', null, ['number']);
    } catch (e) {
      setStatus('wasm bridge missing', 'bad');
      return;
    }

    function connect() {
      var url = 'ws://' + window.location.hostname + ':81/';
      setStatus('connecting…', '');
      ws = new WebSocket(url);
      ws.binaryType = 'arraybuffer';
      ws.onopen = function() {
        setStatus('live', 'ok');
        reconnectDelay = 250;
      };
      ws.onmessage = function(ev) {
        // Ignore the existing JSON text broadcast — we only consume the
        // binary #1 frames added by BroadcastDisplayFrame.
        if (typeof ev.data === 'string') return;
        var bytes = new Uint8Array(ev.data);
        for (var i = 0; i < bytes.length; i++) inject(bytes[i]);
        lastFrameMs = Date.now();
      };
      ws.onclose = function() {
        setStatus('reconnecting…', 'bad');
        setTimeout(connect, reconnectDelay);
        reconnectDelay = Math.min(reconnectDelay * 2, 8000);
      };
      ws.onerror = function() {
        try { ws.close(); } catch (e) {}
      };
    }

    // Periodic stale-data check.  The firmware draws its own NO DATA
    // overlay after 300 ms of frame silence; this just colors the pill.
    setInterval(function() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        var age = Date.now() - lastFrameMs;
        if (lastFrameMs && age > 300) setStatus('stale ' + age + 'ms', 'stale');
        else if (lastFrameMs)         setStatus('live',                 'ok');
      }
    }, 250);

    connect();
  }
})();
</script>

<script async src="/sim/index.js"></script>

</body>
</html>
)=====";
