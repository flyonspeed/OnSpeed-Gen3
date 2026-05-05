#pragma once

// Serial data variables (shared with main display code)
//
// PercentLift is the integer (0..99) percent-of-stall, used for chevron
// color logic and slip-color comparisons.  PercentLiftDeci carries the
// same value with one decimal place (0.0..99.9), used for the AOA bar's
// y position so it advances at sub-pixel temporal smoothness off the
// 20 Hz wire.  Both are populated from the same wire field (tenths of
// a percent, 0..999) on each parsed frame.
extern int             PercentLift;
extern float           PercentLiftDeci;
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
// Indexer percent anchors — populated from #1 wire.  TonesOn / Fast /
// Slow / StallWarn snap to the active detent (operational cues, in
// lockstep with the audio path).  PipPctLift slides smoothly across
// the entire pot range from cleanest to most-deployed detent (visual
// aerodynamic reference).  See onspeed_core/aoa/DisplayPctAnchors.h
// for the design rule (Vac, ld_max.pdf §8).
extern int             TonesOnPctLift;     // active-detent L/Dmax pct (operational, audio gate)
extern int             OnSpeedFastPctLift; // OnSpeedFast body angle → percent
extern int             OnSpeedSlowPctLift; // OnSpeedSlow body angle → percent
extern int             StallWarnPctLift;   // StallWarn body angle → percent
extern int             PipPctLift;         // visual L/Dmax pip pct (aerodynamic, lerp clean→fullflap)
extern int             FlapsMinDeg;        // configured flap travel minimum; from #1 wire
extern int             FlapsMaxDeg;        // configured flap travel maximum; from #1 wire
extern float           gOnsetRate;
extern int             SpinRecoveryCue;
extern int             DataMark;
extern float           DecelRate;
extern float           SmoothedDecelRate;

extern uint64_t        serialMillis;

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
