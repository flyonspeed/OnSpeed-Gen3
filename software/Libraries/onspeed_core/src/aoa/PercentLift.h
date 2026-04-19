// PercentLift.h - Map raw AOA + flap calibration -> integer 0..99
//
// Pure function ported verbatim from the original DisplaySerial.cpp logic
// (see PR 3.1 Task 5). Used by:
//   - DisplaySerial.cpp  (#1 protocol PercentLift field, sent to the M5
//     display at ~20 Hz)
//   - any future native consumer that needs the same scalar
//
// Mapping (per-flap setpoints):
//   AOA <  fLDMAXAOA                            -> 0..50
//   fLDMAXAOA      <= AOA <= fONSPEEDFASTAOA    -> 50..55
//   fONSPEEDFASTAOA < AOA <= fONSPEEDSLOWAOA    -> 55..66
//   fONSPEEDSLOWAOA < AOA <= fSTALLWARNAOA      -> 66..90
//   AOA >  fSTALLWARNAOA                        -> 90..100, ceiling is
//                                                  fAlphaStall when
//                                                  calibrated, otherwise
//                                                  fSTALLWARNAOA*100/90
// Result is then clamped to [0, 99].
//
// When iasValid is false (pilot on the ground / IAS below the audio mute
// floor) the function returns 0 — there is no meaningful percent-of-stall
// without airflow.
//
// KNOWN BUG (do NOT fix here — needs flight-test verification):
//   The 0..fLDMAXAOA range uses fAlpha0 as the zero-lift floor. The
//   pre-extraction code historically used a hardcoded 0 here, which
//   under-reports percent-lift at low AOAs because the actual zero-lift
//   fuselage AOA (alpha_0) is negative (typically ~-2.5 deg). The
//   in-progress fix already lives in DisplaySerial.cpp:151 (g_Config has
//   the per-flap fAlpha0 plumbed through), so this extraction preserves
//   that behavior verbatim. A residual hardcoded `0` floor still exists in
//   the JS liveview (Web/html_liveview.h:121-124, browser-side, not
//   shareable with C++); see issue #199 for the full fix plan.

#ifndef ONSPEED_CORE_AOA_PERCENT_LIFT_H
#define ONSPEED_CORE_AOA_PERCENT_LIFT_H

#include <config/OnSpeedConfig.h>

namespace onspeed {
namespace aoa {

// Map raw AOA (degrees) and the active flap's calibration setpoints into a
// 0..99 percent-of-stall scalar.  Returns 0 when iasValid is false.
//
// flapCfg is one entry of FOSConfig::aFlaps (the SuFlaps for the currently
// detected flap position).
//
// TODO(#199): the JS liveview equivalent in Web/html_liveview.h still uses
// a hardcoded 0 floor below fLDMAXAOA — track-and-fix in a follow-up PR
// so all UIs report the same percent-lift.
int ComputePercentLift(float aoaDeg,
                       const ::onspeed::config::OnSpeedConfig::SuFlaps& flapCfg,
                       bool iasValid);

}  // namespace aoa
}  // namespace onspeed

#endif  // ONSPEED_CORE_AOA_PERCENT_LIFT_H
