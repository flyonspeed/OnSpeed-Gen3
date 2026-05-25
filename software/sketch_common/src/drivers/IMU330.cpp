
#include <Arduino.h>
#include <SPI.h>

#include "src/Globals.h"

using onspeed::accelPitch;
using onspeed::accelRoll;

// define ISM330 registers
#define FIFO_CTRL4          0x0A
#define WHO_AM_I            0x0F  // Who Am I value = 0x6B
#define CTRL1_XL            0x10  // accelerometer control register
#define CTRL2_G             0x11  // gyro control register
#define CTRL3_C             0x12
#define CTRL4_C             0x13
#define CTRL6_C             0x15
#define CTRL7_G             0x16
#define CTRL9_XL            0x18
#define ISM330_OUT_TEMP_L   0x20  // temp output register
#define ISM330_OUT_TEMP_H   0x21
#define OUTX_L_G            0x22  // start of gyro output address
#define OUTX_L_A            0x28  // start of accelerometer output address

#define IMU_WRITE_ADDR(addr)  (uint8_t)(0x7F & addr)
#define IMU_READ_ADDR(addr)   (uint8_t)(0x80 | addr)

#define ACCEL_RES      (8.0f / 32768.0f)    // full scale /resolution (8G)
#define GYRO_RES     (250.0f / 32768.0f)    // full scale / resolution (250 dps, matches CTRL2_G FS_G=00)

#define ISM330_TEMP_SCALE   256.0f
#define ISM330_TEMP_BIAS     25.0f

// ============================================================================

IMU330::IMU330(SpiIO * pSensorSPI, int CsPort)
{
    uChipSel  = CsPort;
    SensorSPI = pSensorSPI;
//    Config    = pConfig;

    lLastImuTempUpdate = millis();

    // Set up chip select pins as outputs
    pinMode(uChipSel, OUTPUT);

    sVerticalGloadAxis = "";
    sLateralGloadAxis  = "";
    sForwardGloadAxis  = "";
    sRollGyroAxis      = "";
    sPitchGyroAxis     = "";
    sYawGyroAxis       = "";

    pTempAvg = new onspeed::RunningMean(20);

    Ax = 0.0;     // Forward G
    Ay = 0.0;     // Lateral G
    Az = 0.0;     // Vertical G (in g)
    Gx = 0.0;     // Roll rate (in deg/sec)
    Gy = 0.0;     // Pitch rate
    Gz = 0.0;     // Yaw rate

} // end contructor

// ----------------------------------------------------------------------------

void IMU330::Init()
{
  Reset();

  // SPI interface: I2C_disable = 1 in CTRL4_C (13h) and DEVICE_CONF = 1 in CTRL9_XL (18h).

  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL9_XL), 0b11100010);
  delay(50);

  // ODR bits selected from g_imuSampleRateHz (latched at boot in setup()
  // from g_Config.iLogRate; see HardwareMap.h for the policy).
  //   208 Hz: ODR_XL[3:0] = 0101, ODR_G[3:0] = 0101
  //   416 Hz: ODR_XL[3:0] = 0110, ODR_G[3:0] = 0110
  // Range / LPF / full-scale bits unchanged across rates.
  const uint8_t uCtrl1Xl = (g_imuSampleRateHz == kImuSampleRateExperimental)
                              ? 0b01101100   // 416 Hz, +/-8G, LPF2 disabled
                              : 0b01011100;  // 208 Hz, +/-8G, LPF2 disabled
  const uint8_t uCtrl2G  = (g_imuSampleRateHz == kImuSampleRateExperimental)
                              ? 0b01100000   // 416 Hz, 250 dps
                              : 0b01010000;  // 208 Hz, 250 dps

  // enable accelerometer
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL1_XL), uCtrl1Xl);
  delay(50);

  // enable gyroscope
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL2_G), uCtrl2G);
  delay(50);

  // disable gyroscope hi-pass filter
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL7_G), 0b00000000); // high performance mode, disable high pass filter
  delay(50);

  // disable LPF1, disable I2C
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL4_C), 0b00000100); // disable low pass filter 1, LPF2 is still on at 67hz bandwidth
  delay(50);

  // set fifo
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(FIFO_CTRL4), 0b00010000); // bypass mode, fifo disabled
  delay(50);

  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL9_XL), 0b11100000);
  delay(50);

  g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "IMU Who Am I : 0x%2.2X\n", g_pIMU->WhoAmI());

}

