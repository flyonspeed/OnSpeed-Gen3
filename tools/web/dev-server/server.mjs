// OnSpeed dev server.  Zero npm deps.  Three modes:
//
//   --mock                 (default if no flag given that picks another mode)
//     Serves the firmware's actual PROGMEM bundle — runs
//     `scripts/build_web_bundle.py`, parses the generated
//     `static_app_js.h`/`static_app_css.h`/`html_stubs.h`, and serves
//     them at the SAME URLs the firmware uses
//     (`/static/app-<etag>.{js,css}` plus per-page stubs).  Auto-rebuilds
//     when source files change — the next page reload picks up the new
//     bundle.  Adds a synthesized `<meta name="onspeed-mode" content="mock">`
//     on every HTML page.  /api/* is answered from dev-server/mocks/*.json.
//     A WebSocket server on the same port at path /ws replays a recorded
//     NDJSON file from dev-server/replay/ (default cruise.ndjson) or a
//     synthetic scenario from lib/scenarios.js when --scenario <name> is
//     passed.
//
//   --proxy <url>
//     Pages served as in mock mode (same bundle), but /api/* is forwarded
//     to <url>.  WebSocket is NOT proxied — JS connects directly to the
//     device via the meta tag's URL.  Inserts
//     <meta name="onspeed-mode" content="proxy <url>"> and a companion
//     <meta name="onspeed-ws" content="ws://<host>:81">.
//
//   default (no mode flag)
//     Static-only.  Pages still serve the bundle.  /api/* returns 503 with
//     a "no backend configured" body.  WebSocket is unreachable.  Useful
//     for offline UI iteration that doesn't depend on data.
//
// Why the bundle and not raw source: serving raw source files exposes a
// different URL surface (browser ES-module imports vs single PROGMEM blob)
// than the firmware does.  Path-resolution bugs that would break in the
// box but work via raw-source serving can hide indefinitely.  Serving the
// bundle catches them on first reload.
//
// Usage:
//   node tools/web/dev-server/server.mjs --mock
//   node tools/web/dev-server/server.mjs --mock --replay tools/web/dev-server/replay/cruise.ndjson
//   node tools/web/dev-server/server.mjs --mock --scenario approach
//   node tools/web/dev-server/server.mjs --proxy http://192.168.0.1
//   node tools/web/dev-server/server.mjs --port 9001 --mock

import http from 'node:http';
import { URL } from 'node:url';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const REPO_ROOT  = path.resolve(__dirname, '..', '..', '..');
const WEB_DIR    = path.resolve(__dirname, '..');
const LIB_DIR    = path.join(WEB_DIR, 'lib');
const UI_CORE_DIR = path.join(REPO_ROOT, 'packages', 'ui-core');
const PUBLIC_DIR = path.join(WEB_DIR, 'public');
const LEGACY_DIR = path.join(WEB_DIR, 'legacy-pages');
const MOCKS_DIR  = path.join(__dirname, 'mocks');
const REPLAY_DIR = path.join(__dirname, 'replay');
// WASM build output: software/Libraries/onspeed_core/wasm/dist/
// Served at /static/onspeed_core/ so wasm_core.js can import it.
const WASM_DIST_DIR = path.resolve(
    REPO_ROOT, 'software', 'Libraries', 'onspeed_core', 'wasm', 'dist');

// Generated PROGMEM headers (produced by scripts/build_web_bundle.py).
// The dev server parses these and serves the same byte content the
// firmware would, at the same URLs.
const BUNDLER         = path.join(REPO_ROOT, 'scripts', 'build_web_bundle.py');
const GEN_DIR         = path.join(REPO_ROOT, 'software', 'OnSpeed-Gen3-ESP32', 'Web');
const HEADER_JS_H     = path.join(GEN_DIR, 'static_app_js.h');
const HEADER_CSS_H    = path.join(GEN_DIR, 'static_app_css.h');
const HEADER_STUBS_H  = path.join(GEN_DIR, 'html_stubs.h');

// Docs-site replay bundle (produced by `--target replay`). The dev
// server rebuilds this whenever the replay source tree changes so an
// MkDocs serve session running alongside picks up edits without a
// manual rebuild step.
const REPLAY_SRC_DIR  = path.join(REPO_ROOT, 'docs', 'site', 'docs',
                                   'data-and-logs', 'replay');
const REPLAY_BUNDLE   = path.join(REPLAY_SRC_DIR, 'replay-bundle.js');
const REPLAY_BUNDLE_URL = '/data-and-logs/replay/replay-bundle.js';

// ---------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------
function parseArgs(argv) {
  const args = {
    mode: 'static',     // 'mock' | 'proxy' | 'static'
    proxy: null,
    replay: null,
    scenario: null,
    port: 8080,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--mock') args.mode = 'mock';
    else if (a === '--proxy') {
      args.mode = 'proxy';
      args.proxy = argv[++i];
    }
    else if (a === '--replay')   args.replay   = argv[++i];
    else if (a === '--scenario') args.scenario = argv[++i];
    else if (a === '--port')     args.port     = parseInt(argv[++i], 10);
    else if (a === '--help' || a === '-h') {
      console.log(USAGE);
      process.exit(0);
    } else {
      console.error(`Unknown argument: ${a}`);
      console.error(USAGE);
      process.exit(2);
    }
  }
  if (args.mode === 'proxy' && !args.proxy) {
    console.error('--proxy requires a URL');
    process.exit(2);
  }
  return args;
}

const USAGE = `OnSpeed dev server

Usage:
  node server.mjs --mock [--replay <path>] [--scenario <name>]
  node server.mjs --proxy <url>
  node server.mjs                          (static-only fallback)

Options:
  --mock                 Mock backend.  /api/* from dev-server/mocks/.
                         WS replays NDJSON or a scenario.
  --replay <path>        NDJSON file for the WS replay loop.
                         Defaults to dev-server/replay/cruise.ndjson.
  --scenario <name>      Override --replay; drives the WS from a
                         synthetic scenario in lib/scenarios.js.
  --proxy <url>          Forward /api/* to a real device.
  --port <n>             Listen port (default 8080).
`;

// ---------------------------------------------------------------------
// MIME map
// ---------------------------------------------------------------------
const MIME = new Map([
  ['.html', 'text/html; charset=utf-8'],
  ['.js',   'application/javascript; charset=utf-8'],
  ['.mjs',  'application/javascript; charset=utf-8'],
  ['.css',  'text/css; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.svg',  'image/svg+xml'],
  ['.png',  'image/png'],
  ['.jpg',  'image/jpeg'],
  ['.ico',  'image/x-icon'],
  ['.txt',  'text/plain; charset=utf-8'],
  ['.map',  'application/json; charset=utf-8'],
  ['.wasm', 'application/wasm'],
]);

function mimeFor(filePath) {
  return MIME.get(path.extname(filePath).toLowerCase()) || 'application/octet-stream';
}

