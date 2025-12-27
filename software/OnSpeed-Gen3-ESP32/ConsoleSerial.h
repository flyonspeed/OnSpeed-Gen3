
#pragma once

#include <HardwareSerial.h>

#include "Globals.h"

//#include <SoftwareSerial.h>
#include <HardwareSerial.h>

#define CONSOLE_BUFFER_SIZE    127


class ConsoleSerialIO
{
public:
    ConsoleSerialIO();

    // Structures

    // Data
public:
    HardwareSerial    * pSerial;
    bool                bEnabled;

    char                CmdBuffer[CONSOLE_BUFFER_SIZE];
    byte                CmdBufferIndex;

//    unsigned long       uTimestamp; // Millisecond timestamp of decoded data
//    unsigned long       LastReceivedTime;

    // Methods
public:
    void Init();
    void Enable(bool bEnable);
    void Read();
    void DisplayConsoleHelp();
    void PrintTaskInfo(TaskHandle_t  xTask);

};