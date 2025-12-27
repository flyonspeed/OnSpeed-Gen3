
#include <arduino.h>
#include <spi.h>

#include "Globals.h"
#include "IMU330.h"

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

#define ACCEL_RES      8.0 / 32768.0     // full scale /resolution (8G)
#define GYRO_RES     245.0 / 32768.0     // full scale / resolution (245 dps)

#define ISM330_TEMP_SCALE   256.0
#define ISM330_TEMP_BIAS     25.0

// ============================================================================

IMU330::IMU330(SpiIO * pSensorSPI, int CsPort)
{
    uChipSel  = CsPort;
    SensorSPI = pSensorSPI;
//    Config    = pConfig;

    lLastImuTempUpdate = millis();

    // Set up chip select pins as outputs
    pinMode(uChipSel, OUTPUT);

    pTempAvg = new RunningAverage(20);

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

  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL9_XL), B11100010);
  delay(50);

  // enable accelerometer
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL1_XL), B01011100); // 208hz ODR, +/-8G, LPF2 disabled
  delay(50);

  // enable gyroscope
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL2_G), B01010000); // 208 hz, 250dps
  delay(50);

  // disable gyroscope hi-pass filter
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL7_G), B00000000); // high performance mode, disable high pass filter
  delay(50);

  // disable LPF1, disable I2C
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL4_C), B00000100); // disable low pass filter 1, LPF2 is still on at 67hz bandwidth
  delay(50);

  // set fifo
  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(FIFO_CTRL4), B00010000); // bypass mode, fifo disabled
  delay(50);

  SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL9_XL), B11100000);
  delay(50);

  g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "IMU Who Am I : 0x%2.2X\n", g_pIMU->WhoAmI());

}

// ----------------------------------------------------------------------------

void IMU330::Reset()
{
    // soft reset accelerometer/gyro
    SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL3_C),  B00000101);
    delay(100);
    SensorSPI->WriteRegByte(uChipSel, IMU_WRITE_ADDR(CTRL3_C),  B00000100);
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
#if 0
    Az = GetAccelForAxis(sVerticalGloadAxis);
    Ay = GetAccelForAxis(sLateralGloadAxis);
    Ax = GetAccelForAxis(sForwardGloadAxis);
    Gx = GetGyroForAxis(sRollGyroAxis);
    Gy = GetGyroForAxis(sPitchGyroAxis);
    Gz = GetGyroForAxis(sYawGyroAxis);
#else
    Ax = *pfAx * fAxSign;
    Ay = *pfAy * fAySign;
    Az = *pfAz * fAzSign;
    Gx = *pfGx * fGxSign;
    Gy = *pfGy * fGySign;
    Gz = *pfGz * fGzSign;
#endif

    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug,
        "Ax %.3f, Ay %.3f, Az %.3f, Gx %.4f, Gy %.4f, Gz %.4f, Temp %.2fC\n",
        Ax, Ay, Az, Gx, Gy, Gz, fTempC);

#ifdef LOGDATA_IMU_RATE
    logData();
#endif

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
    // Get accelerometer axis from box orientation (there must be a better way to do this, but it works for now)
    // orientation arrays defined as box orientation
    // configure axes to North East Down (NED) Axis convention
    // vertical axis (z down), lateral axis (y right), longitudinal axis (x forward)
    // pitch rate positive for pitch up
    // roll rate positive for right roll
    // yaw rate positive for right yaw

    sVerticalGloadAxis = "";
    sLateralGloadAxis  = "";
    sForwardGloadAxis  = "";

      // {sPortsOrientation, sBoxtopOrientation, sVerticalGloadAxis, sLateralGloadAxis, sForwardGloadAxis}
