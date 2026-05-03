// DataRefAdapter — convert X-Plane datarefs into a DisplayBuildInputs
// suitable for onspeed::proto::BuildDisplayFrame.
//
// Most fields are 1:1 dataref reads with unit conversions.  The
// "percent lift" set (tonesOnPctLift / onSpeedFastPctLift /
// onSpeedSlowPctLift / stallWarnPctLift / pipPctLift) is derived
// from the plugin's existing AOA setpoint configuration via
// onspeed_core's ComputePercentLift, using a stack-local SuFlaps
// built from the plugin's four threshold globals plus alpha_0 /
// alpha_stall approximations (see spec section "alpha_0 / alpha_stall
// derivation" — replaced by issue #392).

#pragma once

#include <proto/DisplaySerial.h>

namespace onspeed_xplane::indexer {

// One-time dataref lookup.  Idempotent.  Called from indexer Init().
void InitDataRefs();

// Populate a DisplayBuildInputs from current X-Plane state.
// Returns a fully-populated struct ready for BuildDisplayFrame.
onspeed::proto::DisplayBuildInputs BuildInputsFromDatarefs();

}  // namespace onspeed_xplane::indexer