// ---------------------------------------------------------------------
// Bundle loader — runs scripts/build_web_bundle.py and parses the
// generated PROGMEM headers into a structure we can serve over HTTP.
//
// The dev server gives the browser exactly what the firmware would: a
// single gzipped JS blob, a single gzipped CSS blob, and per-page HTML
// stubs that reference them at /static/app-<etag>.{js,css}.
//
// Re-parses on every change to JS/CSS sources (watched via fs.watch
// elsewhere in this file).
// ---------------------------------------------------------------------

// Cached bundle artifacts. Each call to runBundlerAndLoad() refreshes
// these atomically.
let bundleState = {
  jsGz: Buffer.alloc(0),
  cssGz: Buffer.alloc(0),
  jsEtag: '',
  cssEtag: '',
  jsContentType: 'application/javascript',
  cssContentType: 'text/css',
  stubs: new Map(),  // page-id → raw HTML stub string
  loaded: false,
  loadError: null,
};

function runBundler() {
  const t0 = Date.now();
  const res = spawnSync('python3', [BUNDLER], {
    cwd: REPO_ROOT,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  const dtMs = Date.now() - t0;
  if (res.status !== 0) {
    const stderr = (res.stderr || '').trim();
    const stdout = (res.stdout || '').trim();
    throw new Error(
      `build_web_bundle.py exited with status ${res.status} (${dtMs} ms)\n` +
      `stdout: ${stdout}\nstderr: ${stderr}`);
  }
  return { dtMs, stdout: (res.stdout || '').trim() };
}

// Build the docs-site replay bundle. Standalone from runBundler() so a
// failure in one doesn't take down the other; the firmware UI is
// usable even when the replay tool has a parse error and vice versa.
function runReplayBundler() {
  const t0 = Date.now();
  const res = spawnSync('python3', [BUNDLER, '--target', 'replay'], {
    cwd: REPO_ROOT,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  const dtMs = Date.now() - t0;
  if (res.status !== 0) {
    const stderr = (res.stderr || '').trim();
    const stdout = (res.stdout || '').trim();
    console.error(
      `[dev-server] replay bundle FAILED (status ${res.status}, ${dtMs} ms)\n` +
      `stdout: ${stdout}\nstderr: ${stderr}`);
    return;
  }
  const out = (res.stdout || '').trim();
  console.log(
    `[dev-server] replay bundle reloaded (${dtMs} ms)` +
    (out ? `: ${out}` : ''));
}

// Parse `static_app_js.h` / `static_app_css.h`.  Each file has:
//   static const size_t static_app_<kind>_len = NNNNN;
//   static const char   static_app_<kind>_etag[] = "abc123";
//   static const char   static_app_<kind>_content_type[] = "...";
//   static const uint8_t static_app_<kind>[] PROGMEM = { 0x.., ... };
function parseStaticHeader(headerPath, kind) {
  const txt = fs.readFileSync(headerPath, 'utf8');
  const etagMatch = txt.match(
    new RegExp(`static_app_${kind}_etag\\[\\]\\s*=\\s*"([^"]+)"`));
  if (!etagMatch) {
    throw new Error(`parseStaticHeader: no etag in ${headerPath}`);
  }
  const ctMatch = txt.match(
    new RegExp(`static_app_${kind}_content_type\\[\\]\\s*=\\s*"([^"]+)"`));
  if (!ctMatch) {
    throw new Error(`parseStaticHeader: no content_type in ${headerPath}`);
  }
  const arrayStart = txt.indexOf('PROGMEM = {');
  if (arrayStart < 0) {
    throw new Error(`parseStaticHeader: no PROGMEM array in ${headerPath}`);
  }
  const arrayBlock = txt.slice(arrayStart);
  const arrayEnd = arrayBlock.indexOf('};');
  if (arrayEnd < 0) {
    throw new Error(`parseStaticHeader: unterminated PROGMEM array in ${headerPath}`);
  }
  const bytesStr = arrayBlock.slice(0, arrayEnd);
  const matches = bytesStr.matchAll(/0x([0-9a-fA-F]{2})/g);
  const bytes = [];
  for (const m of matches) bytes.push(parseInt(m[1], 16));
  return {
    etag: etagMatch[1],
    contentType: ctMatch[1],
    bytes: Buffer.from(bytes),
  };
}

// Parse html_stubs.h into a Map<pageId, htmlStub>.  Each stub is a
// raw-string literal: `static const char htmlStub_<id>[] PROGMEM = R"=====(...)=====";`
function parseStubsHeader(headerPath) {
  const txt = fs.readFileSync(headerPath, 'utf8');
  const stubs = new Map();
  const stubRe = /htmlStub_(\w+)\[\][^=]*=\s*R"=====\(([\s\S]*?)\)=====";/g;
  for (const m of txt.matchAll(stubRe)) {
    stubs.set(m[1], m[2]);
  }
  if (stubs.size === 0) {
    throw new Error(`parseStubsHeader: no stubs found in ${headerPath}`);
  }
  return stubs;
}

function runBundlerAndLoad() {
  try {
    const { dtMs } = runBundler();
    const js = parseStaticHeader(HEADER_JS_H, 'js');
    const css = parseStaticHeader(HEADER_CSS_H, 'css');
    const stubs = parseStubsHeader(HEADER_STUBS_H);
    bundleState = {
      jsGz: js.bytes,
      cssGz: css.bytes,
      jsEtag: js.etag,
      cssEtag: css.etag,
      jsContentType: js.contentType,
      cssContentType: css.contentType,
      stubs,
      loaded: true,
      loadError: null,
    };
    console.log(
      `[dev-server] bundle reloaded (js=${js.bytes.length}B etag=${js.etag}, ` +
      `css=${css.bytes.length}B etag=${css.etag}, stubs=${stubs.size}, ${dtMs} ms)`);
  } catch (err) {
    bundleState.loaded = false;
    bundleState.loadError = err;
    console.error(`[dev-server] bundle load FAILED: ${err.message}`);
  }
}

// Watch JS/CSS/bundler-script changes.  Debounce to coalesce flurries
// (editor save → multiple inotify events).
function watchAndReload() {
  let firmwareTimer = null;
  let replayTimer = null;
  const triggerFirmware = () => {
    if (firmwareTimer) clearTimeout(firmwareTimer);
    firmwareTimer = setTimeout(() => {
      firmwareTimer = null;
      runBundlerAndLoad();
    }, 150);
  };
  const triggerReplay = () => {
    if (replayTimer) clearTimeout(replayTimer);
    replayTimer = setTimeout(() => {
      replayTimer = null;
      runReplayBundler();
    }, 150);
  };
  const watchDir = (dir, trigger) => {
    if (!fs.existsSync(dir)) return;
    try {
      fs.watch(dir, { recursive: true }, (evt, name) => {
        if (!name) return;
        // Only trigger on JS / CSS / bundler-script changes.  Ignore
        // node_modules, .git, build artifacts.
        // Skip the replay bundle output itself or we get a self-loop.
        if (name.endsWith('replay-bundle.js')) return;
        if (/\.(js|mjs|css)$/.test(name) || name.endsWith('build_web_bundle.py')) {
          trigger();
        }
      });
    } catch (e) {
      console.warn(`[dev-server] fs.watch(${dir}) failed: ${e.message}`);
    }
  };
  // Firmware bundle inputs.
  watchDir(LIB_DIR, triggerFirmware);
  watchDir(UI_CORE_DIR, triggerFirmware);
  watchDir(path.dirname(BUNDLER), triggerFirmware);
  // Replay bundle inputs. ui-core/ feeds both bundles, so a change
  // there triggers both rebuilds.
  watchDir(REPLAY_SRC_DIR, triggerReplay);
  watchDir(UI_CORE_DIR, triggerReplay);
  watchDir(path.dirname(BUNDLER), triggerReplay);
}

// Splice dev-mode <meta> tags into a firmware stub so the bundle
// can find the WebSocket / logo / version.  Mirrors what the firmware
// does for {{onspeedVersion}}; the dev-mode/ws/logo metas are
// additions the firmware never emits.
function adaptStubForDev(stubHtml, page, args, host) {
  const modeMeta = args.mode === 'mock'
    ? '<meta name="onspeed-mode" content="mock">'
    : args.mode === 'proxy'
      ? `<meta name="onspeed-mode" content="proxy ${args.proxy}">`
      : '';
  const wsMeta = args.mode === 'proxy' && args.proxy
    ? `<meta name="onspeed-ws" content="${proxyToWs(args.proxy)}">`
    : args.mode === 'mock'
      ? `<meta name="onspeed-ws" content="ws://${host}/ws">`
      : '';
  const logoMeta = '<meta name="onspeed-logo" content="/onspeed-logo.png">';
  const injects = [modeMeta, wsMeta, logoMeta].filter(Boolean).join('\n');
  // The firmware stub already carries <meta name="onspeed-version"> with
  // a {{onspeedVersion}} placeholder.  Substitute "dev" the way the
  // firmware's ServePageStub() substitutes BuildInfo::version.
  let out = stubHtml.replace(/\{\{onspeedVersion\}\}/g, 'dev');
  // Inject the dev metas right before </head>.
  out = out.replace('</head>', `${injects}\n</head>`);
  return out;
}
// ---------------------------------------------------------------------
const PAGES = [
  { id: 'indexer',      path: '/indexer',      title: 'Indexer' },
  { id: 'calwiz',       path: '/calwiz',       title: 'Calibration' },
  { id: 'home',         path: '/',             title: 'Home' },
  { id: 'reboot',       path: '/reboot',       title: 'Reboot' },
  { id: 'format',       path: '/format',       title: 'Format SD' },
  { id: 'upgrade',      path: '/upgrade',      title: 'Firmware Upgrade' },
  { id: 'logs',         path: '/logs',         title: 'Logs' },
  { id: 'sensorconfig', path: '/sensorconfig', title: 'Sensor Calibration' },
];

// Routes that 30x to a page rather than rendering one.  `/live` is
// preserved here for backwards-compat with old pilot bookmarks; the
// firmware does the same redirect.
const REDIRECTS = [
  { from: '/live', to: '/indexer' },
];

// Build the page HTML for a given page id, using the firmware's actual
// stub (parsed from html_stubs.h) plus dev-mode meta-tag injections.
// Returns null if the bundle hasn't loaded or no stub exists for that id.
function pageStubHtml(page, args, host) {
  const stub = bundleState.stubs.get(page.id);
  if (!stub) return null;
  return adaptStubForDev(stub, page, args, host);
}

function proxyToWs(httpUrl) {
  try {
    const u = new URL(httpUrl);
    return `ws://${u.hostname}:81`;
  } catch {
    return 'ws://192.168.0.1:81';
  }
}

// ---------------------------------------------------------------------
// File serving
// ---------------------------------------------------------------------
async function serveFile(filePath, res) {
  try {
    const stat = await fsp.stat(filePath);
    if (stat.isDirectory()) {
      res.writeHead(403); res.end(); return true;
    }
    const data = await fsp.readFile(filePath);
    res.writeHead(200, {
      'Content-Type': mimeFor(filePath),
      'Cache-Control': 'no-store',
      'Content-Length': data.length,
    });
    res.end(data);
    return true;
  } catch {
    return false;
  }
}

function safeJoin(rootDir, relPath) {
  // Strip leading slash so path.join treats it as relative.
  const rel = relPath.replace(/^\/+/, '');
  const joined = path.normalize(path.join(rootDir, rel));
  if (!joined.startsWith(rootDir)) return null;  // path traversal guard
  return joined;
}

// ---------------------------------------------------------------------
// /api/* — mock or proxy
// ---------------------------------------------------------------------
async function handleApi(req, res, args) {
  if (args.mode === 'proxy') {
    return proxyRequest(req, res, args.proxy);
  }
  if (args.mode === 'mock') {
    // Path → mocks file: /api/foo/bar -> mocks/api-foo-bar.json (the
    // /api/ prefix is preserved in the filename so the mock layout
    // mirrors the URL exactly; convention documented in README.md).
    const reqPath = req.url.split('?')[0].replace(/^\//, '');
    const mockName = reqPath.replace(/\//g, '-') + '.json';
    const mockFile = safeJoin(MOCKS_DIR, mockName);
    if (mockFile && fs.existsSync(mockFile)) {
      const data = await fsp.readFile(mockFile);
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(data);
      return;
    }
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ message: `No mock for /${reqPath}` }));
    return;
  }
  res.writeHead(503, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ message: 'No backend configured (run with --mock or --proxy)' }));
}

function proxyRequest(req, res, target) {
  const targetUrl = new URL(req.url, target);
  const opts = {
    method: req.method,
    headers: { ...req.headers, host: targetUrl.host },
  };
  const upstream = http.request(targetUrl, opts, (up) => {
    res.writeHead(up.statusCode || 502, up.headers);
    up.pipe(res);
  });
  upstream.on('error', (err) => {
    res.writeHead(502, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ message: `Proxy error: ${err.message}` }));
  });
  req.pipe(upstream);
}

