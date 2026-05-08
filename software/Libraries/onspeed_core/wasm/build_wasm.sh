#!/usr/bin/env bash
# build_wasm.sh — compile onspeed_core to WebAssembly (algorithm-only, no rendering).
#
# Produces:
#   dist/onspeed_core.js   — ES-module loader (~150 KB)
#   dist/onspeed_core.wasm — compiled algorithms (~300 KB)
#
# Usage:
#   bash software/Libraries/onspeed_core/wasm/build_wasm.sh
#
# Prerequisites:
#   - emcc in PATH (Emscripten 4.0.21 or later)
#   - python3 in PATH (for generate_buildinfo.py)
#
# This build is a strict subset of software/OnSpeed-M5-Display/sim/build_wasm.sh:
# same onspeed_core sources, no M5GFX, no SDL2, no rendering.
#
# The --bind / embind flag exposes C++ functions declared in bindings.cpp
# to JavaScript.  Step 0 exports compute_percent_lift; subsequent PRs
# extend bindings.cpp with more of the algorithm surface.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

ONSPEED_CORE_DIR="${REPO_ROOT}/software/Libraries/onspeed_core/src"
TINYXML2_SRC="${REPO_ROOT}/software/Libraries/tinyxml2/tinyxml2.cpp"
VERSION_DIR="${REPO_ROOT}/software/Libraries/version"
OUT_DIR="${SCRIPT_DIR}/dist"
mkdir -p "${OUT_DIR}"

# Regenerate buildinfo.cpp so version string reflects current git state.
if [[ -f "${REPO_ROOT}/scripts/generate_buildinfo.py" && -d "${VERSION_DIR}" ]]; then
    python3 "${REPO_ROOT}/scripts/generate_buildinfo.py" \
        --output "${VERSION_DIR}/buildinfo.cpp" 2>/dev/null || true
fi

# Collect all onspeed_core sources.
SOURCES=()
while IFS= read -r -d '' f; do SOURCES+=("$f"); done < <(
    find "${ONSPEED_CORE_DIR}" -name '*.cpp' -print0 | sort -z)

# tinyxml2 is required by onspeed_core/proto/LogCsv.cpp and config parsers.
if [[ -f "${TINYXML2_SRC}" ]]; then
    SOURCES+=("${TINYXML2_SRC}")
fi

# bindings.cpp — exposes onspeed_core C++ to JS via embind.
SOURCES+=("${SCRIPT_DIR}/bindings.cpp")

echo "[wasm] Compiling ${#SOURCES[@]} sources (onspeed_core + tinyxml2 + bindings)..."

# -O3              — full optimization; keeps the bundle small.
# -std=gnu++17     — matches the firmware build (platformio.ini build_src_flags).
# -s MODULARIZE=1  — wraps the module in a factory function; avoids polluting
#                    the global scope and allows async init.
# -s EXPORT_ES6=1  — emit an ES module so the smoke test can do `import`.
# -s ALLOW_MEMORY_GROWTH=1 — algorithms are small; growth guard is cheap insurance.
# -s SINGLE_FILE=1 — inline the .wasm into the .js as a base64 blob so the
#                    deploy is one file (simpler hosting, one HTTP request).
# --bind           — enable Embind so EMSCRIPTEN_BINDINGS in bindings.cpp work.
# -s FILESYSTEM=0  — no FS emulation; algorithms don't touch files at runtime.

emcc \
    -O3 \
    -std=gnu++17 \
    -I"${ONSPEED_CORE_DIR}" \
    -I"${REPO_ROOT}/software/Libraries/tinyxml2" \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s SINGLE_FILE=1 \
    -s FILESYSTEM=0 \
    --bind \
    "${SOURCES[@]}" \
    -o "${OUT_DIR}/onspeed_core.js"

echo ""
echo "[wasm] Build complete."
echo "  Output: ${OUT_DIR}/onspeed_core.js  (WASM inlined as base64)"
if [[ -f "${OUT_DIR}/onspeed_core.js" ]]; then
    SIZE=$(wc -c < "${OUT_DIR}/onspeed_core.js")
    SIZE_KB=$(( SIZE / 1024 ))
    echo "  Size:   ${SIZE_KB} KB"
fi
