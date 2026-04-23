/*
 * Derived from GaugeWidgets by V.R. Little. Original source lives at
 * software/OnSpeed-M5-Display/lib/GaugeWidgets/ in this repository.
 *
 * Modifications: extracted to pure-function form so the graduation-mark
 * angle layout is natively testable. Addresses finding 041 (all four
 * grad-mark loops used startAngle instead of theta as the anchor).
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

#include "TickLayout.h"

#include <cmath>

namespace onspeed::gauges {

namespace {

inline bool TryEmit(TickAngle* out, int outCapacity, int& count,
                    float angle, bool isMajor)
{
    if (count >= outCapacity) return false;
    out[count].angle   = angle;
    out[count].isMajor = isMajor;
    ++count;
    return true;
}

}  // namespace

int ComputeTickAngles(float      theta,
                      float      arcAngle,
                      int16_t    gradMarks,
                      TickAngle* out,
                      int        outCapacity)
{
    int count = 0;

    if (out == nullptr || outCapacity <= 0) return 0;

    // Absolute number of intervals. |gradMarks| <= 1 produces no ticks,
    // matching the arcGraph guards `gradMarks > 1` and `gradMarks < -1`.
    const int absGrad = (gradMarks >= 0) ?  gradMarks
                                         : -gradMarks;
    if (absGrad <= 1) return 0;

    // Tick positions are computed over |arcAngle|. The original arcGraph
    // uses `_arcAngle = abs(arcAngle) * DEG_TO_RAD` so the function sees
    // only positive arcs; we accept signed input and take the magnitude
    // here for robustness.
    const float span = std::fabs(arcAngle);
    if (span == 0.0f) return 0;

    const float delta = span / static_cast<float>(absGrad);

    // Major ticks: i = 0, delta, 2*delta, ..., span. Iterate by integer
    // index rather than a float loop to avoid FP accumulation drift,
    // which matters when span/delta is near an integer boundary.
    for (int i = 0; i <= absGrad; ++i)
    {
        const float iRad  = static_cast<float>(i) * delta;
        const float angle = iRad + theta;
        if (!TryEmit(out, outCapacity, count, angle, /*isMajor=*/true)) return count;
    }

    // Minor ticks: i = delta/2, delta/2 + delta, ..., < span. The
    // original loop condition in arcGraph is `i < _arcAngle`, so we
    // match that (strict less-than).
    for (int i = 0; i < absGrad; ++i)
    {
        const float iRad  = static_cast<float>(i) * delta + 0.5f * delta;
        if (iRad >= span) break;
        const float angle = iRad + theta;
        if (!TryEmit(out, outCapacity, count, angle, /*isMajor=*/false)) return count;
    }

    return count;
}

}  // namespace onspeed::gauges
