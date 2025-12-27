
#include "Globals.h"
#include "Config.h"
#include "CurveCalc.h"

// ----------------------------------------------------------------------------

// Calculate and return the AOA computed from pressures

float CalcAOA (float Pfwd, float P45, int iFlapsIndex, int iSmoothing)
{
    static float    fPrevAOA = 0.0;
           float    fAOA;
           float    SmoothingAlpha;
//           double   fCoeffP;

#ifdef SPHERICAL_PROBE
    g_fCoeffP = PCOEFF(Pfwd,P45);
#else

    // Calculate pressure coefficient
    if (Pfwd > 0)
    {
        g_fCoeffP = PCOEFF(Pfwd,P45);
    }

    // Can't calc with negative Pfwd pressure
    else
    {
        fAOA      = AOA_MIN_VALUE;
        g_fCoeffP = 0;
        return fAOA;
    }
#endif

    // Calculate smoothing constant
    //// This could probably be done just once
    if (iSmoothing == 0) SmoothingAlpha = 1.0;
    else                 SmoothingAlpha = 1.0 / iSmoothing;

    // Calculate raw AOA
    fAOA = CurveCalc(g_fCoeffP, g_Config.aFlaps[iFlapsIndex].AoaCurve);

    // Now smooth the AOA some. Smoothing is defined by samples of lag, alpha=1/samples
    fAOA = fAOA * SmoothingAlpha + (1-SmoothingAlpha) * fPrevAOA;

    if      (fAOA > AOA_MAX_VALUE) fAOA = AOA_MAX_VALUE;
    else if (fAOA < AOA_MIN_VALUE) fAOA = AOA_MIN_VALUE;

    if (isnan(fAOA))
        fAOA = AOA_MIN_VALUE;

    // Save this AOA for smoothing next time
    fPrevAOA = fAOA;

    return fAOA;
}

