// PercentLift.h - Map raw AOA + flap calibration -> integer 0..99
//
// Used by:
//   - DisplaySerial.cpp  (#1 protocol PercentLift field, sent to the M5
//     display at ~20 Hz)
//   - any future native consumer that needs the same scalar
//
// Mapping (single linear normalization between calibrated aerodynamic
// anchors):
//
//   percentLift = (aoaDeg - fAlpha0) / (fAlphaStall - fAlpha0) * 100
//
// fAlpha0 is the per-flap zero-lift body angle (typically negative).
// fAlphaStall is the per-flap stall body angle.  Both come from the
// calibration wizard's lift-equation fit.
//
// Where each per-flap setpoint lands on this scale (the body-angle ->
// percent mapping for L/Dmax, OnSpeedFast/Slow, StallWarn) is *whatever
// the calibration says* — the percent values vary per flap because the
// aerodynamics vary per flap.  This is the design intent: the displayed
// percent is the honest envelope-fraction reading, comparable across
// flaps as an aerodynamic property of the wing.
//
// Result is clamped to [0, 99].  Below fAlpha0 reads 0; above
// fAlphaStall reads 99 (never 100, by convention — saturation, not
// completion).
//
// When iasValid is false (pilot on the ground / IAS below the audio
// mute floor) the function returns 0 — there is no meaningful
// percent-of-stall without airflow.
//
// Defensive ceiling: when fAlphaStall is uncalibrated (<= fSTALLWARNAOA),
// the function uses fSTALLWARNAOA * 100/90 as a synthetic ceiling so an
// uncalibrated configuration still produces approximately the same
// reading at the upper end as it did under the historical segmented
// implementation.

#ifndef ONSPEED_CORE_AOA_PERCENT_LIFT_H
#define ONSPEED_CORE_AOA_PERCENT_LIFT_H

#include <config/OnSpeedConfig.h>

namespace onspeed {
namespace aoa {

// Map raw AOA (degrees) and the active flap's calibration into a 0..99
// percent-of-stall scalar via the honest single-linear normalization.
// Returns 0 when iasValid is false.
//
// flapCfg is one entry of FOSConfig::aFlaps (the SuFlaps for the
// currently detected flap position).
int ComputePercentLift(float aoaDeg,
                       const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
                       bool iasValid);

}  // namespace aoa
}  // namespace onspeed

#endif  // ONSPEED_CORE_AOA_PERCENT_LIFT_H