// ---------------------------------------------------------------------
// Minimal RFC 6455 WebSocket handshake + frame writer.
// Only the pieces we actually use: text-frame writing + close frame.
// ---------------------------------------------------------------------
function wsAcceptKey(clientKey) {
  // Magic GUID per RFC 6455.
  const MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
  return crypto.createHash('sha1').update(clientKey + MAGIC).digest('base64');
}

function writeWsTextFrame(socket, text) {
  const payload = Buffer.from(text, 'utf-8');
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[1] = len;
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[1] = 127;
    // High 32 bits = 0; low 32 bits = len (we never send >2 GB frames).
    header.writeUInt32BE(0, 2);
    header.writeUInt32BE(len, 6);
  }
  header[0] = 0x81;  // FIN + opcode 0x1 (text)
  socket.write(Buffer.concat([header, payload]));
}

function handleUpgrade(req, socket, head, args) {
  const url = new URL(req.url, `http://${req.headers.host}`);
  if (url.pathname !== '/ws') {
    socket.end('HTTP/1.1 404 Not Found\r\n\r\n');
    return;
  }
  const key = req.headers['sec-websocket-key'];
  if (!key) { socket.end('HTTP/1.1 400 Bad Request\r\n\r\n'); return; }
  const accept = wsAcceptKey(key);
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
    'Upgrade: websocket\r\n' +
    'Connection: Upgrade\r\n' +
    `Sec-WebSocket-Accept: ${accept}\r\n\r\n`
  );
  console.log('[ws] client connected');

  // Drive frames into the socket from the chosen source.  Keep a
  // closed flag so the source loop can exit when the client drops.
  let closed = false;
  const onClose = () => { closed = true; console.log('[ws] client disconnected'); };
  socket.on('close', onClose);
  socket.on('error', onClose);

  driveWsSource(args, (frame) => {
    if (closed) return false;
    writeWsTextFrame(socket, JSON.stringify(frame));
    return true;
  }).catch(err => {
    console.error('[ws] source error:', err);
    closed = true;
    socket.end();
  });
}

