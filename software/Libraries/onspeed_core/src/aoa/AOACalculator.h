// AOACalculator.h - AOA calculation and smoothing

#pragma once

#include <util/OnSpeedTypes.h>
#include <aoa/CurveCalc.h>
#include <filters/AdaptiveEmaFilter.h>

namespace onspeed {

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

/// Stateful AOA calculator with built-in adaptive-EMA smoothing.
///
/// The filter dynamically widens its time constant when |delta_AOA| per
/// frame grows large (pull-up, aerobatic maneuver) and falls back to a
/// heavy smoothing α when the signal is steady. Three tunables per
/// instance via AdaptiveEmaFilter::Config:
///
///   alphaMin — steady-state α (heavy smoothing). Sets the floor.
///   alphaMax — responsive α (light smoothing). Sets the ceiling.
///   kBoost   — error-to-alpha gain in units per degree of |err|.
///
/// Each instance owns its smoother state, so different callers
/// (live sensors vs log replay) can have independent smoothing.
class AOACalculator {
public:
    /// Default constructor — no smoothing (pass-through).
    AOACalculator() = default;

    /// Construct with an adaptive-EMA configuration.
    explicit AOACalculator(const AdaptiveEmaFilter::Config& cfg)
        : _smoother(cfg)
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

    /// Change smoothing config without resetting state.
    void setConfig(const AdaptiveEmaFilter::Config& cfg)
    {
        _smoother.setConfig(cfg);
    }

    /// The alpha applied on the last update() call — for diagnostics.
    float lastAlpha() const { return _smoother.lastAlpha(); }

private:
    AdaptiveEmaFilter _smoother;
};

} // namespace onspeed
