#pragma once

// Serial data variables (shared with main display code)
extern float           AOA;
extern int             PercentLift;
extern float           Pitch;
extern float           Roll;
extern float           IAS;
extern float           Palt;
extern float           iVSI;
extern float           VerticalG;
extern float           LateralG;
extern float           FlightPath;
extern int             FlapPos;
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
void SerialProcess(float frameDtSec);
unsigned int checkSerial();
String readSerialbytes();
void serialSetup();

// Freshness contract for the #1 display serial stream.
// serialMillis is updated on each successfully-parsed frame in SerialRead().
// Threshold is ~6x the nominal 50 ms period, matching the existing NO DATA
// overlay threshold. Call serialDataFresh() before trusting any of the
// serial-read globals (IAS, AOA, Pitch, Roll, VerticalG, etc.) in a
// computation or recording. The render path uses it to draw the NO DATA
// overlay; background recorders (G-history) must also gate on it, or the
// trace fills with stale frozen values during an outage.
extern const uint32_t kSerialDataFreshThresholdMs;
bool serialDataFresh();
