// Parses the OnSpeed #1 serial protocol and preprocesses data for display.

#if defined(ESP_PLATFORM)
#include <Arduino.h>
#include <Preferences.h>
#else
#include "../sim/ArduinoShim.h"
#endif

#include <M5Unified.h>
#include <Free_Fonts.h>
#include <filters/SavGolDerivative.h>
#include <proto/DisplaySerial.h>
#include "SerialRead.h"

using onspeed::proto::ParseDisplayFrame;
using onspeed::proto::kDisplayFrameSizeBytes;
using onspeed::proto::DisplayFrame;

// Decel-rate EMA on the locally-computed IAS derivative. AOA and LateralG
// are used as-received — the main firmware already smooths them at the
// sensor layer (iAoaSmoothing, AccelLatFilter), so the display matches
// what the audio tones react to.
static const float decelSmoothingAlpha = 0.04f;  //1 = max smoothing, 0.01 no smoothing.

// IAS derivative filter (Savitzky-Golay first derivative, window=15)
static double iasDerivativeInput;
static onspeed::SavGolDerivative iasDerivative(&iasDerivativeInput, 15);

extern M5Canvas gdraw;

#if defined(ESP_PLATFORM)
static const uint16_t WIDTH  = 320;
static const uint16_t HEIGHT = 240;

// Port C serial pins. M5Stack Basic exposes Port C on GPIO 16 (RX) /
// GPIO 17 (TX). M5Stack Core2 moves Port C to GPIO 13 (RX) / GPIO 14
// (TX). The simulator-demo "case 3" path used GPIO 22 on Basic; Core2
// routes that role through Port C too, so both envs land on their
// Port C RX for this case.
//
// PlatformIO's m5stack-core2 board definition sets
// -DARDUINO_M5STACK_Core2 (mixed-case suffix "Core2", not "CORE2") —
// see $PIO_HOME/platforms/espressif32/boards/m5stack-core2.json.
// Guard on the exact spelling the toolchain defines.
#ifdef ARDUINO_M5STACK_Core2
static constexpr int PORTC_RX    = 13;
static constexpr int PORTC_TX    = 14;
static constexpr int SIMDEMO_RX  = 13;
#else
static constexpr int PORTC_RX    = 16;
static constexpr int PORTC_TX    = 17;
static constexpr int SIMDEMO_RX  = 22;
#endif
#endif // ESP_PLATFORM

// -----------------------------------------------

// Freshness predicate (finding 037). Matches the 300 ms threshold that the
// existing NO DATA overlay in main.cpp has used since the Gen2 port. Exposed
// so any consumer — render path, G-history recorder, future X-Plane bridge —
// can gate itself on fresh data without duplicating the bare
// millis() - serialMillis > 300 check.
const uint32_t kSerialDataFreshThresholdMs = 300;

bool serialDataFresh()
{
    return (millis() - serialMillis) <= kSerialDataFreshThresholdMs;
}

// -----------------------------------------------
// Byte-stream state machine. Exposed as a public entry point so the
// same parser can be fed from the hardware UART today and from a
// non-UART byte source (e.g. a future CSV replay harness on the
// desktop/WASM sim) without branching the render path. Matches one
// 80-byte #1 frame: '#' '1' ... LF, checksum-verified downstream.
// -----------------------------------------------

