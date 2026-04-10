// AOACalculator.cpp - AOA calculation and smoothing implementation

#include "AOACalculator.h"
#include <cmath>
#include <limits>

namespace onspeed {

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

    // Check for non-finite results (NaN or Inf from bad curve coefficients)
    if (!std::isfinite(result.aoa)) {
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

    // Smooth and clamp.
    // Pass NaN for invalid samples so the EMA filter holds its previous
    // value instead of dragging the smoothed AOA toward -20 degrees.
    float valueToSmooth = raw.valid
        ? raw.aoa
        : std::numeric_limits<float>::quiet_NaN();
    out.aoa = clampAOA(_smoother.update(valueToSmooth));

    return out;
}

} // namespace onspeed
