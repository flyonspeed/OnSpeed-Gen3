// CurveCalc.h - Calibration curve evaluation

#pragma once

#include "OnSpeedTypes.h"

namespace onspeed {

/// Evaluate a calibration curve
/// @param x Input value (e.g., pressure coefficient)
/// @param curve Calibration curve definition
/// @return Output value (e.g., AOA in degrees)
///
/// Curve types:
/// - Type 1 (Polynomial): y = a3*x^3 + a2*x^2 + a1*x + a0
/// - Type 2 (Logarithmic): y = a*ln(x) + b (uses last two coefficients)
/// - Type 3 (Exponential): y = a*e^(b*x) (uses last two coefficients)
/// - Unknown type: returns 0
float CurveCalc(float x, const SuCalibrationCurve& curve);

} // namespace onspeed
