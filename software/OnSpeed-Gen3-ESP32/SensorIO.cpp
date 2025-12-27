

#include <OneWire.h>
#include <DallasTemperature.h>

#include "RunningAverage.h"
#include "RunningMedian.h"

#include "Globals.h"
#include "Config.h"
#include "Flaps.h"
#include "AOAcalc.h"
#include "CurveCalc.h"
#include "SensorIO.h"

// These from config
//int     aoaSmoothing      = 20; // AOA smoothing window (number of samples to lag)
//int     pressureSmoothing = 15; // median filter window for pressure smoothing/despiking

// Timers to reduce read frequency for less critical sensors
static uint32_t uLastFlapsReadMs = 0;
static uint32_t uLastOatReadMs = 0;


// ----------------------------------------------------------------------------

// FreeRTOS task for reading sensors

void SensorReadTask(void *pvParams)
{
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime;
    static unsigned long uLastLateLogMs = 0;
//    unsigned long   uStartMicros = micros();
//    unsigned long   uCurrMicros;
//    static unsigned uLoops = 0;
//    static bool     bSendOK;

    xLastWakeTime = xLAST_TICK_TIME(20);

    while (true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));

        // If this task wasn't delayed before it ran again it means it
        // it ran long for some reason (like the CPU is overloaded) or
        // or was stopped for a time (like during sensor cal). Regardless,
        // make sure the xLastWakeTime parameter is set to an integer
        // multiple of the delay time to maintain time alignment of
        // the data.
        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(20);
            unsigned long uNow = millis();
            if ((uNow - uLastLateLogMs) > 1000)
            {
                g_Log.println(MsgLog::EnSensors, MsgLog::EnWarning, "SensorReadTask Late");
                uLastLateLogMs = uNow;
            }
        }

//        uCurrMicros = micros();

        // There are some other places in the code where sensors are read, for example
        // the sensor bias routines. So wrap this sensor reading routine in a
        // semaphore to make sure only one routine is reading sensors and modifying
        // data at a time. There are no other routines that read sensors on a regular
        // basis so it is OK to skip a read if someone else has the mutex. Go ahead
        // and wait 5 msec just in case someone is doing a quick sensor read.
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)))
        {
            g_Sensors.Read();
            xSemaphoreGive(xSensorMutex);
        }

    }

} // end SensorReadTask



// ============================================================================

SensorIO::SensorIO()
    : P45Median(g_Config.iPressureSmoothing),
      PfwdMedian(g_Config.iPressureSmoothing),
      PfwdAvg(10),
      P45Avg(10),
      OneWireBus(OAT_PIN),
      OatSensor(&OneWireBus),
      IasDerivative(&fIasDerInput, 1, 15)
{
    Palt       = 0.00;
    fDecelRate = 0.0;
}

// ----------------------------------------------------------------------------

void SensorIO::Init()
{
#ifdef OAT_AVAILABLE
    pinMode(OAT_PIN,INPUT_PULLUP);
    OatSensor.begin();  // initialize the DS18B20 sensor
    ReadOatC();
#endif

    // Get initial pressure altitude
    ReadPressureAltMbars();

}

// ----------------------------------------------------------------------------

// Read pressure sensors and calculate AOA and IAS

