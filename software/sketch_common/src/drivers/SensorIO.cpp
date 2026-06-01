

#include <math.h>
#include <type_traits>

#include "src/Globals.h"
#include "src/ahrs/AhrsSnapshot.h"
#include "src/drivers/Ds18b20.h"
#include "src/config/Config.h"
#include "src/tasks/Flaps.h"
#include <sensors/IasAlive.h>
#include <sensors/PressureConvert.h>
#include <sensors/OatConvert.h>
#include <util/Perf.h>

// ============================================================================
// Mutex graph for SensorIO and downstream readers:
//   xSensorMutex - guards the shared SPI bus (IMU + 3 pressure sensors +
//                  MCP3202 ADC).  Held for microseconds at a time, only
//                  for the duration of an SPI transaction.
//   xAhrsMutex   - guards AHRS state (Madgwick / EKFQ) AND the swap of
//                  g_Config.aFlaps + the matching write to g_Flaps.iIndex
//                  in HandleConfigSave.  Per-flap setpoints and the AOA
//                  polynomial must therefore be snapshotted under
//                  xAhrsMutex by every reader on the flight-data path
//                  (SensorIO::Read -> Audio.cpp::UpdateTones, the AOA
//                  curve evaluation, DisplaySerial::Write,
//                  DataServer::UpdateLiveDataJson, Flaps::Update).
//
// Rule: never nest these two.  If a code path needs both, take
// xSensorMutex first, release, then take xAhrsMutex.  Nested takes
// would risk deadlock the moment a future caller takes them in the
// other order; staying un-nested makes that class of bug structurally
// impossible.
// ============================================================================

using onspeed::SuCalibrationCurve;
using onspeed::AOACalculatorResult;
using onspeed::CurveCalc;

// Snapshotting the active flap entry assumes a trivial copy is sound.
// SuCalibrationCurve is a POD; SuFlaps is a small aggregate of scalars and
// one SuCalibrationCurve.  These static_asserts catch the day someone adds
// a non-trivially-copyable member (a std::string for a setpoint label,
// say) which would silently break the snapshot pattern.
static_assert(std::is_trivially_copyable_v<SuCalibrationCurve>,
              "SuCalibrationCurve must remain trivially copyable so the "
              "ActiveFlapSnapshot copy is safe under a brief mutex window.");
static_assert(std::is_trivially_copyable_v<FOSConfig::SuFlaps>,
              "FOSConfig::SuFlaps must remain trivially copyable so the "
              "DisplaySerial / DataServer per-flap snapshots are safe.");

// ----------------------------------------------------------------------------

ActiveFlapSnapshot SnapshotActiveFlap()
{
    ActiveFlapSnapshot snap{};
    snap.bValid = false;

    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return snap;

    const size_t nFlaps = g_Config.aFlaps.size();
    const int    iIdx   = g_Flaps.iIndex;
    if (iIdx >= 0 && (size_t)iIdx < nFlaps)
    {
        const auto& flap   = g_Config.aFlaps[iIdx];
        snap.curve         = flap.AoaCurve;
        snap.th.fLDMAXAOA       = flap.fLDMAXAOA;
        snap.th.fONSPEEDFASTAOA = flap.fONSPEEDFASTAOA;
        snap.th.fONSPEEDSLOWAOA = flap.fONSPEEDSLOWAOA;
        snap.th.fSTALLWARNAOA   = flap.fSTALLWARNAOA;
        snap.bValid             = true;
    }

    xSemaphoreGive(xAhrsMutex);
    return snap;
}


static inline float PressureAltitudeFeetFromMbar(float fStaticMbar)
{
    // Calculate pressure altitude. Pstatic in milliBars, Palt in feet.
    // Bias is stored as millibars to subtract from measured pressure.
    const float fStaticBiasCorrected = fStaticMbar - g_Config.fPStaticBias;
    if (fStaticBiasCorrected <= 0.0f)
        return g_Sensors.Palt;

    // Delegate the ISA formula to the platform-independent core function.
    return onspeed::sensors::StaticMbarToPaltFt(fStaticBiasCorrected);
}

// These from config
//int     aoaSmoothing      = 10; // AOA smoothing window (number of samples to lag)
//int     pressureSmoothing = 15; // median filter window for pressure smoothing/despiking

// Timers to reduce read frequency for less critical sensors
static uint32_t uLastFlapsReadMs = 0;
static uint32_t uLastOatReadMs = 0;

