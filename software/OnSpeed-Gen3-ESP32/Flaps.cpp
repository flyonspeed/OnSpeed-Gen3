
#include "Globals.h"
#include "Config.h"
#include "Flaps.h"
#ifdef HW_V4P
#include "Mcp3204Adc.h"
#endif

// ----------------------------------------------------------------------------

Flaps::Flaps()
{
#ifndef HW_V4P
    pinMode(FLAP_PIN, INPUT_PULLUP);
#endif
}

// ----------------------------------------------------------------------------

// Read and return the flap analog position

uint16_t Flaps::Read()
{
#ifdef HW_V4P
    return Mcp3204Read(ADC_CH_FLAP);
#else
    return analogRead(FLAP_PIN);
#endif
}

// ----------------------------------------------------------------------------

// Read the flap position and update some values

void Flaps::Update()
{
    // Read the analog value
    uValue = Read();

    // If there are no flap definitions then set the position to -1. That
    // should get someone's attention.
    if (g_Config.aFlaps.size() == 0)
    {
        iPosition = -1;
        return;
    }

    // Set it to flap zero if there are no multiple positions available
    iIndex = 0;

    // Figure out where this value is in the array of flap position values
    if (g_Config.aFlaps.size() > 1)
    {
        int     iPotRangeMidpoint;
        bool    bDecendingOrder = false;

        // If the first flap pot position is greater than the last flap pot position then
        // the pot positions must be in decending order.
        if (g_Config.aFlaps[0].iPotPosition > g_Config.aFlaps[g_Config.aFlaps.size()-1].iPotPosition)
            bDecendingOrder = true;

        for (int iFlapIdx = 1; iFlapIdx < g_Config.aFlaps.size(); iFlapIdx++)
        {
            iPotRangeMidpoint = (g_Config.aFlaps[iFlapIdx].iPotPosition + g_Config.aFlaps[iFlapIdx-1].iPotPosition) / 2;

            // Not decending order
            if (!bDecendingOrder)
            {
                if (uValue > iPotRangeMidpoint)
                    iIndex = iFlapIdx;
            }

            // Decending order
            else
            {
                if (uValue < iPotRangeMidpoint)
                    iIndex = iFlapIdx;
            }
        } // end for all flap positions

    } // end if number of flap cal points is greater than 1

    // Set the flap position based on the index value just found
    Update(iIndex);
}

// ----------------------------------------------------------------------------

// Sometimes (like Test Pot mode) you just want to set an index

void Flaps::Update(int iFlapsIndex)
{
    iIndex    = iFlapsIndex;
    iPosition = g_Config.aFlaps[iIndex].iDegrees;
}

