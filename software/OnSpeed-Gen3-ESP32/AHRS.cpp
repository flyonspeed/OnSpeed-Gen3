
#include <math.h>
#include <RunningAverage.h>

#include "Globals.h"
#include "IMU330.h"
#include "AHRS.h"
#include "SensorIO.h"

using onspeed::deg2rad;
using onspeed::rad2deg;
using onspeed::mps2g;
using onspeed::kts2mps;
using onspeed::ft2m;
using onspeed::g2mps;
using onspeed::accelPitch;
using onspeed::accelRoll;

const float accSmoothing        = 0.060899f;            // accelerometer smoothing alpha
const float accSmoothingComplement = 1.0f - accSmoothing;  // precomputed (1 - alpha)

const float iasSmoothing = 0.0179f;                             // airspeed smoothing alpha
const float iasTauFactor = (1.0f / iasSmoothing) - 1.0f;        // tau multiplier for variable-rate EMA

const float kPressureDeltaTime = 1.0f / PRESSURE_SAMPLE_RATE;   // fallback dt for IAS derivative

// ----------------------------------------------------------------------------

AHRS::AHRS(int gyroSmoothing) : GxAvg(gyroSmoothing),GyAvg(gyroSmoothing),GzAvg(gyroSmoothing)
{
    fTAS     = 0.0;
    fPrevTAS = 0.0;
    TASdotSmoothed = 0.0;
    uLastIasUpdateUs = 0;

    //// This was init'ed from real accelerometer values in previous version.
    //// Probably should do that again.
    AccelFwdSmoothed  =  0.0;
    AccelLatSmoothed  =  0.0;
    AccelVertSmoothed = -1.0;
    SmoothedPitch =  0.0;
    SmoothedRoll  =  0.0;
    FlightPath    =  0.0;

    bIasWasBelowThreshold = true;
}

// ----------------------------------------------------------------------------

void AHRS::Init(float fSampleRate)
{
    fImuSampleRate = fSampleRate;
    fImuDeltaTime  = 1.0f / fSampleRate;

//    smoothedPitch = CalcPitch(getAccelForAxis(forwardGloadAxis),getAccelForAxis(lateralGloadAxis), getAccelForAxis(verticalGloadAxis))+pitchBias;
//    smoothedRoll  = calcRoll( getAccelForAxis(forwardGloadAxis),getAccelForAxis(lateralGloadAxis), getAccelForAxis(verticalGloadAxis))+rollBias;
    SmoothedPitch = g_pIMU->PitchAC() + g_Config.fPitchBias;
    SmoothedRoll  = g_pIMU->RollAC()  + g_Config.fRollBias;

    // Precompute trig of installation bias angles (constant after config load).
    // Yaw bias is always zero, so sin(yaw)=0 and cos(yaw)=1 — folded into
    // the rotation expressions in Process() directly.
    const float fPitchBiasRad = deg2rad(g_Config.fPitchBias);
    const float fRollBiasRad  = deg2rad(g_Config.fRollBias);
    fSinPitch = sinf(fPitchBiasRad);
    fCosPitch = cosf(fPitchBiasRad);
    fSinRoll  = sinf(fRollBiasRad);
    fCosRoll  = cosf(fRollBiasRad);

    // Initialize attitude filter based on config setting
    // iAhrsAlgorithm: 0=Madgwick (default), 1=EKF6
    if (g_Config.iAhrsAlgorithm == 1)
    {
        // Initialize EKF6 with accelerometer-derived initial attitude
        // Note: EKF6 expects radians, SmoothedPitch/Roll are in degrees
        Ekf6Filter.init(deg2rad(SmoothedRoll), deg2rad(-SmoothedPitch));
    }
    else
    {
        // MadGwick attitude filter (default)
        // start Madgwick filter at 238Hz for LSM9DS1 and 208Hz for ISM330DHXC
        MadgFilter.begin(fImuSampleRate, -SmoothedPitch, SmoothedRoll);
    }

    // Kalman altitude filter
    KalFilter.Configure(0.79078, 26.0638, 1e-11, ft2m(g_Sensors.Palt),0.00,0.00); // configure the Kalman filter (Smooth altitude and IVSI from Baro + accelerometers)

}

// ----------------------------------------------------------------------------

void AHRS::Process()
{
    Process(fImuDeltaTime);
}

