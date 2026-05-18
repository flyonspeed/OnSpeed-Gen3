// AdaptiveEmaFilter.h - EMA filter with per-frame error-dependent alpha
//
// Effective alpha is boosted toward alphaMax when the per-frame error is
// large, and falls back to alphaMin when the signal is steady. The result
// is smooth at hold and responsive on transients without retuning.
//
//   err   = |x_new - y_prev|
//   alpha = clamp(alphaMin + kBoost * err, alphaMin, alphaMax)
//   y_new = alpha * x_new + (1 - alpha) * y_prev
//
// Degenerate case alphaMin == alphaMax reduces to a fixed-alpha EMA — this
// is intentional and is the migration path from EMAFilter when a caller
// needs deterministic smoothing.

#pragma once

#include <cmath>

namespace onspeed {

class AdaptiveEmaFilter {
public:
    struct Config {
        float alphaMin;     ///< 0.0 - 1.0 (steady-state alpha)
        float alphaMax;     ///< 0.0 - 1.0, should be >= alphaMin
        float kBoost;       ///< boost per unit of |err|. units depend on signal.
    };

    AdaptiveEmaFilter() = default;

    explicit AdaptiveEmaFilter(const Config& cfg)
        : _cfg(clamp(cfg))
    {
    }

    /// Replace the active config. State (current value, initialized flag)
    /// is preserved so callers can tune in flight without resetting the
    /// filter.
    void setConfig(const Config& cfg)
    {
        _cfg = clamp(cfg);
    }

    const Config& getConfig() const { return _cfg; }

    /// Update with a new value and return the smoothed result.
    /// First valid (non-NaN) update seeds the filter; subsequent updates
    /// blend. NaN inputs are ignored — return the previous smoothed value.
    float update(float value)
    {
        if (std::isnan(value)) {
            return _value;
        }

        if (!_initialized) {
            _value = value;
            _initialized = true;
            // Seeding is "alpha=1" by construction (output = input, no blend).
            // Recording that here keeps lastAlpha() honest if a caller probes
            // it right after the first sample.
            _lastAlpha = 1.0f;
            return _value;
        }

        const float err   = std::fabs(value - _value);
        float       alpha = _cfg.alphaMin + _cfg.kBoost * err;
        if (alpha < _cfg.alphaMin) alpha = _cfg.alphaMin;
        if (alpha > _cfg.alphaMax) alpha = _cfg.alphaMax;

        _lastAlpha = alpha;
        _value     = alpha * value + (1.0f - alpha) * _value;
        return _value;
    }

    /// Current smoothed value without updating.
    float get() const { return _value; }

    /// The alpha applied on the last update() call.
    /// Useful for diagnostics ("how often is the filter actually opening up?").
    float lastAlpha() const { return _lastAlpha; }

    /// True iff the filter has been seeded.
    bool isInitialized() const { return _initialized; }

    /// Drop state. Next update() seeds with that value.
    void reset()
    {
        _value       = 0.0f;
        _lastAlpha   = 0.0f;
        _initialized = false;
    }

    /// Seed the filter to a known steady-state value without blending.
    /// NaN values are ignored.
    void seed(float value)
    {
        if (std::isnan(value)) return;
        _value       = value;
        _initialized = true;
    }

private:
    static Config clamp(Config c)
    {
        // Coerce into the valid [0, 1] x [0, 1] x [0, +inf) range.
        // alpha=0 is legal — it means "never update; hold the seeded value."
        // Keep alphaMin <= alphaMax so the per-update clamp interval is
        // non-empty.  Reject NaN by passing through (the update path's NaN
        // check handles the input side; the config side should never be NaN).
        if (c.alphaMin < 0.0f) c.alphaMin = 0.0f;
        if (c.alphaMin > 1.0f) c.alphaMin = 1.0f;
        if (c.alphaMax < c.alphaMin) c.alphaMax = c.alphaMin;
        if (c.alphaMax > 1.0f) c.alphaMax = 1.0f;
        if (c.kBoost   < 0.0f) c.kBoost   = 0.0f;
        return c;
    }

    // Defaults: no smoothing. Anyone who constructs the filter without
    // a config gets a pass-through, matching EMAFilter(samples=0) today.
    Config _cfg{1.0f, 1.0f, 0.0f};
    float  _value       = 0.0f;
    float  _lastAlpha   = 0.0f;
    bool   _initialized = false;
};

} // namespace onspeed
