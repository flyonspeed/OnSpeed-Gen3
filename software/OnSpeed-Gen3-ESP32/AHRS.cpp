
#include <RunningAverage.h>

#include "Globals.h"
#include "IMU330.h"
#include "AHRS.h"
#include "SensorIO.h"

const float accSmoothing = 0.060899; // accelerometer smoothing, exponential
const float iasSmoothing = 0.0179;   // airspeed smoothing, exponential [optimized for ISM330 IMU]

// ----------------------------------------------------------------------------

AHRS::AHRS(int gyroSmoothing) : GxAvg(gyroSmoothing),GyAvg(gyroSmoothing),GzAvg(gyroSmoothing)
{
    fTAS     = 0.0;
    fPrevTAS = 0.0;
    TASdiffSmoothed = 0.0;

    //// This was init'ed from real accelerometer values in previous version.
    //// Probably should do that again.
    AccelFwdSmoothed  =  0.0;
    AccelLatSmoothed  =  0.0;
    AccelVertSmoothed = -1.0;
    SmoothedPitch =  0.0;
    SmoothedRoll  =  0.0;
    FlightPath    =  0.0;

}

// ----------------------------------------------------------------------------

void AHRS::Init(float fSampleRate)
{
    fImuSampleRate = fSampleRate;

//    smoothedPitch = CalcPitch(getAccelForAxis(forwardGloadAxis),getAccelForAxis(lateralGloadAxis), getAccelForAxis(verticalGloadAxis))+pitchBias;
//    smoothedRoll  = calcRoll( getAccelForAxis(forwardGloadAxis),getAccelForAxis(lateralGloadAxis), getAccelForAxis(verticalGloadAxis))+rollBias;
    SmoothedPitch = g_pIMU->PitchAC() + g_Config.fPitchBias; 
    SmoothedRoll  = g_pIMU->RollAC()  + g_Config.fRollBias; 

    // MadGwick attitude filter
    // start Madgwick filter at 238Hz for LSM9DS1 and 208Hz for ISM330DHXC
    MadgFilter.begin(fImuSampleRate, -SmoothedPitch, SmoothedRoll);

    // Kalman altitude filter
    KalFilter.Configure(0.79078, 26.0638, 1e-11, FT2M(g_Sensors.Palt),0.00,0.00); // configure the Kalman filter (Smooth altitude and IVSI from Baro + accelerometers)

}

// ----------------------------------------------------------------------------

