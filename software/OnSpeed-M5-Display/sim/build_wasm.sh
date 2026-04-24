#!/usr/bin/env bash
# build_wasm.sh — build the M5 display simulator for the browser.
#
# Emscripten's `-sUSE_SDL=2` port reuses the SDL2 code path M5GFX's
# Panel_sdl backend already relies on — the same firmware binary that
# runs on the macOS/Linux native target also compiles straight to
# WebAssembly. The DUMMY_SERIAL_DATA flag in COMMON_FLAGS drives the
# display from a synthetic data source (a ramping AOA that sweeps the
# whole tone map) so the sim produces visible output with no external
# data feed.
#
# Output lands under sim/build/wasm/: an index.html loader, a .js
# driver, and a .wasm module. `python3 -m http.server 8080` from that
# directory serves the sim to a browser.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${PROJECT_DIR}/../.." && pwd)"

OUT_DIR="${SCRIPT_DIR}/build/wasm"
mkdir -p "${OUT_DIR}"

# M5Unified and M5GFX are installed by pio into .pio/libdeps/<env>/. We
# reuse the copies from the native env so the build works without a
# second install.  If you haven't built native yet, do that first
# (`pio run -e native` from the M5 display directory).
M5U_DIR="${PROJECT_DIR}/.pio/libdeps/native/M5Unified"
M5G_DIR="${PROJECT_DIR}/.pio/libdeps/native/M5GFX"

if [[ ! -d "${M5U_DIR}" || ! -d "${M5G_DIR}" ]]; then
    echo "ERROR: M5Unified / M5GFX not found at ${M5U_DIR}" >&2
    echo "       Run 'pio run -e native' once first, then retry." >&2
    exit 1
fi

# Regenerate buildinfo.cpp so version string matches current git state.
# We call the generator in "standalone" mode (it accepts a -o arg).
python3 "${REPO_ROOT}/scripts/generate_buildinfo.py" \
    --output "${REPO_ROOT}/software/Libraries/version/buildinfo.cpp"

ONSPEED_CORE_DIR="${REPO_ROOT}/software/Libraries/onspeed_core/src"
VERSION_DIR="${REPO_ROOT}/software/Libraries/version"

# Collect M5GFX SDL / Sprite / font sources. Emscripten dead-strips what
# isn't referenced, so the wide glob is fine. We split into C vs C++
# buckets because emcc applies any single -std flag uniformly — mixing
# -std=gnu++17 with .c files errors out.
M5G_CXX_SOURCES=(
    "${M5G_DIR}/src/M5GFX.cpp"
)
M5G_C_SOURCES=()
while IFS= read -r -d '' f; do M5G_CXX_SOURCES+=("$f"); done < <(
    find "${M5G_DIR}/src/lgfx/v1" \
         -name '*.cpp' \
         ! -path '*/platforms/esp32/*' \
         ! -path '*/platforms/samd*' \
         ! -path '*/platforms/arduino_default/*' \
         ! -path '*/platforms/opencv/*' \
         ! -path '*/platforms/framebuffer/*' \
         ! -path '*/platforms/rp2040/*' \
         ! -path '*/platforms/stm32/*' \
         ! -path '*/platforms/spresense/*' \
         ! -path '*/platforms/wiced/*' \
         ! -path '*/platforms/ra6m5/*' \
         -print0)
while IFS= read -r -d '' f; do M5G_C_SOURCES+=("$f"); done < <(
    find "${M5G_DIR}/src/lgfx/v1" \
         -name '*.c' \
         ! -path '*/platforms/esp32/*' \
         ! -path '*/platforms/samd*' \
         ! -path '*/platforms/arduino_default/*' \
         ! -path '*/platforms/opencv/*' \
         ! -path '*/platforms/framebuffer/*' \
         ! -path '*/platforms/rp2040/*' \
         ! -path '*/platforms/stm32/*' \
         ! -path '*/platforms/spresense/*' \
         ! -path '*/platforms/wiced/*' \
         ! -path '*/platforms/ra6m5/*' \
         -print0)
while IFS= read -r -d '' f; do M5G_CXX_SOURCES+=("$f"); done < <(
    find "${M5G_DIR}/src/lgfx/Fonts" -name '*.cpp' -print0)
while IFS= read -r -d '' f; do M5G_C_SOURCES+=("$f"); done < <(
    find "${M5G_DIR}/src/lgfx/Fonts" -name '*.c' -print0)
while IFS= read -r -d '' f; do M5G_CXX_SOURCES+=("$f"); done < <(
    find "${M5G_DIR}/src/lgfx/utility" -name '*.cpp' -print0)
while IFS= read -r -d '' f; do M5G_C_SOURCES+=("$f"); done < <(
    find "${M5G_DIR}/src/lgfx/utility" -name '*.c' -print0)

