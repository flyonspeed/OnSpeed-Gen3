
#include "Globals.h"
#include "Config.h"

// Calculate an output value based on one of the defined curves. This is used
// to calcualte AOA from coefficient of pressure but really it could be used
// for anything.
//
// y = f(x)

float CurveCalc(float fX, SuCalibrationCurve suCurve )
{
    float fY = 0.00;

    // Polynomial
    if (suCurve.iCurveType == 1) 
    {
        for (int i=0; i<MAX_CURVE_COEFF; i++)
        {  
            fY += suCurve.afCoeff[i]*pow(fX,MAX_CURVE_COEFF-i-1);
            g_Log.printf(MsgLog::EnAHRS, MsgLog::EnDebug, "%.2f * pow(%.2f,%i)+",suCurve.afCoeff[i],fX,MAX_CURVE_COEFF-i-1);
        }
        g_Log.printf(MsgLog::EnAHRS, MsgLog::EnDebug, "=%.2f\n",fY);
    } 

    // Logarithmic  ex: 21 * log(x) + 16.45
    else if (suCurve.iCurveType == 2)  
    {
        fY = suCurve.afCoeff[MAX_CURVE_COEFF-2]*log(fX) + suCurve.afCoeff[MAX_CURVE_COEFF-1]; //use only last two coefficients
        g_Log.printf(MsgLog::EnAHRS, MsgLog::EnDebug, "%.2f * log(%.2f)+%.2f\n",suCurve.afCoeff[MAX_CURVE_COEFF-2],fX,suCurve.afCoeff[MAX_CURVE_COEFF-1]);
    }

    // Exponential ex: 12.5*e^(-1.63x);
    else if (suCurve.iCurveType==3) 
    {                          
        fY = suCurve.afCoeff[MAX_CURVE_COEFF-2]*exp(suCurve.afCoeff[MAX_CURVE_COEFF-1]*fX);
    }

    return fY;
}
