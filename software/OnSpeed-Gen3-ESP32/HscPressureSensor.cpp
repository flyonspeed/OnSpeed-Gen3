
#include <arduino.h>
#include <spi.h>

#include "Globals.h"
#include "HscPressureSensor.h"

// These pressure ranges are the compensated pressure ranges for the sensor

// Honeywell HSCDRNN1.6BASA3 differential pressure sensor
#define COUNTS_MIN_DIFF      409   // 10% of 4095 (12 bits)
#define COUNTS_MAX_DIFF     3686   // 90% of 4095 (12 bits)
#define PRESSURE_MIN_DIFF   -1.0   // PSI
#define PRESSURE_MAX_DIFF    1.0   // PSI

// Honeywell HSCDRRN100MDSA3 absolute pressure sensor
#define COUNTS_MIN_ABS       409   // 10% of 4095 (12 bits)
#define COUNTS_MAX_ABS      3686   // 90% of 4095 (12 bits)
#define PRESSURE_MIN_ABS     0.0   // PSI
#define PRESSURE_MAX_ABS    23.2   // PSI (1.6 bar)


// Construct sensor based on sensor parameters
HscPressureSensor::HscPressureSensor(SpiIO * pSensorSPI, int CsPort, int CountsMin, int CountsMax, float PressureMin, float PressureMax)
  {
    uCountsMin   = CountsMin;
    uCountsMax   = CountsMax;
    fPressureMin = PressureMin;
    fPressureMax = PressureMax;
    uChipSel     = CsPort;
    SensorSPI    = pSensorSPI;

  // Set up chip select pins as outputs
  pinMode(uChipSel, OUTPUT);

  } // end contructor

// Construct sensor based on sensor type
HscPressureSensor::HscPressureSensor(SpiIO * pSensorSPI, int CsPort, EnPressureSensorType enSensorType)
  {
  uChipSel     = CsPort;
  SensorSPI    = pSensorSPI;

  switch (enSensorType)
    {
    case HSCDRRN100MDSA3 :    // Honeywell absolute pressure sensor
      uCountsMin   = COUNTS_MIN_ABS;
      uCountsMax   = COUNTS_MAX_ABS;
      fPressureMin = PRESSURE_MIN_ABS;
      fPressureMax = PRESSURE_MAX_ABS;
      break;
    case   HSCDRNN1_6BASA3 :  // Honeywell differential pressure sensor
      uCountsMin   = COUNTS_MIN_DIFF;
      uCountsMax   = COUNTS_MAX_DIFF;
      fPressureMin = PRESSURE_MIN_DIFF;
      fPressureMax = PRESSURE_MAX_DIFF;
      break;
    }

  // Set up chip select pins as outputs
  pinMode(uChipSel, OUTPUT);

  } // end contructor


// ----------------------------------------------------------------------------

uint16_t  HscPressureSensor::ReadPressureCounts()
{
    UnHSC   uStatusCounts;

    uStatusCounts = ReadStatusCounts();

    // Honeywell HSC sensors include a 2-bit status field. Only accept samples
    // with normal status; otherwise, reuse the last good sample to avoid
    // injecting spikes/glitches into downstream filters.
    if (uStatusCounts.suHSC.uStatus == 0)
        {
        uLastGoodCounts    = uStatusCounts.suHSC.uCounts;
        bHasLastGoodCounts = true;
        return uStatusCounts.suHSC.uCounts;
        }

    if (bHasLastGoodCounts)
        return uLastGoodCounts;

    return uStatusCounts.suHSC.uCounts;
}

// ----------------------------------------------------------------------------

float HscPressureSensor::ReadPressurePSI()
{
    UnHSC   uStatusCounts;
    uint16_t uCounts;

    uStatusCounts = ReadStatusCounts();

    if (uStatusCounts.suHSC.uStatus == 0)
        {
        uCounts            = uStatusCounts.suHSC.uCounts;
        uLastGoodCounts    = uCounts;
        bHasLastGoodCounts = true;
        }
    else if (bHasLastGoodCounts)
        {
        uCounts = uLastGoodCounts;
        }
    else
        {
        uCounts = uStatusCounts.suHSC.uCounts;
        }

    g_Log.printf(MsgLog::EnPressure, MsgLog::EnDebug, "Status 0x%2.2x  Counts %5u\n",
        uStatusCounts.suHSC.uStatus, (unsigned)uCounts);
    return ReadPressurePSI(uCounts);
}

// ----------------------------------------------------------------------------

float HscPressureSensor::ReadPressurePSI(uint16_t uCounts)
{
    return (uCounts - uCountsMin) * (fPressureMax - fPressureMin) / (uCountsMax - uCountsMin) + fPressureMin;
}

// ----------------------------------------------------------------------------

float HscPressureSensor::ReadPressureMillibars()
{
    return PSI2MB(ReadPressurePSI());
}

// ----------------------------------------------------------------------------

float HscPressureSensor::ReadPressureMillibars(uint16_t uCounts)
{
    return PSI2MB(ReadPressurePSI(uCounts));
}

// ----------------------------------------------------------------------------

HscPressureSensor::UnHSC HscPressureSensor::ReadStatusCounts()
  {
    HscPressureSensor::UnHSC  unData;

    uint8_t     aiData[2];
    SensorSPI->ReadBytes(uChipSel, aiData, 2);
    unData.uStatusCounts = (aiData[0] << 8) | aiData[1];

    return unData;
  } // end ReadCounts