# M5Unified — limit to top-level + utility (no touch driver / IMU / mic
# hardware we don't emulate; the PC_BUILD guards short-circuit those).
M5U_SOURCES=(
    "${M5U_DIR}/src/M5Unified.cpp"
)
while IFS= read -r -d '' f; do M5U_SOURCES+=("$f"); done < <(
    find "${M5U_DIR}/src/utility" -name '*.cpp' -print0)

# onspeed_core sources (platform-independent, already WASM-compatible).
ONSPEED_SOURCES=()
while IFS= read -r -d '' f; do ONSPEED_SOURCES+=("$f"); done < <(
    find "${ONSPEED_CORE_DIR}" -name '*.cpp' -print0)

FW_SOURCES=(
    "${PROJECT_DIR}/src/main.cpp"
    "${PROJECT_DIR}/src/SerialRead.cpp"
    "${PROJECT_DIR}/lib/GaugeWidgets/GaugeWidgets.cpp"
    "${SCRIPT_DIR}/SimMain.cpp"
    "${VERSION_DIR}/buildinfo.cpp"
)

INCLUDES=(
    -I"${PROJECT_DIR}/include"
    -I"${PROJECT_DIR}/src"
    -I"${PROJECT_DIR}/lib/GaugeWidgets"
    -I"${M5U_DIR}/src"
    -I"${M5G_DIR}/src"
    -I"${ONSPEED_CORE_DIR}"
    -I"${REPO_ROOT}/software/Libraries/tinyxml2"
    -I"${VERSION_DIR}"
)

COMMON_FLAGS=(
    -O2
    -DDUMMY_SERIAL_DATA
    -DM5GFX_BACK_COLOR=0x000000u
    -DM5GFX_SCALE=1
)

CXXFLAGS=(
    "${COMMON_FLAGS[@]}"
    -std=gnu++17
)

CFLAGS=(
    "${COMMON_FLAGS[@]}"
    -std=gnu11
)

EM_FLAGS=(
    # SDL2 via Emscripten's built-in port (HTML5-canvas backed).
    -sUSE_SDL=2
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=131072
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864
    -o "${OUT_DIR}/index.html"
    --shell-file "${SCRIPT_DIR}/wasm_shell.html"
)

# Some onspeed_core sources reference <Arduino.h> transitively (via
# includes from the sketch). The shim is already active on non-ESP
# builds via #ifdef — but we preempt any stray include by adding the
# sim dir to the include path so `#include "../sim/ArduinoShim.h"`
# from GaugeWidgets.h resolves.

echo "[wasm] Compiling ${#FW_SOURCES[@]} firmware + $((${#M5G_CXX_SOURCES[@]}+${#M5G_C_SOURCES[@]})) M5GFX + ${#M5U_SOURCES[@]} M5Unified + ${#ONSPEED_SOURCES[@]} onspeed_core sources..."

# tinyxml2 dependency (onspeed_core links it in via proto/LogCsv.cpp ->
# types/ChordedXml -> ...; see .gitmodules).
TINYXML_SRC="${REPO_ROOT}/software/Libraries/tinyxml2/tinyxml2.cpp"
if [[ -f "${TINYXML_SRC}" ]]; then
    ONSPEED_SOURCES+=("${TINYXML_SRC}")
fi

# Step 1: compile the C-only fonts/utility as objects into a staging dir.
OBJ_DIR="${OUT_DIR}/obj"
mkdir -p "${OBJ_DIR}"

if [[ ${#M5G_C_SOURCES[@]} -gt 0 ]]; then
    echo "[wasm]  - C sources (${#M5G_C_SOURCES[@]} files)"
    for src in "${M5G_C_SOURCES[@]}"; do
        out="${OBJ_DIR}/$(echo "${src}" | shasum -a 1 | cut -d' ' -f1).o"
        emcc -c "${CFLAGS[@]}" "${INCLUDES[@]}" "${src}" -o "${out}"
    done
fi

# Step 2: compile + link the C++ sources (firmware, M5 libs, fonts in .cpp,
# onspeed_core, tinyxml2) along with the pre-built C objects.
echo "[wasm]  - C++ sources + link"
C_OBJS=("${OBJ_DIR}"/*.o)

emcc \
    "${CXXFLAGS[@]}" \
    "${INCLUDES[@]}" \
    "${EM_FLAGS[@]}" \
    "${FW_SOURCES[@]}" \
    "${M5G_CXX_SOURCES[@]}" \
    "${M5U_SOURCES[@]}" \
    "${ONSPEED_SOURCES[@]}" \
    "${C_OBJS[@]}"

echo ""
echo "[wasm] Build complete."
echo "  Output: ${OUT_DIR}/index.html"
echo "  Serve:  python3 -m http.server --directory ${OUT_DIR} 8080"
echo "  Open:   http://localhost:8080/"
