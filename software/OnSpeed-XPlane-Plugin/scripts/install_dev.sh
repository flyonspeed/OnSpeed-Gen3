#!/usr/bin/env bash
# Install the just-built plugin into a local X-Plane installation for dev testing.
#
# Usage:
#   ./scripts/install_dev.sh /path/to/X-Plane\ 12
#
# The X-Plane root is the directory containing the X-Plane binary itself
# (typically named "X-Plane 12" or "X-Plane 12 Demo"). The script writes
# to <root>/Resources/plugins/AOA-Tone-FlyOnSpeed/<arch>/AOA-Tone-FlyOnSpeed.xpl.
#
# Re-run after each `cmake --build build` cycle. X-Plane has no plugin
# hot-reload — restart the sim for changes to take effect.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <path-to-X-Plane-root>" >&2
    exit 64
fi

XPLANE_ROOT="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PLUGIN_DIR}/build"

if [[ ! -d "$XPLANE_ROOT" ]]; then
    echo "error: X-Plane root not found: $XPLANE_ROOT" >&2
    exit 66
fi
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "error: build dir not found at $BUILD_DIR — run cmake first" >&2
    exit 66
fi

UNAME="$(uname -s)"
case "$UNAME" in
    Darwin)  ARCH_DIR="mac_x64" ;;
    Linux)   ARCH_DIR="lin_x64" ;;
    MINGW*|MSYS*|CYGWIN*) ARCH_DIR="win_x64" ;;
    *)
        echo "error: unsupported OS $UNAME" >&2
        exit 69
        ;;
esac

SRC="${BUILD_DIR}/${ARCH_DIR}/AOA-Tone-FlyOnSpeed.xpl"
if [[ ! -e "$SRC" ]]; then
    echo "error: built plugin not found at $SRC — did cmake --build succeed?" >&2
    exit 66
fi

DEST="${XPLANE_ROOT}/Resources/plugins/AOA-Tone-FlyOnSpeed/${ARCH_DIR}/AOA-Tone-FlyOnSpeed.xpl"
mkdir -p "$(dirname "$DEST")"
cp -R "$SRC" "$DEST"

echo "Installed: $DEST"
echo "Restart X-Plane to load the new build."
