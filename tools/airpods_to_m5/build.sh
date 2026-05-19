#!/bin/bash
# Build airpods_to_m5 as a codesigned .app bundle so macOS TCC honors
# the NSMotionUsageDescription from Info.plist. See the
# macos-coremotion-cli skill for why all four steps are required.
set -euo pipefail

cd "$(dirname "$0")"

rm -rf airpods_to_m5.app
mkdir -p airpods_to_m5.app/Contents/MacOS
cp Info.plist airpods_to_m5.app/Contents/Info.plist
swiftc airpods_to_m5.swift -o airpods_to_m5.app/Contents/MacOS/airpods_to_m5
codesign --force --sign - --entitlements entitlements.plist ./airpods_to_m5.app

echo "Built airpods_to_m5.app"
echo "Run with: ./run.sh /dev/cu.usbserial-XXXX"