// OAT validation is now delegated to onspeed::sensors::FilterOat() in core.
// The valid range, disconnect sentinel, and POR sentinel are all defined there.
static constexpr float kOatDefaultC       = 15.0f;   // ISA standard day fallback
static constexpr uint32_t kOatConversionMs = 800;    // DS18B20 12-bit max is 750 ms


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

    xLastWakeTime = xLAST_TICK_TIME(kPressureIntervalMs);

    while (true)
    {
        // No delay happening is a design flaw so flag it if it happens, or
        // rather doesn't happen.
        xWasDelayed = xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(kPressureIntervalMs));

        // PERF: time only the work after the wait.
        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::Sensors,
            uxTaskGetStackHighWaterMark(nullptr));

        // If this task wasn't delayed before it ran again it means it
        // it ran long for some reason (like the CPU is overloaded) or
        // or was stopped for a time (like during sensor cal). Regardless,
        // make sure the xLastWakeTime parameter is set to an integer
        // multiple of the delay time to maintain time alignment of
        // the data.
        if (xWasDelayed == pdFALSE)
        {
            xLastWakeTime = xLAST_TICK_TIME(kPressureIntervalMs);
            unsigned long uNow = millis();
            if ((uNow - uLastLateLogMs) > 1000)
            {
                g_Log.println(MsgLog::EnSensors, MsgLog::EnWarning, "SensorReadTask Late");
                uLastLateLogMs = uNow;
            }
        }

//        uCurrMicros = micros();

        g_Sensors.Read();

        // 50 Hz log write. The IMU-rate path mirrors this in ImuReadTask.
        // Both writes live in the task loops (not in driver primitives)
        // so the bring-up sensor reads in setup() — which run before the
        // logging ring buffer is allocated — cannot enter Write().
        // Use `< 208` (not `!= 208`) so any future higher IMU rate
        // still takes the IMU-rate write path rather than this one.
        if (g_Config.iLogRate < 208)
            g_LogSensor.Write();
    }

} // end SensorReadTask


// ----------------------------------------------------------------------------

// PERF instrumentation for IMU lateness. The IMU read task is scheduled
// at uBasePeriodUs intervals (4807 us @ 208Hz). When something on this
// core (sync, write, mutex contention) prevents the task from running
// on schedule, iLateUs accumulates and the task surrenders the missed
// sample by resetting uNextWakeUs = micros().
//
// These counters let us see post-flight how often the surrender path
// fires and how badly. Read by LogSensor's PERF tick via __atomic_*.
volatile uint32_t g_uImuLateResets = 0;     // count of schedule-resets in window
volatile uint32_t g_uImuMaxLateUs  = 0;     // worst lateness observed in window
// All-time worst lateness (never reset by PERF emits). Catches bursty
// multi-ms stalls that the per-window counter loses when the PERF
// heartbeat fires mid-event. Read-only after boot from a pilot's
// perspective; only `perf reset` (issue #N) zeroes it.
volatile uint32_t g_uImuMaxLateUsAllTime = 0;

// FreeRTOS task for reading IMU + updating AHRS at g_imuSampleRateHz

