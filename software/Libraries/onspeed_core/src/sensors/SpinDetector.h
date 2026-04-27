// SpinDetector.h — directional rudder cue for incipient/developed spins.
//
// Computes the OnSpeed `#1` wire field `spinRecoveryCue` (offset 66, range
// −1 / 0 / +1) from yaw rate and AOA. The cue points the pilot at the
// rudder pedal that arrests autorotation:
//
//   −1  →  press LEFT rudder
//    0  →  no cue
//   +1  →  press RIGHT rudder
//
// Anti-spin rudder is opposite the yaw direction in both upright and
// inverted spins, so the cue is attitude-invariant. There is no
// vertical-G branch.
//
// Algorithm (latched, three-gate):
//
//   Single-pole IIR on yaw rate (tau = 1.0 s by default).
//   Latch entry requires ALL of:
//     1. AOA > stall_aoa            — wing actually stalled
//     2. |yaw_dps|       > 20°/s    — instantaneous gate
//     3. |yaw_filtered|  > 20°/s    — filtered gate (rejects spikes)
//     4. detector is `armed` (set on construction or after a clean exit)
//   While active, emit latched direction = -sign(yaw_filtered at latch
//   time).  Direction does not flip mid-event.
//   Latch exits in two distinct ways:
//     - Wing un-stalls (`aoa <= stall_aoa`): clean exit, detector is
//       re-armed for a fresh direction on the next entry.
//     - Filtered yaw drops below 20 − 5 = 15°/s while wing still
//       stalled: cue stops emitting (returns 0), but detector is NOT
//       re-armed.  This prevents "chasing arrows" during over-rudder:
//       the algorithm cannot distinguish "yaw really recovered" from
//       "filter transit through zero on the way to a sign flip," so
//       it stays silent until the wing un-stalls.
//
// Provenance:
//   Structure follows the F/A-18 Spin Recovery Mode (OFP v10.7), with
//   three GA deltas:
//     - Rudder semantics, not lateral-stick (PARE recovery).
//     - AOA gate replaces the F-18's airspeed gate (eliminates
//       falling-leaf misclassification: an unstalled wing cannot be in
//       autorotation by definition).
//     - Lower threshold (20°/s vs 17°/s) and shorter tau (1.0 s vs
//       7.2 s) reflect GA timescales for incipient spins.
//   The "no chasing arrows" lesson — earlier OFPs let the F-18 arrow
//   flicker, pilots followed it with reciprocal stick inputs that
//   delayed recovery — is the reason the cue is latched and the
//   re-arm rule requires a clean (un-stall) exit.
//
// References: DTIC ADA256522; UTenn theses on F/A-18 flight controls
// (trace.tennessee.edu); AIAA J. Guidance falling-leaf paper
// (doi:10.2514/1.50675); NATOPS F/A-18A-D; PARE recovery technique.
//
// Pure C++; no Arduino, no FreeRTOS — natively testable.

#ifndef ONSPEED_CORE_SENSORS_SPIN_DETECTOR_H
#define ONSPEED_CORE_SENSORS_SPIN_DETECTOR_H

namespace onspeed {

class SpinDetector {
public:
    /// Default thresholds.  Initial values; expect Vac to tune from
    /// flight test before first ship.
    static constexpr float kDefaultYawThresholdDps  = 20.0f;
    static constexpr float kDefaultYawHysteresisDps =  5.0f;
    static constexpr float kDefaultFilterTauSec     =  1.0f;

    SpinDetector();

    /// Override the detector constants (for testing or future config).
    void Configure(float yawThresholdDps,
                   float yawHysteresisDps,
                   float filterTauSec);

    /// Advance one tick.
    /// @param dtSec     Time since the last call (seconds).  Non-positive
    ///                  or non-finite values are no-ops: state holds and
    ///                  the prior cue value is returned.
    /// @param yawDps    Body-frame yaw rate (deg/s, +nose-right).
    /// @param aoaDeg    Body-angle AOA (deg).
    /// @param stallAoa  Per-flap stall body angle (deg).  Use
    ///                  `g_Config.aFlaps[active].fSTALLAOA` from a
    ///                  consistent snapshot.
    /// @return cue in {−1, 0, +1}: −1 = press left rudder, +1 = press
    ///         right rudder, 0 = no cue.
    int Update(float dtSec, float yawDps, float aoaDeg, float stallAoa);

    /// Current cue without advancing state.  Matches the last Update().
    int Get() const { return active_ ? latchedDir_ : 0; }

    /// Filtered yaw rate (deg/s).  Exposed for telemetry / test
    /// inspection only — do not gate downstream behavior on it.
    float GetFilteredYawDps() const { return yawFiltered_; }

    /// True when the cue is latched (currently emitting ±1).
    bool IsActive() const { return active_; }

    /// True when the detector is armed (will latch on next gate-pass).
    bool IsArmed() const { return armed_; }

    /// Clear all state and re-arm.  Called only when reconfiguring at
    /// runtime; not used in normal flight.
    void Reset();

private:
    float yawThresholdDps_;
    float yawHysteresisDps_;
    float filterTauSec_;

    float yawFiltered_;
    bool  active_;
    int   latchedDir_;
    bool  hasFilterSeed_;   // false until first valid Update() seen
    bool  armed_;           // false after a yaw-hysteresis exit until
                             // wing un-stall re-arms (prevents
                             // chasing-arrows on over-rudder transit)
};

}   // namespace onspeed

#endif  // ONSPEED_CORE_SENSORS_SPIN_DETECTOR_H
