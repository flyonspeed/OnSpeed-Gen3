// m5sim.js — JS wrapper around the M5-Display firmware compiled to WASM.
//
// PR 2 of Project B2. The WASM artifact is produced by:
//
//   bash software/OnSpeed-M5-Display/sim/build_wasm.sh --target replay
//
// and copied to tools/web/lib/replay/m5sim/onspeed_m5.{js,wasm} by the
// build script. This module loads it once per page and exposes a
// minimal JS surface:
//
//   const sim = await M5Sim.create();      // loads onspeed_m5.{js,wasm}
//   sim.advanceTo(virtualMillis);          // sets g_replay_millis_us
//   sim.injectBytes(uint8Array);           // pushes wire bytes through parser
//   sim.setMode(0..4);                     // writes displayType
//   const state = sim.read();              // frozen object with all state vars
//   sim.delete();                          // tear down (defensive; usually noop)
//
// Time semantics: `advanceTo` sets the virtual clock and immediately calls
// `replay_loop()` so the firmware's 50 ms graphics tick / 500 ms numbers
// snapshot fire at the right virtual time. Callers drive at video frame
// rate (~30-60 Hz); `replay_loop` is cheap (no panel blit on this build,
// per RenderShim.h's no-op stubs).
//
// `read()` returns a frozen object with every state-var accessor's
// current value plus a `gHistory` Float32Array. `gHistory` is a copy
// (HEAPF32.slice) rather than a live view: the firmware writes into the
// ring buffer on each `replay_loop()`, and a live view would mutate
// underneath any caller that holds it across frames. Pay the 300×4 byte
// copy per read to keep the read result immutable.

// ---------------------------------------------------------------------
// Internal: load the WASM module once.
//
// build_wasm.sh emits the module with `EXPORT_ES6=0` (CommonJS-style
// factory). The .js file declares `var Module = (...)` at top scope
// and assigns `module.exports = Module` for Node consumers. In a
// browser, neither happens automatically — `var Module` is window
// scope under <script>, but ES dynamic-import() of a non-module .js
// throws a SyntaxError.
//
// Browser path: append a <script> tag with the file URL, wait for it
// to load, then capture `globalThis.Module` (the var Module assigns).
// We snatch the factory immediately so a second load doesn't trip on
// a leftover global.
//
// Node path: the m5sim-smoke.mjs harness passes a factory it loaded
// via `require()` directly to M5Sim.fromFactory, bypassing this.
//
// SINGLE_FILE is NOT enabled on this artifact (the .wasm is separate),
// so the dev-server must also serve `onspeed_m5.wasm` next to the .js.
// Emscripten's `locateFile` resolves the .wasm path relative to the
// .js file's URL — pinning the script src to the dev-server route
// keeps both fetches against the same origin.
// ---------------------------------------------------------------------

// URL for onspeed_m5.js: served by MkDocs from
// docs/site/docs/assets/wasm/m5/onspeed_m5.js. The .js + .wasm live
// in a subdir alongside a package.json that pins CommonJS resolution
// — Emscripten emits with EXPORT_ES6=0 so Node-side createRequire
// loaders (e.g. the m5sim-smoke test) need a CJS-typed package
// boundary.
//
// This module file lives at data-and-logs/replay/lib/replay/m5sim.js
// (4 levels deep), so the shared assets/wasm/m5/ root is reached via
// four ".." segments + assets/wasm/m5/.
//
// Compute the URL against import.meta.url so the resolved href has
// the docs-site origin baked in — necessary because the loader
// injects a <script src=...> tag that must be an absolute URL or
// origin-relative path (script src does NOT resolve relative to the
// importing module the way ESM import() does).
const M5SIM_URL = new URL(
  '../../../../assets/wasm/m5/onspeed_m5.js', import.meta.url).href;

let _factoryPromise = null;

// Browser-only: inject a <script> tag, wait for load, return the
// factory captured from globalThis.Module.
function loadFactoryViaScriptTag(url) {
  return new Promise((resolve, reject) => {
    if (typeof document === 'undefined') {
      reject(new Error(
        'M5Sim: no document — call M5Sim.fromFactory(factory) directly ' +
        'in Node-side code.'));
      return;
    }
    const script = document.createElement('script');
    script.src = url;
    script.async = true;
    script.onload = () => {
      // Capture and clear the factory from the global.
      const factory = globalThis.Module;
      if (typeof factory !== 'function') {
        reject(new Error(
          `M5Sim: ${url} did not assign globalThis.Module to a factory ` +
          `(got ${typeof factory}). Was the build target wrong?`));
        return;
      }
      // Defensive: clear so a second load isn't shadowed by the
      // first. Emscripten reuses the global, so a second module
      // could otherwise inherit unrelated state.
      // Use assign-to-undefined rather than `delete` because some
      // Emscripten output sets Module as a non-configurable own
      // property on window, and `delete` throws in strict mode.
      try { globalThis.Module = undefined; } catch (_) { /* ignore */ }
      resolve(factory);
    };
    script.onerror = () => reject(new Error(
      `M5Sim: failed to load ${url} — was the docs-site build hook ` +
      `(docs/site/hooks/copy_wasm.py) run? It copies onspeed_m5.{js,wasm} ` +
      `from software/OnSpeed-M5-Display/sim/build/wasm-replay/ into ` +
      `docs/site/docs/assets/wasm/.`));
    document.head.appendChild(script);
  });
}