void InjectSerialByte(char inChar)
{
    if (inChar == '#')
    {
        // reset RX buffer
        serialBufferString = inChar;
        return;
    }

    if (serialBufferString.length() > 80)
    {
        // prevent buffer overflow;
        serialBufferString = "";
        Serial.println("Serial data buffer overflow");
        Serial.println(serialBufferString);
        return;
    }

    if (serialBufferString.length() == 0)
        return;

    serialBufferString += inChar;

    if (!(serialBufferString.length() == 80 &&
          serialBufferString[0]       == '#' &&
          serialBufferString[1]       == '1' &&
          inChar                      == char(0x0A))) // ONSPEED protocol
        return;

    #ifdef SERIALDATADEBUG
    Serial.println(serialBufferString);
    #endif

    // Parse the frame via the shared core module.
    // ParseDisplayFrame verifies the checksum and extracts all fields.
    auto result = ParseDisplayFrame(
        reinterpret_cast<const uint8_t*>(serialBufferString.c_str()),
        kDisplayFrameSizeBytes);

    if (!result.has_value())
    {
        Serial.println("ONSPEED CRC Failed");
        return;
    }

    const DisplayFrame& f = result.value();

    Pitch               = f.pitchDeg;
    Roll                = f.rollDeg;
    IAS                 = f.iasKt;
    Palt                = f.paltFt;
    LateralG            = f.lateralG;
    VerticalG           = f.verticalG;
    PercentLift         = f.percentLift;
    AOA                 = f.aoaDeg;
    iVSI                = f.vsiFpm;
    OAT                 = f.oatC;
    FlightPath          = f.flightPathDeg;
    FlapPos             = f.flapsDeg;
    OnSpeedStallWarnAOA = f.stallWarnAoaDeg;
    OnSpeedSlowAOA      = f.onSpeedSlowAoaDeg;
    OnSpeedFastAOA      = f.onSpeedFastAoaDeg;
    OnSpeedTonesOnAOA   = f.tonesOnAoaDeg;
    gOnsetRate          = f.gOnsetRate;
    SpinRecoveryCue     = f.spinRecoveryCue;
    DataMark            = f.dataMark;

    serialBufferString = "";

    // Measure actual frame period rather than assuming a fixed rate.
    // The main firmware's display serial task runs at
    // kDisplaySerialPeriodMs (currently 20 Hz), but hardcoding that here is
    // a fragility we don't want — this mirrors how the main firmware's AHRS
    // (Madgwick, EKF6) also uses measured dt each tick.
    static uint32_t lastFrameMicros = 0;
    uint32_t nowMicros = micros();
    float frameDtSec = (lastFrameMicros == 0)
                           ? 0.05f
                           : (nowMicros - lastFrameMicros) * 1e-6f;

    // Finding 036: on a long serial gap (OnSpeed box reboot, cable reconnect),
    // the SavGol window still holds 15 stale pre-gap IAS samples. Against a
    // single fresh sample those produce a huge bogus derivative for the first
    // ~15 post-gap frames. Reset the filter (and drain the EMAs) whenever we
    // see > 500 ms between frames. This must run BEFORE the dt clamp below,
    // since a genuine outage produces dt >> 0.2 s and the clamp would
    // otherwise hide it.
    if (lastFrameMicros != 0 && frameDtSec > 0.5f)
    {
        iasDerivative.reset();
        DecelRate         = 0.0f;
        SmoothedDecelRate = 0.0f;
    }

    lastFrameMicros = nowMicros;

    // Soft warning if frame cadence drifts outside expected band — real
    // firmware targets 50 ms; bench replay tools may be slightly off.
    if (frameDtSec < 0.020f || frameDtSec > 0.200f)
        Serial.printf("WARN: unexpected frame dt=%.3fs\n", frameDtSec);

    // Finding 035: clamp to a sane band before dividing the SavGol derivative
    // by it. Lower bound is half the nominal 20 ms frame period — well below
    // the protocol rate but far enough from zero to avoid div-by-tiny-number
    // explosion when two frames arrive back-to-back (replay bursts, post-stall
    // buffer drain). Upper bound matches the warn band.
    frameDtSec = constrain(frameDtSec, 0.010f, 0.200f);

    SerialProcess(frameDtSec);

    #ifdef SERIALDATADEBUG
    Serial.printf("ONSPEED data: Millis %i, IAS %.2f, Pitch %.1f, Roll %.1f, LateralG %.2f, VerticalG %.2f, Palt %0.1f, iVSI %.1f, AOA: %.1f", millis()-serialMillis, IAS, Pitch, Roll, LateralG, VerticalG, Palt, iVSI, AOA);
    Serial.println();
    #endif

    serialMillis = millis();
}

