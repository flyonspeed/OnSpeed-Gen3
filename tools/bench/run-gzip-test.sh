#!/usr/bin/env bash
# Self-contained gzip-probe + stress-test script.
#
# Run from a machine connected to the OnSpeed WiFi AP (192.168.0.1).
# Assumes the firehose stim is already running over USB in another
# terminal — this script only touches the WiFi side.
#
# Output goes to /Users/sritchie/Downloads/gzip-test-<timestamp>/ so
# you can come back to /this/ machine (off the AP) and paste the
# results.

set -e
cd "$(dirname "$0")/.."

TS=$(date +%Y%m%d_%H%M%S)
OUT=/Users/sritchie/Downloads/gzip-test-$TS
mkdir -p "$OUT"

echo "OnSpeed gzip probe + stress test"
echo "  output dir: $OUT"
echo ""

# ============================================================================
# Step 1 — curl probe.  Single GET of /aoaconfig advertising gzip.
# The smoking gun: does the response have Content-Encoding: gzip and a
# small body, or is it still 67 KB raw?
# ============================================================================
echo "===== Step 1: curl probe ====="
echo "GET http://192.168.0.1/aoaconfig with Accept-Encoding: gzip"
echo ""

curl -v --compressed \
     -H 'Accept-Encoding: gzip' \
     http://192.168.0.1/aoaconfig \
     -o "$OUT/aoaconfig.html" \
     2> "$OUT/curl-headers.txt" \
     || echo "  curl exited non-zero (timeout?)"

echo "--- response headers (compressed-related) ---"
grep -iE "content-encoding|content-length|vary|content-type" "$OUT/curl-headers.txt" || echo "  (no matching headers — server didn't respond)"
echo ""
echo "--- body size on disk ---"
ls -la "$OUT/aoaconfig.html" 2>/dev/null | awk '{print "  " $5 " bytes saved (post-decompression if curl auto-inflated)"}'

# ============================================================================
# Verdict for gzip:
# - Content-Encoding: gzip in headers + Content-Length around 10-15 KB = YES, gzip works
# - No Content-Encoding header + Content-Length 60-70 KB = NO, gzip didn't activate
# ============================================================================

echo ""
echo "===== Verdict on gzip ====="
if grep -qi "content-encoding.*gzip" "$OUT/curl-headers.txt"; then
    CONTENT_LEN=$(grep -i "^< content-length" "$OUT/curl-headers.txt" | awk '{print $3}' | tr -d '\r' | tail -1)
    echo "  ✓ Server advertised Content-Encoding: gzip"
    echo "  ✓ On-wire body length: ${CONTENT_LEN:-unknown} bytes"
    echo "  ✓ Gzip is WORKING"
else
    CONTENT_LEN=$(grep -i "^< content-length" "$OUT/curl-headers.txt" | awk '{print $3}' | tr -d '\r' | tail -1)
    echo "  ✗ No Content-Encoding: gzip header in response"
    echo "  ✗ Body length: ${CONTENT_LEN:-unknown} bytes (probably uncompressed)"
    echo "  ✗ Gzip is NOT working — investigate before stress test"
fi

# ============================================================================
# Step 2 — stress test (only if gzip is working, or if you want to baseline
# anyway).  2 minutes is enough to see steady-state behavior.
# ============================================================================
echo ""
echo "===== Step 2: 2-min stress test ====="
echo "Running: uv run tools/bench/stress_web_handlers.py --aggressive --no-saves --no-downloads --duration 2"
echo "  (this takes 2 min; full output → $OUT/stress.log)"
echo ""

uv run tools/bench/stress_web_handlers.py \
    --aggressive --no-saves --no-downloads \
    --duration 2 \
    --serial-out "$OUT/stress_serial.log" \
    2>&1 | tee "$OUT/stress.log"

echo ""
echo "===== Done ====="
echo "Results saved to: $OUT/"
echo "  curl-headers.txt   — gzip negotiation result"
echo "  aoaconfig.html     — the actual response body curl saved"
echo "  stress.log         — full stress-test stdout"
echo "  stress_serial.log  — V4P console capture during stress"
echo ""
echo "Disconnect from OnSpeed AP, reconnect to normal WiFi, and paste"
echo "the contents of $OUT/curl-headers.txt and the stress.log summary."
