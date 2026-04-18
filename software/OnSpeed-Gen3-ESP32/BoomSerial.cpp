
#include "Globals.h"
#include "BoomSerial.h"
#include <BoomConvert.h>

#define BOOM_PACKET_SIZE         50

// ----------------------------------------------------------------------------

BoomSerialIO::BoomSerialIO()
{
    BufferIndex     = 0;
    uTimestamp      = millis();

    MaxAvailable = 0;
    Static       = 0.0f;
    Dynamic      = 0.0f;
    Alpha        = 0.0f;
    Beta         = 0.0f;
    IAS          = 0.0f;
}


// ----------------------------------------------------------------------------

void BoomSerialIO::Init(Stream * pBoomSerial)
{
    pSerial = pBoomSerial;
}

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

                        bool bParseData = true;

                        if (g_Config.bBoomChecksum)
                            {
                            // CRC checking
                            int calcCRC = 0;
                            for (int i = 0; i < BufferIndex - 4; i++)
                                calcCRC += Buffer[i];
                            calcCRC = calcCRC & 0xFF;

                            char hexCRC[3];
                            strncpy(hexCRC, &Buffer[BufferIndex - 3], 2);
                            hexCRC[2] = '\0';
                            int expectedCRC = (int)strtol(hexCRC, NULL, 16);

                            if (calcCRC != expectedCRC)
                                {
                                g_Log.printf(MsgLog::EnBoom, MsgLog::EnError, "Bad CRC  Expectd 0x%02X Calc 0x%02X\n",
                                    expectedCRC, calcCRC);
                                bParseData = false;
                                }
                            }

                        if (bParseData)
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
                            if (g_Config.bBoomConvertData)
                            {
                                Static  = onspeed::BoomStaticConvert(parseArrayInt[0]);
                                Dynamic = onspeed::BoomDynamicConvert(parseArrayInt[1]);
                                Alpha   = onspeed::BoomAlphaConvert(parseArrayInt[2]);
                                Beta    = onspeed::BoomBetaConvert(parseArrayInt[3]);
                            }
                            else
                            {
                                Static  = (float)parseArrayInt[0];
                                Dynamic = (float)parseArrayInt[1];
                                Alpha   = (float)parseArrayInt[2];
                                Beta    = (float)parseArrayInt[3];
                            }
                            IAS     = 0;

                            g_Log.printf(MsgLog::EnBoom, MsgLog::EnDebug, "BOOM: Static %.2f, Dynamic %.2f, Alpha %.2f, Beta %.2f, IAS %.2f\n", Static, Dynamic, Alpha, Beta, IAS);
                        } // end parse data
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
