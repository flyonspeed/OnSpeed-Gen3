
#pragma once

#include <HardwareSerial.h>

#include "src/Globals.h"

#define BOOM_BUFFER_SIZE    127


class BoomSerialIO
{
public:
    BoomSerialIO();

    // Structures

    // Data
public:
    Stream            * pSerial;

    char                Buffer[BOOM_BUFFER_SIZE];
    byte                BufferIndex;

    unsigned long       uTimestamp; // Millisecond timestamp of decoded data

    unsigned long       LastReceivedTime;
    int                 MaxAvailable;   // A debug value
    float               Static;
    float               Dynamic;
    float               Alpha;
    float               Beta;
    float               IAS;

    // Methods
public:
    void Init(Stream * pBoomSerial);
    void Read();
};