// Module-worker path: workers have no `document` so we can't use a
// <script> tag. fetch the Emscripten UMD source, eval it inside a
// `Function` body that captures the `var Module=...` assignment, and
// return the factory.
//
// Emscripten emits the WASM artifact with `EXPORT_ES6=0` (UMD: top-
// level `var Module=...`, then a tail that assigns module.exports /
// AMD define when those globals exist). Inside `new Function(...)`
// the `var Module` becomes function-scoped, so the trailing `return
// Module` expression captures it correctly. The Emscripten file's
// own UMD detection (`typeof exports==="object"`) sees neither
// `exports` nor `module` in the Function-body scope and falls
// through harmlessly.
//
// `locateFile` is set on the factory's options arg so the bundled
// WASM binary loads from the same directory as the .js source.
async function loadFactoryViaFetchEval(url) {
  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(
      `M5Sim: failed to fetch ${url} (HTTP ${res.status}). Was the ` +
      `docs-site build hook (sync_wasm.sh) run?`);
  }
  const src = await res.text();
  // eslint-disable-next-line no-new-func
  const rawFactory = new Function(`${src}\nreturn Module;`)();
  if (typeof rawFactory !== 'function') {
    throw new Error(
      `M5Sim: ${url} did not produce a Module factory (got ${typeof rawFactory}).`);
  }
  // Emscripten's `locateFile` resolves .wasm via `currentScript.src` —
  // but `new Function(...)` has no `currentScript`. Without override,
  // the factory fetches "onspeed_m5.wasm" against the worker's own
  // URL (`/data-and-logs/replay/lib/replay/`), 404. Wrap the factory
  // so callers get a `locateFile` that returns the right URL.
  const baseUrl = new URL('.', url).href;
  return function wrappedFactory(moduleArg = {}) {
    return rawFactory({
      ...moduleArg,
      locateFile: (path, prefix) =>
        // Caller-provided locateFile wins; otherwise resolve relative
        // to the m5sim.js URL we fetched.
        moduleArg.locateFile
          ? moduleArg.locateFile(path, prefix)
          : new URL(path, baseUrl).href,
    });
  };
}

async function loadFactory() {
  if (_factoryPromise) return _factoryPromise;
  // Picks the right loader for the current global. Main thread has
  // `document` → <script>-tag path; classic/module Workers have no
  // `document` → fetch+eval path.
  if (typeof document !== 'undefined') {
    _factoryPromise = loadFactoryViaScriptTag(M5SIM_URL);
  } else {
    _factoryPromise = loadFactoryViaFetchEval(M5SIM_URL);
  }
  return _factoryPromise;
}

// ---------------------------------------------------------------------
// M5Sim — wrapper class.
// ---------------------------------------------------------------------

export class M5Sim {
  // Private; use M5Sim.create() / M5Sim.fromFactory() instead.
  constructor(module) {
    this._m = module;
    this._gHistoryPtr = module._replay_get_gHistory_ptr();
    // setup() ran inside the factory promise; the firmware is now in
    // its post-setup state (loopTime/numbersUpdateTime initialised at
    // virtual t=0).
  }

  /**
   * Default factory: load the WASM module from the dev-server's
   * `/lib/replay/m5sim/` URL. Browser path.
   *
   * @returns {Promise<M5Sim>}
   */
  static async create() {
    const factory = await loadFactory();
    return M5Sim.fromFactory(factory);
  }

  /**
   * Construct from an already-loaded Emscripten factory. Used by the
   * Node smoke test, which loads the factory via `require()` to avoid
   * dynamic-import's URL resolution semantics.
   *
   * @param {Function} factory The Emscripten module factory.
   * @returns {Promise<M5Sim>}
   */
  static async fromFactory(factory) {
    const Module = await factory();
    // Boot the firmware at virtual t=0 before returning. After
    // `replay_init()` runs, all state-var accessors return their
    // post-setup values; advanceTo / injectBytes proceed from there.
    Module._replay_set_time(0n);
    Module._replay_init();
    return new M5Sim(Module);
  }

  /**
   * Load + boot from an explicit URL. The default loader picks
   * <script>-tag (main thread) vs. fetch+eval (Worker) based on
   * `typeof document`. Use this overload to bypass that detection
   * — e.g. a test harness that wants to load from a fixture URL.
   *
   * @param {string} url Absolute or origin-relative URL to onspeed_m5.js.
   * @returns {Promise<M5Sim>}
   */
  static async fromFactoryUrl(url) {
    const factory = (typeof document !== 'undefined')
      ? await loadFactoryViaScriptTag(url)
      : await loadFactoryViaFetchEval(url);
    return M5Sim.fromFactory(factory);
  }

