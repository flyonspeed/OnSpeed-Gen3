#!/usr/bin/env bash
# build_wasm.sh — build the M5 display simulator for the browser.
#
# Three targets:
#
#   ./build_wasm.sh                  (or --target docs)
#       The docs-site embed. Drives the display from a synthetic AOA
#       ramp baked in via -DDUMMY_SERIAL_DATA. Output: sim/build/wasm/.
#
#   ./build_wasm.sh --target live
#       The on-device embed. No DUMMY_SERIAL_DATA; instead exports
#       _InjectSerialByte so JS can pipe live #1-protocol frames in
#       from the firmware's WebSocket. Output: sim/build/wasm-live/.
#
#   ./build_wasm.sh --target replay
#       The replay-tool engine. Compiles the M5 firmware as a state
#       machine driven by JS: virtual `millis()`/`micros()` instead of
#       wall clock, no SDL2, no canvas — the firmware's gdraw sprite is
#       allocated but never blitted. JS injects #1 wire bytes and reads
#       state-var accessors (replay_get_displayIAS, replay_get_Slip,
#       etc.). Output: sim/build/wasm-replay/onspeed_m5.{js,wasm} →
#       copied to tools/web/lib/replay/m5sim/.
#
# Emscripten's `-sUSE_SDL=2` port reuses the SDL2 code path M5GFX's
# Panel_sdl backend already relies on — the same firmware binary that
# runs on the macOS/Linux native target also compiles straight to
# WebAssembly. The `replay` target deliberately does not link SDL2;
# its `lgfx::v1::millis/micros` symbols are provided by ReplayMain.cpp.

set -euo pipefail

TARGET="docs"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            TARGET="$2"
            shift 2
            ;;
        --target=*)
            TARGET="${1#--target=}"
            shift
            ;;
        *)
            echo "Unknown arg: $1" >&2
            echo "Usage: $0 [--target docs|live|replay]" >&2
            exit 2
            ;;
    esac
done

case "${TARGET}" in
    docs|live|replay) ;;
    *) echo "Invalid target: ${TARGET} (expected docs, live, or replay)" >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${PROJECT_DIR}/../.." && pwd)"

if [[ "${TARGET}" == "live" ]]; then
    OUT_DIR="${SCRIPT_DIR}/build/wasm-live"
elif [[ "${TARGET}" == "replay" ]]; then
    OUT_DIR="${SCRIPT_DIR}/build/wasm-replay"
else
    OUT_DIR="${SCRIPT_DIR}/build/wasm"
fi
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
#
# Replay target additionally excludes the sdl/ platform sources: that
# build provides its own lgfx::millis/micros from ReplayMain.cpp and
# does not link SDL2.
SDL_EXCLUDE=()
if [[ "${TARGET}" == "replay" ]]; then
    SDL_EXCLUDE=(! -path '*/platforms/sdl/*')
fi
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
         "${SDL_EXCLUDE[@]}" \
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
         "${SDL_EXCLUDE[@]}" \
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
    "${PROJECT_DIR}/src/SettingsMenu.cpp"
    "${PROJECT_DIR}/lib/MenuModel/MenuModel.cpp"
    "${PROJECT_DIR}/lib/GaugeWidgets/GaugeWidgets.cpp"
    "${VERSION_DIR}/buildinfo.cpp"
)
if [[ "${TARGET}" == "replay" ]]; then
    FW_SOURCES+=(
        "${SCRIPT_DIR}/ReplayMain.cpp"
        "${SCRIPT_DIR}/ReplayStubs.cpp"
    )
else
    FW_SOURCES+=("${SCRIPT_DIR}/SimMain.cpp")
fi

INCLUDES=(
    -I"${PROJECT_DIR}/include"
    -I"${PROJECT_DIR}/src"
    -I"${PROJECT_DIR}/lib/GaugeWidgets"
    -I"${PROJECT_DIR}/lib/MenuModel"
    -I"${M5U_DIR}/src"
    -I"${M5G_DIR}/src"
    -I"${ONSPEED_CORE_DIR}"
    -I"${REPO_ROOT}/software/Libraries/tinyxml2"
    -I"${VERSION_DIR}"
)

COMMON_FLAGS=(
    -O2
    -DM5GFX_BACK_COLOR=0x000000u
    -DM5GFX_SCALE=1
)
if [[ "${TARGET}" == "docs" ]]; then
    # Docs sim: synthetic AOA ramp baked into SerialRead.cpp.
    COMMON_FLAGS+=(-DDUMMY_SERIAL_DATA)
fi
if [[ "${TARGET}" == "replay" ]]; then
    # Replay target: virtual time + accessor exports, no SDL.
    COMMON_FLAGS+=(-DREPLAY_TARGET)