// ----------------------------------------------------------------------------

void IMU330::Reset()
{
    // soft reset accelerometer/gyro
    SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL3_C),  0b00000101);
    delay(100);
    SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL3_C),  0b00000100);
    delay(100);
}

// ----------------------------------------------------------------------------

// Read IMU and process AHRS

void IMU330::Read()
{
    bool bTempUpdate = false;
    // heartbeat

    if (millis() - lLastImuTempUpdate > 100)
    {
        lLastImuTempUpdate = millis();
        bTempUpdate = true;
    }

    ReadAccelGyro(bTempUpdate); // read accelerometers

    // Get IMU values in aircraft orientation
    Ax = *pfAx * fAxSign;
    Ay = *pfAy * fAySign;
    Az = *pfAz * fAzSign;
    Gx = *pfGx * fGxSign;
    Gy = *pfGy * fGySign;
    Gz = *pfGz * fGzSign;

    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug,
        "Ax %.3f, Ay %.3f, Az %.3f, Gx %.4f, Gy %.4f, Gz %.4f, Temp %.2fC\n",
        Ax, Ay, Az, Gx, Gy, Gz, fTempC);
    }

// ----------------------------------------------------------------------------

void IMU330::ReadAccelGyro(bool bTempUpdate)
{
    uint8_t     aAccelData[6]; // Six bytes from the accelerometer
    uint8_t     aGyroData[6];  // Six bytes from the gyro
//    int16_t     axRaw,ayRaw,azRaw;
//    int16_t     gxRaw,gyRaw,gzRaw;

    // Read accelerometer output
    SensorSPI->ReadRegBytes(uChipSel, IMU_READ_ADDR(OUTX_L_A), aAccelData, 6); // Read 6 bytes, beginning at OUTX_L_A
    axRaw = (aAccelData[1] << 8) | aAccelData[0]; // Store x-axis values into ax
    ayRaw = (aAccelData[3] << 8) | aAccelData[2]; // Store y-axis values into ay
    azRaw = (aAccelData[5] << 8) | aAccelData[4]; // Store z-axis values into az

    // Read gyro output
    SensorSPI->ReadRegBytes(uChipSel, IMU_READ_ADDR(OUTX_L_G), aGyroData, 6);  // Read the six raw data registers sequentially into data array
    gxRaw = (aGyroData[1] << 8) | aGyroData[0]; // Store x-axis values into gx
    gyRaw = (aGyroData[3] << 8) | aGyroData[2]; // Store y-axis values into gy
    gzRaw = (aGyroData[5] << 8) | aGyroData[4]; // Store z-axis values into gz

    fAccelX     = axRaw * ACCEL_RES;
    fAccelY     = ayRaw * ACCEL_RES;
    fAccelZ     = azRaw * ACCEL_RES;
    fGyroX      = gxRaw * GYRO_RES;
    fGyroY      = gyRaw * GYRO_RES;
    fGyroZ      = gzRaw * GYRO_RES;
    fGyroXwBias = fGyroX + g_Config.fGxBias;
    fGyroYwBias = fGyroY + g_Config.fGyBias;
    fGyroZwBias = fGyroZ + g_Config.fGzBias;

    if (g_Log.Test(MsgLog::EnIMU, MsgLog::EnDebug))
        g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "fAccelX %.3f, fAccelY %.3f, fAccelZ %.3f, fGyroX %.4f, fGyroY %.4f, fGyroZ %.4f\n",
            fAccelX, fAccelY, fAccelZ, fGyroX, fGyroY, fGyroZ);

