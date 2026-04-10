// AOACalculator.cpp - AOA calculation and smoothing implementation

#include "AOACalculator.h"
#include <cmath>

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
    // Invalid samples must not contaminate the EMA — hold last good value.
    // Before the filter is seeded (first valid sample hasn't arrived yet),
    // return the safe floor so downstream doesn't see 0.0 degrees as valid AOA.
    if (raw.valid) {
        out.aoa = clampAOA(_smoother.update(raw.aoa));
    } else if (_smoother.isInitialized()) {
        out.aoa = clampAOA(_smoother.get());  // hold last good value
    } else {
        out.aoa = AOA_MIN_VALUE;  // no valid data yet; safe floor
    }

    return out;
}

} // namespace onspeed