// ----------------------------------------------------------------------------

void AHRS::Process(float fDeltaTimeSeconds)
{
    float fTASdiff;
    float RollRateCorr;
    float PitchRateCorr;
    float YawRateCorr;
    float AccelFwdCompFactor;
    float AccelLatCompFactor;
    float AccelVertCompFactor;
    float q[4];

    // Use measured dt when available; fall back to nominal sample rate if dt is invalid.
    if (isnan(fDeltaTimeSeconds) || isinf(fDeltaTimeSeconds) || fDeltaTimeSeconds <= 0.0f)
        fDeltaTimeSeconds = fImuDeltaTime;

    // Use the best available OAT source for density-corrected TAS
    float fOatC;
    bool  bHaveOat = false;

    // Prefer EFIS OAT when EFIS is the calibration source
    if (g_Config.sCalSource == "EFIS" && g_Config.bReadEfisData)
        {
        fOatC    = g_EfisSerial.suEfis.OAT;
        bHaveOat = (fOatC > -100.0f && fOatC < 100.0f);
        }

    // Fall back to internal DS18B20 sensor
    if (!bHaveOat && g_Config.bOatSensor)
        {
        fOatC    = g_Sensors.OatC;
        bHaveOat = (fOatC > -100.0f && fOatC < 100.0f);
        }

    if (bHaveOat)
        {
        // Density-corrected TAS
        const float Kelvin    = 273.15;
        const float Temp_rate =   0.00198119993;
        float fISA_temp_k = 15 - Temp_rate * g_Sensors.Palt + Kelvin;
        float fOAT_k      = fOatC + Kelvin;
        float fDA          = g_Sensors.Palt + (fISA_temp_k / Temp_rate) * (1 - pow(fISA_temp_k / fOAT_k, 0.2349690));
        fTAS               = kts2mps(g_Sensors.IAS / pow(1 - 6.8755856 * pow(10,-6) * fDA, 2.12794));
        }
    else
        {
        fTAS = kts2mps(g_Sensors.IAS * (1 + g_Sensors.Palt / 1000 * 0.02));
        }

    // Update TAS derivative at IAS update cadence (50Hz), not at the IMU update cadence.
    const uint32_t uIasUpdateUs = g_Sensors.uIasUpdateUs;
    if (uIasUpdateUs != uLastIasUpdateUs)
    {
        if (uLastIasUpdateUs == 0)
        {
            uLastIasUpdateUs = uIasUpdateUs;
            fPrevTAS = fTAS;
            TASdotSmoothed = 0.0f;
        }
        else
        {
            float fIasDtSeconds = float(uint32_t(uIasUpdateUs - uLastIasUpdateUs)) * 1.0e-6f;
            uLastIasUpdateUs = uIasUpdateUs;

            if (isnan(fIasDtSeconds) || isinf(fIasDtSeconds) || fIasDtSeconds <= 0.0f)
                fIasDtSeconds = kPressureDeltaTime;

            fTASdiff = fTAS - fPrevTAS;
            fPrevTAS = fTAS;

            const float fIasTauSeconds = fImuDeltaTime * iasTauFactor;
            const float fAlpha = fIasDtSeconds / (fIasTauSeconds + fIasDtSeconds);
            const float fTASdot = fTASdiff / fIasDtSeconds;
            TASdotSmoothed = fAlpha * fTASdot + (1.0f - fAlpha) * TASdotSmoothed;
        }
    }

    // all TAS are in m/sec at this point

    // update AHRS

    // Correct for installation error using precomputed trig (from Init).
    // Yaw bias is always zero: cos(yaw)=1, sin(yaw)=0.
    // Shorthand: sp=sinPitch, cp=cosPitch, sr=sinRoll, cr=cosRoll.
    const float sp = fSinPitch, cp = fCosPitch;
    const float sr = fSinRoll,  cr = fCosRoll;

    // Installation-corrected gyro values (rotation matrix with yaw=0)
    RollRateCorr  = g_pIMU->Gx *  cp +
                    g_pIMU->Gy * (sr * sp) +
                    g_pIMU->Gz * (cr * sp);
    PitchRateCorr = g_pIMU->Gy *  cr +
                    g_pIMU->Gz * -sr;
    YawRateCorr   = g_pIMU->Gx * -sp +
                    g_pIMU->Gy * (sr * cp) +
                    g_pIMU->Gz * (cp * cr);

    // Installation-corrected accelerometer values (same rotation, yaw=0)
    AccelVertCorr = -g_pIMU->Ax *  sp +
                     g_pIMU->Ay * (sr * cp) +
                     g_pIMU->Az * (cr * cp);
    AccelLatCorr  =  g_pIMU->Ay *  cr +
                     g_pIMU->Az * -sr;
    AccelFwdCorr  =  g_pIMU->Ax *  cp +
                     g_pIMU->Ay * (sr * sp) +
                     g_pIMU->Az * (cr * sp);

    // Average gyro values, not used for AHRS
    GxAvg.addValue(RollRateCorr);
    gRoll = GxAvg.getFastAverage();
    GyAvg.addValue(PitchRateCorr);
    gPitch = GyAvg.getFastAverage();
    GzAvg.addValue(YawRateCorr);
    gYaw = GzAvg.getFastAverage();


    // calculate linear acceleration compensation
    // correct for forward acceleration
    AccelFwdCompFactor  = mps2g(TASdotSmoothed); // m/sec2 to g

    //centripetal acceleration in m/sec2 = speed in m/sec * angular rate in radians
    // When EKF6 is active, use its bias-corrected rates (from previous timestep)
    // for more consistent centripetal compensation.
    float fYawRateForComp   = YawRateCorr;
    float fPitchRateForComp = PitchRateCorr;
    if (g_Config.iAhrsAlgorithm == 1)
    {
        onspeed::EKF6::State prevState = Ekf6Filter.getState();
        fYawRateForComp   = YawRateCorr   - rad2deg(prevState.br);
        fPitchRateForComp = PitchRateCorr - rad2deg(prevState.bq);
    }
    AccelLatCompFactor  = mps2g(deg2rad(fTAS * fYawRateForComp));
    AccelVertCompFactor = mps2g(deg2rad(fTAS * fPitchRateForComp));

    // AccelVertCorr = install corrected acceleration, unsmoothed
    // aVert         = install corrected acceleration, smoothed
    // AccelVertComp     = install corrected compensated acceleration, smoothed
    // aVert         = avg(AvertCorr)
    // AccelVertCompFactor       = centripetal compensation
    // AccelVertComp     = avg(AvertCorr)+avg(avertCp);

    // Smooth accelerometer values and add compensation
    //aFwdCorrAvg.addValue(AccelFwdCorr);
    //aFwd=aFwdCorrAvg.getFastAverage(); // corrected, smoothed
    AccelFwdSmoothed  = accSmoothing * AccelFwdCorr  + accSmoothingComplement * AccelFwdSmoothed;
    AccelFwdComp      = AccelFwdSmoothed - AccelFwdCompFactor; //corrected, smoothed and compensated

    AccelLatSmoothed  = accSmoothing * AccelLatCorr  + accSmoothingComplement * AccelLatSmoothed;
    AccelLatComp      = AccelLatSmoothed - AccelLatCompFactor; //corrected, smoothed and compensated

    AccelVertSmoothed = accSmoothing * AccelVertCorr + accSmoothingComplement * AccelVertSmoothed;
    AccelVertComp     = AccelVertSmoothed + AccelVertCompFactor; //corrected, smoothed and compensated

    // Update attitude filter based on config setting
    // iAhrsAlgorithm: 0=Madgwick (default), 1=EKF6
    if (g_Config.iAhrsAlgorithm == 1)
    {
        // EKF6 attitude filter
        // EKF6 expects aerospace sign convention:
        //   - az = -g in level flight (sensor measures reaction to gravity)
        //   - OnSpeed IMU pipeline uses NED where az = +g, so negate vertical axis
        //   - Accelerometer in m/s^2 (AccelComp values are in G, multiply by 9.80665)
        //   - Gyroscope in rad/s (RateCorr values are in deg/s, convert to rad/s)
        //   - gamma (flight path angle) in radians
        const float G_MPS2 = 9.80665f;
        float gamma_rad = deg2rad(FlightPath);  // Use previous FlightPath estimate

        onspeed::EKF6::Measurements meas = {
            .ax =  AccelFwdComp * G_MPS2,     // forward accel in m/s^2
            .ay =  AccelLatComp * G_MPS2,     // lateral accel in m/s^2
            .az = -AccelVertComp * G_MPS2,    // vertical: negate NED→aerospace
            .p  = deg2rad(RollRateCorr),      // roll rate in rad/s
            .q  = deg2rad(PitchRateCorr),     // pitch rate in rad/s
            .r  = deg2rad(YawRateCorr),       // yaw rate in rad/s
            .gamma = gamma_rad                 // flight path angle in rad
        };

        Ekf6Filter.update(meas, fDeltaTimeSeconds);
        onspeed::EKF6::State state = Ekf6Filter.getState();

        // EKF6 state uses: phi=roll, theta=pitch (both in radians)
        // Convert to degrees to match Madgwick output convention
        SmoothedPitch = state.theta_deg();
        SmoothedRoll  = state.phi_deg();

        // Estimate earth vertical G from attitude for the Kalman altitude filter
        float sph = sin(state.phi);
        float cph = cos(state.phi);
        float sth = sin(state.theta);
        float cth = cos(state.theta);
        EarthVertG = -sth * AccelFwdCorr + sph * cth * AccelLatCorr + cph * cth * AccelVertCorr - 1.0f;
    }
    else
    {
        // Madgwick attitude filter (default)
        MadgFilter.setDeltaTime(fDeltaTimeSeconds);
        MadgFilter.UpdateIMU(RollRateCorr, PitchRateCorr, YawRateCorr, AccelFwdComp, AccelLatComp, AccelVertComp);

        SmoothedPitch = -MadgFilter.getPitch();
        SmoothedRoll  = -MadgFilter.getRoll();

        // calculate VSI and flightpath
        MadgFilter.getQuaternion(&q[0],&q[1],&q[2],&q[3]);

        // get earth referenced vertical acceleration
        EarthVertG = 2.0f * (q[1]*q[3] - q[0]*q[2])                         * AccelFwdCorr  +
                     2.0f * (q[0]*q[1] + q[2]*q[3])                         * AccelLatCorr  +
                            (q[0]*q[0] - q[1]*q[1] - q[2]*q[2] + q[3]*q[3]) * AccelVertCorr - 1.0f;
    }

    KalFilter.Update(ft2m(g_Sensors.Palt), g2mps(EarthVertG), fDeltaTimeSeconds, &KalmanAlt, &KalmanVSI); // altitude in meters, acceleration in m/s^2

    // zero VSI when airspeed is not yet alive
    if (g_Sensors.IAS < 25)
    {
        KalmanVSI = 0;
        bIasWasBelowThreshold = true;
    }
    else if (bIasWasBelowThreshold && g_Config.iAhrsAlgorithm == 1)
    {
        Ekf6Filter.resetAlphaCovariance();
        bIasWasBelowThreshold = false;
    }
    else
    {
        bIasWasBelowThreshold = false;
    }

    // calculate flight path and derived AOA
    if (g_Sensors.IAS != 0.0)
        FlightPath = rad2deg(asin(KalmanVSI/fTAS)); // TAS in m/s, radians to degrees
    else
        FlightPath = 0.0;

    // DerivedAOA calculation depends on AHRS algorithm
    if (g_Config.iAhrsAlgorithm == 1)
    {
        // EKF6 directly estimates alpha as part of its state vector
        onspeed::EKF6::State state = Ekf6Filter.getState();
        DerivedAOA = state.alpha_deg();
    }
    else
    {
        // Madgwick: derive AOA from pitch angle minus flight path angle
        DerivedAOA = SmoothedPitch - FlightPath;
    }

}

float AHRS::PitchWithBias()         { return accelPitch(AccelFwdCorr,     AccelLatCorr,     AccelVertCorr);     }
float AHRS::PitchWithBiasSmth()     { return accelPitch(AccelFwdSmoothed, AccelLatSmoothed, AccelVertSmoothed); }
float AHRS::PitchWithBiasSmthComp() { return accelPitch(AccelFwdComp,     AccelLatComp,     AccelVertComp);     }
float AHRS::RollWithBias()          { return accelRoll (AccelFwdCorr,     AccelLatCorr,     AccelVertCorr);     }
float AHRS::RollWithBiasSmth()      { return accelRoll (AccelFwdSmoothed, AccelLatSmoothed, AccelVertSmoothed); }
float AHRS::RollWithBiasSmthComp()  { return accelRoll (AccelFwdComp,     AccelLatComp,     AccelVertComp);     }