#if 1
    // read IMU temperature output
    if (bTempUpdate)
        {
        ReadTempC();

        pTempAvg->addValue(fTempC);
        fTempC = pTempAvg->getFastAverage();
        //imuTempDerivativeInput=imuTempRaw;
        //imuTempRateAvg.addValue(-imuTempDerivative.Compute()*10.0); //10Hz sample rate on imuTemp, SavGolay derivative filter takes 20-25uSec
        //imuTempRate=imuTempRateAvg.getFastAverage();

//        g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Temp: %.1fC\n",fTempC);
        }
#endif
}

// ----------------------------------------------------------------------------

// IMU chip temperature in degrees Celsius

float IMU330::ReadTempC()
{
    uint8_t     TempL, TempH;
    int16_t     iTempCount;

    TempL = SensorSPI->ReadRegByte(uChipSel, IMU_READ_ADDR(ISM330_OUT_TEMP_L));
    TempH = SensorSPI->ReadRegByte(uChipSel, IMU_READ_ADDR(ISM330_OUT_TEMP_H));

    iTempCount = (TempH << 8) | TempL;
    fTempC     = ((float) (iTempCount / ISM330_TEMP_SCALE + ISM330_TEMP_BIAS));
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Temp: %.1fC\n",fTempC);

    return fTempC;
}

// ----------------------------------------------------------------------------
// 0x6B --> ISM330DHCX
// 0x6C --> LSM6DSOX / LSM6DSO32

uint8_t IMU330::WhoAmI()
{
  uint8_t     iWhoAmI;

  iWhoAmI = SensorSPI->ReadRegByte(uChipSel, IMU_READ_ADDR(WHO_AM_I));

  return iWhoAmI;
}

// ----------------------------------------------------------------------------

void IMU330::ConfigAxes()
    {
    // Get accelerometer axis from box orientation
    // orientation arrays defined as box orientation
    // configure axes to North East Down (NED) Axis convention
    // vertical axis (z down), lateral axis (y right), longitudinal axis (x forward)
    // pitch rate positive for pitch up
    // roll rate positive for right roll
    // yaw rate positive for right yaw

    sVerticalGloadAxis = "";
    sLateralGloadAxis  = "";
    sForwardGloadAxis  = "";

    // Per-board orientation table from HardwareMap.h. The local reference
    // keeps all the downstream axisMapArray[i][j] indexing unchanged.
    const auto& axisMapArray = kImuOrientationTable;

    for (int i = 0; i < kImuOrientationRowCount; i++)
        {
        const ImuOrientationRow& row = axisMapArray[i];
        if (g_Config.sPortsOrientation == row.portsOrientation &&
            g_Config.sBoxtopOrientation == row.boxtopOrientation)
            {
            sVerticalGloadAxis = row.verticalGloadAxis;
            sLateralGloadAxis  = row.lateralGloadAxis;
            sForwardGloadAxis  = row.forwardGloadAxis;
            sYawGyroAxis       = sVerticalGloadAxis;
            sPitchGyroAxis     = sLateralGloadAxis;
            sRollGyroAxis      = sForwardGloadAxis;

            GetAccelForAxis(row.verticalGloadAxis, &fAzSign, &pfAz);
            GetAccelForAxis(row.lateralGloadAxis,  &fAySign, &pfAy);
            GetAccelForAxis(row.forwardGloadAxis,  &fAxSign, &pfAx);
            GetGyroForAxis( row.verticalGloadAxis, &fGzSign, &pfGz);    //// CHECK THESE!!!
            GetGyroForAxis( row.lateralGloadAxis,  &fGySign, &pfGy);
            GetGyroForAxis( row.forwardGloadAxis,  &fGxSign, &pfGx);
            break;
            }
        }

    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "portsOrientation  : %s\n", g_Config.sPortsOrientation.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "boxtopOrientation : %s\n", g_Config.sBoxtopOrientation.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Vertical axis     : %s\n", sVerticalGloadAxis);
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Lateral axis      : %s\n", sLateralGloadAxis);
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Forward axis      : %s\n", sForwardGloadAxis);
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Yaw Gyro axis     : %s\n", sYawGyroAxis);
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Pitch Gyro axis   : %s\n", sPitchGyroAxis);
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Roll Gyro axis    : %s\n", sRollGyroAxis);
    }

