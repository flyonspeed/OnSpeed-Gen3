#!/usr/bin/env node
// build_replay.mjs — esbuild-based replay bundle build.
//
// Bundles docs/site/docs/data-and-logs/replay/replay-entry.js into a
// single self-contained IIFE that sits next to replay.md. The deployed
// page loads it with `<script src="replay-bundle.js"></script>` — a
// sibling-relative URL that survives mike's per-version `/latest/` path
// prefix without counting `..`s by hand.
//
// This script replaces the regex-based Python bundler for the replay
// target only. The firmware target (PROGMEM byte-array C headers)
// remains on the Python path; esbuild can't emit those.
//
// Why we needed real ESM tooling (issue #547):
//   The Python regex bundler didn't namespace bare `function`/`var`
//   declarations across files. Same-named top-level helpers from
//   different modules collided silently (e.g. `buildBoxPath`,
//   `formatHms`, `safeLsGet`/`safeLsSet`) — last-write-wins, with no
//   build warning. esbuild wraps every source module in its own scope
//   so these collisions can't happen.
//
// Usage:
//   node scripts/build_replay.mjs
//
// Invoked indirectly via:
//   python3 scripts/build_web_bundle.py --target replay
//   (the Python script shells out to this one)

import * as esbuild from 'esbuild';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const REPO_ROOT = path.resolve(
  path.dirname(url.fileURLToPath(import.meta.url)), '..');

const REPLAY_DIR = path.join(
  REPO_ROOT, 'docs', 'site', 'docs', 'data-and-logs', 'replay');
const ENTRY = path.join(REPLAY_DIR, 'replay-entry.js');
const OUTFILE = path.join(REPLAY_DIR, 'replay-bundle.js');

// Path alias: replay-tool source imports the shared UI library via the
// virtual `docs/site/docs/packages/` path (a symlink in the source tree
// to `<repo>/packages/`). Rewrite import resolution so esbuild can find
// the modules even when the symlink isn't present (e.g. on a fresh
// checkout where the bundle is rebuilt before any docs hook runs).
const VIRTUAL_PACKAGES_PREFIX = path.join(
  REPO_ROOT, 'docs', 'site', 'docs', 'packages') + path.sep;
const REAL_PACKAGES_PREFIX = path.join(REPO_ROOT, 'packages') + path.sep;

/**
 * esbuild plugin: rewrite `<repo>/docs/site/docs/packages/...` paths
 * to `<repo>/packages/...` during resolution.
 *
 * Implementation: a generic onResolve that runs ahead of the default
 * resolver. We compute the resolved path the way Node would (relative
 * to args.importer's dir), check whether it falls under the virtual
 * prefix, and if so rewrite to the real prefix. Anything else, we
 * return `undefined` so esbuild's default resolver runs.
 *
 * The filter is `/.*\//` so it matches only relative-path imports
 * (those containing a slash). Bare-package imports (e.g. `import
 * "preact"`) don't match and fall through to the default resolver.
 */
const PACKAGES_ALIAS_PLUGIN = {
  name: 'packages-symlink-alias',
  setup(build) {
    build.onResolve({ filter: /^\.\.?\// }, async (args) => {
      // Resolve the relative import against the importer's directory
      // ourselves; we need the absolute path to check against the
      // virtual prefix. esbuild's default resolver would also find it,
      // but only after symlink traversal — we want the rewrite to
      // happen regardless of whether the symlink exists.
      const importerDir = path.dirname(args.importer);
      const raw = path.resolve(importerDir, args.path);
      if (raw.startsWith(VIRTUAL_PACKAGES_PREFIX)) {
        const rewritten =
          REAL_PACKAGES_PREFIX + raw.slice(VIRTUAL_PACKAGES_PREFIX.length);
        return { path: rewritten };
      }
      // Not under the virtual prefix — let esbuild's default resolver
      // handle it.
      return undefined;
    });
  },
};

// Banner emitted before the IIFE body. Sets a global
// `window.__replayBundleBase` from `document.currentScript.src`, used
// by the WASM-loader modules (m5sim.js, wasm_core.js) to build
// absolute URLs to their assets. Same convention as the Python
// bundler's preamble.
const BANNER = `
(function () {
  // __replayBundleBase: URL of the directory containing this bundle.
  // WASM-loader modules read this global to build absolute URLs to
  // their assets (e.g. /assets/wasm/m5/onspeed_m5.js). Under esbuild's
  // IIFE format, import.meta.url resolves to the bundle's own URL
  // (not the source file's URL), which would make the relative-path
  // math wrong; the WASM-loader modules consult this global instead.
  try {
    var s = document.currentScript && document.currentScript.src;
    if (s) {
      window.__replayBundleBase = s.substring(0, s.lastIndexOf('/') + 1);
    }
  } catch (_) { /* ignore */ }
  if (!window.__replayBundleBase) {
    window.__replayBundleBase = location.href.replace(/\\/[^\\/]*$/, '/');
  }
})();
`.trim();

async function main() {
  await esbuild.build({
    entryPoints: [ENTRY],
    outfile: OUTFILE,
    bundle: true,
    format: 'iife',
    target: ['chrome100', 'firefox100', 'safari15'],
    banner: { js: BANNER },
    plugins: [PACKAGES_ALIAS_PLUGIN],
    logLevel: 'warning',
    // m5sim.js / wasm_core.js reference `import.meta.url` inside a
    // Node-only fallback branch (`typeof window === 'undefined'`).
    // esbuild's static scan can't see the runtime guard, so it warns;
    // silence it — the dead-on-browser branch is intentional, and the
    // bundle is never loaded in Node (the smoke tests import the
    // un-bundled source).
    logOverride: { 'empty-import-meta': 'silent' },
    // Loaded as a classic script (not type=module), so source maps
    // would need a corresponding //# sourceMappingURL comment.
    // Off for now — turn on if a debugging session needs it.
    sourcemap: false,
  });
  const stats = await fs.stat(OUTFILE);
  console.log(`build_replay: replay-bundle.js=${stats.size.toLocaleString()}`);
}

main().catch((err) => {
  console.error('build_replay: FAILED');
  console.error(err && err.stack ? err.stack : err);
  process.exit(1);
});
