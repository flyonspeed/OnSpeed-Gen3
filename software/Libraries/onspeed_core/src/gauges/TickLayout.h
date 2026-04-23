/*
 * Derived from GaugeWidgets by V.R. Little. Original source lives at
 * software/OnSpeed-M5-Display/lib/GaugeWidgets/ in this repository.
 *
 * Modifications: extracted to pure-function form so the graduation-mark
 * angle layout is natively testable. Addresses finding 041: arcGraph's
 * graduation-mark loops use startAngle as their rotation origin, but the
 * corresponding range bars and pointer use theta (= startAngle -
 * normAxis * minDisplay). Whenever minDisplay != 0, those two origins
 * diverge and the ticks no longer line up with the range bars. The
 * extracted function takes `theta` explicitly so the rewire is a one-line
 * replacement in GaugeWidgets.cpp. Drawing code remains in the original
 * library.
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

#ifndef ONSPEED_GAUGES_TICK_LAYOUT_H
#define ONSPEED_GAUGES_TICK_LAYOUT_H

#include <cstdint>

namespace onspeed::gauges {

/// Maximum number of major + minor ticks across both gradMarks modes.
/// A typical gauge has gradMarks == 10, producing 11 major + 10 minor =
/// 21 ticks. 64 is a conservative upper bound and allows gradMarks up to
/// 30 (which would produce 31 major + 30 minor = 61 ticks).
inline constexpr int kMaxTicks = 64;

/// A single tick mark angle (radians from the gauge origin).
struct TickAngle {
    float angle   = 0.0f;  ///< Absolute angle in radians
    bool  isMajor = true;  ///< true = major, false = minor
};

/// ComputeTickAngles — compute all tick mark angles for arcGraph.
///
/// Major ticks are emitted first (in ascending loop order: i = 0,
/// delta, 2*delta, ..., |arcAngle|). Then minor ticks (i = delta/2,
/// delta/2 + delta, ...). Every emitted angle is `i + theta`.
///
/// Writes up to `outCapacity` entries, returning the number written.
/// If the natural tick count would exceed `outCapacity`, writes are
/// truncated at capacity and the returned value equals `outCapacity`.
///
/// @param theta       Rotation origin = startAngle - normAxis * minDisplay.
///                    This is the value arcGraph calls `_theta`. Finding
///                    041 requires this (not startAngle) as the anchor.
/// @param arcAngle    Total arc span in radians; signed. Ticks are laid
///                    out over the absolute value of the arc. Negative
///                    arcs produce the same angle positions as the
///                    corresponding positive arc.
/// @param gradMarks   Number of major intervals (same sign convention as
///                    GaugeWidgets: |gradMarks| > 1 produces ticks;
///                    |gradMarks| <= 1 produces none). The sign only
///                    changes which style parameters the caller uses for
///                    rendering; angular positions are identical for
///                    +gradMarks and -gradMarks.
/// @param out         Caller-allocated output buffer.
/// @param outCapacity Number of TickAngle slots in `out`.
/// @return            Number of TickAngle entries written (0..outCapacity).
int ComputeTickAngles(float      theta,
                      float      arcAngle,
                      int16_t    gradMarks,
                      TickAngle* out,
                      int        outCapacity);

}  // namespace onspeed::gauges

#endif  // ONSPEED_GAUGES_TICK_LAYOUT_H