// -----------------------------------------------

void SerialRead()
{
#ifndef DUMMY_SERIAL_DATA
#ifdef ESP_PLATFORM
    if (Serial2.available())
        InjectSerialByte(Serial2.read());
#endif
    // Desktop / WASM without DUMMY_SERIAL_DATA: no-op. A future CSV
    // replay harness drives the parser by calling InjectSerialByte()
    // directly from its own tick.
#else
    // Provide dummy display data
    uint64_t  currMillis;
    currMillis = millis();

    // Update if 100 msec (10 Hz) has passed
    if (serialMillis + 100 < currMillis)
    {
        Pitch               = 5.0;
        Roll                = 0.0;
        IAS                 = 100.0;
        Palt                = 2500.0;
        LateralG            = 0.0;
        VerticalG           = 1.0;  // 1g straight-and-level (bench realism)
        iVSI                = 0.0;
        OAT                 = 70;
        FlightPath          = 0.0;
        FlapPos             = 0;
        OnSpeedStallWarnAOA = 20.0;
        OnSpeedSlowAOA      = 15.0;
        OnSpeedFastAOA      = 10.0;
        OnSpeedTonesOnAOA   =  5.0;
        gOnsetRate          = 0.0;
        SpinRecoveryCue     = 0;
        DataMark            = 0;

        // Body-angle sweep covering alpha_0 through past stall-warn for
        // a typical RV-class aircraft. The sweep top sits above the
        // dummy frame's StallWarnAOA (20°, set above) so the indexer's
        // top-chevron flash engages — same visual a pilot would see in
        // the airplane when AOA crosses StallWarn. PercentLift uses
        // alpha_0 as the zero-lift floor and clamps at 99 to match the
        // %02u wire protocol field in the #1 frame; a real stall reads
        // 99 on the device, never 100.
        static constexpr float kSimAlpha0     = -4.0f;
        static constexpr float kSimAlphaStall = 18.0f;
        static constexpr float kSimAoaMax     = 24.0f;

        if (AOA < kSimAoaMax) AOA += 0.2f;
        else                  AOA  = kSimAlpha0;

        const float fraction =
            (AOA - kSimAlpha0) / (kSimAlphaStall - kSimAlpha0);
        PercentLift = (int)(fraction * 100.0f);
        if (PercentLift < 0)  PercentLift = 0;
        if (PercentLift > 99) PercentLift = 99;

        // Dummy data ticks every 100 ms.
        SerialProcess(0.1f);

        serialMillis = currMillis;
    }
#endif

} // end SerialRead()


// -----------------------------------------------

// Preprocess some of the serial data

void SerialProcess(float frameDtSec)
{
    // don't display invalid values;
    if (AOA == -100)
        AOA = 0.0;

    // Slip gauge: LateralG is already EMA-smoothed by the main firmware
    // (AccelLatFilter in onspeed_core/ahrs). Use as-received.
    Slip               = int(LateralG * 34 / 0.04);
    Slip               = constrain(Slip,-99,99);

    // Compute IAS derivative (deceleration) in knots/sec.
    // SavGolDerivative returns d(IAS)/d(sample); dividing by the measured
    // frame period converts per-sample to per-second. Sign is already
    // correct (positive for increasing IAS, negative for deceleration).
    iasDerivativeInput =  IAS;
    DecelRate          =  iasDerivative.Compute() / frameDtSec;
    SmoothedDecelRate  =  DecelRate * decelSmoothingAlpha + SmoothedDecelRate * (1-decelSmoothingAlpha);
} // end SerialProcess()


#if defined(ESP_PLATFORM)

// -----------------------------------------------

