// Parses the OnSpeed #1 serial protocol and preprocesses data for display.

#if defined(ESP_PLATFORM)
#include <Arduino.h>
#include <Preferences.h>
#else
#include "../sim/ArduinoShim.h"
#endif

#if defined(HUVVER)
#include "../sim/HuvverShim.h"
#else
#include <M5Unified.h>
#endif
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
// Byte-stream framing — delegates to the natively-testable
// DisplayFrameAccumulator in onspeed_core. Exposed as a public entry
// point so the same parser can be fed from the hardware UART today and
// from a non-UART byte source (e.g. a future CSV replay harness on the
// desktop/WASM sim) without branching the render path.
//
// All state is in the accumulator. The wire-format size is the only
// thing this function depends on, and that's pinned by
// kDisplayFrameSizeBytes — adding fields to the wire only requires
// updating proto/DisplaySerial.{h,cpp}; this function is unchanged.
// -----------------------------------------------

static onspeed::proto::DisplayFrameAccumulator g_frameAccum;

void InjectSerialByte(char inChar)
{
    auto result = g_frameAccum.Inject(static_cast<uint8_t>(inChar));
    if (!result.has_value())
        return;

    const DisplayFrame& f = result.value();

    Pitch                = f.pitchDeg;
    Roll                 = f.rollDeg;
    IAS                  = f.iasKt;
    Palt                 = f.paltFt;
    LateralG             = f.lateralG;
    VerticalG            = f.verticalG;
    PercentLift          = f.percentLift;
    iVSI                 = f.vsiFpm;
    OAT                  = f.oatC;
    FlightPath           = f.flightPathDeg;
    FlapPos              = f.flapsDeg;
    TonesOnPctLift       = f.tonesOnPctLift;
    OnSpeedFastPctLift   = f.onSpeedFastPctLift;
    OnSpeedSlowPctLift   = f.onSpeedSlowPctLift;
    StallWarnPctLift     = f.stallWarnPctLift;
    FlapsMinDeg          = f.flapsMinDeg;
    FlapsMaxDeg          = f.flapsMaxDeg;
    gOnsetRate           = f.gOnsetRate;
    SpinRecoveryCue      = f.spinRecoveryCue;
    DataMark             = f.dataMark;

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
    Serial.printf("ONSPEED data: Millis %i, IAS %.2f, Pitch %.1f, Roll %.1f, LateralG %.2f, VerticalG %.2f, Palt %0.1f, iVSI %.1f, PctLift: %d", millis()-serialMillis, IAS, Pitch, Roll, LateralG, VerticalG, Palt, iVSI, PercentLift);
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
        // Demo: cycle the active flap detent through clean / half /
        // full and emit per-flap percent anchors that show the L/Dmax
        // pip and band edges sliding per calibration.  All percent
        // values are computed with the honest single-linear formula
        // (AOA - alpha_0) / (alpha_stall - alpha_0) — exactly what
        // ComputePercentLift produces firmware-side.
        struct FlapCfg {
            int   degrees;
            float alpha0;
            float alphaStall;
            float ldmax;
            float fast;
            float slow;
            float warn;
        };
        static const FlapCfg kFlaps[] = {
            { 0,  -3.7211f, 10.309f,  3.24f, 3.98f, 5.26f, 8.24f },  // Clean
            { 16, -6.2231f,  9.5681f, 1.11f, 2.44f, 3.88f, 7.17f },  // Half
            { 33, -9.2107f, 11.5701f,-2.24f, 2.19f, 4.09f, 7.94f },  // Full
        };

        // 12 s cycle: dwell on each detent for 4 s.  Step through
        // clean → half → full → clean.
        const int detentIdx = (int)((currMillis / 4000) % 3);
        const FlapCfg& snap = kFlaps[detentIdx];

        // Honest percent for any body angle within the active flap's
        // calibrated lift envelope.  Clamped 0..99 to match the
        // firmware-side wire convention.
        auto pctOf = [&](float bodyAngleDeg) -> int {
            const float span = snap.alphaStall - snap.alpha0;
            if (span <= 0.0f) return 0;
            int p = (int)((bodyAngleDeg - snap.alpha0) / span * 100.0f);
            if (p < 0)  p = 0;
            if (p > 99) p = 99;
            return p;
        };

        // Sweep AOA up and down through the full envelope on a 6-s
        // cycle so the index bar visibly slides through the indexer.
        const float aoaPhase = (float)((currMillis % 6000) / 6000.0f); // 0..1
        const float aoaSweep = (aoaPhase <= 0.5f)
                                   ? (aoaPhase * 2.0f)             // 0→1
                                   : (2.0f - aoaPhase * 2.0f);     // 1→0
        const float demoAOA = snap.alpha0 + aoaSweep * (snap.alphaStall - snap.alpha0);

        Pitch                = 5.0;
        Roll                 = 0.0;
        IAS                  = 100.0;
        Palt                 = 2500.0;
        LateralG             = 0.0;
        VerticalG            = 1.0;
        iVSI                 = 0.0;
        OAT                  = 70;
        FlightPath           = 0.0;
        FlapPos              = snap.degrees;
        FlapsMinDeg          = kFlaps[0].degrees;
        FlapsMaxDeg          = kFlaps[2].degrees;

        // Per-flap band-edge percents — exactly what the firmware
        // emits over the wire.  Each varies per flap because the
        // body-angle setpoints vary per flap.
        TonesOnPctLift       = pctOf(snap.ldmax);
        OnSpeedFastPctLift   = pctOf(snap.fast);
        OnSpeedSlowPctLift   = pctOf(snap.slow);
        StallWarnPctLift     = pctOf(snap.warn);

        // Current AOA's percent — drives the index-bar position.
        PercentLift          = pctOf(demoAOA);

        // Drive the right-edge G-onset tape with a 4 s sinusoid, ±1.5
        // G/s, so we can eyeball the widget: positive onset → orange
        // tape grows upward from the zero line; negative onset → grows
        // downward.  Issue #324 tracks wiring the firmware-side
        // computation that would replace this demo signal in flight.
        const float onsetPhase = (currMillis % 4000) / 4000.0f;   // 0..1
        gOnsetRate           = 1.5f * sinf(onsetPhase * 2.0f * 3.14159265f);
        SpinRecoveryCue      = 0;
        DataMark             = 0;

        SerialProcess(0.1f);

        serialMillis = currMillis;
    }
#endif

} // end SerialRead()


// -----------------------------------------------

// Preprocess some of the serial data

void SerialProcess(float frameDtSec)
{
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
