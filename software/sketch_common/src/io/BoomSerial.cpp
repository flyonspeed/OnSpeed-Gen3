// BoomSerial.cpp — UART adapter that drives onspeed::boom::Decode.
//
// Hot-path optimizations relative to the prior strtok/atoi/strtol path:
//   - Single-pass Decode() in onspeed_core (no strtok, no atoi, no
//     strtol — locale-independent and ~2-3× faster on Xtensa LX7).
//   - millis() hoisted out of the per-byte loop (only the active-link
//     timestamp updates per call).
//   - parseHex2() for the CRC instead of strncpy + strtol(_, 16).

#include "src/Globals.h"
#include <boom/BoomParser.h>
#include <sensors/BoomConvert.h>

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
    if (!g_Config.bReadBoom)
        return;

    int  packetCount   = 0;
    bool anyByteSeen   = false;

    while ((pSerial->available() > 0) && (packetCount < BOOM_PACKET_SIZE))
    {
        char InChar = pSerial->read();
#ifdef BOOMDATADEBUG
        Serial.print(InChar);
#endif
        packetCount++;
        anyByteSeen = true;

        // Prevent buffer overflow
        if (BufferIndex >= BOOM_BUFFER_SIZE - 1)
        {
            BufferIndex = 0;
        }

        if (BufferIndex > 0 || InChar == '$')
        {
            if (InChar == '\n')
            {
                // Strip trailing CR if present, then null-terminate.
                int decodeLen = BufferIndex;
                if (decodeLen > 0 && Buffer[decodeLen - 1] == '\r')
                    decodeLen--;
                Buffer[decodeLen] = '\0';

                if (decodeLen >= 21 && Buffer[0] == '$')
                {
                    onspeed::boom::BoomFrame f =
                        onspeed::boom::Decode(Buffer, decodeLen,
                                              g_Config.bBoomChecksum);

                    if (!f.valid && g_Config.bBoomChecksum)
                    {
                        g_Log.printf(MsgLog::EnBoom, MsgLog::EnError,
                                     "Boom decode rejected (bad CRC or malformed)\n");
                    }

                    if (f.valid)
                    {
                        uTimestamp = millis();
                        if (g_Config.bBoomConvertData)
                        {
                            Static  = onspeed::BoomStaticConvert (f.staticCounts);
                            Dynamic = onspeed::BoomDynamicConvert(f.dynamicCounts);
                            Alpha   = onspeed::BoomAlphaConvert  (f.alphaCounts);
                            Beta    = onspeed::BoomBetaConvert   (f.betaCounts);
                        }
                        else
                        {
                            Static  = static_cast<float>(f.staticCounts);
                            Dynamic = static_cast<float>(f.dynamicCounts);
                            Alpha   = static_cast<float>(f.alphaCounts);
                            Beta    = static_cast<float>(f.betaCounts);
                        }
                        IAS = 0;

                        g_Log.printf(MsgLog::EnBoom, MsgLog::EnDebug,
                                     "BOOM: Static %.2f, Dynamic %.2f, Alpha %.2f, Beta %.2f, IAS %.2f\n",
                                     Static, Dynamic, Alpha, Beta, IAS);
                    }
                }

                BufferIndex = 0;
            }
            else
            {
                Buffer[BufferIndex++] = InChar;
            }
        }
#ifdef BOOMDATADEBUG
        else
        {
            Serial.print(InChar);
        }
#endif
    }

    // Update the link-alive timestamp once per Read() rather than per
    // byte — saves ~50 millis() syscalls per second at 50 Hz boom rate.
    if (anyByteSeen)
        LastReceivedTime = millis();
}
