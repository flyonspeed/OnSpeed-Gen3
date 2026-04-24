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

// Per-byte injection into the #1 frame state machine. Primary consumer is
// the ESP-side Serial2 drain in SerialRead(); the desktop/WASM simulator
// uses it to replay CSV-sourced frames without a physical UART.
void InjectSerialByte(char c);

#if defined(ESP_PLATFORM)
unsigned int checkSerial();
String readSerialbytes();
void serialSetup();
#endif

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
