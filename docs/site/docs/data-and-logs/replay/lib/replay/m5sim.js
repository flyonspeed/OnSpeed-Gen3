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
// In the bundled docs-site deployment:
//   replay-bundle.js   → data-and-logs/replay/replay-bundle.js
//   onspeed_m5.js      → assets/wasm/m5/onspeed_m5.js
// so the WASM lives at __replayBundleBase + '../../assets/wasm/m5/…'.
// scripts/build_replay.mjs seeds `window.__replayBundleBase` from
// `document.currentScript.src` in the bundle preamble. We prefer
// reading it from there rather than `import.meta.url` because under
// esbuild's IIFE output, import.meta is empty (the bundle itself isn't
// a module). The Node smoke test imports this file as ESM, so the
// fallback to import.meta.url keeps that path working unchanged.
const M5SIM_URL = (typeof window !== 'undefined'
                   && window.__replayBundleBase)
  ? new URL('../../assets/wasm/m5/onspeed_m5.js',
            window.__replayBundleBase).href
  : new URL('../../../../assets/wasm/m5/onspeed_m5.js',
            import.meta.url).href;

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

async function loadFactory() {
  if (_factoryPromise) return _factoryPromise;
  _factoryPromise = loadFactoryViaScriptTag(M5SIM_URL);
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
