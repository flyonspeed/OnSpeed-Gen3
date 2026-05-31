# Builds efis_stim_vn300, a host-side helper that emits VN-300 138-byte
# frames on stdout. Driven by uart_efis_stim.py, which pipes the output
# to a USB-TTL dongle at 921600 baud.
#
# Build:
#   make            (default: optimized release build)
#   make clean
#
# Run direct (sanity):
#   ./efis_stim_vn300 --frames 1 | xxd        # one frame, hex dumped
#
# Run via the Python driver:
#   ../uart_efis_stim.py                       # auto-detect dongle
#   ../uart_efis_stim.py --port /dev/cu.usbserial-FT...
#
# We deliberately do NOT use PlatformIO for this — it's a host-side tool,
# not firmware. Same reason tools/regression/run_snapshot.py builds with
# plain c++ instead.

CORE  := ../../../software/Libraries/onspeed_core/src
CXX   ?= c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -I$(CORE)

SRCS := main.cpp $(CORE)/test_frames/SynthFrames.cpp
BIN  := efis_stim_vn300

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

.PHONY: clean
clean:
	rm -f $(BIN)
