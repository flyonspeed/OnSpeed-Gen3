

#ifndef _HSC_PRESSURE_SENSOR_H_
#define _HSC_PRESSURE_SENSOR_H_

#include <Arduino.h>

#include "src/drivers/SPI_IO.h"

  enum EnPressureSensorType
  {
  HSCMRNN1_6BASA3,    // Honeywell 1.6 bar absolute pressure sensor (static)
  HSCMRRN001PDSA3,    // Honeywell 1 PSI differential pressure sensor (pitot, AOA)
  };

class HscPressureSensor
{
public:
  HscPressureSensor(SpiIO * pSensorSPI, int CsPort, int CountsMin, int CountsMax, float PressureMin, float PressureMax);
  HscPressureSensor(SpiIO * pSensorSPI, int CsPort, EnPressureSensorType enSensorType);

public:

  // Structure of data returned from sensor
  union   UnHSC
  {
    uint16_t    uStatusCounts;
    struct SuHSC
    {
      uint16_t    uCounts   : 14;
      uint16_t    uStatus   :  2;
    } suHSC;
  };

  // Data
protected:
  SpiIO       * SensorSPI;
  unsigned      uChipSel;
  uint16_t      uLastGoodCounts      = 0;
  bool          bHasLastGoodCounts   = false;

public:
  unsigned      uCountsMin, uCountsMax;
  float         fPressureMin, fPressureMax;

  // Methods
public:
  uint16_t ReadPressureCounts();

  float    ReadPressurePSI();
  float    ReadPressurePSI(uint16_t uCounts);

  float    ReadPressureMillibars();
  float    ReadPressureMillibars(uint16_t uCounts);

  UnHSC    ReadStatusCounts();

};

#endif
