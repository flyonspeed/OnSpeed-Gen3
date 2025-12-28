// OnSpeedTypes.h - Platform-independent types and constants for OnSpeed algorithms

#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

// ============================================================================
// CALIBRATION CONSTANTS
// ============================================================================

/// Maximum number of flap/gear configurations
constexpr int MAX_AOA_CURVES = 5;

/// Polynomial degree + 1
constexpr int MAX_CURVE_COEFF = 4;

// ============================================================================
// AOA LIMITS
// ============================================================================

/// Maximum valid AOA value (degrees)
/// Was 20 before, but in a sudden stall when the nose quickly goes up,
/// AOA gets larger very quickly and then onspeed goes silent right as
/// the aircraft stalls.
constexpr float AOA_MAX_VALUE = 40.0f;

/// Minimum valid AOA value (degrees)
constexpr float AOA_MIN_VALUE = -20.0f;

/// Clamp AOA to valid range, treating NaN as minimum
inline float clampAOA(float aoa)
{
    if (std::isnan(aoa)) {
        return AOA_MIN_VALUE;
    }
    return std::clamp(aoa, AOA_MIN_VALUE, AOA_MAX_VALUE);
}

// ============================================================================
// UNIT CONVERSION FUNCTIONS
// ============================================================================

/// Degrees to radians
constexpr float deg2rad(float deg) { return deg * 0.0174533f; }

/// Radians to degrees
constexpr float rad2deg(float rad) { return rad * 57.2958f; }

/// Gravitational units to meters/second squared
constexpr float g2mps(float gs) { return gs * 9.80665f; }

/// Meters/second squared to gravitational units
constexpr float mps2g(float mps) { return mps * 0.101971621f; }

/// Feet to meters
constexpr float ft2m(float ft) { return ft * 0.3048f; }

/// Meters to feet
constexpr float m2ft(float m) { return m * 3.28084f; }

/// Meters/second to feet/minute
constexpr float mps2fpm(float mps) { return mps * 196.85f; }

/// Meters/second to knots
constexpr float mps2kts(float mps) { return mps * 1.94384f; }

/// Knots to meters/second
constexpr float kts2mps(float kts) { return kts * 0.514444f; }

/// Inches of mercury to millibars
constexpr float inhg2mb(float inhg) { return inhg * 33.8639f; }

/// PSI to millibars
constexpr float psi2mb(float psi) { return psi * 68.94757f; }

/// Millibars to PSI
constexpr float mb2psi(float mb) { return mb * 0.0145038f; }

// ============================================================================
// ACCELEROMETER-BASED ATTITUDE ESTIMATION
// ============================================================================

/// Pitch from accelerometer readings (degrees)
inline float accelPitch(float ax, float ay, float az) {
    return rad2deg(std::atan2(ax, std::sqrt(ay * ay + az * az)));
}

/// Roll from accelerometer readings (degrees)
inline float accelRoll(float ax, float ay, float az) {
    return rad2deg(-std::atan2(ay, std::sqrt(ax * ax + az * az)));
}

// ============================================================================
// PRESSURE COEFFICIENT CALCULATION
// ============================================================================

/// Ratiometric pressure coefficient (CP3)
/// Can't divide by P45 - it goes through zero on Dynon probe.
inline float pressureCoeff(float pfwd, float p45) {
    return (pfwd > 0.0f) ? (p45 / pfwd) : 0.0f;
}

// ============================================================================
// CALIBRATION CURVE TYPES
// ============================================================================

/// Calibration curve definition
/// Used for AOA-to-pressure-coefficient mapping and CAS correction
struct SuCalibrationCurve {
    float   afCoeff[MAX_CURVE_COEFF];  ///< Polynomial coefficients [a3, a2, a1, a0]
    uint8_t iCurveType;                ///< 1=polynomial, 2=logarithmic, 3=exponential
};

// ============================================================================
// ALGORITHM RESULT STRUCTURES
// ============================================================================

/// Result of AOA calculation
/// Returns the calculated AOA and intermediate values for logging/display.
/// Smoothing is handled separately via EMAFilter.
struct AOAResult {
    float aoa;     ///< Raw calculated AOA (degrees)
    float coeffP;  ///< Pressure coefficient used in calculation
    bool  valid;   ///< False if calculation failed (e.g., negative Pfwd)
};

