#!/usr/bin/env bash
# Capture a perf report WHILE the box is under realistic stress.
# Run from a machine connected to the OnSpeed AP.
#
# Sequence:
#   1. Start the web stress in the background (2 min, aggressive)
#   2. Wait 10 sec for the stress to settle into steady state
#   3. Capture 60 sec of PERF telemetry
#   4. Stop stress (will already be running its course)
#   5. Save the perf report + the stress transcript
#
# The firehose stim must already be running over USB in another
# terminal — this script doesn't touch the stim.

set -e
cd "$(dirname "$0")/.."

TS=$(date +%H%M%S)
OUT=/Users/sritchie/Downloads/perf-with-stress-$TS
mkdir -p "$OUT"

echo "perf-under-stress capture"
echo "  output dir: $OUT"
echo ""

# Step 1: launch stress in background. Logs to $OUT/stress.log.
echo "===== launching 2-min stress in background ====="
(uv run tools/bench/stress_web_handlers.py \
    --aggressive --no-saves --no-downloads \
    --duration 2 \
    --no-ws-chaos \
    --serial-port none \
    > "$OUT/stress.log" 2>&1) &
STRESS_PID=$!
echo "  stress PID: $STRESS_PID"

# Step 2: wait for steady state
echo "===== waiting 10s for stress to settle ====="
sleep 10

# Step 3: capture perf report
echo "===== capturing 60s perf report ====="
uv run tools/perf-report/capture_perf_report.py \
    --label refactor-with-stress-$TS \
    --duration 60 \
    --port /dev/cu.usbserial-410 \
    2>&1 | tee "$OUT/perf-capture.log"

# Step 4: wait for stress to finish
echo "===== waiting for stress to complete ====="
wait $STRESS_PID 2>/dev/null || true

# Step 5: show summary
echo ""
echo "===== done ====="
echo "Perf report: docs/perf-reports/refactor-with-stress-$TS.md"
echo "Stress log:  $OUT/stress.log"
echo ""
echo "Stress summary (last 15 lines):"
tail -15 "$OUT/stress.log"