fi

CXXFLAGS=(
    "${COMMON_FLAGS[@]}"
    -std=gnu++17
)

CFLAGS=(
    "${COMMON_FLAGS[@]}"
    -std=gnu11
)

if [[ "${TARGET}" == "replay" ]]; then
    # Replay target: no SDL, no canvas, no main(). Output is a JS
    # loader + .wasm pair that JS imports. Drives setup/loop manually
    # via replay_init/replay_loop and reads state-var accessors.
    #
    # Bundle sizing: ALLOW_MEMORY_GROWTH so the firmware's gdraw sprite
    # (320 × 240 × 1 byte = 75 KB at 8bpp) plus PSRAM-style heap headroom
    # don't bound the working set. Initial 16 MB matches what M5Unified
    # expects on Core2 (the firmware never sees the difference).
    EM_FLAGS=(
        -sMODULARIZE=1
        -sEXPORT_ES6=0
        -sNO_EXIT_RUNTIME=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=16777216
        -sENVIRONMENT=node,web
        -sEXPORTED_FUNCTIONS=_replay_init,_replay_set_time,_replay_loop,_replay_inject_byte,_replay_set_displayType,_replay_get_displayIAS,_replay_get_displayPalt,_replay_get_displayPitch,_replay_get_displayVerticalG,_replay_get_displayPercentLift,_replay_get_displayDecelRate,_replay_get_Slip,_replay_get_PercentLift,_replay_get_gOnsetRate,_replay_get_IAS,_replay_get_Palt,_replay_get_IasIsValid,_replay_get_displayType,_replay_get_iVSI,_replay_get_OAT,_replay_get_FlightPath,_replay_get_Pitch,_replay_get_Roll,_replay_get_TonesOnPctLift,_replay_get_OnSpeedFastPctLift,_replay_get_OnSpeedSlowPctLift,_replay_get_StallWarnPctLift,_replay_get_PipPctLift,_replay_get_FlapsMinDeg,_replay_get_FlapsMaxDeg,_replay_get_FlapPos,_replay_get_gHistoryIndex,_replay_get_gHistory_ptr,_replay_get_SpinRecoveryCue,_replay_get_DataMark,_malloc,_free
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8,HEAPF32
        -o "${OUT_DIR}/onspeed_m5.js"
    )
else
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
fi
if [[ "${TARGET}" == "live" ]]; then
    # Live target: expose the byte-injection entry point so the JS
    # bridge can pipe #1-protocol bytes received over the firmware's
    # WebSocket into the parser. The wrapper lives in SimMain.cpp
    # under #ifdef SIM_LIVE.
    CXXFLAGS+=(-DSIM_LIVE)
    CFLAGS+=(-DSIM_LIVE)
    EM_FLAGS+=(
        -sEXPORTED_FUNCTIONS=_main,_inject_serial_byte,_malloc,_free
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8
    )
fi

# Some onspeed_core sources reference <Arduino.h> transitively (via
# includes from the sketch). The shim is already active on non-ESP
# builds via #ifdef — but we preempt any stray include by adding the
# sim dir to the include path so `#include "../sim/ArduinoShim.h"`
# from GaugeWidgets.h resolves.

echo "[wasm] Target: ${TARGET}"
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
if [[ "${TARGET}" == "replay" ]]; then
    echo "  Output: ${OUT_DIR}/onspeed_m5.{js,wasm}"
    if [[ -f "${OUT_DIR}/onspeed_m5.wasm" ]]; then
        WASM_BYTES=$(wc -c < "${OUT_DIR}/onspeed_m5.wasm")
        WASM_KB=$(( WASM_BYTES / 1024 ))
        echo "  WASM size: ${WASM_KB} KB"
    fi
    # Copy artifacts to the replay tool's expected location so the JS
    # `import` works without a separate copy step. PR 2 will wire JS
    # consumers; PR 1 leaves the artifacts on disk for the Node test
    # harness and any future browser bridge.
    REPLAY_DEST="${REPO_ROOT}/tools/web/lib/replay/m5sim"
    mkdir -p "${REPLAY_DEST}"
    cp "${OUT_DIR}/onspeed_m5.js"   "${REPLAY_DEST}/"
    cp "${OUT_DIR}/onspeed_m5.wasm" "${REPLAY_DEST}/"
    echo "  Copied to: ${REPLAY_DEST}/onspeed_m5.{js,wasm}"
else
    echo "  Output: ${OUT_DIR}/index.html"
    echo "  Serve:  python3 -m http.server --directory ${OUT_DIR} 8080"
    echo "  Open:   http://localhost:8080/"
fi
