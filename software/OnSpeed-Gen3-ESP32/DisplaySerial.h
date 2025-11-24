
#pragma once

#include "Globals.h"


// FreeRTOS task for writing data to a display
void WriteDisplayDataTask(void * pvParams);

// ============================================================================

class DisplaySerial
{
public:
    DisplaySerial();

    // Data
public:
    float       fPAltSmoothed;
    float       fVerticalGSmoothed;
    float       fLateralGSmoothed;

    // In the original G2V3 implementation the panel output port could be
    // selected. In this implementation panel output is a fixed serial
    // port. But to easily allow run time configuration I have made the
    // serial port a pointer to the Stream base object.
    Stream    * pSerial;

    // Methods
public:
    void Init(Stream * pDispSerial);
    void Write();

};