// ---------------------------------------------------------------------
// WS source: replay NDJSON or run a scenario.  `emit(frame)` returns
// false when the client has disconnected; the loop stops then.
// ---------------------------------------------------------------------
async function driveWsSource(args, emit) {
  if (args.scenario) {
    return driveScenario(args.scenario, emit);
  }
  const replayPath = args.replay
    ? path.resolve(args.replay)
    : path.join(REPLAY_DIR, 'cruise.ndjson');
  return driveReplay(replayPath, emit);
}

const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

async function driveReplay(replayPath, emit) {
  if (!fs.existsSync(replayPath)) {
    console.warn(`[ws] replay file not found: ${replayPath}`);
    console.warn('[ws] sending an empty record forever; populate with capture.mjs');
    while (true) {
      if (!emit({})) return;
      await sleep(1000);
    }
  }
  const text = await fsp.readFile(replayPath, 'utf-8');
  const lines = text.split('\n')
    .filter(l => l.trim().length > 0 && !l.trim().startsWith('//'));
  if (lines.length === 0) {
    console.warn(`[ws] no frames in ${replayPath}`);
    return;
  }
  console.log(`[ws] replaying ${lines.length} frames from ${path.relative(REPO_ROOT, replayPath)}`);
  // Loop forever so the page stays live across long iterations.
  while (true) {
    for (const line of lines) {
      let entry;
      try { entry = JSON.parse(line); }
      catch (e) { console.warn('[ws] bad NDJSON line:', e.message); continue; }
      const delay = Math.max(0, entry.tDelay || 0);
      if (delay) await sleep(delay);
      if (!emit(entry.frame ?? entry)) return;
    }
    await sleep(1000);  // 1-second pause before looping
  }
}

async function driveScenario(name, emit) {
  // Load the scenario module dynamically so the dev server doesn't pay
  // its parse cost in proxy / static modes.
  const mod = await import(new URL('../lib/scenarios.js', import.meta.url));
  const fn = mod.scenarios?.[name];
  if (!fn) {
    console.error(`[ws] no scenario named "${name}". Available: ${Object.keys(mod.scenarios || {}).join(', ')}`);
    return;
  }
  console.log(`[ws] driving scenario "${name}" at 20 Hz`);
  const start = Date.now();
  while (true) {
    const t = Date.now() - start;
    const r = fn(t);
    // The lib/scenarios.js shapes records to the canonical record
    // shape (verticalG, percentLift, etc.), but the firmware WS sends
    // raw JSON keyed by AOA/IAS/etc.  Translate back so the page-side
    // wsClient.frameToRecord() doesn't break.
    if (!emit(recordToFrame(r))) return;
    await sleep(50);  // 20 Hz
  }
}

// Inverse of wsClient.frameToRecord() — mainly for scenario data.
// Pass-through any unrecognized fields so future scenario extensions
// don't get silently dropped.
function recordToFrame(r) {
  return {
    AOA:               r.aoaDeg ?? r.aoa ?? null,
    DerivedAOA:        r.derivedAoaDeg ?? null,
    Pitch:             r.pitchDeg ?? 0,
    Roll:              r.rollDeg ?? 0,
    verticalGLoad:     r.verticalG ?? 1,
    lateralGLoad:      r.lateralG ?? 0,
    PitchRate:         r.pitchRate ?? 0,
    IAS:               r.iasKt ?? null,
    PAlt:              r.paltFt ?? 0,
    OAT:               r.oatC ?? 20,
    kalmanVSI:         r.vsiFpm ?? 0,
    flightPath:        r.flightPathDeg ?? 0,
    DecelRate:         r.decelRate ?? 0,
    percentLift:        r.percentLift ?? 0,
    tonesOnPctLift:     r.tonesOnPctLift ?? 0,
    onSpeedFastPctLift: r.onSpeedFastPctLift ?? 0,
    onSpeedSlowPctLift: r.onSpeedSlowPctLift ?? 0,
    stallWarnPctLift:   r.stallWarnPctLift ?? 0,
    pipPctLift:         r.pipPctLift ?? 0,
    flapsPos:           r.flapsDeg ?? 0,
    flapsMinDeg:        r.flapsMinDeg ?? 0,
    flapsMaxDeg:        r.flapsMaxDeg ?? 33,
    gOnsetRate:         r.gOnsetRate ?? 0,
    dataMark:           r.dataMark ?? 0,
  };
}

// ---------------------------------------------------------------------
// Server entry
// ---------------------------------------------------------------------
function main() {
  const args = parseArgs(process.argv.slice(2));
  console.log(`[dev-server] mode=${args.mode}` +
              (args.proxy ? ` proxy=${args.proxy}` : '') +
              (args.replay ? ` replay=${args.replay}` : '') +
              (args.scenario ? ` scenario=${args.scenario}` : ''));

  // Build the firmware bundle once at startup so the first request can
  // serve real artifacts; then watch source trees to refresh on save.
  runBundlerAndLoad();
  // Build the docs-site replay bundle so an MkDocs serve running
  // alongside this dev server can fetch /data-and-logs/replay/replay-bundle.js
  // (also served via the route below for parallel-with-MkDocs setups).
  runReplayBundler();
  watchAndReload();

  const server = http.createServer(async (req, res) => {
    try {
      await route(req, res, args);
    } catch (err) {
      console.error('[http]', err);
      if (!res.headersSent) {
        res.writeHead(500, { 'Content-Type': 'text/plain' });
        res.end('Internal server error');
      } else {
        res.end();
      }
    }
  });

  server.on('upgrade', (req, socket, head) => {
    if (args.mode === 'mock') {
      handleUpgrade(req, socket, head, args);
    } else {
      // No WS in proxy / static modes: page connects to the device
      // directly.  Returning 501 keeps it honest.
      socket.end('HTTP/1.1 501 Not Implemented\r\n\r\n');
    }
  });

  server.listen(args.port, () => {
    console.log(`[dev-server] http://localhost:${args.port}/`);
    for (const p of PAGES) console.log(`               ${p.path}`);
    console.log(`               /scenarios.html (offline harness)`);
  });
}

