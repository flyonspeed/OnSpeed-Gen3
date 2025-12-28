// EMA.h - Exponential Moving Average filter

#pragma once

#include <cmath>

/// Exponential Moving Average (EMA) filter
///
/// Implements first-order IIR low-pass filter:
///   output = alpha * input + (1 - alpha) * previous_output
class EMAFilter {
public:
    /// Construct with alpha based on number of samples.
    /// @param samples Number of samples for smoothing window.
    ///                alpha = 1/samples, so larger = more smoothing.
    ///                0 means no smoothing (alpha = 1.0, pass-through).
    explicit EMAFilter(int samples = 0)
        : _value(0.0f)
        , _alpha(1.0f)
        , _initialized(false)
    {
        setSamples(samples);
    }

    /// Construct directly with alpha.
    /// @param alpha Smoothing factor in range (0.0, 1.0].
    explicit EMAFilter(float alpha)
        : _value(0.0f)
        , _alpha(1.0f)
        , _initialized(false)
    {
        setAlpha(alpha);
    }

    /// Update with a new value and return the smoothed result.
    ///
    /// On first call with valid input, seeds the filter (returns input as-is).
    /// NaN inputs are ignored - returns previous smoothed value.
    ///
    /// @param value New measurement to incorporate
    /// @return Smoothed value
    float update(float value)
    {
        if (std::isnan(value)) {
            return _value;
        }

        if (!_initialized) {
            _value = value;
            _initialized = true;
        } else {
            _value = _alpha * value + (1.0f - _alpha) * _value;
        }
        return _value;
    }

    /// Get current smoothed value without updating.
    float get() const
    {
        return _value;
    }

    /// Reset to uninitialized state.
    /// Next update() call will seed with that value.
    void reset()
    {
        _value = 0.0f;
        _initialized = false;
    }

    /// Change smoothing via sample count.
    /// @param samples Number of samples (0 = no smoothing)
    void setSamples(int samples)
    {
        _alpha = (samples <= 0) ? 1.0f : (1.0f / static_cast<float>(samples));
    }

    /// Change smoothing via alpha directly.
    /// @param alpha Smoothing factor, clamped to (0.0, 1.0]
    void setAlpha(float alpha)
    {
        if (alpha <= 0.0f) {
            _alpha = 0.001f;  // Prevent division issues, near-zero response
        } else if (alpha > 1.0f) {
            _alpha = 1.0f;
        } else {
            _alpha = alpha;
        }
    }

    /// Get current alpha value.
    float getAlpha() const
    {
        return _alpha;
    }

    /// Check if filter has been initialized with a value.
    bool isInitialized() const
    {
        return _initialized;
    }

private:
    float _value;
    float _alpha;
    bool  _initialized;
};
