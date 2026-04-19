
#include <Arduino.h>
#include <SPI.h>

#include "Globals.h"
#include <sensors/PressureConvert.h>

using onspeed::psi2mb;

// These pressure ranges are the compensated pressure ranges for the sensor

// Honeywell HSCMRRN001PDSA3 differential pressure sensor (pitot, AOA)
#define COUNTS_MIN_DIFF     1638   // 10% of 16383 (14 bits)
#define COUNTS_MAX_DIFF    14745   // 90% of 16383 (14 bits)
#define PRESSURE_MIN_DIFF   -1.0   // PSI
#define PRESSURE_MAX_DIFF    1.0   // PSI

// Honeywell HSCMRNN1.6BASA3 absolute pressure sensor (static)
#define COUNTS_MIN_ABS      1638   // 10% of 16383 (14 bits)
#define COUNTS_MAX_ABS     14745   // 90% of 16383 (14 bits)
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
    case HSCMRNN1_6BASA3 :    // Honeywell 1.6 bar absolute pressure sensor
      uCountsMin   = COUNTS_MIN_ABS;
      uCountsMax   = COUNTS_MAX_ABS;
      fPressureMin = PRESSURE_MIN_ABS;
      fPressureMax = PRESSURE_MAX_ABS;
      break;
    case HSCMRRN001PDSA3 :    // Honeywell 1 PSI differential pressure sensor
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
    // Delegate to the platform-independent transfer function in onspeed_core.
    // If counts are outside the sensor's valid window, CountsToPsi returns
    // nullopt; fall back to the clamped endpoint so the caller sees a
    // saturated but numeric value rather than an uninitialized float.
    onspeed::sensors::HscRange range{
        static_cast<uint16_t>(uCountsMin),
        static_cast<uint16_t>(uCountsMax),
        fPressureMin,
        fPressureMax,
    };
    auto result = onspeed::sensors::CountsToPsi(uCounts, range);
    if (result.has_value())
        return *result;

    // Saturated: return the endpoint that matches which limit was breached.
    return (uCounts < uCountsMin) ? fPressureMin : fPressureMax;
}

// ----------------------------------------------------------------------------

float HscPressureSensor::ReadPressureMillibars()
{
    return psi2mb(ReadPressurePSI());
}

// ----------------------------------------------------------------------------

float HscPressureSensor::ReadPressureMillibars(uint16_t uCounts)
{
    return psi2mb(ReadPressurePSI(uCounts));
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

// ----------------------------------------------------------------------------

HscPressureSensor::RawPressureSnapshot HscPressureSensor::Snapshot() const
  {
    RawPressureSnapshot out;
    out.rawCounts = uLastGoodCounts;
    if (bHasLastGoodCounts)
        {
        // Delegate to the platform-independent transfer function.
        onspeed::sensors::HscRange range{
            static_cast<uint16_t>(uCountsMin),
            static_cast<uint16_t>(uCountsMax),
            fPressureMin,
            fPressureMax,
        };
        auto result = onspeed::sensors::CountsToPsi(out.rawCounts, range);
        out.pressurePsi = result.value_or(fPressureMin);
        }
    out.timestampUs = 0;   // HSC driver does not maintain a read timestamp
    return out;
  }
