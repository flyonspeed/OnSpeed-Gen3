
#ifndef _IMU330_H_
#define _IMU330_H_

#include <Arduino.h>
#include "RunningAverage.h"

#include "SPI_IO.h"

// IMU functions

class IMU330
{
public:
  IMU330(SpiIO * pSensorSPI, int CsPort);

  // Data
protected:
    SpiIO     * SensorSPI;
    unsigned    uChipSel;

public:
    // Axis definitions
    String      sVerticalGloadAxis;
    String      sLateralGloadAxis;
    String      sForwardGloadAxis;
    String      sRollGyroAxis;
    String      sPitchGyroAxis;
    String      sYawGyroAxis;

    // IMU raw values
    int16_t     axRaw,   ayRaw,   azRaw;
    int16_t     gxRaw,   gyRaw,   gzRaw;

    // IMU values in IMU orientation
    float       fAccelX, fAccelY, fAccelZ;                 // G values
    float       fGyroX,       fGyroY,       fGyroZ;        // Rotation rates
    float       fGyroXwBias,  fGyroYwBias,  fGyroZwBias;   // Rotation rates with bias removed

    float       fTempC;                     // IMU temperature
    long        lLastImuTempUpdate;

    // Pointers to IMU orientation values and sign for a particular aircraft orientation
    float     * pfAx, fAxSign;
    float     * pfAy, fAySign;
    float     * pfAz, fAzSign;
    float     * pfGx, fGxSign;
    float     * pfGy, fGySign;
    float     * pfGz, fGzSign;

    // IMU values in aircraft orientation
    float       Ax;     // Forward G
    float       Ay;     // Lateral G
    float       Az;     // Vertical G (in g)
    float       Gx;     // Roll rate (in deg/sec)
    float       Gy;     // Pitch rate
    float       Gz;     // Yaw rate

    RunningAverage  * pTempAvg;
//    RunningAverage TempAvg(20);
//    RunningAverage TempAvg(ImuTempSmoothing);

  // Methods
protected:

public:
    void        Init();
    void        Reset();
    void        ReadAccelGyro(bool bTempUpdate);
    void        Read();
    float       ReadTempC();
    uint8_t     WhoAmI();
    void        ConfigAxes();

    float       GetAccelForAxis(String accelAxis);
    void        GetAccelForAxis(String accelAxis, float * pfAccelSign, float ** ppfAccel);
    float       GetGyroForAxis(String gyroAxis);
    void        GetGyroForAxis(String gyroAxis, float * pfGyroSign, float ** ppfGryo);
//    float       GetGyroForAxisNoBias(String gyroAxis);

    // Pitch and roll calculations from accelerations
    float       PitchIMU();
    float       PitchAC();
    float       RollIMU();
    float       RollAC();
};

#endif
