#pragma once

// Serial data variables (shared with main display code)
extern float           AOA;
extern float           SmoothedAOA;
extern int             PercentLift;
extern float           Pitch;
extern float           Roll;
extern float           IAS;
extern float           Palt;
extern float           iVSI;
extern float           VerticalG;
extern float           LateralG;
extern float           SmoothedLateralG;
extern float           FlightPath;
extern int             FlapPos;
extern float           TurnRate;
extern int             OAT;
extern int16_t         Slip;
extern float           OnSpeedStallWarnAOA;
extern float           OnSpeedSlowAOA;
extern float           OnSpeedFastAOA;
extern float           OnSpeedTonesOnAOA;
extern float           gOnsetRate;
extern int             SpinRecoveryCue;
extern int             DataMark;
extern float           DecelRate;
extern float           SmoothedDecelRate;

extern uint64_t        serialMillis;
extern String          serialBufferString;

// Serial port management
extern unsigned int    selectedPort;

void SerialRead();
void SerialProcess();
unsigned int checkSerial();
String readSerialbytes();
void serialSetup();
