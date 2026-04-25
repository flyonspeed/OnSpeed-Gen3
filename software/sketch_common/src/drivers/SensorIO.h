
#pragma once

// For OAT OneWire
#include <OneWire.h>            //https://github.com/PaulStoffregen/OneWire

#include "src/Globals.h"
#include "src/drivers/Ds18b20.h"

#include <filters/RunningMean.h>
#include <filters/RunningMedian.h>
#include <filters/SavGolDerivative.h>
#include <aoa/AOACalculator.h>
#include <audio/ToneCalc.h>
#include <types/SensorSample.h>
#include <util/OnSpeedTypes.h>

using onspeed::SavGolDerivative;
using onspeed::AOACalculator;
using onspeed::RunningMean;
using onspeed::RunningMedian;

// One-shot snapshot of the active flap entry's setpoints + AOA polynomial.
// Built under xAhrsMutex with a bounds check on g_Flaps.iIndex; passed by
// value to consumers (AOA calc, tone calc) so they never index aFlaps[]
// directly during a HandleConfigSave swap.
//
// bValid is false when the snapshot could not be built (mutex timeout or
// out-of-bounds iIndex).  Consumers treat invalid as fail-silent: zero
// AOA, no tone -- strictly safer than acting on torn or freed memory.
struct ActiveFlapSnapshot {
    onspeed::SuCalibrationCurve curve;
    onspeed::ToneThresholds     th;
    bool                        bValid;
};

// Snapshot the AOA polynomial curve and the four tone setpoints from
// g_Config.aFlaps[g_Flaps.iIndex] under xAhrsMutex.  Returns bValid=false
// on mutex timeout or if g_Flaps.iIndex is out of bounds for the current
// vector size.
ActiveFlapSnapshot SnapshotActiveFlap();

// FreeRTOS task for reading sensors
void SensorReadTask(void *pvParams);
void ImuReadTask(void *pvParams);

// ============================================================================

class SensorIO
{
public:
    SensorIO();

    // Structures
public:

    // Data
    int                 iPfwd;          // Pressure in counts
    float               PfwdSmoothed;
    RunningMedian       PfwdMedian;
    RunningMean         PfwdAvg;

    int                 iP45;           // Pressure in counts
    float               P45Smoothed;
    RunningMedian       P45Median;
    RunningMean         P45Avg;

    SavGolDerivative    IasDerivative;  // Computes the first derivative
    float               fDecelRate;     // Deceleration rate derived from IAS

    AOACalculator       AoaCalc;        // AOA calculation with smoothing

    OneWire             OneWireBus;
    Ds18b20             OatSensor;

    float               PStatic;        // Static pressure in millibars
    float               Palt;           // Pressure altitude in feet, corrected for bias
    float               OatC;           // OAT in degrees C
    float               IAS;
    float               AOA;            // Averaged AOA
    uint32_t            uIasUpdateUs;   // Timestamp (micros) of last IAS update
    bool                bIasAlive;      // Air-data validity: true when IAS is above the
                                        // pitot noise floor with hysteresis.  Matches the
                                        // ARINC-429 SSM "No Computed Data" concept and is
                                        // the sketch-side source for SensorSample::iasAlive.

    double              fIasDerInput;   // Source for IAS for deceleration calc

    // Methods
public:
    void    Init();
    void    Read();
    float   ReadOatC();
    float   ReadPressureAltMbars();
//  float   GetPressureAltMbars();

    // Returns the current aggregated sensor reading as a core POD struct.
    // psMbar, ptMbar, p45Mbar, and densityAltitudeFt are not yet tracked by
    // this class (PR 1.2 will add them); those fields default to 0 in this PR.
    // oatCelsius is set to onspeed::kOatInvalid when bOatSensor is false.
    onspeed::SensorSample Snapshot() const;

private:
    bool        bOatConversionPending = false;
    uint32_t    uOatRequestMs = 0;

};
