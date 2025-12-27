
//#include "SoftwareSerial.h"

#include "Globals.h"
#include "BoomSerial.h"

// Not sure what these are all about
// log raw counts for boom, no curves
#define BOOM_ALPHA_CALC(x)      x
#define BOOM_BETA_CALC(x)       x
#define BOOM_STATIC_CALC(x)     x
#define BOOM_DYNAMIC_CALC(x)    x

// boom curves
//#define BOOM_ALPHA_CALC(x)      7.0918*pow(10,-13)*x*x*x*x - 1.1698*pow(10,-8)*x*x*x + 7.0109*pow(10,-5)*x*x - 0.21624*x + 310.21; //degrees
//#define BOOM_BETA_CALC(x)       2.0096*pow(10,-13)*x*x*x*x - 3.7124*pow(10,-9)*x*x*x + 2.5497*pow(10,-5)*x*x - 3.7141*pow(10,-2)*x - 72.505; //degrees
//#define BOOM_STATIC_CALC(x)     0.00012207*(x - 1638)*1000; // millibars
//#define BOOM_DYNAMIC_CALC(x)    (0.01525902*(x - 1638)) - 100; // millibars

#define BOOM_PACKET_SIZE         50

// ----------------------------------------------------------------------------

BoomSerialIO::BoomSerialIO()
{
    BufferIndex     = 0;
    uTimestamp      = millis();

    MaxAvailable = 0;
    Static       = 0.0;
    Dynamic      = 0.0;
    Alpha        = 0.0;
    Beta         = 0.0;
    IAS          = 0.0;
}


// ----------------------------------------------------------------------------

void BoomSerialIO::Init(Stream * pBoomSerial)
{
//    uint32_t hwSerialConfig = SerialConfig::SERIAL_8E1;

    // Set the boom serial port driver
    pSerial = pBoomSerial;

    // Start in enabled mode
//    Enable(true);

}

// ----------------------------------------------------------------------------

#if 0
// Enable / disable reading of boom data

void BoomSerialIO::Enable(bool bEnable)
{
    // If being enabled then flush the input buffer
    if (!bEnabled && bEnable)
        pSerial->flush();

    bEnabled = bEnable;
}
#endif

// ----------------------------------------------------------------------------

void BoomSerialIO::Read()
{
    if (g_Config.bReadBoom)
    {
        int PacketCount = 0;

        while ((pSerial->available() > 0) && (PacketCount < BOOM_PACKET_SIZE))
        {
            char InChar = pSerial->read();
#ifdef BOOMDATADEBUG
            Serial.print(InChar);
#endif
            PacketCount++;
            LastReceivedTime = millis();

            // Prevent buffer overflow
            if (BufferIndex >= BOOM_BUFFER_SIZE - 1)
            {
                BufferIndex = 0;
            }

            if ((BufferIndex > 0 || InChar == '$'))
            {
                if (InChar == '\n')
                {
                    // Ensure the string is null-terminated
                    Buffer[BufferIndex] = '\0';

                    if (Buffer[0] == '$' && BufferIndex >= 21)
                    {
                        uTimestamp=millis();

#ifndef NOBOOMCHECKSUM
                        // CRC checking
                        int calcCRC = 0;
                        for (int i = 0; i < BufferIndex - 4; i++)
                        {
                            calcCRC += Buffer[i];
                        }
                        calcCRC = calcCRC & 0xFF;

                        // Extract CRC from buffer and convert from hex string to int
                        char hexCRC[3];
                        strncpy(hexCRC, &Buffer[BufferIndex - 3], 2);
                        hexCRC[2] = '\0';  // null-terminate the string
                        int expectedCRC = (int)strtol(hexCRC, NULL, 16);

                        if (calcCRC != expectedCRC)
                            {
                            g_Log.printf(MsgLog::EnBoom, MsgLog::EnError, "Bad CRC  Expectd 0x%s Calc 0x%s\n",
                                String(expectedCRC,HEX), String(calcCRC,HEX));
                            } // end bad CRC

                        // CRC OK
                        else
#endif
                        {
                            // Split the string and convert to integers
                            int parseArrayInt[4] = {0, 0, 0, 0};
                            char *token = strtok(Buffer + 21, ",");
                            int parseArrayIndex = 0;
                            while (token != NULL && parseArrayIndex < 4)
                            {
                                parseArrayInt[parseArrayIndex] = atoi(token);
                                token = strtok(NULL, ",");
                                parseArrayIndex++;
                            }

                            // Continue processing as before
                            Static  = BOOM_STATIC_CALC(parseArrayInt[0]);
                            Dynamic = BOOM_DYNAMIC_CALC(parseArrayInt[1]);
                            IAS     = 0;
                            Alpha   = BOOM_ALPHA_CALC(parseArrayInt[2]);
                            Beta    = BOOM_BETA_CALC(parseArrayInt[3]);

                            g_Log.printf(MsgLog::EnBoom, MsgLog::EnDebug, "BOOM: Static %.2f, Dynamic %.2f, Alpha %.2f, Beta %.2f, IAS %.2f\n", Static, Dynamic, Alpha, Beta, IAS);
                        } // end CRC OK
                    } // end if full boom message in buffer

                    BufferIndex = 0;
                } // end if CR

                // No CR so store the character
                else
                {
                    Buffer[BufferIndex++] = InChar;
                }
            } // end if reading valid message

#ifdef BOOMDATADEBUG
            // Message hasn't started so this must be an error
            else
            {
                Serial.print(InChar);
            }
#endif
        } // end while characters are available for reading
    } // end if read boom
}// end Read()
