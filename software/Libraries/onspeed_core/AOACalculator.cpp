// AOACalculator.cpp - AOA calculation and smoothing implementation

#include "AOACalculator.h"
#include <cmath>

// ============================================================================
// Pure AOA calculation
// ============================================================================

AOAResult CalcAOA(
    float pfwd,
    float p45,
    const SuCalibrationCurve& curve
) {
    AOAResult result;
    result.valid = true;

    // Can't calculate with non-positive forward pressure
    if (pfwd <= 0.0f) {
        result.aoa    = 0.0f;
        result.coeffP = 0.0f;
        result.valid  = false;
        return result;
    }

    result.coeffP = pressureCoeff(pfwd, p45);

    // Calculate raw AOA from calibration curve
    result.aoa = CurveCalc(result.coeffP, curve);

    // Check for NaN (can occur with bad curve coefficients)
    if (std::isnan(result.aoa)) {
        result.aoa   = 0.0f;
        result.valid = false;
    }

    return result;
}

// ============================================================================
// AOACalculator methods
// ============================================================================

AOACalculatorResult AOACalculator::calculate(
    float pfwd,
    float p45,
    const SuCalibrationCurve& curve
) {
    AOACalculatorResult out;

    AOAResult raw = CalcAOA(pfwd, p45, curve);

    out.coeffP = raw.coeffP;
    out.valid  = raw.valid;

    // Smooth and clamp
    float valueToSmooth = raw.valid ? raw.aoa : AOA_MIN_VALUE;
    out.aoa = clampAOA(_smoother.update(valueToSmooth));

    return out;
}
