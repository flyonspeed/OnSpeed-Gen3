

#include <math.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "RunningAverage.h"
#include "RunningMedian.h"

#include "Globals.h"
#include "Config.h"
#include "Flaps.h"
#include "SensorIO.h"

using onspeed::SuCalibrationCurve;
using onspeed::AOACalculatorResult;
using onspeed::CurveCalc;
using onspeed::psi2mb;


static inline float PressureAltitudeFeetFromMbar(float fStaticMbar)
{
    // Calculate pressure altitude. Pstatic in milliBars, Palt in feet.
    // Bias is stored as millibars to subtract from measured pressure.
    const float fStaticBiasCorrected = fStaticMbar - g_Config.fPStaticBias;
    if (fStaticBiasCorrected <= 0.0f)
        return g_Sensors.Palt;

    return 145366.45f * (1.0f - powf(fStaticBiasCorrected / 1013.25f, 0.190284f));
}

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

    xLastWakeTime = xLAST_TICK_TIME(PRESSURE_INTERVAL_MS);

    while (true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PRESSURE_INTERVAL_MS));

        // If this task wasn't delayed before it ran again it means it
        // it ran long for some reason (like the CPU is overloaded) or
        // or was stopped for a time (like during sensor cal). Regardless,
        // make sure the xLastWakeTime parameter is set to an integer
        // multiple of the delay time to maintain time alignment of
        // the data.
        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(PRESSURE_INTERVAL_MS);
            unsigned long uNow = millis();
            if ((uNow - uLastLateLogMs) > 1000)
            {
                g_Log.println(MsgLog::EnSensors, MsgLog::EnWarning, "SensorReadTask Late");
                uLastLateLogMs = uNow;
            }
        }

//        uCurrMicros = micros();

        g_Sensors.Read();

    }

} // end SensorReadTask


// ----------------------------------------------------------------------------

// FreeRTOS task for reading IMU + updating AHRS at IMU_SAMPLE_RATE

void ImuReadTask(void *pvParams)
{
    (void)pvParams;

    // 1 second = 1,000,000us. Use a fractional accumulator so the average period is exact
    // for rates that don't divide 1,000,000 evenly.
    const uint32_t uBasePeriodUs   = 1000000UL / IMU_SAMPLE_RATE; // 4807us @ 208Hz
    const uint32_t uRemainderUs    = 1000000UL % IMU_SAMPLE_RATE; // 144us  @ 208Hz
    uint32_t       uRemainderAcc   = 0;
    uint32_t       uNextWakeUs     = micros();
    uint32_t       uLastImuReadUs  = uNextWakeUs;
    unsigned long  uLastLateLogMs  = 0;

    while (true)
    {
        // Schedule the next tick.
        uNextWakeUs += uBasePeriodUs;
        uRemainderAcc += uRemainderUs;
        if (uRemainderAcc >= IMU_SAMPLE_RATE)
        {
            uNextWakeUs += 1;
            uRemainderAcc -= IMU_SAMPLE_RATE;
        }

        // Wait until it's time (coarse sleep then microsecond trim).
        int32_t iWaitUs = int32_t(uNextWakeUs - micros());
        if (iWaitUs > 2000)
            vTaskDelay(pdMS_TO_TICKS((iWaitUs - 1000) / 1000));

        iWaitUs = int32_t(uNextWakeUs - micros());
        if (iWaitUs > 0)
            delayMicroseconds(uint32_t(iWaitUs));

        // If this task is running late, log it (rate-limited).
        const int32_t iLateUs = int32_t(micros() - uNextWakeUs);
        if (iLateUs > 1000)
        {
            const unsigned long uNowMs = millis();
            if ((uNowMs - uLastLateLogMs) > 1000)
            {
                g_Log.println(MsgLog::EnIMU, MsgLog::EnWarning, "ImuReadTask Late");
                uLastLateLogMs = uNowMs;
            }

            // Don't try to "catch up" forever; re-sync to now.
            uNextWakeUs = micros();
            uRemainderAcc = 0;
        }

        // Read IMU over SPI (guard the bus).
        xSemaphoreTake(xSensorMutex, portMAX_DELAY);
        const uint32_t uImuReadUs = micros();
        g_pIMU->Read();
        const float fStaticMbar = g_pStatic->ReadPressureMillibars();
        xSemaphoreGive(xSensorMutex);

        g_Sensors.PStatic = fStaticMbar;
        g_Sensors.Palt    = PressureAltitudeFeetFromMbar(fStaticMbar);

        const uint32_t uDtUs = uImuReadUs - uLastImuReadUs;
        uLastImuReadUs = uImuReadUs;
        const float fDtSeconds = (uDtUs > 0) ? (float(uDtUs) * 1.0e-6f) : (1.0f / IMU_SAMPLE_RATE);

        // Update AHRS (guard against re-entrant Process() calls).
        xSemaphoreTake(xAhrsMutex, portMAX_DELAY);
        g_AHRS.Process(fDtSeconds);
        xSemaphoreGive(xAhrsMutex);
    }
}



// ============================================================================

SensorIO::SensorIO()
    : PfwdMedian(g_Config.iPressureSmoothing),
      PfwdAvg(10),
      P45Median(g_Config.iPressureSmoothing),
      P45Avg(10),
      IasDerivative(&fIasDerInput, 15),
      OneWireBus(OAT_PIN),
      OatSensor(&OneWireBus)
{
    Palt       = 0.00;
    fDecelRate = 0.0;
    uIasUpdateUs = 0;
}