#ifdef HW_V4P
    String axisMapArray[24][5] = {
        // This is the orientation for the V4P box
        // IMU inside the V4P box is rotated relative to the V4B box:
        // V4P(X)=V4B(-Y)  V4P(Y)=V4B(-X)  V4P(Z)=V4B(-Z)
      {"FORWARD", "LEFT",     "X","-Z", "Y"},
      {"FORWARD", "RIGHT",   "-X", "Z", "Y"},
      {"FORWARD", "UP",       "Z", "X", "Y"},
      {"FORWARD", "DOWN",    "-Z","-X", "Y"},

      {"AFT",     "LEFT",    "-X","-Z","-Y"},
      {"AFT",     "RIGHT",    "X", "Z","-Y"},
      {"AFT",     "UP",       "Z","-X","-Y"},
      {"AFT",     "DOWN",    "-Z", "X","-Y"},

      {"LEFT",    "FORWARD", "-X","-Y", "Z"},
      {"LEFT",    "AFT",      "X","-Y","-Z"},
      {"LEFT",    "UP",       "Z","-Y", "X"},
      {"LEFT",    "DOWN",    "-Z","-Y","-X"},

      {"RIGHT",   "FORWARD",  "X", "Y", "Z"},
      {"RIGHT",   "AFT",     "-X", "Y","-Z"},
      {"RIGHT",   "UP",       "Z", "Y","-X"},
      {"RIGHT",   "DOWN",    "-Z", "Y", "X"},

      {"UP",      "FORWARD",  "Y","-X", "Z"},
      {"UP",      "AFT",      "Y", "X","-Z"},
      {"UP",      "LEFT",     "Y","-Z","-X"},
      {"UP",      "RIGHT",    "Y", "Z", "X"},

      {"DOWN",    "FORWARD", "-Y", "X", "Z"},
      {"DOWN",    "AFT",     "-Y","-X","-Z"},
      {"DOWN",    "LEFT",    "-Y","-Z", "X"},
      {"DOWN",    "RIGHT",   "-Y", "Z","-X"}
      };
#else
    String axisMapArray[24][5] = {
        // This is the orientation for the V4B box
        // V4B +X back (away from pressure ports)  +Y right (towards USB ports)  +Z down
      {"FORWARD", "LEFT",    "-Y", "Z","-X"}, // TESTED GOOD Paul's
      {"FORWARD", "RIGHT",    "Y","-Z","-X"}, // TESTED GOOD
      {"FORWARD", "UP",      "-Z","-Y","-X"}, // TESTED GOOD, Vac's RV-4
      {"FORWARD", "DOWN",     "Z", "Y","-X"}, // TESTED GOOD

      {"AFT",     "LEFT",     "Y", "Z", "X"}, // TESTED GOOD
      {"AFT",     "RIGHT",   "-Y","-Z", "X"}, // TESTED GOOD
      {"AFT",     "UP",      "-Z", "Y", "X"}, // TESTED GOOD, bench box
      {"AFT",     "DOWN",     "Z","-Y", "X"}, // TESTED GOOD

      {"LEFT",    "FORWARD",  "Y", "X","-Z"}, // TESTED GOOD
      {"LEFT",    "AFT",     "-Y", "X", "Z"}, // TESTED GOOD
      {"LEFT",    "UP",      "-Z", "X","-Y"}, // TESTED GOOD, Zlin Z-50
      {"LEFT",    "DOWN",     "Z", "X", "Y"}, // TESTED GOOD

      {"RIGHT",   "FORWARD", "-Y","-X","-Z"}, // TESTED GOOD
      {"RIGHT",   "AFT",      "Y","-X", "Z"}, // TESTED GOOD
      {"RIGHT",   "UP",      "-Z","-X", "Y"}, // TESTED GOOD
      {"RIGHT",   "DOWN",     "Z","-X","-Y"}, // TESTED GOOD, Tron's RV-8

      {"UP",      "FORWARD", "-X", "Y","-Z"}, // TESTED GOOD
      {"UP",      "AFT",     "-X","-Y", "Z"}, // TESTED GOOD
      {"UP",      "LEFT",    "-X", "Z", "Y"}, // TESTED GOOD
      {"UP",      "RIGHT",   "-X","-Z","-Y"}, // TESTED GOOD, Doc's box on Vac's RV-4

      {"DOWN",    "FORWARD",  "X","-Y","-Z"}, // TESTED GOOD
      {"DOWN",    "AFT",      "X", "Y", "Z"}, // TESTED GOOD, Lenny's RV-10
      {"DOWN",    "LEFT",     "X", "Z","-Y"}, // TESTED GOOD
      {"DOWN",    "RIGHT",    "X","-Z", "Y"}  // TESTED GOOD
      };