// ----------------------------------------------------------------------------

float IMU330::GetAccelForAxis(const char* accelAxis)
    {
    float result=0.0;
    char lastChar = accelAxis[strlen(accelAxis)-1];
    if      (lastChar == 'X') result = fAccelX;
    else if (lastChar == 'Y') result = fAccelY;
    else                      result = fAccelZ;

    if (accelAxis[0]=='-') result*=-1;

    return result;
    }

// ----------------------------------------------------------------------------

void IMU330::GetAccelForAxis(const char* accelAxis, float * pfAccelSign, float ** ppfAccel)
    {
    char lastChar = accelAxis[strlen(accelAxis)-1];
    if      (lastChar == 'X') *ppfAccel = &fAccelX;
    else if (lastChar == 'Y') *ppfAccel = &fAccelY;
    else                      *ppfAccel = &fAccelZ;

    if (accelAxis[0] == '-') *pfAccelSign = -1;
    else                     *pfAccelSign =  1;
    }

// ----------------------------------------------------------------------------

float IMU330::GetGyroForAxis(const char* gyroAxis)
    {
    float result=0.0;
    char lastChar = gyroAxis[strlen(gyroAxis)-1];
    if      (lastChar == 'X') result = fGyroX + g_Config.fGxBias;
    else if (lastChar == 'Y') result = fGyroY + g_Config.fGyBias;
    else                      result = fGyroZ + g_Config.fGzBias;

    // For gyro the sign is inverted
    if (gyroAxis[0]!='-') result *= -1;

    return result;
    }

// ----------------------------------------------------------------------------

void IMU330::GetGyroForAxis(const char* gyroAxis, float * pfGyroSign, float ** ppfGryo)
    {
    char lastChar = gyroAxis[strlen(gyroAxis)-1];
    if      (lastChar == 'X') *ppfGryo = &fGyroXwBias;
    else if (lastChar == 'Y') *ppfGryo = &fGyroYwBias;
    else                      *ppfGryo = &fGyroZwBias;

    // For gyro the sign is inverted
    if (gyroAxis[0] != '-') *pfGyroSign = -1;
    else                    *pfGyroSign =  1;
    }

// ----------------------------------------------------------------------------

// Pitch in IMU coordinates

float IMU330::PitchIMU()
    {
    return accelPitch(fAccelX, fAccelY, fAccelZ);
    }

// ----------------------------------------------------------------------------

// Pitch in aircraft coordinates

float IMU330::PitchAC()
    {
    return accelPitch(Ax, Ay, Az);
    }

// ----------------------------------------------------------------------------

float IMU330::RollIMU()
    {
    return accelRoll(fAccelX, fAccelY, fAccelZ);
    }

// ----------------------------------------------------------------------------

float IMU330::RollAC()
    {
    return accelRoll(Ax, Ay, Az);
    }

// ----------------------------------------------------------------------------

onspeed::ImuSample IMU330::Snapshot() const
    {
    onspeed::ImuSample out;
    out.accelXG      = Ax;       // forward G (installation-bias applied)
    out.accelYG      = Ay;       // lateral G
    out.accelZG      = Az;       // vertical G
    out.gyroRollDps  = Gx;       // roll rate  (deg/s)
    out.gyroPitchDps = Gy;       // pitch rate (deg/s)
    out.gyroYawDps   = Gz;       // yaw rate   (deg/s)
    out.tempCelsius  = fTempC;   // IMU die temperature
    out.timestampUs  = 0;        // IMU330 does not track a read timestamp
    return out;
    }