// ----------------------------------------------------------------------------

void SensorIO::Init()
{
    if (g_Config.bOatSensor)
        {
        pinMode(OAT_PIN,INPUT_PULLUP);
        OatSensor.begin();  // initialize the DS18B20 sensor
        ReadOatC();
        }

    // Get initial pressure altitude
    ReadPressureAltMbars();

    // Configure AOA calculator smoothing
    AoaCalc.setSamples(g_Config.iAoaSmoothing);
}

// ----------------------------------------------------------------------------

// Read pressure sensors and calculate AOA and IAS

void SensorIO::Read()
{
    float           PfwdPascal;

    // Read pressure sensors
    xSemaphoreTake(xSensorMutex, portMAX_DELAY);
    iPfwd    = g_pPitot->ReadPressureCounts() - g_Config.iPFwdBias;
    iP45     = g_pAOA->ReadPressureCounts()   - g_Config.iP45Bias;
    xSemaphoreGive(xSensorMutex);

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
    if (g_Config.bOatSensor && (millis() - uLastOatReadMs > 1000))
        {
        ReadOatC();
        uLastOatReadMs = millis();
        }

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
        const SuCalibrationCurve& curve = g_Config.aFlaps[g_Flaps.iIndex].AoaCurve;
        AOACalculatorResult result = AoaCalc.calculate(PfwdSmoothed, P45Smoothed, curve);
        AOA = result.aoa;
        g_fCoeffP = result.coeffP;

        // Calculate airspeed
        // Calculate airspeed from smoothed dynamic pressure
        // The smoothed value is without bias, so we add it back for the PSI conversion.
        float PfwdPSI = g_pPitot->ReadPressurePSI(PfwdSmoothed + g_Config.iPFwdBias);
        PfwdPascal = psi2mb(PfwdPSI) * 100; // Convert PSI to Pascals
        if (PfwdPascal > 0)
        {
            IAS = sqrtf(2.0f*PfwdPascal/1.225f) * 1.94384f; // knots // physics based calculation
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

	    uIasUpdateUs = micros();

	    // Take derivative of airspeed for deceleration calc.
	    // Match the 10Hz display behavior by updating DecelRate at 100ms intervals.
	    // C++ guarantees static locals are initialized on first function entry,
	    // so millis() is called at runtime (not at static-init time).
	    static unsigned long uLastDecelUpdateMs = millis();
    const unsigned long uNowMs = millis();
    const unsigned long uDecelDeltaMs = uNowMs - uLastDecelUpdateMs;
    if (uDecelDeltaMs >= 100)
    {
        uLastDecelUpdateMs = uNowMs;

#ifdef SPHERICAL_PROBE
        fIasDerInput = g_EfisSerial.suEfis.IAS;
#else
        fIasDerInput = IAS;
#endif

        // SavGolDerivative returns derivative per sample; scale by actual update frequency (kts/sec).
        const float fDecelSampleHz = (uDecelDeltaMs > 0) ? (1000.0f / float(uDecelDeltaMs)) : 10.0f;
        // Positive for increasing IAS, negative for decreasing IAS (deceleration).
        fDecelRate = IasDerivative.Compute() * fDecelSampleHz;
    }

#ifdef LOGDATA_PRESSURE_RATE
    g_LogSensor.Write();
#endif
    g_AudioPlay.UpdateTones();

    if (g_Log.Test(MsgLog::EnSensors, MsgLog::EnDebug) == true)
    {
        static unsigned long uLastDebugPrintMs = 0;
        if ((uNowMs - uLastDebugPrintMs) >= 1000)
        {
            uLastDebugPrintMs = uNowMs;
            g_Log.printf("timeStamp: %lu,iPfwd: %i,PfwdSmoothed: %.2f,iP45: %i,P45Smoothed: %.2f,Pstatic: %.2f,Palt: %.2f,IAS: %.2f,AOA: %.2f,flapsPos: %i,VerticalG: %.2f,LateralG: %.2f,ForwardG: %.2f,RollRate: %.2f,PitchRate: %.2f,YawRate: %.2f, SmoothedPitch %.2f\n",
                millis(), iPfwd, PfwdSmoothed, iP45, P45Smoothed, PStatic, Palt, IAS, AOA, g_Flaps.iPosition,
                g_AHRS.AccelVertComp, g_AHRS.AccelLatComp, g_AHRS.AccelFwdComp,
                g_AHRS.gRoll, g_AHRS.gPitch, g_AHRS.gYaw, g_AHRS.SmoothedPitch);
        }
    } // end if debug print

} // end Read()


// ----------------------------------------------------------------------------

// Get pressure altitude. Pstatic in milliBars, Palt in feet.

float SensorIO::ReadPressureAltMbars()
{

    // Calculate pressure altitude. Pstatic in milliBars, Palt in feet.
    xSemaphoreTake(xSensorMutex, portMAX_DELAY);
    PStatic = g_pStatic->ReadPressureMillibars();
    xSemaphoreGive(xSensorMutex);
    Palt    = PressureAltitudeFeetFromMbar(PStatic);

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
