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
    // Wire field is tenths of a percent (0..999) at v4.23. Two consumers:
    // PercentLift (integer 0..99) drives chevron color logic and slip
    // color comparisons; PercentLiftDeci (float 0.0..99.9) drives the
    // index-bar y position so it advances at sub-pixel temporal smoothness
    // off the 20 Hz wire.
    PercentLift          = f.percentLift / 10;
    PercentLiftDeci      = f.percentLift / 10.0f;
    iVSI                 = f.vsiFpm;
    OAT                  = f.oatC;
    FlightPath           = f.flightPathDeg;
    FlapPos              = f.flapsDeg;
    TonesOnPctLift       = f.tonesOnPctLift;
    OnSpeedFastPctLift   = f.onSpeedFastPctLift;
    OnSpeedSlowPctLift   = f.onSpeedSlowPctLift;
    StallWarnPctLift     = f.stallWarnPctLift;
    PipPctLift           = f.pipPctLift;
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
    // Late-binding USB-CDC detection.  If the user has the M5 tethered
    // to the X-Plane plugin and the host wasn't sending bytes during
    // checkSerial()'s probe window, switch to USB-CDC (selectedPort=4)
    // when we see the full "#1" frame-start signature.  ONE '#' byte
    // is not enough to switch — the byte 0x23 appears in plenty of
    // legitimate non-OnSpeed USB traffic (kernel debug output, package
    // version strings, dmesg dumps).  A docked laptop on a real
    // airplane could sneak any single byte into Serial; requiring the
    // full "#1" raises the false-positive threshold to OnSpeed-only.
    //
    // We also do NOT persist the USB-CDC choice to NVS — it's a sim-
    // only mode and must not survive a power cycle into a flight
    // context.  Each boot re-detects.
    if (selectedPort != 4) {
        // Drain up to 256 bytes per tick scanning for "#1".  Same
        // motivation as the main read loop below — one-byte-per-tick
        // can't keep up with a 1520 B/s host stream and the buffer
        // overflows.  Once we match "#1" we switch and break;
        // remaining queued bytes are picked up by the selectedPort==4
        // branch below on this same tick.
        static char s_lastSerialByte = 0;
        for (int i = 0; i < 256 && Serial.available(); ++i) {
            const int b = Serial.read();
            if (s_lastSerialByte == '#' && b == '1') {
                selectedPort = 4;
                // Re-inject both bytes of the matched frame start so
                // the accumulator picks up from the real beginning.
                InjectSerialByte('#');
                InjectSerialByte('1');
                break;
            }
            s_lastSerialByte = static_cast<char>(b);
        }
    }

    // Drain the entire RX buffer per call instead of reading just one
    // byte.  At 115200 8N1 the line rate is 11520 bytes/sec, and
    // OnSpeed sends 76 bytes × 20 Hz = 1520 bytes/sec.  loop() runs
    // somewhere in the 30-100 Hz range depending on render workload —
    // not fast enough to keep up at one byte per loop, so the kernel
    // / CDC RX buffer would fill and drop frames.  Caps drain at 256
    // bytes per tick (~3 frames) to avoid starving display updates if
    // a host floods us.
    if (selectedPort == 4) {
        for (int i = 0; i < 256 && Serial.available(); ++i)
            InjectSerialByte(Serial.read());
    } else {
        for (int i = 0; i < 256 && Serial2.available(); ++i)
            InjectSerialByte(Serial2.read());
    }
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
        // Demo: sweep PercentLift cleanly from 0 → 99 → 0 against a
        // fixed flap calibration (RV-10 full flaps) so every visual
        // state of the indexer can be inspected at known percents.
        // The band-edge percents are exactly what the firmware would
        // emit on the wire for this calibration; only PercentLift
        // (the index bar's position) animates.  Full flaps is chosen
        // to make the per-flap variation visible — L/Dmax lands at
        // ~33% (vs ~51% clean) and the bottom chevron's "fast but
        // safe" green band spans ~33–53% (vs only ~51–55% clean).
        constexpr float kAlpha0     = -9.2107f;
        constexpr float kAlphaStall = 11.5701f;
        constexpr float kLdmax      = -2.24f;
        constexpr float kFast       =  2.19f;
        constexpr float kSlow       =  4.09f;
        constexpr float kWarn       =  7.94f;

        auto pctOf = [&](float bodyAngleDeg) -> int {
            const float span = kAlphaStall - kAlpha0;
            int p = (int)((bodyAngleDeg - kAlpha0) / span * 100.0f);
            if (p < 0)  p = 0;
            if (p > 99) p = 99;
            return p;
        };

        // 30 s sweep: 15 s up (0 → 99), 15 s down (99 → 0).  About 7%
        // per second — slow enough to read each transition (chevron
        // colors, donut arcs, L/Dmax pip alignment) on the way through.
        const float phase = (float)((currMillis % 30000) / 15000.0f); // 0..2
        const float frac  = (phase <= 1.0f) ? phase : (2.0f - phase); // 0→1→0
        int demoPct = (int)(frac * 99.0f + 0.5f);
        if (demoPct < 0)  demoPct = 0;
        if (demoPct > 99) demoPct = 99;

        Pitch                = 5.0;
        Roll                 = 0.0;
        IAS                  = 100.0;
        Palt                 = 2500.0;
        LateralG             = 0.0;
        VerticalG            = 1.0;
        iVSI                 = 0.0;
        OAT                  = 70;
        FlightPath           = 0.0;
        FlapPos              = 33;
        FlapsMinDeg          = 0;
        FlapsMaxDeg          = 33;

        // Per-flap band-edge percents — what the firmware emits over
        // the wire for this fixed calibration.  Constant across the
        // sweep so the visual state at any given PercentLift is
        // readable.  PipPctLift collapses to TonesOnPctLift in the
        // dummy-data path because the synthetic feed has no lever-pot
        // motion to interpolate over.
        TonesOnPctLift       = pctOf(kLdmax);
        OnSpeedFastPctLift   = pctOf(kFast);
        OnSpeedSlowPctLift   = pctOf(kSlow);
        StallWarnPctLift     = pctOf(kWarn);
        PipPctLift           = TonesOnPctLift;

        // Drive only the index-bar position.
        PercentLift          = demoPct;

        gOnsetRate           = 0.0f;
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
    // Wire's LateralG is already EMA-smoothed by the main firmware (via
    // AccelLatFilter in onspeed_core/ahrs) and in body-frame sign at v4.23
    // (positive = airframe accelerating rightward, matching IMU + log +
    // WebSocket JSON). Negate locally for the ball-frame rendering
    // convention (positive Slip = ball drawn right of center). Same
    // pattern the LiveView's slipBall.js uses.  See LATERAL_G_CONVENTION.md.
    Slip               = int(-LateralG * 34 / 0.04);
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

    // USB-CDC (X-Plane plugin host).  Serial is always-on once the
    // USB host enumerates the M5; no Serial.begin needed for CDC.  We
    // probe it briefly before the hardware-UART variants so a sim-
    // tethered pilot doesn't sit through 15 s of UART probing.
    //
    // IMPORTANT: only do the 2s wait if there's already inbound USB
    // traffic.  A real airplane M5 has nothing on Serial (no laptop
    // plugged in), and we'd otherwise add 2 s of dead boot time
    // every flight.
    if (Serial.available()) {
        serialString = "";
        unsigned long t0 = millis();
        while (millis() - t0 < 2000 && serialString.length() < 200) {
            if (Serial.available()) serialString += (char)Serial.read();
        }
        if (serialString.indexOf("#1") >= 0)
            return 4;
    }

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
    // On boards where Serial is the native USB-CDC peripheral (HWCDC
    // — ESP32-S3 / ESP32-C3), make TX non-blocking so debug prints
    // never stall the loop when no host is draining the RX side.
    // On boards where Serial is a hardware UART going through an
    // external USB-bridge chip (CP2104 on M5Stack Core2, CH9102F on
    // some Core2 batches), this method doesn't exist on
    // HardwareSerial — and the hardware-UART path doesn't suffer the
    // same indefinite-block hazard since the bridge chip drains at
    // line rate.  Compile-gate on the same target macros HWCDC.h
    // uses internally.
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
    Serial.setTxTimeoutMs(0);
#endif

    Preferences preferences;
    preferences.begin("OnSpeed", false);
    selectedPort=preferences.getUInt("SerialPort", 0);
    unsigned long detectSerialStart=millis();

    while (selectedPort==0 && millis()-detectSerialStart<30000) // allow 30 seconds to detect serial port
    {
        // check serial port
        selectedPort=checkSerial();
        // save serial port preference — but ONLY for hardware-UART
        // selections (1, 2, 3).  selectedPort=4 (USB-CDC) is sim-only:
        // a real OnSpeed installation MUST re-detect on each boot
        // because the USB-CDC connection is transient (laptop plugged
        // in for log retrieval can leave a stale port=4 in NVS,
        // which would brick the in-airplane M5 on the next flight
        // by skipping Serial2 detection entirely).
        if (selectedPort!=0 && selectedPort!=4)
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
        case 4: {
            // USB-CDC: M5 is plugged into a host (typically the X-Plane
            // plugin) which writes display-serial bytes to the USB
            // device.  No Serial2.begin — SerialRead() reads from
            // Serial directly when selectedPort==4.  Serial TX timeout
            // is already 0 from the top of serialSetup().
            Serial.println("USB-CDC: reading display-serial frames from Serial");
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
