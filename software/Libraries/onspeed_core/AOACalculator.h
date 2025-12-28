// AOACalculator.h - AOA calculation and smoothing

#pragma once

#include "OnSpeedTypes.h"
#include "CurveCalc.h"
#include "EMAFilter.h"

/// Pure AOA calculation
///
/// Converts pressure readings to angle of attack using calibration curve.
/// Use AOACalculator to get smoothed AoA
///
/// @param pfwd  Forward (dynamic) pressure
/// @param p45   45-degree (AOA differential) pressure
/// @param curve AOA calibration curve for current flap position
/// @return AOAResult with raw AOA and pressure coefficient
AOAResult CalcAOA(
    float pfwd,
    float p45,
    const SuCalibrationCurve& curve
);

// ============================================================================
// Stateful AOA calculator with smoothing
// ============================================================================

/// Result from AOACalculator
struct AOACalculatorResult {
    float aoa;     ///< Smoothed and clamped AOA (degrees)
    float coeffP;  ///< Pressure coefficient (for logging/display)
    bool  valid;   ///< False if calculation failed
};

/// Stateful AOA calculator with built-in EMA smoothing.
///
/// Each instance owns its smoother state, so different callers
/// (live sensors vs log replay) can have independent smoothing.
class AOACalculator {
public:
    /// Construct with smoothing factor.
    /// @param smoothingSamples Number of samples for smoothing (0 = no smoothing)
    explicit AOACalculator(int smoothingSamples = 0)
        : _smoother(smoothingSamples)
    {
    }

    /// Calculate smoothed AOA from pressure readings.
    ///
    /// @param pfwd  Forward (dynamic) pressure
    /// @param p45   45-degree (AOA differential) pressure
    /// @param curve Calibration curve for current flap position
    /// @return Smoothed AOA, coeffP, and validity flag
    AOACalculatorResult calculate(float pfwd, float p45, const SuCalibrationCurve& curve);

    /// Reset smoother state.
    /// Call when starting log replay or other discontinuity.
    void reset()
    {
        _smoother.reset();
    }

    /// Change smoothing factor.
    void setSamples(int samples)
    {
        _smoother.setSamples(samples);
    }

private:
    EMAFilter _smoother;
};
