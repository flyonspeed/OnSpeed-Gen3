/*
 * Derived from GaugeWidgets by V.R. Little. Original source lives at
 * software/OnSpeed-M5-Display/lib/GaugeWidgets/ in this repository.
 *
 * Modifications: extracted to pure-data struct with in-class default
 * initializers so every GaugeState instance is fully deterministic at
 * construction, regardless of storage class. Addresses finding 045.
 * Drawing code remains in the original library.
 *
 * -----------------------------------------------------------------
 * Copyright 2021 V.R. Little
 *
 * Permission is hereby granted, free of charge, to any person provided a copy of this software
 * and associated documentation files (the "Software") to use, copy, modify, or merge copies of
 * the Software for non-commercial purposes, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ONSPEED_GAUGES_GAUGE_STATE_H
#define ONSPEED_GAUGES_GAUGE_STATE_H

#include <cstdint>

namespace onspeed::gauges {

/// Maximum number of range bands. Mirror of GaugeWidgets NUM_RANGES.
inline constexpr int kNumRanges = 5;

/// Maximum number of pointers. Mirror of GaugeWidgets NUM_POINTERS.
inline constexpr int kNumPointers = 8;

/// GaugeState holds all the mutable configuration for one gauge instance.
///
/// Every member has a C++11 in-class default initializer so the struct is
/// fully zero-initialised (or set to a safe non-zero default) at construction,
/// regardless of storage class (static, stack, or heap). This eliminates the
/// undefined-behaviour risk identified in finding 045, where Gauges::Gauges(){}
/// left members uninitialised for non-static instances.
///
/// This struct is Arduino/M5GFX-free. Color values are uint16_t (RGB565), but
/// the struct has no dependency on TFT_WHITE, TFT_BLACK, etc. Callers that
/// live in Arduino-land can use those constants when calling setters; this
/// struct just stores the raw value.
///
/// Arrays are sized `kNumRanges + 1` / `kNumPointers + 1` to match the
/// original GaugeWidgets 1-based indexing convention (index 0 is unused so
/// that callers can pass 1-based band / pointer numbers directly).
struct GaugeState {
    // ---- Range bands (set via setRange / clearRanges) ----
    bool    rangeValid[kNumRanges + 1]  = {};
    int32_t rangeTop  [kNumRanges + 1]  = {};
    int32_t rangeBot  [kNumRanges + 1]  = {};
    int32_t rangeColor[kNumRanges + 1]  = {};

    // ---- Pointers (set via setPointer / clearPointers) ----
    int32_t pointerValue[kNumPointers + 1] = {};
    int32_t pointerType [kNumPointers + 1] = {};
    int32_t pointerColor[kNumPointers + 1] = {};
    char    pointerTag  [kNumPointers + 1] = {};

    // ---- Gauge geometry ----
    bool    clockWise  = true;
    int16_t maxDisplay = 0;
    int16_t minDisplay = 0;
    int16_t barWidth   = 0;
    int16_t barSize    = 0;

    // ---- Datum helpers (written after each draw call) ----
    int16_t topDatumX = 0;
    int16_t topDatumY = 0;
    int16_t btmDatumX = 0;
    int16_t btmDatumY = 0;

    // ---- Graduation mark parameters ----
    int16_t  gradMarks       = 0;
    uint16_t gradMajorColor  = 0xFFFF;  // TFT_WHITE equivalent
    uint16_t gradMajorLength = 0;
    uint16_t gradMajorWidth  = 0;
    uint16_t gradMinorColor  = 0xFFFF;
    uint16_t gradMinorLength = 0;
    uint16_t gradMinorWidth  = 0;
    uint8_t  gradLineEnd     = 0;       // NONE == 0

    // ---- Line and edge style ----
    uint16_t fillColor = 0x0000;        // TFT_BLACK equivalent
    uint16_t lineColor = 0xFFFF;
    uint16_t edgeColor = 0xFFFF;
    uint16_t lineWidth = 1;
    uint16_t edgeWidth = 0;
    uint8_t  lineEnd   = 0;             // NONE == 0
    uint8_t  edgeEnd   = 0;
};

}  // namespace onspeed::gauges

#endif  // ONSPEED_GAUGES_GAUGE_STATE_H
