// wasm_core.js — single-loader for the onspeed_core WASM module.
//
// The module is loaded once on first call and cached for the lifetime of
// the page.  All replay-tool algorithm files import getWasmCore() from
// here rather than loading the module themselves.
//
// The WASM artifact is built by:
//   bash software/Libraries/onspeed_core/wasm/build_wasm.sh
// and served at /static/onspeed_core/onspeed_core.js by the dev-server.
//
// Callers:
//   import { getWasmCore } from './wasm_core.js';
//   const w = await getWasmCore();
//   const pct = w.compute_percent_lift(aoaDeg, alpha0, alphaStall, stallwarn, iasValid);

// Path at which the dev-server (and the future static deploy) serves the
// WASM artifact.  The build script outputs a SINGLE_FILE bundle: the .wasm
// is inlined as base64 in the .js, so only one import is needed.
const WASM_MODULE_URL = '/static/onspeed_core/onspeed_core.js';

let _module = null;
let _initPromise = null;

// Return the initialized WASM module instance.  Loads the module once on
// first call; subsequent calls return the cached instance immediately.
// Throws if the artifact is not available (build has not been run).
export async function getWasmCore() {
    if (_module) return _module;
    if (!_initPromise) {
        _initPromise = (async () => {
            // Dynamic import so missing artifacts produce a clear error rather
            // than a parse-time failure.  The dev-server must serve the
            // artifact at WASM_MODULE_URL (see dev-server/server.mjs).
            const mod = await import(/* @vite-ignore */ WASM_MODULE_URL);
            const factory = mod.default ?? mod;
            _module = await factory();
            return _module;
        })();
    }
    return _initPromise;
}
