#!/usr/bin/env bash
# sync_wasm.sh — copy built WASM artifacts into docs/site/docs/assets/wasm/
# so MkDocs picks them up at build time.
#
# Run this AFTER building the artifacts:
#   bash software/Libraries/onspeed_core/wasm/build_wasm.sh
#   bash software/OnSpeed-M5-Display/sim/build_wasm.sh --target replay
#
# Then this:
#   bash docs/site/scripts/sync_wasm.sh
#
# Followed by mkdocs build / mkdocs serve as usual.
#
# The CI workflow (.github/workflows/docs.yml) runs all three in
# sequence — see "Build WASM" / "Sync WASM into docs" jobs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

CORE_JS="${REPO_ROOT}/software/Libraries/onspeed_core/wasm/dist/onspeed_core.js"
M5_JS="${REPO_ROOT}/software/OnSpeed-M5-Display/sim/build/wasm-replay/onspeed_m5.js"
M5_WASM="${REPO_ROOT}/software/OnSpeed-M5-Display/sim/build/wasm-replay/onspeed_m5.wasm"

DST="${REPO_ROOT}/docs/site/docs/assets/wasm"
M5_SUBDIR="${DST}/m5"

for f in "${CORE_JS}" "${M5_JS}" "${M5_WASM}"; do
    if [[ ! -f "${f}" ]]; then
        echo "FAIL: missing ${f}" >&2
        echo "      Run the build scripts first (see header comment)." >&2
        exit 1
    fi
done

mkdir -p "${DST}" "${M5_SUBDIR}"
cp "${CORE_JS}" "${DST}/"
# M5 artifacts live in a subdir alongside a package.json that pins
# CommonJS resolution. Emscripten emits onspeed_m5.js with
# EXPORT_ES6=0, so Node's createRequire (used by the smoke test)
# needs the parent directory to be type=commonjs.
cp "${M5_JS}"   "${M5_SUBDIR}/"
cp "${M5_WASM}" "${M5_SUBDIR}/"

echo "Synced WASM artifacts:"
echo "  onspeed_core.js → ${DST}/"
echo "  onspeed_m5.{js,wasm} → ${M5_SUBDIR}/"
ls -lh "${DST}/" "${M5_SUBDIR}/"
