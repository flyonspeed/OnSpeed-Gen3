// CalwizStateJson.h
//
// Read-only JSON serializer for GET /api/calwiz/state.  The wizard
// reads its starting state from this endpoint instead of the full
// config blob — only the fields the legacy /calwiz GET handler
// consumes are exposed:
//
//   aircraft.{grossWeightLb, bestGlideKt, vfeKt, gLimit}
//   currentFlapIndex
//   flaps[].{degrees, alpha0Deg, alphaStallDeg,
//            ldMaxAoaDeg, onSpeedFastAoaDeg, onSpeedSlowAoaDeg,
//            stallWarnAoaDeg, stallAoaDeg, maneuveringAoaDeg}
//
// The setpoints are body angles (degrees), not wing AOA — see
// CLAUDE.md "OnSpeed measures body angle, not wing AOA".  Field
// names carry the unit suffix per PLAN_WEB_PREACT_REWRITE §4k.
//
// Pure host-runnable helper; no Arduino, no globals.

#ifndef ONSPEED_CORE_API_CALWIZ_STATE_JSON_H
#define ONSPEED_CORE_API_CALWIZ_STATE_JSON_H

#include <cstddef>
#include <string>
#include <vector>

#include "../config/OnSpeedConfig.h"

namespace onspeed::api {

struct CalwizStateInputs {
    int   acGrossWeightLb   = 0;
    float acBestGlideKt     = 0.0f;  ///< from cfg.fAcBestGlideIAS (KIAS)
    float acVfeKt           = 0.0f;  ///< from cfg.fAcVfe (KIAS)
    float acGLimit          = 0.0f;  ///< from cfg.fAcGlimit (G)

    // Per-flap setpoints, copied from cfg.aFlaps.  Bounded at
    // onspeed::MAX_AOA_CURVES (same cap the parser enforces).
    std::vector<onspeed::config::OnSpeedConfig::SuFlaps> flaps;

    // Index of the active detent (g_Flaps.iIndex on the firmware).
    // Always in range [0, flaps.size()) on a calibrated config; the
    // serializer clamps and emits 0 when the input is out of range.
    int currentFlapIndex = 0;
};

// Serialize the wizard's starting state into a JSON document.  Returns
// the JSON string; never throws.  Empty `flaps` produces an empty
// `"flaps": []` array.  All floats use %.6g for compact yet
// round-trippable output; integers are %d.
std::string SerializeCalwizState(const CalwizStateInputs& in);

}  // namespace onspeed::api

#endif  // ONSPEED_CORE_API_CALWIZ_STATE_JSON_H
