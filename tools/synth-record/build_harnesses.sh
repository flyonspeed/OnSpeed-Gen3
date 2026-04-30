#!/usr/bin/env bash
# Build the host-side C++ harnesses used by the orchestrator.
#
# Outputs:
#   tools/synth-record/build/spin_detector_harness
#   (audio harness lives at tools/audio-sweep/engines/master/harness, built by its own script)
set -euo pipefail
cd "$(dirname "$0")"

REPO=$(cd ../.. && pwd)
CORE="$REPO/software/Libraries/onspeed_core/src"

mkdir -p build

clang++ \
    -std=c++17 -O2 -Wall \
    -I"$CORE" \
    spin_detector_harness.cpp \
    "$CORE/sensors/SpinDetector.cpp" \
    -o build/spin_detector_harness

echo "built $(pwd)/build/spin_detector_harness"

clang++ \
    -std=c++17 -O2 -Wall \
    -I"$CORE" \
    audio_harness.cpp \
    "$CORE/audio/AudioMixer.cpp" \
    "$CORE/audio/Envelope.cpp" \
    "$CORE/audio/ToneCalc.cpp" \
    "$CORE/audio/ToneSynth.cpp" \
    -o build/audio_harness

echo "built $(pwd)/build/audio_harness"

clang++ \
    -std=c++17 -O2 -Wall \
    -I"$CORE" \
    display_anchors_harness.cpp \
    "$CORE/aoa/DisplayPctAnchors.cpp" \
    "$CORE/aoa/PercentLift.cpp" \
    "$CORE/config/OnSpeedConfig.cpp" \
    -o build/display_anchors_harness

echo "built $(pwd)/build/display_anchors_harness"