void ImuReadTask(void *pvParams)
{
    (void)pvParams;

    // Snapshot the boot-latched IMU rate into a local so the schedule
    // math is stable for the lifetime of the task. g_imuSampleRateHz is
    // read-only after setup() but cache the value locally to avoid an
    // implicit assumption about that ever changing.
    const int      iSampleRateHz   = g_imuSampleRateHz;

    // 1 second = 1,000,000us. Use a fractional accumulator so the average period is exact
    // for rates that don't divide 1,000,000 evenly.
    const uint32_t uBasePeriodUs   = 1000000UL / iSampleRateHz; // 4807us @ 208Hz, 2403us @ 416Hz
    const uint32_t uRemainderUs    = 1000000UL % iSampleRateHz; // 144us  @ 208Hz, 72us   @ 416Hz
    uint32_t       uRemainderAcc   = 0;
    uint32_t       uNextWakeUs     = micros();
    uint32_t       uLastImuReadUs  = uNextWakeUs;
    unsigned long  uLastLateLogMs  = 0;

    while (true)
    {
        // Schedule the next tick.
        uNextWakeUs += uBasePeriodUs;
        uRemainderAcc += uRemainderUs;
        if (uRemainderAcc >= (uint32_t)iSampleRateHz)
        {
            uNextWakeUs += 1;
            uRemainderAcc -= (uint32_t)iSampleRateHz;
        }

        // Wait until it's time (coarse sleep then microsecond trim).
        int32_t iWaitUs = int32_t(uNextWakeUs - micros());
        if (iWaitUs > 2000)
            vTaskDelay(pdMS_TO_TICKS((iWaitUs - 1000) / 1000));

        iWaitUs = int32_t(uNextWakeUs - micros());
        if (iWaitUs > 0)
            delayMicroseconds(uint32_t(iWaitUs));

        // PERF: time only the work after the wait. Loop period is fixed
        // by the schedule above; we want CPU spent, not wall time.
        onspeed::util::perf::PerfLoop perfGuard(
            onspeed::util::perf::TaskId::Imu,
            uxTaskGetStackHighWaterMark(nullptr));

        // Track lateness on every iteration so the noise floor is
        // visible in PERF, not just the >1ms outliers. uImuMaxLateUs
        // captures the worst sub-iteration delay since last emit,
        // regardless of whether it tripped the reset threshold.
        const int32_t iLateUs = int32_t(micros() - uNextWakeUs);
        if (iLateUs > 0)
        {
            const uint32_t uLate = (uint32_t)iLateUs;
            uint32_t uPrev = __atomic_load_n(&g_uImuMaxLateUs, __ATOMIC_RELAXED);
            while (uLate > uPrev &&
                   !__atomic_compare_exchange_n(&g_uImuMaxLateUs, &uPrev, uLate,
                                                false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                {}

            // All-time worst — never reset by the per-window PERF emit.
            // A multi-ms stall that lands between heartbeats was getting
            // wiped by the next emit, so single-event spikes (like the
            // 613us blocked-on-xAhrsMutex case in the 416Hz stress) were
            // invisible. This counter preserves them.
            uint32_t uPrevAT = __atomic_load_n(&g_uImuMaxLateUsAllTime, __ATOMIC_RELAXED);
            while (uLate > uPrevAT &&
                   !__atomic_compare_exchange_n(&g_uImuMaxLateUsAllTime, &uPrevAT, uLate,
                                                false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                {}
        }

        // Schedule-reset on real lateness (>1 ms). Smaller hiccups let
        // the task naturally catch up via the iWaitUs > 0 check above —
        // next iteration just fires immediately. g_uImuLateResets counts
        // these schedule-resets (matching its name); the sub-ms
        // noise floor is captured by g_uImuMaxLateUs / g_uImuMaxLateUsAT
        // (peak metrics, no count) so a noisy floor doesn't trip the
        // PERF heartbeat on every iteration.
        if (iLateUs > 1000)
        {
            __atomic_fetch_add(&g_uImuLateResets, 1u, __ATOMIC_RELAXED);

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

        // Read IMU over SPI (guard the bus). Static-pressure read lives
        // in SensorReadTask now — at IMU rates >208 Hz, holding the
        // mutex for both IMU (~67us) + static (~200us) per iteration
        // caused ~12% of IMU iterations to block waiting on
        // SensorReadTask's pressure reads for up to ~970us (measured
        // during the 833 Hz characterization work). Static pressure
        // changes far slower than IMU rate; 50 Hz updates are plenty
        // for altitude. See #627 item 1 for the empirical data.
        xSemaphoreTake(xSensorMutex, portMAX_DELAY);
        const uint32_t uImuReadUs = micros();
        g_pIMU->Read();
        xSemaphoreGive(xSensorMutex);

        const uint32_t uDtUs = uImuReadUs - uLastImuReadUs;
        uLastImuReadUs = uImuReadUs;
        const float fDtSeconds = (uDtUs > 0) ? (float(uDtUs) * 1.0e-6f) : (1.0f / iSampleRateHz);

        // Update AHRS (guard against re-entrant Process() calls).
        xSemaphoreTake(xAhrsMutex, portMAX_DELAY);
        g_AHRS.Process(fDtSeconds);
        xSemaphoreGive(xAhrsMutex);

        // IMU-rate log write. The 50 Hz path mirrors this in
        // SensorReadTask. Both writes live in the task loops (not in
        // driver primitives) so the bring-up sensor reads in setup()
        // — which run before the logging ring buffer is allocated —
        // cannot enter Write().
        // Use `>= 208` (not `== 208`) so any future higher IMU rate
        // continues to take this path.
        if (g_Config.iLogRate >= 208)
            g_LogSensor.Write();
    }
}



// ============================================================================

SensorIO::SensorIO()
    : PfwdMedian(g_Config.iPressureSmoothing),
      PfwdAvg(10),
      P45Median(g_Config.iPressureSmoothing),
      P45Avg(10),
      IasDerivative(&fIasDerInput, 15),
      OatSensor(kPinOat)
{
    Palt       = 0.00;
    OatC       = kOatDefaultC;
    fDecelRate = 0.0;
    uIasUpdateUs = 0;
    bIasAlive  = false;
}

// ----------------------------------------------------------------------------

void SensorIO::Init()
{
    if (g_Config.bOatSensor)
        {
        pinMode(kPinOat,INPUT_PULLUP);
        OatSensor.Begin(12);                     // 12-bit: ~750 ms conversion time
        ReadOatC();                              // blocking read OK before scheduler starts
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
    // Read pressure sensors — pitot, AOA, and static all share the SPI
    // bus, so batch them under one mutex acquire. Static was previously
    // read inside ImuReadTask's mutex hold, which at IMU rates >208 Hz
    // caused mutex contention with this task's pressure reads (see
    // #627 item 1). Static pressure changes slowly; 50 Hz is plenty.
    xSemaphoreTake(xSensorMutex, portMAX_DELAY);
    iPfwd                   = g_pPitot ->ReadPressureCounts()   - g_Config.iPFwdBias;
    iP45                    = g_pAOA   ->ReadPressureCounts()   - g_Config.iP45Bias;
    const float fStaticMbar = g_pStatic->ReadPressureMillibars();
    xSemaphoreGive(xSensorMutex);

    PStatic = fStaticMbar;
    Palt    = PressureAltitudeFeetFromMbar(fStaticMbar);

    // Update flaps position about once per second.  Flaps::Read() takes
    // xSensorMutex itself for its SPI transaction, and Flaps::Update()
    // takes xAhrsMutex for the size+index snapshot.  Calling them here
    // without an outer mutex keeps xSensorMutex and xAhrsMutex strictly
    // un-nested on this task -- a precondition for the AOA snapshot
    // below, which takes xAhrsMutex from outside any xSensorMutex hold.
    if (millis() - uLastFlapsReadMs > 1000)
    {
        if (g_Config.suDataSrc.enSrc != SuDataSource::EnTestPot)
            g_Flaps.Update();
        else
            g_Flaps.Update(0);
        uLastFlapsReadMs = millis();
    }

    // Update the OAT asynchronously (non-blocking).
    // Phase 1: kick off a conversion request once per second.
    // Phase 2: read the result after the 750 ms conversion completes.
    if (g_Config.bOatSensor)
    {
        if (!bOatConversionPending && (millis() - uLastOatReadMs > 1000))
        {
            OatSensor.RequestConversion();      // non-blocking kick
            bOatConversionPending = true;
            uOatRequestMs = millis();
        }
        else if (bOatConversionPending && (millis() - uOatRequestMs >= kOatConversionMs))
        {
            float fNew = OatSensor.ReadCelsius();
            // Delegate sentinel and range filtering to the platform-independent
            // core function (OatConvert::FilterOat). Returns nullopt for
            // -127 (disconnect), 85 (POR), or out-of-range values.
            auto validated = onspeed::sensors::FilterOat(fNew);
            if (validated.has_value())
            {
                OatC = *validated;    // valid reading
            }
            else
            {
                // Sensor disconnected or returning garbage; hold last good value.
                // Log once per minute to avoid spam.
                static unsigned long uLastOatWarnMs = 0;
                unsigned long uNow = millis();
                if ((uNow - uLastOatWarnMs) > 60000)
                {
                    g_Log.println(MsgLog::EnSensors, MsgLog::EnWarning,
                        "OAT sensor read failed; holding last good value");
                    uLastOatWarnMs = uNow;
                }
            }
            bOatConversionPending = false;
            uLastOatReadMs = millis();
        }
    }

    // Get AOA speed set points for the current flap position.
//  SetAOApoints(g_Flaps.iIndex);

    // Median filter pressure then a simple moving average
    PfwdMedian.add(iPfwd);
    PfwdAvg.addValue(PfwdMedian.getMedian());
    PfwdSmoothed = PfwdAvg.getFastAverage();

    // Clamp residual sensor noise (~1-2 LSB after median+average) to zero.
    // The sqrt() in the pitot equation amplifies the noise floor into
    // 4-8 kt of phantom IAS at rest; clamping in the pressure domain kills
    // it before IAS is computed — PitotPsiToIasKt(0) = 0, so the zero
    // cascades cleanly through AOA and IAS with no step discontinuity.
    // Magic constant + reasoning live in sensors/IasAlive.h.
    PfwdSmoothed = onspeed::sensors::ApplyPfwdDeadband(PfwdSmoothed);

    P45Median.add(iP45);
    P45Avg.addValue(P45Median.getMedian());
    P45Smoothed = P45Avg.getFastAverage();

    // Snapshot the AOA polynomial AND the four tone setpoints from the
    // active flap entry in a single xAhrsMutex window.  Doing both reads
    // under one take guarantees the AOA computed from the polynomial and
    // the tone decision made from the setpoints come from the same
    // aFlaps[iIndex] revision -- no half-old / half-new combination
    // across a HandleConfigSave swap on Core 0.  The snapshot is then
    // passed by const reference into both AoaCalc and UpdateTones so
    // neither function indexes g_Config.aFlaps directly.
    const ActiveFlapSnapshot snap = SnapshotActiveFlap();

    // Calculate AOA based on Pfwd/P45;
    if ((g_Config.suDataSrc.enSrc != SuDataSource::EnTestPot) &&
        (g_Config.suDataSrc.enSrc != SuDataSource::EnRangeSweep))
    {
        if (snap.bValid)
        {
            AOACalculatorResult result = AoaCalc.calculate(PfwdSmoothed, P45Smoothed, snap.curve);
            AOA = result.aoa;
            g_fCoeffP = result.coeffP;
        }
        else
        {
            // Mutex timeout or out-of-bounds iIndex.  Hold AOA at zero so
            // downstream tone calc treats the input as uncalibrated and
            // stays silent; do not run the polynomial against a torn or
            // freed curve.
            AOA = 0.0f;
            g_fCoeffP = 0.0f;
        }

        // Calculate airspeed from smoothed dynamic pressure.
        // The smoothed value is without bias, so we add it back for the PSI conversion.
        float PfwdPSI = g_pPitot->ReadPressurePSI(PfwdSmoothed + g_Config.iPFwdBias);
        // Delegate the pitot equation to the platform-independent core function.
        IAS = onspeed::sensors::PitotPsiToIasKt(PfwdPSI);
        if (IAS > 0.0f)
        {
            if (g_Config.bCasCurveEnabled)
                IAS = CurveCalc(g_Sensors.IAS,g_Config.CasCurve);  // use CAS correction curve if enabled
        }

        // Only update the IAS timestamp when IAS was actually computed from
        // live sensor data. In TestPot/RangeSweep modes IAS is not updated,
        // so advancing the timestamp would cause AHRS to wastefully recompute
        // TAS at 50 Hz using stale data.
        uIasUpdateUs = micros();
    } // end if not in test pot or range sweep mode

    // IAS-alive hysteresis: downstream consumers (AHRS compensation,
    // audio mute, display gates) check this flag rather than comparing
    // IAS against a raw threshold, so the entire signal path shares one
    // coherent "air data valid" concept.  Thresholds + citations live
    // in sensors/IasAlive.h.
    {
        const bool bWasIasAlive = bIasAlive;
        bIasAlive = onspeed::sensors::UpdateIasDisplayable(
            bIasAlive, IAS,
            static_cast<float>(g_Config.iIasDisplayThresholdKt));

        // bIasAlive false→true transition (taxi→takeoff roll, IAS rising
        // through the deadband): the SavGol window holds ~15 frames of stale
        // dead-air samples (mostly zero with sensor noise).  Differencing
        // those against fresh post-transition samples produces a spurious
        // positive DecelRate spike for ~750 ms while the window flushes.
        // Reset the filter at the edge so the first 15 post-transition
        // frames read 0 instead of garbage.  See issue #481 and the
        // companion gap-triggered reset below (PR #477).
        if (bIasAlive && !bWasIasAlive)
        {
            IasDerivative.reset();
            fDecelRate = 0.0f;
        }
    }

    // Take derivative of airspeed for deceleration calc.
    // Update once per display serial period to match tone buffer update rate.
    // Initialized to 0 (sentinel meaning "never updated") so the outage-reset
    // guard below can distinguish first-tick from a long-since-last gap.
    static unsigned long uLastDecelUpdateMs = 0;
    const unsigned long uNowMs = millis();
    const unsigned long uDecelDeltaMs = uNowMs - uLastDecelUpdateMs;
    if (uDecelDeltaMs >= kDisplaySerialPeriodMs)
    {
        // Finding 036 (firmware-side mirror of OnSpeed-M5-Display/src/SerialRead.cpp).
        // A decel-update cadence gap > 500 ms (task stall, long sensor-cal block,
        // CPU overload that triggers the "SensorReadTask Late" path) leaves the
        // SavGol window holding stale pre-gap IAS samples; differencing them
        // against fresh post-gap samples produces a large spurious DecelRate
        // spike for ~15 frames, which the WebSocket ships as DecelRate to
        // /indexer.  Reset on the gap.  Guarded on uLastDecelUpdateMs != 0 so
        // the boot-time delta does not trip it.
        if (uLastDecelUpdateMs != 0 && uDecelDeltaMs > 500)
        {
            IasDerivative.reset();
            fDecelRate = 0.0f;
        }

        uLastDecelUpdateMs = uNowMs;

        fIasDerInput = IAS;

        // SavGolDerivative returns derivative per sample; scale by actual update frequency (kts/sec).
        const float fDecelSampleHz = (uDecelDeltaMs > 0) ? (1000.0f / float(uDecelDeltaMs)) : 10.0f;
        // Positive for increasing IAS, negative for decreasing IAS (deceleration).
        fDecelRate = IasDerivative.Compute() * fDecelSampleHz;
    }

    g_AudioPlay.UpdateTones(snap);

    if (g_Log.Test(MsgLog::EnSensors, MsgLog::EnDebug) == true)
    {
        static unsigned long uLastDebugPrintMs = 0;
        if ((uNowMs - uLastDebugPrintMs) >= 1000)
        {
            uLastDebugPrintMs = uNowMs;
            // Read the AHRS output fields from the coherent snapshot so
            // this 1 Hz debug line is internally consistent and doesn't
            // read g_AHRS members cross-core (this runs in SensorReadTask,
            // a different task from the ImuReadTask producer).
            const onspeed::ahrs::AhrsSnapshotPayload ahrsSnap =
                onspeed::ahrs::g_AhrsSnapshot.read();
            g_Log.printf("timeStamp: %lu,iPfwd: %i,PfwdSmoothed: %.2f,iP45: %i,P45Smoothed: %.2f,Pstatic: %.2f,Palt: %.2f,IAS: %.2f,AOA: %.2f,flapsPos: %i,VerticalG: %.2f,LateralG: %.2f,ForwardG: %.2f,RollRate: %.2f,PitchRate: %.2f,YawRate: %.2f, SmoothedPitch %.2f\n",
                millis(), iPfwd, PfwdSmoothed, iP45, P45Smoothed, PStatic, Palt, IAS, AOA, g_Flaps.iPosition,
                ahrsSnap.accelVertFilteredG, ahrsSnap.accelLatFilteredG, ahrsSnap.accelFwdFilteredG,
                ahrsSnap.gRollDps, ahrsSnap.gPitchDps, ahrsSnap.gYawDps, ahrsSnap.pitchDeg);
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
    // Blocking read — only called during Init() before the scheduler
    // starts. Ds18b20::BlockingReadCelsius does request + 750ms wait
    // + read scratchpad in one call.
    float fNew = OatSensor.BlockingReadCelsius();
    auto validated = onspeed::sensors::FilterOat(fNew);
    if (validated.has_value())
        OatC = *validated;
    // else: disconnect sentinel (-127°C), POR value (85°C), or out-of-range;
    // hold last good value. Caller logs if needed.
    return OatC;
}

// ----------------------------------------------------------------------------

onspeed::SensorSample SensorIO::Snapshot() const
{
    onspeed::SensorSample out;
    out.iasKt       = IAS;
    out.paltFt      = Palt;
    out.psMbar      = PStatic;
    out.oatCelsius  = g_Config.bOatSensor ? OatC : onspeed::kOatInvalid;
    out.iasAlive    = bIasAlive;

    // Density altitude: only meaningful with a valid OAT reading.
    // When OAT is absent (kOatInvalid), use ISA temperature at Palt so the
    // result equals pressure altitude — a safe, conservative fallback.
    const float oatForDa = (out.oatCelsius != onspeed::kOatInvalid)
                           ? out.oatCelsius
                           : (15.0f - 0.001981f * Palt);
    out.densityAltitudeFt = onspeed::sensors::DensityAltitudeFt(Palt, oatForDa);

    out.timestampUs = uIasUpdateUs;
    return out;
}