void SensorIO::Read()
{

    int             iFlapsIndex;
    float           PfwdPascal;

    // Read pressure sensors
    iPfwd   = g_pPitot->ReadPressureCounts() - g_Config.iPFwdBias;
    iP45    = g_pAOA->ReadPressureCounts()   - g_Config.iP45Bias;
    ReadPressureAltMbars();

    // Read IMU
    g_pIMU->Read();

    // Update flaps position about once per second
    if (millis() - uLastFlapsReadMs > 1000)
    {
        if (g_Config.suDataSrc.enSrc != SuDataSource::EnTestPot)
            g_Flaps.Update();
        else
            g_Flaps.Update(0);
        uLastFlapsReadMs = millis();
    }

    // Update the OAT about once per second
#ifdef OAT_AVAILABLE
    if (millis() - uLastOatReadMs > 1000) {
        ReadOatC();
        uLastOatReadMs = millis();
    }
#endif

    // Process AHRS now
    g_AHRS.Process();

    // Get AOA speed set points for the current flap position.
//  SetAOApoints(g_Flaps.iIndex);

    // Median filter pressure then a simple moving average
    PfwdMedian.add(iPfwd);
    PfwdAvg.addValue(PfwdMedian.getMedian());
    PfwdSmoothed = PfwdAvg.getFastAverage();

    P45Median.add(iP45);
    P45Avg.addValue(P45Median.getMedian());
    P45Smoothed = P45Avg.getFastAverage();

    // Calculate AOA based on Pfwd/P45;
    if ((g_Config.suDataSrc.enSrc != SuDataSource::EnTestPot) &&
        (g_Config.suDataSrc.enSrc != SuDataSource::EnRangeSweep))
    {
        AOA = CalcAOA(PfwdSmoothed,P45Smoothed, g_Flaps.iIndex, g_Config.iAoaSmoothing);

        // Calculate airspeed
        //PfwdPascal = ((PfwdSmoothed + g_Config.iPFwdBias - 0.1*16383) * 2/(0.8*16383) -1) * 6894.757;

        // Calculate airspeed from smoothed dynamic pressure
        // The smoothed value is without bias, so we add it back for the PSI conversion.
        float PfwdPSI = g_pPitot->ReadPressurePSI(PfwdSmoothed + g_Config.iPFwdBias);
        PfwdPascal = PSI2MB(PfwdPSI) * 100; // Convert PSI to Pascals
        if (PfwdPascal > 0)
        {
            IAS = sqrt(2*PfwdPascal/1.225)* 1.94384; // knots // physics based calculation
#ifdef SPHERICAL_PROBE
            IAS = IASCURVE(IAS); // for now use a hardcoded IAS curve for a spherical probe. CAS curve parameters can only take 4 decimals. Not accurate enough.
#else
            if (g_Config.bCasCurveEnabled)
                IAS = CurveCalc(g_Sensors.IAS,g_Config.CasCurve);  // use CAS correction curve if enabled
#endif
        }

        // Catch negative pressure case
        else
            IAS = 0;
    } // end if not in test pot or range sweep mode

    // Take derivative of airspeed for decelaration calc
#ifdef SPHERICAL_PROBE
    fIasDerInput = g_EfisSerial.suEfis.IAS;
#else
    fIasDerInput = IAS;
#endif
    //// This logic seems suspect but this is the way it is done in the original code
    // SavLayFilter returns derivative per sample; convert to kts/sec for the 20ms sensor task period.
    const float fSensorSampleHz = 50.0f;
    fDecelRate = -IasDerivative.Compute() * fSensorSampleHz;

#ifdef LOGDATA_PRESSURE_RATE
    g_LogSensor.Write();
#endif
    g_AudioPlay.UpdateTones();

    if (g_Log.Test(MsgLog::EnSensors, MsgLog::EnDebug) == true)
    {
        static int iDecimate = 0;  // Decimate to once per second

        if (iDecimate == 0)
        {
            g_Log.printf("timeStamp: %lu,iPfwd: %i,PfwdSmoothed: %.2f,iP45: %i,P45Smoothed: %.2f,Pstatic: %.2f,Palt: %.2f,IAS: %.2f,AOA: %.2f,flapsPos: %i,VerticalG: %.2f,LateralG: %.2f,ForwardG: %.2f,RollRate: %.2f,PitchRate: %.2f,YawRate: %.2f, SmoothedPitch %.2f\n",
                millis(), iPfwd, PfwdSmoothed, iP45, P45Smoothed, PStatic, Palt, IAS, AOA, g_Flaps.iPosition,
                g_AHRS.AccelVertComp, g_AHRS.AccelLatComp, g_AHRS.AccelFwdComp,
                g_AHRS.gRoll, g_AHRS.gPitch, g_AHRS.gYaw, g_AHRS.SmoothedPitch);

            iDecimate = 50;
        }
        else
            iDecimate--;
    } // end if debug print

} // end Read()


// ----------------------------------------------------------------------------

// Get pressure altitude. Pstatic in milliBars, Palt in feet.

float SensorIO::ReadPressureAltMbars()
{

    // Calculate pressure altitude. Pstatic in milliBars, Palt in feet.
    PStatic = g_pStatic->ReadPressureMillibars();
    Palt    = 145366.45 * (1 - pow((PStatic - g_Config.fPStaticBias) / 1013.25, 0.190284));

    g_Log.printf(MsgLog::EnPressure, MsgLog::EnDebug, "pStatic %8.3f mb Bias %6.3f mb Palt %5.0f\n", PStatic, g_Config.fPStaticBias, Palt);

    return Palt;
}


// ----------------------------------------------------------------------------

// Return the OAT in degrees C
// OAT is so simple I just stuck it in this class for convenience.

float SensorIO::ReadOatC()
{
    OatSensor.requestTemperatures();      // Send the command to get temperatures
    OatC = OatSensor.getTempCByIndex(0);  // Read temperature in �C

    return OatC;
}
