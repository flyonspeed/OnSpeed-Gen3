// wasm_core.js — single-loader for the onspeed_core WASM module.
//
// The module is loaded once on first call and cached for the lifetime of
// the page. All replay-tool algorithm files import getWasmCore() from
// here rather than loading the module themselves.
//
// The WASM artifact is built by:
//   bash software/Libraries/onspeed_core/wasm/build_wasm.sh
// and copied to docs/site/docs/assets/wasm/onspeed_core.js by the
// docs-site build hook (see docs/site/scripts/sync_wasm.sh).
//
// Callers:
//   import { getWasmCore } from './wasm_core.js';
//   const w = await getWasmCore();
//
// Load mechanism — browser path:
//   onspeed_core.js is emitted by Emscripten with EXPORT_ES6=1, so it's
//   an ES module with `export default Module`. The bundled replay code
//   runs as a classic-script IIFE (esbuild format=iife), so we can't
//   call `import.meta.url` and we can't expose the factory via a plain
//   <script> tag (an ES module wouldn't attach to a global).
//
//   The pattern: inject a `<script type="module">` with inline source
//   that imports the factory and stores it on the global. We poll for
//   the global to appear, then use it. This mirrors m5sim.js's
//   script-tag injection pattern (which can use a plain <script>
//   because the M5 WASM is EXPORT_ES6=0).
//
// Load mechanism — Node smoke-test path:
//   The Node smoke test imports this file directly with ESM resolution.
//   `window` is undefined, so we fall back to dynamic `import()` with
//   `import.meta.url`. Node ESM `import()` of `onspeed_core.js` works
//   because the file is itself an ES module.

const WASM_MODULE_REL = '../../assets/wasm/onspeed_core.js';

let _module = null;
let _initPromise = null;

// Resolve the absolute URL of onspeed_core.js. In a browser (bundled
// path), we resolve against `window.__replayBundleBase` — set by the
// bundle preamble in scripts/build_replay.mjs from
// `document.currentScript.src`. Outside a browser (Node smoke test),
// we fall back to `import.meta.url`, which still works because Node
// loads this file as ESM.
function _wasmCoreUrl() {
    if (typeof window !== 'undefined' && window.__replayBundleBase) {
        return new URL(WASM_MODULE_REL, window.__replayBundleBase).href;
    }
    // Node-side ESM resolution. Source layout: lib/replay/wasm_core.js
    // → up four levels lands at the docs root; from there assets/wasm/.
    return new URL('../../../../assets/wasm/onspeed_core.js',
                   import.meta.url).href;
}

// Browser path: inject a <script type="module"> whose source imports
// the factory and attaches it to the global. Polling for the global
// instead of `script.onload` because module loading is asynchronous
// from the load event's perspective on some browsers.
function _loadFactoryViaModuleScript(url) {
    return new Promise((resolve, reject) => {
        if (typeof document === 'undefined') {
            reject(new Error(
                'wasm_core: no document — Node callers should reach the ' +
                'dynamic-import path, not this one.'));
            return;
        }
        const globalKey = '__onspeedCoreFactory';
        // If a previous load left the factory attached, reuse it.
        if (typeof window[globalKey] === 'function') {
            resolve(window[globalKey]);
            return;
        }
        // Inline module: import the default export and stash it on
        // window. JSON-stringify the URL to avoid any quoting trouble.
        const src =
            `import factory from ${JSON.stringify(url)};` +
            `window.${globalKey} = factory;` +
            `window.dispatchEvent(new Event('onspeed-core-factory-ready'));`;
        const script = document.createElement('script');
        script.type = 'module';
        script.textContent = src;
        // Reject on script error (parse / network / import failure).
        script.onerror = () => reject(new Error(
            `wasm_core: failed to load ${url} — was the docs-site sync ` +
            `(docs/site/scripts/sync_wasm.sh) run? It copies ` +
            `onspeed_core.js into docs/site/docs/assets/wasm/.`));
        let settled = false;
        function settle(ok, errOrValue) {
            if (settled) return;
            settled = true;
            window.removeEventListener(
                'onspeed-core-factory-ready', onReady);
            if (ok) resolve(errOrValue);
            else reject(errOrValue);
        }
        function onReady() {
            const f = window[globalKey];
            if (typeof f === 'function') settle(true, f);
            else settle(false, new Error(
                `wasm_core: ${url} loaded but window.${globalKey} is ` +
                `${typeof f}; expected a factory function.`));
        }
        window.addEventListener('onspeed-core-factory-ready', onReady,
                                 { once: true });
        document.head.appendChild(script);
    });
}

async function getWasmCore() {
    if (_module) return _module;
    if (!_initPromise) {
        _initPromise = (async () => {
            const url = _wasmCoreUrl();
            let factory;
            if (typeof window !== 'undefined') {
                factory = await _loadFactoryViaModuleScript(url);
            } else {
                // Node smoke-test path: dynamic ESM import.
                const mod = await import(url);
                factory = mod.default ?? mod;
            }
            _module = await factory();
            return _module;
        })();
    }
    return _initPromise;
}

export { getWasmCore };
