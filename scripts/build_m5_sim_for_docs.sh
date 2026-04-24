#!/usr/bin/env bash
# Build the M5 display WebAssembly simulator and copy its artifacts
# into the docs site's assets tree so mkdocs-material can serve them.
#
# Called from .github/workflows/docs.yml before `mkdocs build`.
# Safe to run locally too: regenerates sim/build/wasm/ and the copies
# under docs/site/docs/assets/sim/.
#
# Prerequisites (CI sets these up via workflow steps):
# - emscripten (emcc in PATH)
# - platformio (pio in PATH)
# - python3
#
# Exits non-zero on any step failure so CI stops before deploying a
# docs site with a broken sim embed.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
M5_DIR="${REPO_ROOT}/software/OnSpeed-M5-Display"
SIM_DIR="${M5_DIR}/sim"
DOCS_DEST="${REPO_ROOT}/docs/site/docs/assets/sim"

# build_wasm.sh pulls M5Unified + M5GFX from the native env's libdeps
# cache; populate that without running the full native compile (we
# only need the headers to be extracted by pio's lib manager).
echo "[docs-sim] Installing native env libraries..."
pio pkg install -e native --project-dir "${M5_DIR}" --silent

echo "[docs-sim] Building WASM bundle..."
"${SIM_DIR}/build_wasm.sh"

echo "[docs-sim] Copying artifacts to ${DOCS_DEST}..."
mkdir -p "${DOCS_DEST}"
# Only the distributable files — skip obj/ intermediates and
# any stray CSV that might get bundled in future.
cp "${SIM_DIR}/build/wasm/index.html" "${DOCS_DEST}/"
cp "${SIM_DIR}/build/wasm/index.js"   "${DOCS_DEST}/"
cp "${SIM_DIR}/build/wasm/index.wasm" "${DOCS_DEST}/"

echo "[docs-sim] Done. Bundle sizes:"
ls -lh "${DOCS_DEST}/"
