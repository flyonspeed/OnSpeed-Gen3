// wasm_core.js — single-loader for the onspeed_core WASM module.
//
// The module is loaded once on first call and cached for the lifetime of
// the page. All replay-tool algorithm files import getWasmCore() from
// here rather than loading the module themselves.
//
// The WASM artifact is built by:
//   bash software/Libraries/onspeed_core/wasm/build_wasm.sh
// and copied to docs/site/docs/assets/wasm/onspeed_core.js by the
// docs-site build hook (see docs/site/hooks/copy_wasm.py).
//
// Path is relative to this module file (lib/replay/wasm_core.js):
//   start  : data-and-logs/replay/lib/replay/wasm_core.js
//   target : assets/wasm/onspeed_core.js
// Go up four levels (lib/ → replay/ → data-and-logs/ → docs root),
// then down into assets/wasm/.
//
// Callers:
//   import { getWasmCore } from './wasm_core.js';
//   const w = await getWasmCore();

const WASM_MODULE_URL = '../../../../assets/wasm/onspeed_core.js';

let _module = null;
let _initPromise = null;

async function getWasmCore() {
    if (_module) return _module;
    if (!_initPromise) {
        _initPromise = (async () => {
            // Dynamic import with a path resolved against the importing
            // module (the lib/replay/ subdirectory). The replay app lives
            // at .../replay/index.html, so this resolves against
            // .../replay/lib/replay/wasm_core.js → up two to
            // .../replay/, then across to .../assets/wasm/.
            // import.meta.url gives us the file URL; build the absolute
            // URL from there.
            const here = new URL(import.meta.url);
            const wasmUrl = new URL(WASM_MODULE_URL, here).href;
            const mod = await import(/* @vite-ignore */ wasmUrl);
            const factory = mod.default ?? mod;
            _module = await factory();
            return _module;
        })();
    }
    return _initPromise;
}

export { getWasmCore };