unsigned int checkSerial()
{
    gdraw.setColorDepth(8);
    gdraw.createSprite(WIDTH, HEIGHT);
    gdraw.fillSprite (TFT_BLACK);
    gdraw.setFont(FSS12);
    gdraw.setTextDatum(MC_DATUM);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.drawString("Looking for Serial data",160,120);
    gdraw.drawString("Please wait...",160,190);
    gdraw.pushSprite (0, 0);
    gdraw.deleteSprite();
    String serialString;

    // TTL input (including v2 Onspeed with vern's power board)
    Serial2.begin(115200, SERIAL_8N1, PORTC_RX, PORTC_TX, false);
    serialString=readSerialbytes();
    Serial2.end();
    if (serialString.indexOf("#1")>=0)
        return 1;

    // rs232 input via power board (including v3 Onspeed)
    Serial2.begin(115200, SERIAL_8N1, PORTC_RX, PORTC_TX, true);
    serialString=readSerialbytes();
    Serial2.end();
    if (serialString.indexOf("#1")>=0)
        return 2;

    // simulator demo M5 with onspeed v3 on pin 9 TTL
    Serial2.begin(115200, SERIAL_8N1, SIMDEMO_RX, PORTC_TX, false);
    serialString=readSerialbytes();
    Serial2.end();
    if (serialString.indexOf("#1") >= 0)
        return 3;

    return 0;
}


// -----------------------------------------------

String readSerialbytes()
{
    String ResultString="";
    char inChar;
    unsigned long readSerialTimeout=millis();

    while (millis()-readSerialTimeout<5000 && ResultString.length()<200)
    {
        if (Serial2.available())
        {
            inChar=Serial2.read();
            ResultString+=inChar;
        }
    }
    Serial.println(ResultString);
    return ResultString;
}


// -----------------------------------------------

void serialSetup()
{
    Preferences preferences;
    preferences.begin("OnSpeed", false);
    selectedPort=preferences.getUInt("SerialPort", 0);
    unsigned long detectSerialStart=millis();

    while (selectedPort==0 && millis()-detectSerialStart<30000) // allow 30 seconds to detect serial port
    {
        // check serial port
        selectedPort=checkSerial();
        // save serial port preference
        if (selectedPort!=0)
            preferences.putUInt("SerialPort", selectedPort);
    }

    preferences.end();

    //start selected serial port as Serial2

    switch (selectedPort) {
        case 1: {
            // TTL input via power board (including v2 Onspeed)
            Serial2.begin(115200, SERIAL_8N1, PORTC_RX, PORTC_TX, false);
            Serial.printf("Port C: GPIO%d RX / GPIO%d TX, TTL\n", PORTC_RX, PORTC_TX);
            break;
            }
        case 2: {
            // rs232 input via power board (including v3 Onspeed)
            Serial2.begin(115200, SERIAL_8N1, PORTC_RX, PORTC_TX, true);
            Serial.printf("Port C: GPIO%d RX / GPIO%d TX, RS232\n", PORTC_RX, PORTC_TX);
            break;
            }
        case 3: {
            // simulator demo M5 with onspeed v3 on pin 9 TTL
            Serial2.begin(115200, SERIAL_8N1, SIMDEMO_RX, PORTC_TX, false);
            Serial.printf("Sim demo: GPIO%d RX / GPIO%d TX, TTL\n", SIMDEMO_RX, PORTC_TX);
            break;
            }
        case 0: {
            gdraw.setColorDepth(8);
            gdraw.createSprite(WIDTH, HEIGHT);
            gdraw.fillSprite (TFT_BLACK);
            gdraw.setFont(FSS12);
            gdraw.setTextDatum(MC_DATUM);
            gdraw.setTextColor (TFT_RED);
            gdraw.drawString("No Serial Stream Detected",160,120);
            gdraw.setTextColor (TFT_WHITE);
            gdraw.drawString("Is OnSpeed running?",160,160);
            gdraw.pushSprite (0, 0);
            gdraw.deleteSprite();
            delay(3000);
            break;
            }
    } // end switch on selected port
} // end serialSetup()

#endif // ESP_PLATFORM