void AHRS::Process()
{
    float fTASdiff;
    float fPitchBiasRad;
    float fRollBiasRad;
    float fYawBiasRad;
    float RollRateCorr;
    float PitchRateCorr;
    float YawRateCorr;
    float AccelFwdCompFactor;
    float AccelLatCompFactor;
    float AccelVertCompFactor;
    float q[4];

#ifdef OAT_AVAILABLE
    const float Kelvin    = 273.15;
    const float Temp_rate =   0.00198119993;
    float       fISA_temp_k;
    float       fOAT_k;
    float       fDA;

    fISA_temp_k = 15 - Temp_rate * g_Sensors.Palt + Kelvin;
    fOAT_k      = g_Sensors.ReadOatC() + Kelvin;
    fDA         = g_Sensors.Palt+(fISA_temp_k/Temp_rate)*(1-pow(fISA_temp_k/fOAT_k,0.2349690));
    fTAS        = KTS2MPS(g_Sensors.IAS/pow(1 - 6.8755856 * pow(10,-6) * fDA, 2.12794)); // formulas from https://edwilliams.org/avform147.htm#Mach   // m/sec
#else
    fTAS        = KTS2MPS(g_Sensors.IAS*(1+ g_Sensors.Palt / 1000 * 0.02)); // m/sec
#endif

    // diff IAS and then smooth it. Used for forward acceleration correction
    fTASdiff = fTAS - fPrevTAS;
    fPrevTAS = fTAS;
    TASdiffSmoothed = iasSmoothing*fTASdiff+(1-iasSmoothing)*TASdiffSmoothed;

    // all TAS are in m/sec at this point

    // update AHRS

    // correct for installation error
    fPitchBiasRad = DEG2RAD(g_Config.fPitchBias);
    fRollBiasRad  = DEG2RAD(g_Config.fRollBias);
    fYawBiasRad   = 0.0; // assuming zero yaw (twist) on install

    // Calculate installation corrected gyro values
    //// This is a lot of sin() and cos() math on constant values. This should be precalcualted in Init().
    RollRateCorr  =  g_pIMU->Gx *   cos(fPitchBiasRad) * cos(fYawBiasRad) + 
////                 g_pIMU->Gy * ( cos(fYawBiasRad)   * sin(fRollBiasRad) * sin(fPitchBiasRad) * - sin(fYawBiasRad) * cos(fRollBiasRad) ) +  THAT "* -" IN THE ORIGINAL LOOKS FISHY
                     g_pIMU->Gy * ( cos(fYawBiasRad)   * sin(fRollBiasRad) * sin(fPitchBiasRad) - sin(fYawBiasRad) * cos(fRollBiasRad) ) + 
                     g_pIMU->Gz * ( cos(fYawBiasRad)   * cos(fRollBiasRad) * sin(fPitchBiasRad) + sin(fYawBiasRad) * sin(fRollBiasRad));
    PitchRateCorr =  g_pIMU->Gx *   cos(fPitchBiasRad) * sin(fYawBiasRad) + 
                     g_pIMU->Gy * ( sin(fYawBiasRad)   * sin(fRollBiasRad) * sin(fPitchBiasRad) + cos(fYawBiasRad) * cos(fRollBiasRad)) + 
                     g_pIMU->Gz * ( sin(fYawBiasRad)   * cos(fRollBiasRad) * sin(fPitchBiasRad) - cos(fYawBiasRad) * sin(fRollBiasRad));
    YawRateCorr   =  g_pIMU->Gx *  -sin(fPitchBiasRad) + 
                     g_pIMU->Gy *   sin(fRollBiasRad)  * cos(fPitchBiasRad) + 
                     g_pIMU->Gz *   cos(fPitchBiasRad) * cos(fRollBiasRad);

    // Displacement from CG calculation is omitted
    AccelVertCorr = -g_pIMU->Ax * sin(fPitchBiasRad)                        +  // OK
                     g_pIMU->Ay * sin(fRollBiasRad)  * cos(fPitchBiasRad) + 
                     g_pIMU->Az * cos(fRollBiasRad)  * cos(fPitchBiasRad);
    AccelLatCorr  =  g_pIMU->Ax * cos(fPitchBiasRad) * sin(fYawBiasRad)   + 
                     g_pIMU->Ay * (sin(fYawBiasRad)  * sin(fPitchBiasRad) * sin(fRollBiasRad)  + cos(fYawBiasRad) * cos(fRollBiasRad)) + 
                     g_pIMU->Az * (sin(fYawBiasRad)  * cos(fRollBiasRad)  * sin(fPitchBiasRad) - cos(fYawBiasRad) * sin(fRollBiasRad));
    AccelFwdCorr  =  g_pIMU->Ax * cos(fPitchBiasRad) * cos(fYawBiasRad)   + 
                     g_pIMU->Ay * (sin(fRollBiasRad) * sin(fPitchBiasRad) * cos(fYawBiasRad)   - sin(fYawBiasRad) * cos(fRollBiasRad)) + 
                     g_pIMU->Az * (cos(fYawBiasRad)  * cos(fRollBiasRad)  * sin(fPitchBiasRad) + sin(fYawBiasRad) * sin(fRollBiasRad));

    // Average gyro values, not used for AHRS
    GxAvg.addValue(RollRateCorr);
    gRoll = GxAvg.getFastAverage();
    GyAvg.addValue(PitchRateCorr);
    gPitch = GyAvg.getFastAverage();
    GzAvg.addValue(YawRateCorr);
    gYaw = GzAvg.getFastAverage();


    // calculate linear acceleration compensation
    // correct for forward acceleration
    AccelFwdCompFactor  = MPS2G((TASdiffSmoothed)/(1/fImuSampleRate)); //1/208hz (update rate), m/sec2 to g

    //centripetal acceleration in m/sec2 = speed in m/sec * angular rate in radians
    AccelLatCompFactor  = MPS2G(DEG2RAD(fTAS * YawRateCorr));
    AccelVertCompFactor = MPS2G(DEG2RAD(fTAS * PitchRateCorr)); // TAS knots to m/sec, pitchrate in radians, m/sec2 to g

    // AccelVertCorr = install corrected acceleration, unsmoothed
    // aVert         = install corrected acceleration, smoothed
    // AccelVertComp     = install corrected compensated acceleration, smoothed
    // aVert         = avg(AvertCorr)
    // AccelVertCompFactor       = centripetal compensation
    // AccelVertComp     = avg(AvertCorr)+avg(avertCp);

    // Smooth accelerometer values and add compensation
    //aFwdCorrAvg.addValue(AccelFwdCorr);
    //aFwd=aFwdCorrAvg.getFastAverage(); // corrected, smoothed
    AccelFwdSmoothed  = accSmoothing * AccelFwdCorr+(1-accSmoothing) * AccelFwdSmoothed;
    AccelFwdComp      = AccelFwdSmoothed - AccelFwdCompFactor; //corrected, smoothed and compensated

    AccelLatSmoothed  = accSmoothing*AccelLatCorr+(1-accSmoothing)*AccelLatSmoothed;
    AccelLatComp      = AccelLatSmoothed-AccelLatCompFactor; //corrected, smoothed and compensated

    AccelVertSmoothed = accSmoothing*AccelVertCorr+(1-accSmoothing)*AccelVertSmoothed;
    AccelVertComp     = AccelVertSmoothed+AccelVertCompFactor; //corrected, smoothed and compensated

    MadgFilter.UpdateIMU(RollRateCorr, PitchRateCorr, YawRateCorr, AccelFwdComp, AccelLatComp, AccelVertComp);

    SmoothedPitch = -MadgFilter.getPitch();
    SmoothedRoll  = -MadgFilter.getRoll();

    // calculate VSI and flightpath
    MadgFilter.getQuaternion(&q[0],&q[1],&q[2],&q[3]);

    // get earth referenced vertical acceleration
    EarthVertG = 2.0f * (q[1]*q[3] - q[0]*q[2])                         * AccelFwdCorr  + 
                 2.0f * (q[0]*q[1] + q[2]*q[3])                         * AccelLatCorr  + 
                        (q[0]*q[0] - q[1]*q[1] - q[2]*q[2] + q[3]*q[3]) * AccelVertCorr - 1.0f;

    KalFilter.Update(FT2M(g_Sensors.Palt), G2MPS(EarthVertG), float(1/fImuSampleRate), &KalmanAlt, &KalmanVSI); // altitude in meters, acceleration in m/s^2

    // zero VSI when airspeed is not yet alive
    if (g_Sensors.IAS < 25) 
        KalmanVSI = 0;

    // calculate flight path and derived AOA
    if (g_Sensors.IAS != 0.0) 
        FlightPath = RAD2DEG(asin(KalmanVSI/fTAS)); // TAS in m/s, radians to degrees
    else 
        FlightPath = 0.0;

    DerivedAOA = SmoothedPitch - FlightPath;

}

float AHRS::PitchWithBias()         { return PITCH(AccelFwdCorr,     AccelLatCorr,     AccelVertCorr);     }
float AHRS::PitchWithBiasSmth()     { return PITCH(AccelFwdSmoothed, AccelLatSmoothed, AccelVertSmoothed); }
float AHRS::PitchWithBiasSmthComp() { return PITCH(AccelFwdComp,     AccelLatComp,     AccelVertComp);     }
float AHRS::RollWithBias()          { return ROLL (AccelFwdCorr,     AccelLatCorr,     AccelVertCorr);     }
float AHRS::RollWithBiasSmth()      { return ROLL (AccelFwdSmoothed, AccelLatSmoothed, AccelVertSmoothed); }
float AHRS::RollWithBiasSmthComp()  { return ROLL (AccelFwdComp,     AccelLatComp,     AccelVertComp);     }
