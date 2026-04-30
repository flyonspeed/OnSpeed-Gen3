// CalwizSave.cpp — implementation of ApplyCalwizSave.
//
// The mutation order and field assignments match HandleCalWizard's
// step=save block in ConfigWebServer.cpp line-for-line so the
// differential test (test_calwiz_save_diff) can prove byte-identical
// post-state.  Don't reorder, don't combine, don't "improve" — drift
// from the legacy sequence is exactly what the test catches.

#include "CalwizSave.h"

namespace onspeed::api {

void ApplyCalwizSave(onspeed::config::OnSpeedConfig::SuFlaps& flap,
                     const CalwizSaveInput& in) {
    flap.fLDMAXAOA       = in.ldMaxAoaDeg;
    flap.fONSPEEDFASTAOA = in.onSpeedFastAoaDeg;
    flap.fONSPEEDSLOWAOA = in.onSpeedSlowAoaDeg;
    flap.fSTALLWARNAOA   = in.stallWarnAoaDeg;
    flap.fSTALLAOA       = in.stallAoaDeg;
    flap.fMANAOA         = in.maneuveringAoaDeg;
    flap.fAlpha0         = in.alpha0Deg;
    flap.fAlphaStall     = in.alphaStallDeg;
    flap.fKFit           = in.kFit;

    flap.AoaCurve.afCoeff[0] = 0;
    flap.AoaCurve.afCoeff[1] = in.curve0;
    flap.AoaCurve.afCoeff[2] = in.curve1;
    flap.AoaCurve.afCoeff[3] = in.curve2;
    flap.AoaCurve.iCurveType = 1;  // polynomial
}

}  // namespace onspeed::api
