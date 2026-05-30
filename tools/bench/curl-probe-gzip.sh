#!/usr/bin/env bash
# Verify the gzip fix.  Run from a machine connected to the OnSpeed AP.
# Saves results to ~/Downloads so you can disconnect to paste back.
set -e
TS=$(date +%H%M%S)
OUT=/Users/sritchie/Downloads/gzip-probe-$TS.txt
{
  echo "=== curl --compressed http://192.168.0.1/aoaconfig ==="
  curl -v --compressed http://192.168.0.1/aoaconfig -o /tmp/cfg-$TS.html 2>&1 \
    | grep -iE "^< (content-encoding|content-length|vary|http)|^>" \
    | head -20
  echo ""
  echo "=== body size on disk (curl auto-inflated if gzip) ==="
  ls -la /tmp/cfg-$TS.html | awk '{print "  " $5 " bytes (decompressed)"}'
} | tee "$OUT"
echo ""
echo "Saved to: $OUT"
