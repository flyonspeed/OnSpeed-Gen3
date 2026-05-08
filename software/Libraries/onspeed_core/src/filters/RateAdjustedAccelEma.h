// RateAdjustedAccelEma.h — Rate-adjusted EMA for replay path
//
// The firmware runs its accel EMA at IMU rate (208 Hz) with α=kAccSmoothing.
// SD logs capture raw IMU at 50 Hz (default log rate).  To reproduce the
// firmware's smoothed output from 50 Hz log data, a replay consumer needs an
// EMA with the same continuous-time τ but an α adjusted for the lower input
// rate.
//
// This class constructs with (inputHz, targetTauSec) and computes:
//
//     α = 1 - exp(-(1/inputHz) / targetTauSec)
//
// For inputHz=208 and targetTauSec=kAccelEmaTauSec, α ≈ kAccSmoothing
// (the firmware's constant). For inputHz=50 with the same τ, α ≈ 0.230 —
// four times larger because the sample interval is four times longer.
//
// This file is part of onspeed_core and must remain platform-free.
// check_core_purity.sh enforces the invariant.
//
// Usage (replay path):
//
//   using onspeed::filters::RateAdjustedAccelEma;
//   using onspeed::filters::kAccelEmaTauSec;
//
//   // Construct at the log's sample rate, targeting firmware τ.
//   RateAdjustedAccelEma lateralEma(50.0f, kAccelEmaTauSec);
//
//   // Per log row (50 Hz):
//   float smoothed = lateralEma.update(row.imuLateralG);
//
// For signals below 25 Hz (50 Hz Nyquist), smoothed values converge to what
// the M5 displayed. For signals between 25 Hz and 67 Hz (the IMU's analog
// LPF bandwidth), the 50 Hz log has aliased content the filter cannot recover.
// The test_rate_adjusted_accel_ema suite documents the aliasing bound with a
// 30 Hz test case.

#pragma once

#include <cmath>

namespace onspeed::filters {

// ---------------------------------------------------------------------------
// Canonical continuous-time τ for the firmware's accel EMA.
//
// Derived from kAccSmoothing (Ahrs.cpp) at the IMU sample rate:
//   τ = -(1/208) / ln(1 − 0.060899) ≈ 0.076516 s
//
// If kAccSmoothing in Ahrs.cpp changes, this constant must be updated to
// match: recompute with  τ = -(1/imuHz) / ln(1 − kAccSmoothing).
//
// Cross-reference: software/Libraries/onspeed_core/src/ahrs/Ahrs.cpp
//   constexpr float kAccSmoothing = 0.060899f;  // EMA alpha for accels
//   (running at imuSampleRateHz = 208 Hz)
// ---------------------------------------------------------------------------
inline constexpr float kAccelEmaTauSec = 0.076516f;

// ---------------------------------------------------------------------------
// RateAdjustedAccelEma
//
// Header-only, no platform dependencies. Pattern mirrors EMAFilter.h but
// derives α from (inputHz, targetTauSec) instead of taking α directly.
// ---------------------------------------------------------------------------
class RateAdjustedAccelEma {
public:
    /// Construct with input sample rate and target continuous-time τ.
    ///
    /// α is computed as 1 - exp(-(1/inputHz) / targetTauSec).
    ///
    /// @param inputHz       Sample rate of the data fed to update() (Hz). Must be > 0.
    /// @param targetTauSec  Target continuous-time time constant (seconds). Must be > 0.
    RateAdjustedAccelEma(float inputHz, float targetTauSec)
        : _value(0.0f)
        , _alpha(0.0f)
        , _initialized(false)
    {
        if (inputHz > 0.0f && targetTauSec > 0.0f) {
            _alpha = 1.0f - std::exp(-(1.0f / inputHz) / targetTauSec);
        } else {
            // Degenerate inputs: pass-through (α=1).
            _alpha = 1.0f;
        }
    }

    /// Update with raw value and return smoothed.
    ///
    /// First call seeds the filter (returns input unchanged) and marks
    /// the filter initialized. NaN inputs are ignored — returns the
    /// previous smoothed value to prevent poisoning downstream.
    ///
    /// @param raw   New measurement.
    /// @return      Smoothed value.
    float update(float raw)
    {
        if (std::isnan(raw)) {
            return _value;
        }

        if (!_initialized) {
            _value = raw;
            _initialized = true;
        } else {
            _value = _alpha * raw + (1.0f - _alpha) * _value;
        }
        return _value;
    }

    /// Current smoothed value without updating.
    float get() const
    {
        return _value;
    }

    /// Reset to uninitialized state.
    /// The next update() call will seed with that value.
    void reset()
    {
        _value = 0.0f;
        _initialized = false;
    }

    /// Seed with a known steady-state value.
    ///
    /// Marks the filter initialized so the next update() blends against
    /// this seed instead of replacing it. NaN values are ignored.
    ///
    /// @param value  Known steady-state value to seed with.
    void seed(float value)
    {
        if (std::isnan(value)) return;
        _value = value;
        _initialized = true;
    }

    /// Effective α computed from (inputHz, targetTauSec) at construct time.
    float getAlpha() const
    {
        return _alpha;
    }

    /// True after the first update() or a seed() call.
    bool isInitialized() const
    {
        return _initialized;
    }

private:
    float _value;
    float _alpha;
    bool  _initialized;
};

}  // namespace onspeed::filters