// Render the /aoaconfig legacy page from the template + JS files in
// tools/web/legacy-pages/, substituting {{name}} markers from
// dev-server/mocks/aoaconfig.json.  Mirrors HandleConfig() in
// software/sketch_common/src/web_server/ConfigWebServer.cpp.
async function renderAoaConfigPage() {
  const tplPath  = path.join(LEGACY_DIR, 'aoaconfig.html');
  const jsPath   = path.join(LEGACY_DIR, 'aoaconfig.js');
  const postPath = path.join(LEGACY_DIR, 'aoaconfig-post.js');
  const cfgPath  = path.join(MOCKS_DIR, 'aoaconfig.json');
  const [tpl, js, postJs, cfgRaw] = await Promise.all([
    fsp.readFile(tplPath, 'utf-8'),
    fsp.readFile(jsPath, 'utf-8'),
    fsp.readFile(postPath, 'utf-8'),
    fsp.readFile(cfgPath, 'utf-8'),
  ]);
  const cfg = JSON.parse(cfgRaw);
  return substituteAoaConfig(tpl, cfg, js, postJs);
}

function selAttr(b) { return b ? ' selected' : ''; }
function checkedAttr(b) { return b ? ' checked' : ''; }
function displayBlock(b) {
  return b ? 'style="display:block"' : 'style="display:none"';
}

function curveParamStrings(curveType, casVariant) {
  // Match HandleConfig's per-flap initial-render labels.  CAS curve uses
  // a slightly different set; legacy bytes preserved verbatim.
  if (casVariant) {
    if (curveType === 1) return [' *X<sup>3</sup>+ ', ' *X<sup>2</sup>+ ', ' *X<sup></sup>+ ', ''];
    if (curveType === 2) return [' * 0 + ', ' * 0 + ', '*ln(x)+', ''];
    if (curveType === 3) return [' * 0 + ', ' * 0 + ', '* e^ (', ' * x)'];
    return ['', '', '', ''];
  }
  if (curveType === 1) return [' *X<sup>3</sup>+', ' *X<sup>2</sup>+', '*X+', ''];
  if (curveType === 2) return [' * 0 + ', ' * 0 + ', '*ln(x)+ ', ''];
  if (curveType === 3) return [' * 0 + ', ' * 0 + ', '* e^ (', ') * x'];
  return ['', '', '', ''];
}

