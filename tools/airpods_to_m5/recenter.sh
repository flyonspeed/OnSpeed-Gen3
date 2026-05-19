#!/bin/bash
# Re-zero the AirPods horizon. Run this from any terminal while
# airpods_to_m5 is running. Sits still, hold your head forward and
# level, then run this — the next sample becomes the new zero.
pkill -USR1 -f airpods_to_m5 && echo "recenter signal sent" || echo "no airpods_to_m5 running"
