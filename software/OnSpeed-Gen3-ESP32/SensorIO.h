
#pragma once

// For OAT OneWire
#include <OneWire.h>            //https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h>  //https://github.com/milesburton/Arduino-Temperature-Control-Library

#include "Globals.h"

#include "RunningAverage.h"
#include "RunningMedian.h"
#include <SavLayFilter.h>


// FreeRTOS task for reading sensors
void SensorReadTask(void *pvParams);

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

    SavLayFilter        IasDerivative;  // Computes the first derivative
    float               fDecelRate;     // Deceleration rate derived from IAS

    OneWire             OneWireBus;
    DallasTemperature   OatSensor;

    float               PStatic;        // Static pressure in millibars
    float               Palt;           // Pressure altitude in feet, corrected for bias
    float               OatC;           // OAT in degrees C
    float               IAS;
    float               AOA;            // Averaged AOA

    double              fIasDerInput;   // Source for IAS for deceleration calc

    // Methods
public:
    void    Init();
    void    Read();
    float   ReadOatC();
    float   ReadPressureAltMbars();
//  float   GetPressureAltMbars();

};
