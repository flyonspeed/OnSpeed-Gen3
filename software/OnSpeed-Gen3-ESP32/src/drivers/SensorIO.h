
#pragma once

// For OAT OneWire
#include <OneWire.h>            //https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h>  //https://github.com/milesburton/Arduino-Temperature-Control-Library

#include "Globals.h"

#include "RunningAverage.h"
#include "RunningMedian.h"
#include <filters/SavGolDerivative.h>
#include <aoa/AOACalculator.h>
#include <types/SensorSample.h>

using onspeed::SavGolDerivative;
using onspeed::AOACalculator;

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
    RunningAverage      PfwdAvg;

    int                 iP45;           // Pressure in counts
    float               P45Smoothed;
    RunningMedian       P45Median;
    RunningAverage      P45Avg;

    SavGolDerivative    IasDerivative;  // Computes the first derivative
    float               fDecelRate;     // Deceleration rate derived from IAS

    AOACalculator       AoaCalc;        // AOA calculation with smoothing

    OneWire             OneWireBus;
    DallasTemperature   OatSensor;

    float               PStatic;        // Static pressure in millibars
    float               Palt;           // Pressure altitude in feet, corrected for bias
    float               OatC;           // OAT in degrees C
    float               IAS;
    float               AOA;            // Averaged AOA
    uint32_t            uIasUpdateUs;   // Timestamp (micros) of last IAS update

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