  /**
   * Advance virtual time and run one firmware loop tick.
   *
   * The M5 firmware's `loop()` checks `millis()` against tracked
   * timestamps to fire 50 ms graphics ticks, 500 ms numbers
   * snapshots, and 200 ms gHistory samples. Drive `advanceTo` at the
   * caller's frame rate (typ. 30-60 Hz) — the firmware's internal
   * cadence handles down-sampling.
   *
   * @param {number} virtualMillis Wall-clock-equivalent virtual time
   *   in milliseconds since the firmware's t=0.
   */
  advanceTo(virtualMillis) {
    // Coerce to BigInt for the uint64 ABI. Floor first since the
    // firmware's millis() is integer.
    const m = BigInt(Math.max(0, Math.floor(virtualMillis)));
    this._m._replay_set_time(m);
    this._m._replay_loop();
  }

  /**
   * Push wire bytes through the firmware's `#1` parser. The parser
   * runs synchronously inside `_replay_inject_byte` — on the trailing
   * LF of a valid frame, all SerialRead globals (`PercentLift`,
   * `Slip`, `iVSI`, ...) update before the call returns.
   *
   * @param {Uint8Array} bytes
   */
  injectBytes(bytes) {
    if (!bytes || typeof bytes.length !== 'number') return;
    const inject = this._m._replay_inject_byte;
    for (let i = 0; i < bytes.length; i++) inject(bytes[i]);
  }

  /**
   * Set the active display mode (0..4). Out-of-range values are
   * clamped by the firmware-side accessor.
   *
   * @param {number} mode
   */
  setMode(mode) {
    this._m._replay_set_displayType(mode | 0);
  }

  /**
   * Read every exposed state var into a plain JS object.
   *
   * The returned object is frozen; mutate it and Object.freeze
   * complains. `gHistory` is a copy of the WASM heap (Float32Array
   * length 300) — safe to retain across frames.
   *
   * @returns {object}
   */
  read() {
    const m = this._m;
    // Float32 view of the gHistory ring. Slice so the caller can
    // hold onto it across `advanceTo` calls (which would otherwise
    // mutate it underneath them).
    const histStart = this._gHistoryPtr / 4;
    const gHistory = m.HEAPF32.slice(histStart, histStart + 300);

    const out = {
      // Always populated (every mode reads at least these).
      displayIAS:           m._replay_get_displayIAS(),
      displayPalt:          m._replay_get_displayPalt(),
      displayPitch:         m._replay_get_displayPitch(),
      displayVerticalG:     m._replay_get_displayVerticalG(),
      displayPercentLift:   m._replay_get_displayPercentLift(),
      displayDecelRate:     m._replay_get_displayDecelRate(),
      Slip:                 m._replay_get_Slip(),
      // Unclamped lateralG / verticalG floats — Slip is constrained
      // to ±99 (≈ ±0.116 g), so any rendering pipeline that wants to
      // smooth in continuous space must read these instead of
      // reconstructing from Slip.
      LateralG:             m._replay_get_LateralG(),
      VerticalG:            m._replay_get_VerticalG(),
      PercentLift:          m._replay_get_PercentLift(),
      gOnsetRate:           m._replay_get_gOnsetRate(),
      IAS:                  m._replay_get_IAS(),
      Palt:                 m._replay_get_Palt(),
      IasIsValid:           m._replay_get_IasIsValid() !== 0,
      displayType:          m._replay_get_displayType(),
      iVSI:                 m._replay_get_iVSI(),
      OAT:                  m._replay_get_OAT(),
      FlightPath:           m._replay_get_FlightPath(),
      Pitch:                m._replay_get_Pitch(),
      Roll:                 m._replay_get_Roll(),

      // Mode 0 / Mode 2 anchors.
      TonesOnPctLift:       m._replay_get_TonesOnPctLift(),
      OnSpeedFastPctLift:   m._replay_get_OnSpeedFastPctLift(),
      OnSpeedSlowPctLift:   m._replay_get_OnSpeedSlowPctLift(),
      StallWarnPctLift:     m._replay_get_StallWarnPctLift(),
      PipPctLift:           m._replay_get_PipPctLift(),
      FlapsMinDeg:          m._replay_get_FlapsMinDeg(),
      FlapsMaxDeg:          m._replay_get_FlapsMaxDeg(),
      FlapPos:              m._replay_get_FlapPos(),

      // Mode 4 (Historic G).
      gHistoryIndex:        m._replay_get_gHistoryIndex(),
      gHistory,

      // Spin / DataMark.
      SpinRecoveryCue:      m._replay_get_SpinRecoveryCue(),
      DataMark:             m._replay_get_DataMark(),
    };
    return Object.freeze(out);
  }

  /**
   * Tear down. Defensive: Emscripten WASM modules don't need explicit
   * teardown for our usage, but callers that load and unload sims
   * (e.g. tests) can call this to drop the reference.
   */
  delete() {
    this._m = null;
    this._gHistoryPtr = 0;
  }
}
