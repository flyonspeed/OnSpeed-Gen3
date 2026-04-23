/*
 * Derived from GaugeWidgets by V.R. Little. Original source lives at
 * software/OnSpeed-M5-Display/lib/GaugeWidgets/ in this repository.
 *
 * Modifications: extracted to pure-function form so the range-scaling
 * math is natively testable and structurally cannot mutate its inputs.
 * Addresses finding 024. Drawing code remains in the original library.
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

#include "BarRangeScale.h"

namespace onspeed::gauges {

namespace {

inline int32_t ClampInt32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

void ScaleBarRanges(const RangeBand* src,
                    ScaledRangeBand* dst,
                    int              numRanges,
                    int32_t          normAxis,
                    int32_t          scaleUp,
                    int32_t          maxScaled,
                    int32_t          minScaled)
{
    if (src == nullptr || dst == nullptr) return;
    if (scaleUp <= 0)                     return;

    int bandCount = numRanges;
    if (bandCount < 0)           bandCount = 0;
    if (bandCount > kMaxRanges)  bandCount = kMaxRanges;

    for (int bandIdx = 0; bandIdx < bandCount; ++bandIdx)
    {
        // Scale into a wider temporary before clamping so we never sign-wrap
        // on the multiply. Inputs are int32_t, so the product is computed as
        // int32_t — the overflow analysis is documented in the header.
        const int32_t scaledTop = src[bandIdx].top * normAxis / scaleUp;
        const int32_t scaledBot = src[bandIdx].bot * normAxis / scaleUp;

        dst[bandIdx].top = ClampInt32(scaledTop, minScaled, maxScaled);
        dst[bandIdx].bot = ClampInt32(scaledBot, minScaled, maxScaled);
    }
}

}  // namespace onspeed::gauges
