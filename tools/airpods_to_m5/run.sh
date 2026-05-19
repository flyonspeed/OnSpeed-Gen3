#!/bin/bash
# Launch airpods_to_m5 via `open` so macOS TCC sees the bundle identity.
# stderr (with the live pitch/roll readout) goes to /tmp/airpods_to_m5.stderr;
# `tail -F` it in another terminal to watch.
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 /dev/cu.usbserial-XXXX"
    exit 2
fi

cd "$(dirname "$0")"
SCRIPT_DIR="$(pwd)"

if [ ! -d "${SCRIPT_DIR}/airpods_to_m5.app" ]; then
    echo "airpods_to_m5.app not found — run ./build.sh first"
    exit 1
fi

rm -f /tmp/airpods_to_m5.stdout /tmp/airpods_to_m5.stderr
open --stdout /tmp/airpods_to_m5.stdout \
     --stderr /tmp/airpods_to_m5.stderr \
     "${SCRIPT_DIR}/airpods_to_m5.app" \
     --args "$@"

echo "launched. Watch live output with:"
echo "  tail -F /tmp/airpods_to_m5.stderr"
echo "Recenter horizon at any time with:"
echo "  ./recenter.sh"
echo "Stop with:"
echo "  pkill -f airpods_to_m5"
