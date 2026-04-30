// CalwizSave.h
//
// Pure host-runnable mutation logic for POST /api/calwiz/save.  The
// firmware HTTP handler reads form fields off `CfgServer.arg(...)`,
// parses each into a float via the same `atof` helper Config.cpp's
// `ToFloat` uses, fills a `CalwizSaveInput` struct, and calls
// `ApplyCalwizSave` to mutate the matching `SuFlaps`.  The
// differential test calls `ApplyCalwizSave` directly — no HTTP, no
// Arduino — and asserts byte-identical post-state vs the legacy
// inline mutation sequence.
//
// Field mapping (mirrors ConfigWebServer.cpp HandleCalWizard step=save):
//
//   ldMaxAoaDeg        → flap.fLDMAXAOA
//   onSpeedFastAoaDeg  → flap.fONSPEEDFASTAOA
//   onSpeedSlowAoaDeg  → flap.fONSPEEDSLOWAOA
//   stallWarnAoaDeg    → flap.fSTALLWARNAOA
//   stallAoaDeg        → flap.fSTALLAOA
//   maneuveringAoaDeg  → flap.fMANAOA
//   alpha0Deg          → flap.fAlpha0
//   alphaStallDeg      → flap.fAlphaStall
//   kFit               → flap.fKFit
//   curve0..curve2     → flap.AoaCurve.afCoeff[1..3]
//   (afCoeff[0] is hardcoded 0; iCurveType is hardcoded 1 = polynomial.)
//
// Pure function: takes a `SuFlaps&` and a const-input struct, mutates
// the flap in-place.  No globals, no I/O, no logging.  Save-to-file
// + warning emission stays in the HTTP wrapper.

#ifndef ONSPEED_CORE_API_CALWIZ_SAVE_H
#define ONSPEED_CORE_API_CALWIZ_SAVE_H

#include "../config/OnSpeedConfig.h"

namespace onspeed::api {

struct CalwizSaveInput {
    float ldMaxAoaDeg       = 0.0f;
    float onSpeedFastAoaDeg = 0.0f;
    float onSpeedSlowAoaDeg = 0.0f;
    float stallWarnAoaDeg   = 0.0f;
    float stallAoaDeg       = 0.0f;
    float maneuveringAoaDeg = 0.0f;
    float alpha0Deg         = 0.0f;
    float alphaStallDeg     = 0.0f;
    float kFit              = 0.0f;

    // Polynomial coefficients of the legacy CP→AOA fit (regression.js
    // `equation` array, order 2).  Stored into afCoeff[1..3]; afCoeff[0]
    // is hardcoded 0 to match the legacy sequence.
    float curve0 = 0.0f;  ///< quadratic coefficient (a2)
    float curve1 = 0.0f;  ///< linear coefficient    (a1)
    float curve2 = 0.0f;  ///< constant term         (a0)
};

// Apply the wizard's computed setpoints + curve coefficients to a
// single SuFlaps in-place.  Returns nothing — the caller decides
// whether to emit a warning by querying flap.SetpointOrderError()
// after the call.
void ApplyCalwizSave(onspeed::config::OnSpeedConfig::SuFlaps& flap,
                     const CalwizSaveInput& in);

}  // namespace onspeed::api

#endif  // ONSPEED_CORE_API_CALWIZ_SAVE_H