#endif

    for (int i=0;i<24;i++)
        {
        if (axisMapArray[i][0] == g_Config.sPortsOrientation && axisMapArray[i][1] == g_Config.sBoxtopOrientation)
            {
            sVerticalGloadAxis = axisMapArray[i][2];
            sLateralGloadAxis  = axisMapArray[i][3];
            sForwardGloadAxis  = axisMapArray[i][4];
            sYawGyroAxis       = sVerticalGloadAxis;
            sPitchGyroAxis     = sLateralGloadAxis;
            sRollGyroAxis      = sForwardGloadAxis;

            GetAccelForAxis(axisMapArray[i][2], &fAzSign, &pfAz);
            GetAccelForAxis(axisMapArray[i][3], &fAySign, &pfAy);
            GetAccelForAxis(axisMapArray[i][4], &fAxSign, &pfAx);
            GetGyroForAxis( axisMapArray[i][2], &fGzSign, &pfGz);    //// CHECK THESE!!!
            GetGyroForAxis( axisMapArray[i][3], &fGySign, &pfGy);
            GetGyroForAxis( axisMapArray[i][4], &fGxSign, &pfGx);
            break;
            }
        }

    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "portsOrientation  : %s\n", g_Config.sPortsOrientation.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "boxtopOrientation : %s\n", g_Config.sBoxtopOrientation.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Vertical axis     : %s\n", sVerticalGloadAxis.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Lateral axis      : %s\n", sLateralGloadAxis.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Forward axis      : %s\n", sForwardGloadAxis.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Yaw Gyro axis     : %s\n", sYawGyroAxis.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Pitch Gyro axis   : %s\n", sPitchGyroAxis.c_str());
    g_Log.printf(MsgLog::EnIMU, MsgLog::EnDebug, "Roll Gyro axis    : %s\n", sRollGyroAxis.c_str());
    }

// ----------------------------------------------------------------------------

float IMU330::GetAccelForAxis(String accelAxis)
    {
    float result=0.0;
    if      (accelAxis[accelAxis.length()-1] == 'X') result = fAccelX;
    else if (accelAxis[accelAxis.length()-1] == 'Y') result = fAccelY;
    else                                             result = fAccelZ;

    if (accelAxis[0]=='-') result*=-1;

    return result;
    }

// ----------------------------------------------------------------------------

void IMU330::GetAccelForAxis(String accelAxis, float * pfAccelSign, float ** ppfAccel)
    {
    if      (accelAxis[accelAxis.length()-1] == 'X') *ppfAccel = &fAccelX;
    else if (accelAxis[accelAxis.length()-1] == 'Y') *ppfAccel = &fAccelY;
    else                                             *ppfAccel = &fAccelZ;

    if (accelAxis[0] == '-') *pfAccelSign = -1;
    else                   *pfAccelSign =  1;
    }

// ----------------------------------------------------------------------------

float IMU330::GetGyroForAxis(String gyroAxis)
    {
    float result=0.0;
    if      (gyroAxis[gyroAxis.length()-1] == 'X') result = fGyroX + g_Config.fGxBias;
    else if (gyroAxis[gyroAxis.length()-1] == 'Y') result = fGyroY + g_Config.fGyBias;
    else                                           result = fGyroZ + g_Config.fGzBias;

    // For gyro the sign is inverted
    if (gyroAxis[0]!='-') result *= -1;

    return result;
    }

// ----------------------------------------------------------------------------

void IMU330::GetGyroForAxis(String gyroAxis, float * pfGyroSign, float ** ppfGryo)
    {
    float result=0.0;
    if      (gyroAxis[gyroAxis.length()-1] == 'X') *ppfGryo = &fGyroXwBias;
    else if (gyroAxis[gyroAxis.length()-1] == 'Y') *ppfGryo = &fGyroYwBias;
    else                                           *ppfGryo = &fGyroZwBias;

    // For gyro the sign is inverted
    if (gyroAxis[0] != '-') *pfGyroSign = -1;
    else                    *pfGyroSign =  1;
    }

// ----------------------------------------------------------------------------

//float IMU330::GetGyroForAxisNoBias(String gyroAxis)
//    {
//    float result=0.0;
//    if      (gyroAxis[gyroAxis.length()-1] == 'X') result = fGyroX;
//    else if (gyroAxis[gyroAxis.length()-1] == 'Y') result = fGyroY;
//    else                                           result = fGyroZ;
//
//    if (gyroAxis[0]!='-') result*=-1;
//
//    return result;
//    }

// ----------------------------------------------------------------------------

// Pitch in IMU coordinates

float IMU330::PitchIMU()
    {
    return PITCH(fAccelX, fAccelY, fAccelZ);
    }

// ----------------------------------------------------------------------------

// Pitch in aircraft coordinates

float IMU330::PitchAC()
    {
    return PITCH(Ax, Ay, Az);
    }

// ----------------------------------------------------------------------------

float IMU330::RollIMU()
    {
    return ROLL(fAccelX, fAccelY, fAccelZ);
    }

// ----------------------------------------------------------------------------

float IMU330::RollAC()
    {
    return ROLL(Ax, Ay, Az);
    }