function substituteAoaConfig(tpl, cfg, js, postJs) {
  // 1. Per-flap block.
  const startMarker = '<!-- FLAP_BLOCK_START -->';
  const endMarker   = '<!-- FLAP_BLOCK_END -->';
  const startIdx = tpl.indexOf(startMarker);
  const endIdx   = tpl.indexOf(endMarker);
  if (startIdx < 0 || endIdx < 0) {
    throw new Error('aoaconfig.html missing FLAP_BLOCK markers');
  }
  const flapTpl = tpl.slice(startIdx + startMarker.length, endIdx);
  const flapsRendered = (cfg.flaps || []).map((f, idx) => {
    const [p0, p1, p2, p3] = curveParamStrings(f.curveType || 1, false);
    let s = flapTpl;
    s = s.replaceAll('{{flap.idx}}',          String(idx));
    s = s.replaceAll('{{flap.label}}',        String(idx + 1));
    s = s.replaceAll('{{flap.degrees}}',      String(f.degrees));
    s = s.replaceAll('{{flap.potPosition}}',  String(f.potPosition));
    s = s.replaceAll('{{flap.LDMAXAOA}}',     String(f.LDMAXAOA));
    s = s.replaceAll('{{flap.OnSpeedFastAOA}}', String(f.OnSpeedFastAOA));
    s = s.replaceAll('{{flap.OnSpeedSlowAOA}}', String(f.OnSpeedSlowAOA));
    s = s.replaceAll('{{flap.StallWarnAOA}}', String(f.StallWarnAOA));
    s = s.replaceAll('{{flap.StallAOA}}',     String(f.StallAOA));
    s = s.replaceAll('{{flap.ManAOA}}',       String(f.ManAOA));
    s = s.replaceAll('{{flap.KFit}}',         String(f.KFit));
    s = s.replaceAll('{{flap.Alpha0}}',       String(f.Alpha0));
    s = s.replaceAll('{{flap.AlphaStall}}',   String(f.AlphaStall));
    s = s.replaceAll('{{flap.curvePolySel}}', selAttr(f.curveType === 1));
    s = s.replaceAll('{{flap.curveLogSel}}',  selAttr(f.curveType === 2));
    s = s.replaceAll('{{flap.curveExpSel}}',  selAttr(f.curveType === 3));
    const [c0, c1, c2, c3] = f.coeffs || [0, 0, 0, 0];
    s = s.replaceAll('{{flap.coeff0}}', String(c0));
    s = s.replaceAll('{{flap.coeff1}}', String(c1));
    s = s.replaceAll('{{flap.coeff2}}', String(c2));
    s = s.replaceAll('{{flap.coeff3}}', String(c3));
    s = s.replaceAll('{{flap.param0}}', p0);
    s = s.replaceAll('{{flap.param1}}', p1);
    s = s.replaceAll('{{flap.param2}}', p2);
    s = s.replaceAll('{{flap.param3}}', p3);
    return s;
  }).join('');

  let body = tpl.slice(0, startIdx) + flapsRendered + tpl.slice(endIdx + endMarker.length);

  // 2. Replay log file picker datalist.
  const replayOptions = (cfg.replayLogFiles || [])
    .map(name => `                    <option value="${name}">\n`)
    .join('');
  body = body.replaceAll('{{replayLogFileOptions}}', replayOptions);
  body = body.replaceAll('{{replayLogFileName}}', cfg.replayLogFileName || '');
  body = body.replaceAll('{{replayLogFileStyle}}',
                         displayBlock(cfg.dataSource === 'REPLAYLOGFILE'));

  // 3. Visibility toggles.
  body = body.replaceAll('{{casCurveVisibility}}',  displayBlock(!!cfg.casCurveEnabled));
  body = body.replaceAll('{{loadLimitVisibility}}', displayBlock(!!cfg.overgWarning));
  body = body.replaceAll('{{vnoVisibility}}',       displayBlock(!!cfg.vnoChimeEnabled));
  body = body.replaceAll('{{volumeControlWidth}}',  cfg.volumeControl ? '9' : '6');
  body = body.replaceAll('{{defaultVolumeVisibility}}', displayBlock(!cfg.volumeControl));
  body = body.replaceAll('{{volumeLevelsVisibility}}',  displayBlock(!!cfg.volumeControl));

  // 4. Scalars + select markers.
  body = body.replaceAll('{{aoaSmoothing}}',      String(cfg.aoaSmoothing));
  body = body.replaceAll('{{pressureSmoothing}}', String(cfg.pressureSmoothing));
  body = body.replaceAll('{{dataSrcSensorsSel}}',    selAttr(cfg.dataSource === 'SENSORS'));
  body = body.replaceAll('{{dataSrcTestPotSel}}',    selAttr(cfg.dataSource === 'TESTPOT'));
  body = body.replaceAll('{{dataSrcRangeSweepSel}}', selAttr(cfg.dataSource === 'RANGESWEEP'));
  body = body.replaceAll('{{dataSrcReplaySel}}',     selAttr(cfg.dataSource === 'REPLAYLOGFILE'));

  body = body.replaceAll('{{boomEnabledSel}}',          selAttr(!!cfg.boomEnabled));
  body = body.replaceAll('{{boomDisabledSel}}',         selAttr(!cfg.boomEnabled));
  body = body.replaceAll('{{boomChecksumEnabledSel}}',  selAttr(!!cfg.boomChecksum));
  body = body.replaceAll('{{boomChecksumDisabledSel}}', selAttr(!cfg.boomChecksum));
  body = body.replaceAll('{{boomConvertRawSel}}',  selAttr(!cfg.boomConvertData));
  body = body.replaceAll('{{boomConvertConvSel}}', selAttr(!!cfg.boomConvertData));

  body = body.replaceAll('{{casCurveEnabledSel}}',  selAttr(!!cfg.casCurveEnabled));
  body = body.replaceAll('{{casCurveDisabledSel}}', selAttr(!cfg.casCurveEnabled));
  body = body.replaceAll('{{casCurveTypePolySel}}', selAttr(cfg.casCurveType === 1));
  body = body.replaceAll('{{casCurveTypeLogSel}}',  selAttr(cfg.casCurveType === 2));
  body = body.replaceAll('{{casCurveTypeExpSel}}',  selAttr(cfg.casCurveType === 3));
  const casCoeffs = cfg.casCoeffs || [0, 0, 0, 0];
  body = body.replaceAll('{{casCurveCoeff0}}', String(casCoeffs[0]));
  body = body.replaceAll('{{casCurveCoeff1}}', String(casCoeffs[1]));
  body = body.replaceAll('{{casCurveCoeff2}}', String(casCoeffs[2]));
  body = body.replaceAll('{{casCurveCoeff3}}', String(casCoeffs[3]));
  const [casP0, casP1, casP2, casP3] = curveParamStrings(cfg.casCurveType || 1, true);
  body = body.replaceAll('{{casCurveParam0}}', casP0);
  body = body.replaceAll('{{casCurveParam1}}', casP1);
  body = body.replaceAll('{{casCurveParam2}}', casP2);
  body = body.replaceAll('{{casCurveParam3}}', casP3);

  for (const o of ['Up', 'Down', 'Left', 'Right', 'Forward', 'Aft']) {
    body = body.replaceAll(`{{portsOr${o}Sel}}`,
                           selAttr(cfg.portsOrientation === o.toUpperCase()));
    body = body.replaceAll(`{{boxOr${o}Sel}}`,
                           selAttr(cfg.boxtopOrientation === o.toUpperCase()));
  }

  body = body.replaceAll('{{readEfisEnabledSel}}',  selAttr(!!cfg.readEfisData));
  body = body.replaceAll('{{readEfisDisabledSel}}', selAttr(!cfg.readEfisData));
  body = body.replaceAll('{{efisDynonD10Sel}}', selAttr(cfg.efisType === 'DYNOND10'));
  body = body.replaceAll('{{efisAdvancedSel}}', selAttr(cfg.efisType === 'ADVANCED'));
  body = body.replaceAll('{{efisG5Sel}}',       selAttr(cfg.efisType === 'GARMING5'));
  body = body.replaceAll('{{efisG3xSel}}',      selAttr(cfg.efisType === 'GARMING3X'));
  body = body.replaceAll('{{efisVn300Sel}}',    selAttr(cfg.efisType === 'VN-300'));
  body = body.replaceAll('{{efisVn100Sel}}',    selAttr(cfg.efisType === 'VN-100'));
  body = body.replaceAll('{{efisMglSel}}',      selAttr(cfg.efisType === 'MGL'));

  body = body.replaceAll('{{oatEnabledSel}}',  selAttr(!!cfg.oatSensor));
  body = body.replaceAll('{{oatDisabledSel}}', selAttr(!cfg.oatSensor));

  body = body.replaceAll('{{calSrcOnspeedSel}}',
                         selAttr(!cfg.calSource || cfg.calSource === 'ONSPEED'));
  body = body.replaceAll('{{calSrcEfisSel}}', selAttr(cfg.calSource === 'EFIS'));

  body = body.replaceAll('{{ahrsMadgwickSel}}', selAttr(cfg.ahrsAlgorithm === 0));
  body = body.replaceAll('{{ahrsEkfqSel}}',     selAttr(cfg.ahrsAlgorithm === 1));

  body = body.replaceAll('{{volumeEnabledSel}}',  selAttr(!!cfg.volumeControl));
  body = body.replaceAll('{{volumeDisabledSel}}', selAttr(!cfg.volumeControl));
  body = body.replaceAll('{{defaultVolume}}',     String(cfg.defaultVolume));
  body = body.replaceAll('{{volumeLowAnalog}}',   String(cfg.volumeLowAnalog));
  body = body.replaceAll('{{volumeHighAnalog}}',  String(cfg.volumeHighAnalog));
  body = body.replaceAll('{{muteAudioUnderIAS}}', String(cfg.muteAudioUnderIAS));
  body = body.replaceAll('{{audio3dEnabledSel}}',  selAttr(!!cfg.audio3D));
  body = body.replaceAll('{{audio3dDisabledSel}}', selAttr(!cfg.audio3D));

  body = body.replaceAll('{{overgEnabledSel}}',   selAttr(!!cfg.overgWarning));
  body = body.replaceAll('{{overgDisabledSel}}',  selAttr(!cfg.overgWarning));
  body = body.replaceAll('{{loadLimitPositive}}',   Number(cfg.loadLimitPositive).toFixed(2));
  body = body.replaceAll('{{loadLimitNegative}}',   Number(cfg.loadLimitNegative).toFixed(2));
  body = body.replaceAll('{{asymmetricGyroLimit}}', Number(cfg.asymmetricGyroLimit).toFixed(1));
  body = body.replaceAll('{{asymmetricReduction}}', Number(cfg.asymmetricReduction).toFixed(3));

  body = body.replaceAll('{{vnoChimeEnabledSel}}',  selAttr(!!cfg.vnoChimeEnabled));
  body = body.replaceAll('{{vnoChimeDisabledSel}}', selAttr(!cfg.vnoChimeEnabled));
  body = body.replaceAll('{{vno}}',                 String(cfg.vno));
  body = body.replaceAll('{{vnoChimeInterval}}',    String(cfg.vnoChimeInterval));

  body = body.replaceAll('{{sdLoggingEnabledSel}}',  selAttr(!!cfg.sdLogging));
  body = body.replaceAll('{{sdLoggingDisabledSel}}', selAttr(!cfg.sdLogging));
  body = body.replaceAll('{{logRate50Sel}}',  selAttr(cfg.logRate === 50));
  body = body.replaceAll('{{logRate208Sel}}', selAttr(cfg.logRate === 208));
  body = body.replaceAll('{{logRate416Sel}}', selAttr(cfg.logRate === 416));

  body = body.replaceAll('{{serialOutG3xSel}}',
                         selAttr(cfg.serialOutFormat === 'G3X'));
  body = body.replaceAll('{{serialOutOnspeedSel}}',
                         selAttr(!cfg.serialOutFormat ||
                                 cfg.serialOutFormat === 'ONSPEED'));

  body = body.replaceAll('{{acGrossWeight}}',  String(cfg.acGrossWeight));
  body = body.replaceAll('{{acBestGlideIAS}}', String(cfg.acBestGlideIAS));
  body = body.replaceAll('{{acVfe}}',          String(cfg.acVfe));
  // The Custom-G inputs always render the pilot's typed Custom
  // values, regardless of which radio is active.  Mirrors HandleConfig.
  // Negative G is shown as a positive magnitude; the mock JSON stores
  // it negative to match firmware persistence.
  body = body.replaceAll('{{customAcGlimit}}',
                         Number(cfg.customAcGlimit).toFixed(2));
  body = body.replaceAll('{{customAcNegGlimit}}',
                         Math.abs(Number(cfg.customAcNegGlimit)).toFixed(2));
  // A named category is "active" only when both pos and neg sides
  // match the preset's pair (mirrors HandleConfig's matches() lambda).
  const fPos = Number(cfg.acGlimit);
  const fNeg = Number(cfg.acNegGlimit);
  const matches = (a, b) => Math.abs(a - b) <= 0.005;
  const isNormal    = matches(fPos,  3.80) && matches(fNeg, -1.52);
  const isUtility   = matches(fPos,  4.40) && matches(fNeg, -1.76);
  const isAerobatic = matches(fPos,  6.00) && matches(fNeg, -3.00);
  const isPreset    = isNormal || isUtility || isAerobatic;
  body = body.replaceAll('{{glimitNormalChecked}}',    checkedAttr(isNormal));
  body = body.replaceAll('{{glimitUtilityChecked}}',   checkedAttr(isUtility));
  body = body.replaceAll('{{glimitAerobaticChecked}}', checkedAttr(isAerobatic));
  body = body.replaceAll('{{glimitCustomChecked}}',    checkedAttr(!isPreset));
  body = body.replaceAll('{{glimitCustomDisplay}}',    isPreset ? 'display:none' : '');

  // 5. Inline scripts last (so { } in their bodies don't trip earlier
  // passes).  Wrap with the dev shell so the page still has a basic
  // header/footer.
  body = body.replaceAll('{{aoaconfigJs}}',     js);
  body = body.replaceAll('{{aoaconfigPostJs}}', postJs);

  // Wrap with the same chrome the firmware's HandleConfig wraps it
  // with: a logo + nav `<ul id="liveview-nav-ul">` so the dev page
  // matches what pilots see at flight time.  Loads PageShell.css +
  // legacy-forms.css — together they cover every class the legacy
  // template references (.content-container, .round-box, .form-divs,
  // .flex-col-N, .button, .greybutton, .inputField, .switch-field,
  // .sp-row family, etc.).  This is the same pair the bundler ships
  // to firmware as /static/app-<sha>.css.
  return legacyPageHtml('/aoaconfig (dev)', 'aoaconfig', body);
}

