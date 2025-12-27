
//#include <arduino.h>
#include "globals.h"

// Move audio with the ball, scaling is 0.08 LateralG/ball width
// This curve works up to about 0.08 G of lateral acceleration
#define AUDIO_3D_CURVE(x)         -92.822*x*x + 20.025*x

float       fChannelGain     = 1.0;
const float fSmoothingFactor = 0.1;

// ----------------------------------------------------------------------------

void Check3DAudioTask(void * pvParams)
    {
    float       fLateralG;
    int         iSignLateralG;
    float       fCurveGain;

    while (true)
        {
        // Run every 100 msec
        vTaskDelay(pdMS_TO_TICKS(100));

        if (g_Config.bAudio3D)
            {
            fLateralG     = g_AHRS.AccelLatCorr;
            iSignLateralG = fLateralG >= 0 ? 1 : -1;     // (fLateralG > 0) - (fLateralG < 0);

            fCurveGain = AUDIO_3D_CURVE(abs(fLateralG));
            if (fCurveGain > 1.0) fCurveGain = 1.0;
            if (fCurveGain < 0.0) fCurveGain = 0.0;

            fCurveGain   = fCurveGain * iSignLateralG;
            fChannelGain = fSmoothingFactor * fCurveGain + (1 - fSmoothingFactor) * fChannelGain;

            // With no lateral G's then the gain for each channel is 1.0. As lateral G increase
            // one channel increase gain up to about 2.0. The other channel decrease gain down
            // to 0.0. This only works to about 0.08 G of lateral acceleration.

            float fLeftGain  = abs(-1 + fChannelGain);
            float fRightGain = abs( 1 + fChannelGain);
            g_AudioPlay.SetGain(fLeftGain, fRightGain);

            g_Log.printf(MsgLog::EnAudio, MsgLog::EnDebug, "%0.3fG, Left: %0.3f, Right: %0.3f\n", fLateralG, fLeftGain, fRightGain);
            } // end if 3D audio enabled

        } // end while forever
    }
