// GOnsetFilter.h - Low-pass-filtered first derivative of vertical G.
//
// Produces the "G onset rate" signal in g/s for the M5 secondary display's
// rate-tape widget. Wire format reserves ±9.99 g/s with ×100 scaling
// (see proto/DisplaySerial.h).
//
// Design:
//   - Single-pole IIR (EMA) over the raw difference quotient (vG - prev) / dt.
//   - Tau-based: alpha = dt / (tau + dt). Same dt sourced sample-to-sample,
//     so the effective time constant tracks the actual cadence whether ticked
//     at 20 Hz (wire rate) or 208 Hz (AHRS rate).
//   - First sample seeds prev and returns 0 — no spurious derivative spike.
//   - NaN/Inf input is rejected: state holds, output unchanged.
//
// Pure C++; no Arduino, no FreeRTOS — natively testable.
//
// Sign convention:
//   Mirrors the input sign convention. When fed g_AHRS.AccelVertFilter.get()
//   (production reaction-force convention: +1 g at level, +2 g in a 2-g
//   pull-up), positive output means "G load increasing" — matches the M5
//   render which draws the bar above the zero line for positive values.

#ifndef ONSPEED_CORE_FILTERS_G_ONSET_FILTER_H
#define ONSPEED_CORE_FILTERS_G_ONSET_FILTER_H

namespace onspeed {

class GOnsetFilter {
public:
    /// Construct with a low-pass time constant.
    /// @param timeConstantSec Target tau (seconds). 250 ms is a reasonable
    ///                        default for a 20 Hz wire-rate update — enough
    ///                        smoothing to suppress single-sample noise,
    ///                        responsive enough to catch a half-second pull.
    explicit GOnsetFilter(float timeConstantSec = 0.25f);

    /// Incorporate a new vertical-G sample taken `dtSec` after the previous one.
    ///
    /// Returns the smoothed derivative in g/s. On the very first valid sample
    /// the filter seeds `prev` and returns 0. NaN/Inf inputs (and dt <= 0)
    /// are no-ops: state holds, the prior smoothed output is returned.
    ///
    /// @param verticalG Vertical-G sample (g)
    /// @param dtSec     Time since last valid Update() (seconds)
    /// @return Smoothed derivative (g/s)
    float Update(float verticalG, float dtSec);

    /// Current smoothed derivative without advancing state.
    float Get() const { return smoothed_; }

    /// Clear all state. Next Update() seeds fresh — no derivative spike
    /// will be produced from the pre-reset sample.
    void Reset();

    /// Override the time constant (seconds). Takes effect on next Update().
    void SetTimeConstant(float timeConstantSec);

    /// Current time constant (seconds).
    float GetTimeConstant() const { return tauSec_; }

private:
    float tauSec_;       // low-pass time constant
    float prevG_;        // previous vertical-G sample
    float smoothed_;     // current smoothed derivative
    bool  hasPrev_;      // false until first valid sample seen
};

} // namespace onspeed

#endif // ONSPEED_CORE_FILTERS_G_ONSET_FILTER_H
