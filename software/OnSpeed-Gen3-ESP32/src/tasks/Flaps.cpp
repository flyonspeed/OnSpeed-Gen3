
#include "Globals.h"
#include "src/config/Config.h"
#include "src/drivers/Mcp3202Adc.h"
#include <sensors/FlapsDetector.h>

// ----------------------------------------------------------------------------

Flaps::Flaps()
{
    if constexpr (!kHasExternalMcp3202)
        pinMode(kPinFlap, INPUT_PULLUP);
}

// ----------------------------------------------------------------------------

// Read and return the flap analog position

uint16_t Flaps::Read()
{
    if constexpr (kHasExternalMcp3202)
        return Mcp3202Read(kAdcChFlap);
    else
        return analogRead(kPinFlap);
}

// ----------------------------------------------------------------------------

// Read the flap position and update some values

void Flaps::Update()
{
    // Read the analog value
    uValue = Read();

    // Build a temporary array of pot positions for the core detector.
    // The config stores iPotPosition as int; cast to uint16_t (ADC range
    // is 0–4095, so fits).
    const size_t nFlaps = g_Config.aFlaps.size();
    if (nFlaps == 0u)
    {
        iPosition = -1;
        return;
    }

    uint16_t potPositions[MAX_AOA_CURVES];
    for (size_t i = 0u; i < nFlaps && i < MAX_AOA_CURVES; ++i)
        potPositions[i] = static_cast<uint16_t>(g_Config.aFlaps[i].iPotPosition);

    // Delegate the midpoint-threshold detection to the platform-independent
    // core function. It handles both ascending and descending wiring.
    onspeed::FlapState state = onspeed::sensors::DetectFlaps(uValue, potPositions, nFlaps);

    // Transfer the detected index back to our member fields, then resolve
    // the degree value from config. Update(int) handles clamping.
    Update(state.detectedIndex);
}

// ----------------------------------------------------------------------------

// Sometimes (like Test Pot mode) you just want to set an index

void Flaps::Update(int iFlapsIndex)
{
    if (g_Config.aFlaps.empty())
    {
        iIndex    = 0;
        iPosition = -1;
        return;
    }

    iIndex    = constrain(iFlapsIndex, 0, (int)g_Config.aFlaps.size() - 1);
    iPosition = g_Config.aFlaps[iIndex].iDegrees;
}

