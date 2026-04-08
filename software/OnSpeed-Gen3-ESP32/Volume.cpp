
#include "Globals.h"
#include "Volume.h"
#ifdef HW_V4P
#include "Mcp3202Adc.h"
#endif

// CheckVolumeTask moved to Housekeeping.cpp

// ----------------------------------------------------------------------------

// Read the voltage value from the volume pot.

uint16_t    ReadVolume()
    {
#ifdef HW_V4P
    return Mcp3202Read(ADC_CH_VOLUME);
#else
    return analogRead(VOLUME_PIN);
#endif
    }
