/*
 * Derived from GaugeWidgets by V.R. Little. Original source lives at
 * software/OnSpeed-M5-Display/lib/GaugeWidgets/ in this repository.
 *
 * Modifications: extracted to pure-function form so the arc-quad and
 * end-cap geometry is natively testable. Fixes finding 029 (the CCW
 * branch in fillArc reflected cosine rather than reversing traversal),
 * fixes finding 033 (the end-cap coordinates re-used stale loop-local
 * variables rather than computing the exact final angle), and closes
 * finding 047 at these call sites by using sinf/cosf (single precision)
 * throughout. Drawing code remains in the original library.
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

#ifndef ONSPEED_GAUGES_ARC_GEOMETRY_H
#define ONSPEED_GAUGES_ARC_GEOMETRY_H

#include <cstdint>

namespace onspeed::gauges {

/// Arc traversal direction.
enum class ArcDirection : uint8_t { CW = 0, CCW = 1 };

/// Trig pair for one arc edge (inner or outer, at some angle theta).
/// Values are sampled from the unit circle: cosV == cos(theta),
/// sinV == sin(theta).
struct ArcEdge {
    float cosV = 0.0f;
    float sinV = 0.0f;
};

/// The four corners of one arc quad, ready for use by fillTriangle calls.
///
/// Naming matches GaugeWidgets.cpp convention:
///   edgeA = leading edge of this step (at theta)
///   edgeB = trailing edge of this step (at theta + stepRad or theta - stepRad)
struct ArcQuad {
    ArcEdge edgeA;
    ArcEdge edgeB;
};

/// ComputeArcQuad — pure function, no side effects.
///
/// Given a loop variable `j` (in radians, 0 <= j < abs(arcAngle)) and
/// direction, returns the cos/sin values for the A and B edges of this quad.
///
/// For CW:  thetaA = startAngle + j,  thetaB = startAngle + j + stepRad
/// For CCW: thetaA = startAngle - j,  thetaB = startAngle - j - stepRad
///
/// This is the geometrically correct implementation of CW/CCW traversal:
/// the arc covers the same angular span in either case, just traversed in
/// opposite rotational directions. Unlike the existing GaugeWidgets.cpp
/// CCW branch which negates only cosine (a Y-axis reflection), this
/// function reverses the traversal direction, which is the intended
/// semantic. Addresses finding 029.
///
/// Uses sinf/cosf (single precision) — appropriate for the int16_t pixel
/// grid and for the Xtensa LX7 FPU, which supports only single precision.
ArcQuad ComputeArcQuad(float        startAngle,
                       float        j,
                       float        stepRad,
                       ArcDirection direction);

/// ComputeEndCapAngle — returns cos/sin for the requested final angle.
///
/// The end-cap of fillArc should land at exactly `startAngle + arcAngle`,
/// not at the last loop iteration's theta +/- stepRad (which overshoots by
/// up to stepRad, or is zero if the loop never ran). This function
/// computes those exact coordinates from the parameters. The caller uses
/// these for the end-cap drawEdge call instead of relying on loop-local
/// variables. Addresses finding 033.
///
/// The formula is `startAngle + arcAngle` in both directions: the caller
/// passes signed `arcAngle` (negative for CCW intent), and the returned
/// angle lands at the correct spot in either case. The `direction`
/// parameter is accepted for API symmetry with ComputeArcQuad and for
/// documentation, but is not otherwise used — the sign of `arcAngle`
/// carries all the directional information.
ArcEdge ComputeEndCapAngle(float        startAngle,
                           float        arcAngle,
                           ArcDirection direction);

}  // namespace onspeed::gauges

#endif  // ONSPEED_GAUGES_ARC_GEOMETRY_H
