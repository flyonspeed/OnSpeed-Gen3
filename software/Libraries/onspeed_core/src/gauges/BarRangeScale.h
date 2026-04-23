/*
 * Derived from GaugeWidgets by V.R. Little. Original source lives at
 * software/OnSpeed-M5-Display/lib/GaugeWidgets/ in this repository.
 *
 * Modifications: extracted to pure-function form so the range-scaling
 * math is natively testable and structurally cannot mutate its inputs.
 * Addresses finding 024 (vBarGraph/hBarGraph mutated caller-owned
 * rangeTop[] / rangeBot[] members in place). Drawing code remains in
 * the original library.
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

#ifndef ONSPEED_GAUGES_BAR_RANGE_SCALE_H
#define ONSPEED_GAUGES_BAR_RANGE_SCALE_H

#include <cstdint>

namespace onspeed::gauges {

/// Maximum number of range bands (mirrors GaugeWidgets NUM_RANGES).
inline constexpr int kMaxRanges = 5;

/// Inputs describing one range band (engineering-unit values, as set by setRange()).
struct RangeBand {
    bool    valid = false;
    int32_t top   = 0;    ///< Engineering-unit top of band
    int32_t bot   = 0;    ///< Engineering-unit bottom of band
    int32_t color = 0;
};

/// Result of scaling one range band to pixel (or integer axis) coordinates.
struct ScaledRangeBand {
    int32_t top = 0;      ///< Scaled top, clamped to [minScaled, maxScaled]
    int32_t bot = 0;      ///< Scaled bottom, clamped to [minScaled, maxScaled]
};

/// ScaleBarRanges — pure function, no side effects on inputs.
///
/// For each of `numRanges` entries in `src`, compute:
///   dst[i].top = clamp(src[i].top * normAxis / scaleUp, minScaled, maxScaled)
///   dst[i].bot = clamp(src[i].bot * normAxis / scaleUp, minScaled, maxScaled)
///
/// `src` and `dst` must not alias. Entries with `valid == false` are still
/// written to `dst` (the caller decides whether to draw based on `valid`;
/// this function does no drawing).
///
/// The caller owns the normAxis formula. Typically `normAxis =
/// scaleUp * barSize / (maxDisplay - minDisplay) - 1`. Passing both normAxis
/// and scaleUp keeps this function ignorant of that formula and lets tests
/// exercise the scaling in isolation.
///
/// Overflow note: for the values in use by GaugeWidgets (src.top up to a
/// few thousand engineering units, normAxis up to ~SCALEUP = 4096), the
/// intermediate `src[i].top * normAxis` fits comfortably in int32_t.
///
/// @param src        Array of input range bands (engineering units).
/// @param dst        Output array for scaled bands (must not alias src).
/// @param numRanges  Number of entries to process. Values greater than
///                   kMaxRanges are clamped to kMaxRanges; negative values
///                   are treated as zero.
/// @param normAxis   Scaling numerator (see formula above).
/// @param scaleUp    Scaling denominator (typically SCALEUP = 4096). A
///                   zero or negative value is treated as no-op (no writes).
/// @param maxScaled  Upper clamp limit (scaled maxDisplay).
/// @param minScaled  Lower clamp limit (scaled minDisplay).
void ScaleBarRanges(const RangeBand* src,
                    ScaledRangeBand* dst,
                    int              numRanges,
                    int32_t          normAxis,
                    int32_t          scaleUp,
                    int32_t          maxScaled,
                    int32_t          minScaled);

}  // namespace onspeed::gauges

#endif  // ONSPEED_GAUGES_BAR_RANGE_SCALE_H
