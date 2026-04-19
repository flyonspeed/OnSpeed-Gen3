
#include "../../Globals.h"
#include "Volume.h"
#include "../drivers/Mcp3202Adc.h"

// CheckVolumeTask moved to Housekeeping.cpp

// ----------------------------------------------------------------------------

// Read the voltage value from the volume pot.

uint16_t    ReadVolume()
    {
    if constexpr (kHasExternalMcp3202)
        return Mcp3202Read(kAdcChVolume);
    else
        return analogRead(kPinVolume);
    }
