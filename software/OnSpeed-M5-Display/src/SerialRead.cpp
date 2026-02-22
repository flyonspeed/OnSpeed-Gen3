// Ported from OnSpeed Gen2 OnSpeed_M5_display/SerialRead.ino
// Parses the OnSpeed #1 serial protocol and preprocesses data for display.

#include <Arduino.h>
#include <M5Stack.h>
#include <Free_Fonts.h>
#include <Preferences.h>
#include <SavGolDerivative.h>
#include "SerialRead.h"

// Smoothing constants
static const float aoaSmoothingAlpha   = 0.7f;  //1 = max smoothing, 0.01 no smoothing.
static const float slipSmoothingAlpha  = 0.5f;  //1 = max smoothing, 0.01 no smoothing.
static const float decelSmoothingAlpha = 0.04f;  //1 = max smoothing, 0.01 no smoothing.
static const float serialRate          = 0.1f;  // 10 Hz

// IAS derivative filter (Savitzky-Golay first derivative, window=15)
static double iasDerivativeInput;
static onspeed::SavGolDerivative iasDerivative(&iasDerivativeInput, 15);

extern TFT_eSprite gdraw;
static const uint16_t WIDTH  = 320;
static const uint16_t HEIGHT = 240;

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

                // parse Onspeed data stream
                String parseString;

                //calculate CRC
                int calcCRC = 0;
                for (int i = 0;i <= 75; i++)
                    calcCRC+=serialBufferString[i];
                calcCRC = calcCRC & 0xFF;

                // convert from hex back into integer for comparison
                if (calcCRC == (int)strtol(&serialBufferString.substring(76, 78)[0],NULL,16))
                {
                    // CRC passed
                    parseString=serialBufferString.substring(2, 6);
                    Pitch=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(6, 11);
                    Roll=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(11, 15);
                    IAS=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(15, 21);
                    Palt=parseString.toFloat();

                    parseString=serialBufferString.substring(21, 26);
                    TurnRate=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(26, 29);
                    LateralG=parseString.toFloat()/100;

                    parseString=serialBufferString.substring(29, 32);
                    VerticalG=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(32, 34);
                    PercentLift=parseString.toInt();

                    parseString=serialBufferString.substring(34, 38);
                    AOA=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(38, 42);
                    iVSI=parseString.toFloat()*10;

                    parseString=serialBufferString.substring(42, 45);
                    OAT=parseString.toInt();

                    parseString=serialBufferString.substring(45, 49);
                    FlightPath=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(49, 52);
                    FlapPos=parseString.toInt();

                    parseString=serialBufferString.substring(52, 56);
                    OnSpeedStallWarnAOA=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(56, 60);
                    OnSpeedSlowAOA=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(60, 64);
                    OnSpeedFastAOA=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(64, 68);
                    OnSpeedTonesOnAOA=parseString.toFloat()/10;

                    parseString=serialBufferString.substring(68, 72);
                    gOnsetRate=parseString.toFloat()/100;

                    parseString=serialBufferString.substring(72, 74);
                    SpinRecoveryCue=parseString.toInt();

                    parseString=serialBufferString.substring(74, 76);
                    DataMark=parseString.toInt();

                    serialBufferString="";

                    SerialProcess();

                    #ifdef SERIALDATADEBUG
                    Serial.printf("ONSPEED data: Millis %i, IAS %.2f, Pitch %.1f, Roll %.1f, LateralG %.2f, VerticalG %.2f, Palt %0.1f, iVSI %.1f, AOA: %.1f", millis()-serialMillis, IAS, Pitch, Roll, LateralG, VerticalG, Palt, iVSI,SmoothedAOA);
                    Serial.println();
                    #endif

                    serialMillis=millis();
                } // end if CRC passed
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

        SerialProcess();

        serialMillis = currMillis;
    }
#endif

} // end SerialRead()


// -----------------------------------------------

// Preprocess some of the serial data

void SerialProcess()
{
    // don't display invalid values;
    if (AOA == -100)
        AOA = 0.0;

    // smooth the noisier inputs
    SmoothedLateralG   = SmoothedLateralG * slipSmoothingAlpha+(1-slipSmoothingAlpha)*LateralG;
    Slip               = int(SmoothedLateralG * 34 / 0.04);
    Slip               = constrain(Slip,-99,99);
    SmoothedAOA        = SmoothedAOA * aoaSmoothingAlpha + (1-aoaSmoothingAlpha) * AOA;

    // compute IAS derivative (deceleration)
    // SavGolDerivative has correct sign: positive for increasing IAS.
    // DecelRate should be negative when decelerating (IAS decreasing), which
    // is what we get directly — no sign inversion needed.
    iasDerivativeInput =  IAS;
    DecelRate          =  iasDerivative.Compute();
    DecelRate          =  DecelRate/serialRate;
    SmoothedDecelRate  =  DecelRate * decelSmoothingAlpha + SmoothedDecelRate * (1-decelSmoothingAlpha);
} // end SerialProcess()


// -----------------------------------------------

unsigned int checkSerial()
{
    gdraw.setColorDepth(8);
    gdraw.createSprite(WIDTH, HEIGHT);
    gdraw.fillSprite (TFT_BLACK);
    gdraw.setFreeFont(FSS12);
    gdraw.setTextDatum(MC_DATUM);
    gdraw.setTextColor (TFT_WHITE);
    gdraw.drawString("Looking for Serial data",160,120);
    gdraw.drawString("Please wait...",160,190);
    gdraw.pushSprite (0, 0);
    gdraw.deleteSprite();
    String serialString;

    // TTL input (including v2 Onspeed with vern's power board)
    Serial2.begin (115200, SERIAL_8N1, 16, 17,false); //GPIO16 is RX, GPIO17 is TX  (Vern's power board)
    serialString=readSerialbytes();
    Serial2.end();
    if (serialString.indexOf("#1")>=0)
        return 1;

    // rs232 input via power board (including v3 Onspeed)
    Serial2.begin (115200, SERIAL_8N1, 16, 17,true); //GPIO16 is RX, GPIO17 is TX  (Vern's power board)
    serialString=readSerialbytes();
    Serial2.end();
    if (serialString.indexOf("#1")>=0)
        return 2;

    // simulator demo M5 with onspeed v3 on pin 9 TTL
    Serial2.begin (115200, SERIAL_8N1, 22, 17,false); //GPIO22 is RX, GPIO17 is TX  (straight M5)
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
            Serial2.begin (115200, SERIAL_8N1, 16, 17,false); //GPIO16 is RX, GPIO17 is TX  (Vern's power board)
            Serial.println("GPIO16 is RX, GPIO17 is TX, TTL");
            break;
            }
        case 2: {
            // rs232 input via power board (including v3 Onspeed)
            Serial2.begin (115200, SERIAL_8N1, 16, 17,true); //GPIO16 is RX, GPIO17 is TX  (Vern's power board)
            Serial.println("GPIO16 is RX, GPIO17 is TX, RS232");
            break;
            }
        case 3: {
            // simulator demo M5 with onspeed v3 on pin 9 TTL
            Serial2.begin (115200, SERIAL_8N1, 22, 17,false); //GPIO22 is RX, GPIO17 is TX  (straight M5)
            Serial.println("GPIO22 is RX, GPIO17 is TX, TTL");
            break;
            }
        case 0: {
            gdraw.setColorDepth(8);
            gdraw.createSprite(WIDTH, HEIGHT);
            gdraw.fillSprite (TFT_BLACK);
            gdraw.setFreeFont(FSS12);
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
