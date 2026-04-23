/*
 * Derived from GaugeWidgets by V.R. Little. Original source lives at
 * software/OnSpeed-M5-Display/lib/GaugeWidgets/ in this repository.
 *
 * Modifications: extracted to pure-function form so the arc geometry is
 * natively testable. Fixes findings 029, 033, 047. Drawing code remains
 * in the original library.
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

#include "ArcGeometry.h"

#include <cmath>

namespace onspeed::gauges {

ArcQuad ComputeArcQuad(float        startAngle,
                       float        j,
                       float        stepRad,
                       ArcDirection direction)
{
    // Option A CCW fix: walk theta backwards. For CW we walk forwards.
    // Both directions cover the same angular span [startAngle, startAngle +
    // abs(arcAngle)] in terms of the unique set of angles visited, just in
    // opposite rotational order.
    float thetaA = 0.0f;
    float thetaB = 0.0f;

    if (direction == ArcDirection::CW)
    {
        thetaA = startAngle + j;
        thetaB = thetaA + stepRad;
    }
    else  // CCW
    {
        thetaA = startAngle - j;
        thetaB = thetaA - stepRad;
    }

    ArcQuad quad;
    quad.edgeA.cosV = cosf(thetaA);
    quad.edgeA.sinV = sinf(thetaA);
    quad.edgeB.cosV = cosf(thetaB);
    quad.edgeB.sinV = sinf(thetaB);
    return quad;
}

ArcEdge ComputeEndCapAngle(float        startAngle,
                           float        arcAngle,
                           ArcDirection direction)
{
    // The formula is the same for CW and CCW: the caller passes signed
    // arcAngle (negative for CCW intent), so the final angle is always
    // startAngle + arcAngle. Cast direction to void to suppress unused-
    // parameter warnings under -Wextra while keeping the API symmetric
    // with ComputeArcQuad.
    (void)direction;

    const float finalTheta = startAngle + arcAngle;

    ArcEdge edge;
    edge.cosV = cosf(finalTheta);
    edge.sinV = sinf(finalTheta);
    return edge;
}

}  // namespace onspeed::gauges
