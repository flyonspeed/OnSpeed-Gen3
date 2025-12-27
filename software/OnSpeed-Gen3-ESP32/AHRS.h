
#pragma once

#include <RunningAverage.h>

#include "Globals.h"

#include "MadgwickFusion.h"
#include "KalmanFilter.h"

class AHRS
{
public:
    AHRS(int gyroSmoothing);

    // IMU acceleration values are used to eventualy calcuate smoothed and
    // corrected pitch and roll. Here are the 3 steps they go through before
    // they are eventually handed to the Madgwick functions.

    // Step 1 - IMU raw acceleration readings ard corrected for pitch and roll bias
    float           AccelFwdCorr;
    float           AccelLatCorr;
    float           AccelVertCorr;

    // Step 2 - Corrected accelerations are smoothed
    float           AccelFwdSmoothed;
    float           AccelLatSmoothed;
    float           AccelVertSmoothed;

    // Step 3 - Corrected and smoothed acceleration values are compensated for angular rates
    // These are the values that go to the Madgwick functions
    float           AccelFwdComp;
    float           AccelLatComp;
    float           AccelVertComp;

    // These are what eventually come out of the Madgwick filter
    float           SmoothedPitch;
    float           SmoothedRoll;

    float           TASdiffSmoothed;
    float           KalmanAlt;
    float           KalmanVSI;
    float           FlightPath;
    float           EarthVertG;
    float           DerivedAOA;

    RunningAverage  GxAvg;
    RunningAverage  GyAvg;
    RunningAverage  GzAvg;

    float           gRoll,gPitch,gYaw;    // Gyro rates in the various axes

    float           fImuSampleRate;

    Madgwick        MadgFilter;
    KalmanFilter    KalFilter;

public:
    float           fTAS;
    float           fPrevTAS;

    // Methods
    void    Init(float fSampleRate);
    void    Process();

    float   PitchWithBias();
    float   PitchWithBiasSmth();
    float   PitchWithBiasSmthComp();
    float   RollWithBias();
    float   RollWithBiasSmth();
    float   RollWithBiasSmthComp();

};