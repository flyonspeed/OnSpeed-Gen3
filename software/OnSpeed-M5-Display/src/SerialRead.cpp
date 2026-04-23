// Ported from OnSpeed Gen2 OnSpeed_M5_display/SerialRead.ino
// Parses the OnSpeed #1 serial protocol and preprocesses data for display.

#include <Arduino.h>
#include <M5Unified.h>
#include <Free_Fonts.h>
#include <Preferences.h>
#include <filters/SavGolDerivative.h>
#include <proto/DisplaySerial.h>
#include "SerialRead.h"

using onspeed::proto::ParseDisplayFrame;
using onspeed::proto::kDisplayFrameSizeBytes;
using onspeed::proto::DisplayFrame;

// Smoothing constants
static const float aoaSmoothingAlpha   = 0.7f;  //1 = max smoothing, 0.01 no smoothing.
static const float slipSmoothingAlpha  = 0.5f;  //1 = max smoothing, 0.01 no smoothing.
static const float decelSmoothingAlpha = 0.04f;  //1 = max smoothing, 0.01 no smoothing.

// IAS derivative filter (Savitzky-Golay first derivative, window=15)
static double iasDerivativeInput;
static onspeed::SavGolDerivative iasDerivative(&iasDerivativeInput, 15);

extern M5Canvas gdraw;
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

// -----------------------------------------------

void SerialRead()
{
#ifndef DUMMY_SERIAL_DATA
    char inChar;

    if (Serial2.available())
    {
        inChar=Serial2.read();
        if (inChar == '#')
        {
            // reset RX buffer
            serialBufferString = inChar;
            return;
        }

        if  (serialBufferString.length() > 80)
        {
            // prevent buffer overflow;
            serialBufferString = "";
            Serial.println("Serial data buffer overflow");
            Serial.println(serialBufferString);
            return;
        }

        if (serialBufferString.length() > 0)
        {
            serialBufferString += inChar;

            if (serialBufferString.length() == 80 &&
                serialBufferString[0]       =='#' &&
                serialBufferString[1]       =='1' &&
                inChar                      == char(0x0A)) // ONSPEED protocol
            {
                #ifdef SERIALDATADEBUG
                Serial.println(serialBufferString);
                #endif

                // Parse the frame via the shared core module.
                // ParseDisplayFrame verifies the checksum and extracts all fields.
                auto result = ParseDisplayFrame(
                    reinterpret_cast<const uint8_t*>(serialBufferString.c_str()),
                    kDisplayFrameSizeBytes);

                if (result.has_value())
                {
                    const DisplayFrame& f = result.value();

                    Pitch               = f.pitchDeg;
                    Roll                = f.rollDeg;
                    IAS                 = f.iasKt;
                    Palt                = f.paltFt;
                    TurnRate            = f.turnRateDps;
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

                    serialBufferString="";

                    // Measure actual frame period rather than assuming a fixed rate.
                    // The main firmware's display serial task runs at
                    // kDisplaySerialPeriodMs (currently 20 Hz), but hardcoding
                    // that here is a fragility we don't want — a prior version
                    // assumed 10 Hz and the Savitzky-Golay IAS derivative was
                    // divided by the wrong denominator, so DecelRate read half
                    // its true value. This mirrors how the main firmware's
                    // AHRS (Madgwick, EKF6) also uses measured dt each tick.
                    static uint32_t lastFrameMicros = 0;
                    uint32_t nowMicros = micros();
                    float frameDtSec = (lastFrameMicros == 0)
                                           ? 0.05f
                                           : (nowMicros - lastFrameMicros) * 1e-6f;
                    lastFrameMicros = nowMicros;

                    // Soft warning if frame cadence drifts outside expected
                    // band — real firmware targets 50 ms; bench replay tools
                    // may be slightly off.
                    if (frameDtSec < 0.020f || frameDtSec > 0.200f)
                        Serial.printf("WARN: unexpected frame dt=%.3fs\n", frameDtSec);

                    SerialProcess(frameDtSec);

                    #ifdef SERIALDATADEBUG
                    Serial.printf("ONSPEED data: Millis %i, IAS %.2f, Pitch %.1f, Roll %.1f, LateralG %.2f, VerticalG %.2f, Palt %0.1f, iVSI %.1f, AOA: %.1f", millis()-serialMillis, IAS, Pitch, Roll, LateralG, VerticalG, Palt, iVSI,SmoothedAOA);
                    Serial.println();
                    #endif

                    serialMillis=millis();
                } // end if parse succeeded
                else
                    Serial.println("ONSPEED CRC Failed");

            } // end if complete serial message is in the buffer
        } // end if not null string
    } // end if serial port chars are available
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
        TurnRate            = 0.0;
        LateralG            = 0.0;
        VerticalG           = 0.0;
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

        if (AOA < 25.0) AOA += 0.2;
        else            AOA  = 0.0;

        if (AOA < 20.0) PercentLift = AOA * 5.0;
        else            PercentLift = 100.0;

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

    // smooth the noisier inputs
    SmoothedLateralG   = SmoothedLateralG * slipSmoothingAlpha+(1-slipSmoothingAlpha)*LateralG;
    Slip               = int(SmoothedLateralG * 34 / 0.04);
    Slip               = constrain(Slip,-99,99);
    SmoothedAOA        = SmoothedAOA * aoaSmoothingAlpha + (1-aoaSmoothingAlpha) * AOA;

    // Compute IAS derivative (deceleration) in knots/sec.
    // SavGolDerivative returns d(IAS)/d(sample); dividing by the measured
    // frame period converts per-sample to per-second. Sign is already
    // correct (positive for increasing IAS, negative for deceleration).
    iasDerivativeInput =  IAS;
    DecelRate          =  iasDerivative.Compute() / frameDtSec;
    SmoothedDecelRate  =  DecelRate * decelSmoothingAlpha + SmoothedDecelRate * (1-decelSmoothingAlpha);
} // end SerialProcess()


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