// Common HTML wrapper for the dev-server's legacy-page renders.
// `activeId` controls which nav entry / dropdown item gets the
// `class="active"` annotation; the bundle CSS link list mirrors the
// firmware's pageHeader.
function legacyPageHtml(title, activeId, body) {
  return [
    '<!DOCTYPE html>',
    '<html lang="en">',
    '<head>',
    '<meta charset="utf-8">',
    '<meta name="viewport" content="width=device-width, initial-scale=1">',
    `<title>OnSpeed — ${title}</title>`,
    // Equivalent to firmware's /static/app-<sha>.css (which is the
    // bundled concat of these two source files).  Loading the source
    // files directly here means edits don't need a re-bundle to
    // preview locally; the firmware still loads the bundled output.
    '<link rel="stylesheet" href="/lib/shell/PageShell.css">',
    '<link rel="stylesheet" href="/lib/shell/legacy-forms.css">',
    '</head>',
    '<body>',
    legacyChromeHtml(activeId),
    body,
    '</body>',
    '</html>',
  ].join('\n');
}

// Mirror `pageHeader`'s logo + nav so a legacy page rendered on the
// dev server looks like the firmware-served version.  `activeId`
// flags the matching dropdown item / primary link with class="active":
// 'aoaconfig' | 'sensorconfig' | 'calwiz' light up the Settings
// dropbtn + their item; primary-link ids ('logs', 'format', etc.)
// light up the Tools dropbtn + their item.  The version string is
// "dev" to make it obvious the user is on the dev-server.
function legacyChromeHtml(activeId) {
  // Items belonging to each dropdown.  Mirrors NAV in lib/shell/nav.js
  // so the firmware-rendered legacy markup matches what PageShell.js
  // renders on Preact pages.
  const toolsItems = [
    { id: 'logs',    href: '/logs',    label: 'Log Files' },
    { id: 'format',  href: '/format',  label: 'Format SD Card' },
    { id: 'upgrade', href: '/upgrade', label: 'Firmware Upgrade' },
    { id: 'reboot',  href: '/reboot',  label: 'Reboot System' },
  ];
  const settingsItems = [
    { id: 'aoaconfig',    href: '/aoaconfig',    label: 'System Configuration' },
    { id: 'sensorconfig', href: '/sensorconfig', label: 'Sensor Calibration' },
    { id: 'calwiz',       href: '/calwiz',       label: 'AOA Calibration Wizard' },
  ];
  const toolsActive    = toolsItems.some(i => i.id === activeId);
  const settingsActive = settingsItems.some(i => i.id === activeId);
  const itemMarkup = (items) => items
    .map(i => `      <a href="${i.href}"${i.id === activeId ? ' class="active"' : ''}>${i.label}</a>`)
    .join('\n');
  return [
    '<div class="header-container">',
    '  <img src="/onspeed-logo.png" alt="OnSpeed" />',
    '  <div class="firmware">OnSpeed Version: dev</div>',
    '</div>',
    '<ul id="liveview-nav-ul">',
    `  <li><a href="/"${activeId === 'home' ? ' class="active"' : ''}>Home</a></li>`,
    '  <li class="dropdown">',
    `    <a href="javascript:void(0)" class="dropbtn${toolsActive ? ' active' : ''}">Tools</a>`,
    '    <div class="dropdown-content">',
    itemMarkup(toolsItems),
    '    </div>',
    '  </li>',
    '  <li class="dropdown">',
    `    <a href="javascript:void(0)" class="dropbtn${settingsActive ? ' active' : ''}">Settings</a>`,
    '    <div class="dropdown-content">',
    itemMarkup(settingsItems),
    '    </div>',
    '  </li>',
    `  <li><a href="/indexer"${activeId === 'indexer' ? ' class="active"' : ''}>Indexer</a></li>`,
    '</ul>',
  ].join('\n');
}

async function route(req, res, args) {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const pathname = url.pathname;

  // /api/*
  if (pathname.startsWith('/api/')) {
    return handleApi(req, res, args);
  }

  // /aoaconfig — legacy form-driven page rendered from the template.
  if (pathname === '/aoaconfig') {
    try {
      const html = await renderAoaConfigPage();
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(html);
    } catch (e) {
      res.writeHead(500, { 'Content-Type': 'text/plain' });
      res.end(`/aoaconfig render error: ${e.message}`);
    }
    return;
  }

  // /sensorconfig is served from the Preact bundle like /indexer
  // and /calwiz — falls through to the page-stub renderer below.

  // Stale-bookmark redirects (e.g. /live → /indexer).
  for (const r of REDIRECTS) {
    if (pathname === r.from) {
      res.writeHead(302, { Location: r.to });
      res.end();
      return;
    }
  }

  // Page stubs.  Pass the request Host so the WS meta tag uses the
  // same hostname the browser used to reach us (works for LAN IPs).
  // The HTML is the firmware's actual per-page stub (parsed from
  // html_stubs.h) with dev-mode meta tags spliced into <head>.
  for (const page of PAGES) {
    if (pathname === page.path) {
      const host = req.headers.host || `localhost:${args.port}`;
      const html = pageStubHtml(page, args, host);
      if (html === null) {
        const why = bundleState.loadError
          ? `Bundle failed to build: ${bundleState.loadError.message}`
          : `No stub for page id "${page.id}"; bundle still loading?`;
        res.writeHead(503, { 'Content-Type': 'text/plain; charset=utf-8' });
        res.end(why);
        return;
      }
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(html);
      return;
    }
  }

  // Bundled JS + CSS.  Served at the same URLs the firmware would use:
  //   /static/app-<etag>.js
  //   /static/app-<etag>.css
  // The bytes are pre-gzipped (parsed from static_app_<kind>.h), so we
  // emit them with Content-Encoding: gzip.  Etag mismatch returns 404
  // (firmware behavior is to ignore the etag and serve the latest; we
  // could mimic that, but a 404 on stale bundle URLs makes drift
  // visible immediately).
  if (pathname.startsWith('/static/app-')) {
    if (!bundleState.loaded) {
      res.writeHead(503, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end(bundleState.loadError
        ? `Bundle failed to build: ${bundleState.loadError.message}`
        : 'Bundle still loading');
      return;
    }
    const jsMatch  = pathname.match(/^\/static\/app-([0-9a-f]+)\.js$/);
    const cssMatch = pathname.match(/^\/static\/app-([0-9a-f]+)\.css$/);
    if (jsMatch) {
      if (jsMatch[1] !== bundleState.jsEtag) {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end(`Stale JS bundle etag ${jsMatch[1]}; current is ${bundleState.jsEtag}`);
        return;
      }
      res.writeHead(200, {
        'Content-Type':     bundleState.jsContentType,
        'Content-Encoding': 'gzip',
        'Cache-Control':    'no-store',
      });
      res.end(bundleState.jsGz);
      return;
    }
    if (cssMatch) {
      if (cssMatch[1] !== bundleState.cssEtag) {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end(`Stale CSS bundle etag ${cssMatch[1]}; current is ${bundleState.cssEtag}`);
        return;
      }
      res.writeHead(200, {
        'Content-Type':     bundleState.cssContentType,
        'Content-Encoding': 'gzip',
        'Cache-Control':    'no-store',
      });
      res.end(bundleState.cssGz);
      return;
    }
  }

  // Synthetic-scenarios harness.
  if (pathname === '/scenarios.html' || pathname === '/scenarios') {
    if (await serveFile(path.join(PUBLIC_DIR, 'scenarios.html'), res)) return;
  }

  // Docs-site replay bundle.  An MkDocs serve session running on its
  // own port (typically :8000) loads this URL via a same-origin
  // sibling path; when developing the replay tool against this dev
  // server, the bundle has to be reachable here too.  Served from the
  // file the bundler writes — re-built on source changes via
  // watchAndReload().
  if (pathname === REPLAY_BUNDLE_URL) {
    if (await serveFile(REPLAY_BUNDLE, res)) return;
    res.writeHead(503, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('replay-bundle.js not built yet — wait for the next rebuild.');
    return;
  }

  // WASM artifact under /static/onspeed_core/ → wasm/dist/
  // Built by: bash software/Libraries/onspeed_core/wasm/build_wasm.sh
  // Returns 404 with a helpful message if the build hasn't been run yet.
  if (pathname.startsWith('/static/onspeed_core/')) {
    const rel = pathname.slice('/static/onspeed_core/'.length);
    const f = safeJoin(WASM_DIST_DIR, rel);
    if (f && await serveFile(f, res)) return;
    // Artifact missing — build hasn't been run yet.
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end(
      `WASM artifact not found: ${pathname}\n` +
      'Run: bash software/Libraries/onspeed_core/wasm/build_wasm.sh\n'
    );
    return;
  }

  // The /lib/ raw-source route was removed when the dev server switched
  // to serving the firmware's bundled artifacts.  Any leftover /lib/*
  // requests would indicate a stale tab against an old dev-server
  // build — answer with a 410 Gone so the failure is loud.
  if (pathname.startsWith('/lib/')) {
    res.writeHead(410, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end(
      `/lib/ source serving has been removed.  The dev server now serves\n` +
      `the firmware's bundled artifacts at /static/app-<etag>.{js,css}.\n` +
      `Reload the page (the new stub references the bundled URLs directly).\n`);
    return;
  }

  // The "home" page is registered above with path "/"; if we get here,
  // it's because no page matched (theoretically unreachable).

  // Anything else: try /public/.  Use this for /style.css etc.
  const pubFile = safeJoin(PUBLIC_DIR, pathname);
  if (pubFile && await serveFile(pubFile, res)) return;

  res.writeHead(404, { 'Content-Type': 'text/plain' });
  res.end(`Not found: ${pathname}`);
}

main();
