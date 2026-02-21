
#include "Globals.h"
#include "Helpers.h"
#include "EfisSerial.h"

#define EFIS_PACKET_SIZE        512
#define DYNON_SERIAL_LEN         53     // 53 with live data, 52 with logged data

// Various message data structures
// -------------------------------

#pragma pack(push, 1)
struct MGL_Msg1
{
    int32_t   PAltitude;        // longint; Pressure altitude in feet
    int32_t   BAltitude;        // longint; Pressure altitude in feet, baro corrected
    uint16_t  IAS;              // word; Indicated airspeed in 10th Km/h
    uint16_t  TAS;              // word; True airspeed in 10th Km/h
    int16_t   AOA;              // smallint; Angle of attack in tenth of a degree
    int16_t   VSI;              // smallint; Vertical speed in feet per minute
    uint16_t  Baro;             // word; Barometric pressure in 10th millibars (actual measurement from altimeter sensor, actual pressure)
    uint16_t  Local;            // word; Local pressure setting in 10th millibars (QNH)
    int16_t   OAT;              // smallint; Outside air temperature in degrees C
    uint8_t   Humidity;         // byte; 0-99%. If not available 0xFF
    uint8_t   SystemFlags;      // Byte; See description below
    uint8_t   Hour;             // bytes; Time as set in RTC. 24 hour format, two digit year.
    uint8_t   Minute;
    uint8_t   Second;
    uint8_t   Date;
    uint8_t   Month;
    uint8_t   Year;
    uint8_t   FTHour;           // bytes; Flight time since take off. Hours, minutes.
    uint8_t   FTMin;
    int32_t   Checksum;         // longint; CRC32
}; // end Msg1

struct MGL_Msg3
{
    uint16_t  HeadingMag;       // word; Magnetic heading from compass. 10th of a degree
    int16_t   PitchAngle;       // smallint; AHRS pitch angle 10th of a degree
    int16_t   BankAngle;        // smallint; AHRS bank angle 10th of a degree
    int16_t   YawAngle;         // smallint; AHRS yaw angle 10th of a degree (see notes below)
    int16_t   TurnRate;         // smallint; Turn rate in 10th of a degree per second
    int16_t   Slip;             // smallint; Slip (ball position) -50 (left) to +50 (right)
    int16_t   GForce;           // smallint; Acceleration acting on aircraft in Z axis (+ is down)
    int16_t   LRForce;          // smallint; Acceleration acting on aircraft in left/right axis (+ if right)
    int16_t   FRForce;          // smallint; Acceleration acting on aircraft in forward/rear axis (+ is forward)
    int16_t   BankRate;         // smallint; Rate of bank angle change (See notes on units)
    int16_t   PitchRate;        // smallint; Rate of pitch angle change
    int16_t   YawRate;          // smallint; Rate of yaw angle change
    uint8_t   SensorFlags;      // byte; See description below
    uint8_t   PaddingByte1;     // byte; 0x00 For alignment
    uint8_t   PaddingByte2;     // byte; 0x00 For alignment
    uint8_t   PaddingByte3;     // byte; 0x00 For alignment
    int32_t   Checksum;         // longint; CRC32
}; // end Msg3

struct MGL
{
    // Common to all messages
    uint8_t   DLE;              // byte; 0x05
    uint8_t   STX;              // byte; 0x02
    uint8_t   MessageLength;    // byte; 0x18 36 bytes following MessageVersion - 12
    uint8_t   MessageLengthXOR; // byte; 0xE7
    uint8_t   MessageType;      // byte; 0x01
    uint8_t   MessageRate;      // byte; 0x05
    uint8_t   MessageCount;     // byte; Message Count within current second
    uint8_t   MessageVersion;   // byte; 0x01

    union
    {
        MGL_Msg1    Msg1;
        MGL_Msg3    Msg3;
    }; // end union of message types
}; // end MGL message struct
#pragma pack(pop)

// Helpers
// -------

float array2float(byte buffer[], int startIndex)
{
    float out;
    memcpy(&out, &buffer[startIndex], sizeof(float));
    return out;
}

long double array2double(byte buffer[], int startIndex)
{
    long double out;
    memcpy(&out, &buffer[startIndex], sizeof(double));
    return out;
}

// Parse len chars starting at pos as float. Returns fallback if field matches sentinel.
static float parseFieldFloat(const char* buf, int pos, int len,
                             const char* sentinel, float fallback, float scale)
{
    char tmp[16];
    memcpy(tmp, buf + pos, len);
    tmp[len] = '\0';
    if (sentinel && memcmp(tmp, sentinel, len) == 0)
        return fallback;
    return strtof(tmp, nullptr) / scale;
}

static int parseFieldInt(const char* buf, int pos, int len,
                         const char* sentinel, int fallback, int scale)
{
    char tmp[16];
    memcpy(tmp, buf + pos, len);
    tmp[len] = '\0';
    if (sentinel && memcmp(tmp, sentinel, len) == 0)
        return fallback;
    return (int)(strtol(tmp, nullptr, 10)) * scale;
}

// Variants that only update *dest when the field is NOT the sentinel (keep previous value).
static void parseFieldFloatKeep(const char* buf, int pos, int len,
                                const char* sentinel, float scale, float* dest)
{
    char tmp[16];
    memcpy(tmp, buf + pos, len);
    tmp[len] = '\0';
    if (sentinel && memcmp(tmp, sentinel, len) == 0)
        return;
    *dest = strtof(tmp, nullptr) / scale;
}

static void parseFieldIntKeep(const char* buf, int pos, int len,
                              const char* sentinel, int scale, int* dest)
{
    char tmp[16];
    memcpy(tmp, buf + pos, len);
    tmp[len] = '\0';
    if (sentinel && memcmp(tmp, sentinel, len) == 0)
        return;
    *dest = (int)(strtol(tmp, nullptr, 10)) * scale;
}

// Parse a 2-char hex CRC field at the given position
static int parseHexCRC(const char* buf, int pos)
{
    char szCRC[3] = { buf[pos], buf[pos + 1], '\0' };
    return (int)strtol(szCRC, nullptr, 16);
}

// ----------------------------------------------------------------------------

EfisSerialIO::EfisSerialIO()
{
    efisPacketInProgress = false;
    PrevInByte = 0;
    PrevInChar = 0;

    suEfis.DecelRate        = 0.00;
    suEfis.IAS              = 0.00;
    suEfis.Pitch            = 0.00;
    suEfis.Roll             = 0.00;
    suEfis.LateralG         = 0.00;
    suEfis.VerticalG        = 0.00;
    suEfis.PercentLift      = 0;
    suEfis.Palt             = 0;
    suEfis.VSI              = 0;
    suEfis.TAS              = 0;
    suEfis.OAT              = 0;
    suEfis.FuelRemaining    = 0.00;
    suEfis.FuelFlow         = 0.00;
    suEfis.MAP              = 0.00;
    suEfis.RPM              = 0;
    suEfis.PercentPower     = 0;
    suEfis.Heading          = -1;
    suEfis.szTime[0]        = '\0';

    BufferIndex        = 0;

    suVN300.AngularRateRoll = 0.00;
    suVN300.AngularRatePitch = 0.00;
    suVN300.AngularRateYaw  = 0.00;
    suVN300.VelNedNorth     = 0.00;
    suVN300.VelNedEast      = 0.00;
    suVN300.VelNedDown      = 0.00;
    suVN300.AccelFwd        = 0.00;
    suVN300.AccelLat        = 0.00;
    suVN300.AccelVert       = 0.00;
    suVN300.Yaw             = 0.00;
    suVN300.Pitch           = 0.00;
    suVN300.Roll            = 0.00;
    suVN300.LinAccFwd       = 0.00;
    suVN300.LinAccLat       = 0.00;
    suVN300.LinAccVert      = 0.00;
    suVN300.YawSigma        = 0.00;
    suVN300.RollSigma       = 0.00;
    suVN300.PitchSigma      = 0.00;
    suVN300.GnssVelNedNorth = 0.00;
    suVN300.GnssVelNedEast  = 0.00;
    suVN300.GnssVelNedDown  = 0.00;
    suVN300.GPSFix          = 0;
    suVN300.GnssLat         = 0.00L;     // 8 byte double
    suVN300.GnssLon         = 0.00L;
    suVN300.szTimeUTC[0]    = '\0';

    BufferIndex             = 0;

    mglMsgLen               = 0;

    iBufferLen              = 0;

    uTimestamp = millis();
}

// ----------------------------------------------------------------------------

void EfisSerialIO::Init(EnEfisType enEfisType, HardwareSerial * pEfisSerial)
{
    // All text-based EFIS types (Dynon SkyView, Garmin, etc.) and the VN-300
    // use 8N1 framing.
    uint32_t hwSerialConfig = SerialConfig::SERIAL_8N1;

    // Save the EFIS type
    enType = enEfisType;

    // Set the EFIS serial port driver
    pSerial = pEfisSerial;

    // Close the port if it is open
    pSerial->end();

    // Config could / should be set based on EFIS type but for now they all are the same
    if (enType != EnNone)
    {
        pSerial->begin(115200, hwSerialConfig, EFIS_SER_RX, EFIS_SER_TX, false);
    }

    // Start in enabled mode
//    Enable(true);

}

// ----------------------------------------------------------------------------

#if 0
// Enable / disable reading of EFIS data

void EfisSerialIO::Enable(bool bEnable)
{
    // If being enabled then flush the input buffer
    if (!bEnabled && bEnable)
        pSerial->flush();

    bEnabled = bEnable;
}
#endif

// ----------------------------------------------------------------------------

void EfisSerialIO::Read()
{
    if (g_Config.bReadEfisData)
    {
        int packetCount=0;

        if (enType == EnVN300 ) // VN-300 type data, binary format. Was "1"
        {
            // parse VN-300 and VN-100 data
#ifdef EFISDATADEBUG
            MaxAvailable=MAX(pSerial->available(),MaxAvailable);
#endif
            while (pSerial->available() && packetCount<EFIS_PACKET_SIZE)
            {
                // receive one line of data and break
                byte    InByte = pSerial->read();
                lastReceivedEfisTime=millis();
                packetCount++;
//                charsreceived++;
                if (InByte == 0x19 && PrevInByte == 0xFA) // first two bytes must match for packet start
                {
                    efisPacketInProgress = true;
                    Buffer[0]   = 0xFA;
                    Buffer[1]   = 0x19;
                    BufferIndex = 2; // reset buffer index when sync byte is received
                    continue;
                }

                if (BufferIndex<127 && efisPacketInProgress)
                {
                    // add character to buffer
                    Buffer[BufferIndex]=InByte;
                    BufferIndex++;
                }

                if (BufferIndex==127 && efisPacketInProgress) // 103 without lat/lon
                {
                    // got full packet, check header
                    //byte packetHeader[8]={0xFA,0x19,0xA0,0x01,0x91,0x00,0x42,0x01}; without lat/lon
                    byte packetHeader[8]={0xFA,0x19,0xE0,0x01,0x91,0x00,0x42,0x01}; // with lat/long
                    // groups (0x19): 0001 1001: group 1 (General Purpose Group), group 4 (GPS1 Measurement Group.), group 5 (INS Group.)
                    //                          FA19E00191004201
                    //                          E0 01: 1110 0000 , 0000 0001  (6,7,8,9)
                    //                          91 00: 1001 0001 , 0000 0000  (1,5,8)
                    //                          42 01: 0100 0010 , 0000 0001  (2,7,9)
                    if (memcmp(Buffer,packetHeader,8)!=0)
                    {
                        // bad packet header, dump packet
                        efisPacketInProgress=false;
                        g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "Bad VN packet header");
                        continue;
                        //return;
                    }
                    // check CRC
                    uint16_t vnCrc = 0;

                    //for (int i = 1; i < 103; i++) // starting after the sync byte
                    for (int i = 1; i < 127; i++) // starting after the sync byte
                    {
                        vnCrc = (uint16_t) (vnCrc >> 8) | (vnCrc << 8);

                        vnCrc ^= (uint8_t) Buffer[i];
                        vnCrc ^= (uint16_t) (((uint8_t) (vnCrc & 0xFF)) >> 4);
                        vnCrc ^= (uint16_t) ((vnCrc << 8) << 4);
                        vnCrc ^= (uint16_t) (((vnCrc & 0xFF) << 4) << 1);
                    }

                    if (vnCrc!=0)
                    {
                        // bad CRC, dump packet
                        efisPacketInProgress=false;
                        g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "Bad VN packet CRC");
                        continue;
                        //return;
                    }

                    // process packet data
//                  for (int i=0;i<BufferIndex;i++)
//                      {
//                      Serial.print(Buffer[i],HEX);
//                      Serial.print(" ");
//                      }

                    // Common
                    suVN300.AngularRateRoll  = array2float(Buffer,8);
                    suVN300.AngularRatePitch = array2float(Buffer,12);
                    suVN300.AngularRateYaw   = array2float(Buffer,16);
                    suVN300.GnssLat          = array2double(Buffer,20);
                    suVN300.GnssLon          = array2double(Buffer,28);
                    //vnGnssAlt          = array2double(Buffer,36);

                    suVN300.VelNedNorth      = array2float(Buffer,44);
                    suVN300.VelNedEast       = array2float(Buffer,48);
                    suVN300.VelNedDown       = array2float(Buffer,52);

                    suVN300.AccelFwd         = array2float(Buffer,56);
                    suVN300.AccelLat         = array2float(Buffer,60);
                    suVN300.AccelVert        = array2float(Buffer,64);

                    // GNSS
                    //vnTimeUTC       = Buffer,68 (+8 bytes);
                    //int8_t vnYear   = int8_t(Buffer[68]);
                    //uint8_t vnMonth = uint8_t(Buffer[69]);
                    //uint8_t vnDay   = uint8_t(Buffer[70]);
                    uint8_t vnHour     = uint8_t(Buffer[71]);
                    uint8_t vnMin      = uint8_t(Buffer[72]);
                    uint8_t vnSec      = uint8_t(Buffer[73]);
                    //uint16_t vnFracSec = (Buffer[75] << 8) | Buffer[74]; // gps fractional seconds only update at GPS update rates, 5Hz. We'll calculate our own

                    // calculate fractional seconds 1/100
                    int iFrac = (int)(millis() / 10) % 100;
                    snprintf(suVN300.szTimeUTC, sizeof(suVN300.szTimeUTC),
                             "%u:%u:%u.%02d", vnHour, vnMin, vnSec, iFrac);

                    suVN300.GPSFix           = Buffer[76];
                    suVN300.GnssVelNedNorth  = array2float(Buffer,77);
                    suVN300.GnssVelNedEast   = array2float(Buffer,81);
                    suVN300.GnssVelNedDown   = array2float(Buffer,85);

                    // Attitude
                    suVN300.Yaw              = array2float(Buffer,89);
                    suVN300.Pitch            = array2float(Buffer,93);
                    suVN300.Roll             = array2float(Buffer,97);

                    suVN300.LinAccFwd        = array2float(Buffer,101);
                    suVN300.LinAccLat        = array2float(Buffer,105);
                    suVN300.LinAccVert       = array2float(Buffer,109);

                    suVN300.YawSigma         = array2float(Buffer,113);
                    suVN300.RollSigma        = array2float(Buffer,117);
                    suVN300.PitchSigma       = array2float(Buffer,121);
                    uTimestamp         = millis();

                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                        {
                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "%lu", uTimestamp);
                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "\nvnAngularRateRoll: %.2f,vnAngularRatePitch: %.2f,vnAngularRateYaw: %.2f,vnVelNedNorth: %.2f,vnVelNedEast: %.2f,vnVelNedDown: %.2f,vnAccelFwd: %.2f,vnAccelLat: %.2f,vnAccelVert: %.2f,vnYaw: %.2f,vnPitch: %.2f,vnRoll: %.2f,vnLinAccFwd: %.2f,vnLinAccLat: %.2f,vnLinAccVert: %.2f,vnYawSigma: %.2f,vnRollSigma: %.2f,vnPitchSigma: %.2f,vnGnssVelNedNorth: %.2f,vnGnssVelNedEast: %.2f,vnGnssVelNedDown: %.2f,vnGnssLat: %.6f,vnGnssLon: %.6f,vnGPSFix: %i,TimeUTC: %s\n",
                            suVN300.AngularRateRoll, suVN300.AngularRatePitch, suVN300.AngularRateYaw,
                            suVN300.VelNedNorth, suVN300.VelNedEast, suVN300.VelNedDown,
                            suVN300.AccelFwd, suVN300.AccelLat, suVN300.AccelVert,
                            suVN300.Yaw, suVN300.Pitch, suVN300.Roll,
                            suVN300.LinAccFwd, suVN300.LinAccLat, suVN300.LinAccVert,
                            suVN300.YawSigma, suVN300.RollSigma, suVN300.PitchSigma,
                            suVN300.GnssVelNedNorth, suVN300.GnssVelNedEast, suVN300.GnssVelNedDown,
                            suVN300.GnssLat, suVN300.GnssLon, suVN300.GPSFix, suVN300.szTimeUTC);
                        }
                    efisPacketInProgress = false;
                }
                PrevInByte = InByte;
            } // while serial available
        } // end if VN-300 data

        // MGL data, binary format
        else if (enType == EnMglBinary) // Was 6
        {
            while (pSerial->available() && packetCount<100)
            {
                // receive one byte
                byte     InByte;
                MGL    * mglMsg = (MGL *)Buffer;

                // Data Read
                // ---------

                InByte = pSerial->read();
                lastReceivedEfisTime=millis();
                packetCount++;
//                charsreceived++;

                // Sync byte 1
                if (BufferIndex == 0)
                {
                    if (InByte == 0x05)
                    {
                        Buffer[0] = InByte;
                        BufferIndex++;
                    }
                }

                // Sync byte 2
                else if (BufferIndex == 1)
                {
                    if (InByte == 0x02)
                    {
                        Buffer[1] = InByte;
                        BufferIndex++;
                    }
                    else
                    {
                        BufferIndex = 0;
                    }
                }

                // Message length bytes
                else if ((BufferIndex == 2) || (BufferIndex == 3))
                {
                    Buffer[BufferIndex] = InByte;
                    BufferIndex++;
                    if (BufferIndex == 4)
                    {
                        // Check for corrupted length data
                        if ((mglMsg->MessageLength ^ mglMsg->MessageLengthXOR) != 0xFF)
                            BufferIndex = 0;
                        else
                        {
                            // Make a proper message length
                            mglMsgLen = mglMsg->MessageLength;
                            if (mglMsgLen == 0x00)
                                mglMsgLen = 256;
                            mglMsgLen += 20;
                        }
                    }
                } // end if length bytes

                // Read in the rest of the message up to the message length or up to
                // our max buffer size.
                else if ((BufferIndex > 3               ) &&
                         (BufferIndex < mglMsgLen       ))
                {
                    // Only write if buffer index is not greater than buffer size
                    if (BufferIndex < sizeof(Buffer))
                        Buffer[BufferIndex] = InByte;
                    BufferIndex++;
                }

                // Data Decode
                // -----------

                // If we have a full buffer of message data then decode it
                if ((BufferIndex >  3        ) &&
                    (BufferIndex >= mglMsgLen))
                {
                    switch (mglMsg->MessageType)
                    {
                        case 1 : // Primary flight data

                            if (BufferIndex != 44)
                            {
                                g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "MGL primary - BAD message length");
                                break;
                            }

                            suEfis.Palt         =       mglMsg->Msg1.PAltitude;
                            suEfis.IAS          =       mglMsg->Msg1.IAS * 0.05399565f;    // airspeed in 10th of Km/h.  * 0.05399565 to knots. * 0.6213712 to mph
                            suEfis.TAS          =       mglMsg->Msg1.TAS * 0.05399565f;    // convert to knots
                            suEfis.PercentLift  =       mglMsg->Msg1.AOA;                  // aoa
                            suEfis.VSI          =       mglMsg->Msg1.VSI;                  // vsi in FPM.
                            suEfis.OAT          = float(mglMsg->Msg1.OAT);                 // c

                            snprintf(suEfis.szTime, sizeof(suEfis.szTime), "%u:%u:%u",
                                     Buffer[32], Buffer[33], Buffer[34]);
                            uTimestamp = millis();

                            if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                {
                                g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "MGL primary  time:%i:%i:%i Palt: %i \tIAS: %.2f\tTAS: %.2f\tpLift: %i\tVSI:%i\tOAT:%.2f\n",
                                    mglMsg->Msg1.Hour, mglMsg->Msg1.Minute, mglMsg->Msg1.Second, suEfis.Palt, suEfis.IAS, suEfis.TAS, suEfis.PercentLift, suEfis.VSI, suEfis.OAT);
                                }
                            break;

                        case 3 : // Attitude flight data

                            if (BufferIndex != 40)
                            {
                                g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "MGL Attitude> BAD message length");
                                break;
                            }

                            suEfis.Heading   = int(mglMsg->Msg3.HeadingMag * 0.1);
                            suEfis.Pitch     =     mglMsg->Msg3.PitchAngle * 0.1f;
                            suEfis.Roll      =     mglMsg->Msg3.BankAngle  * 0.1f;
                            suEfis.VerticalG =     mglMsg->Msg3.GForce     * 0.01f;
                            suEfis.LateralG  =     mglMsg->Msg3.LRForce    * 0.01f;

                            uTimestamp = millis();

                            if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "MGL Attitude  Head: %i \tPitch: %.2f\tRoll: %.2f\tvG:%.2f\tlG:%.2f\n",
                                    suEfis.Heading, suEfis.Pitch, suEfis.Roll, suEfis.VerticalG, suEfis.LateralG);
                            break;

                        default :
                            break;
                    } // end switch on message type

                    // Get buffer ready for next message
                    BufferIndex = 0;

                    // Break out of the loop to give other processes a chance
                    break;
                } // end if full message
            } // end while serial bytes available
        } // end if MGL data

        // Default text EFIS case
        else
        {
            //read EFIS data, text format
            int efisCharCount=0;
            while ((pSerial->available() > 0) && (packetCount < EFIS_PACKET_SIZE))
            {
                efisCharCount++;
#ifdef EFISDATADEBUG
                MaxAvailable = MAX(pSerial->available(),MaxAvailable);
#endif
                char InChar      = pSerial->read();
                lastReceivedEfisTime=millis();
                packetCount++;
//                charsreceived++;
                if  (iBufferLen > 230)
                {
                    g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "Efis data buffer overflow");
                    iBufferLen = 0; // prevent buffer overflow;
                }

                // Data line terminates with 0D0A, when buffer is empty look for 0A in the incoming stream and dump everything else
                if ((iBufferLen > 0 || PrevInChar == char(0x0A)))
                {
                    szBuffer[iBufferLen++] = InChar;

                    if (InChar == char(0x0A))
                    {
                        // Null-terminate for strtol/strtof safety
                        szBuffer[iBufferLen] = '\0';

                        // end of line
                        if (enType == EnDynonSkyview) // Advanced, was "2"
                        {
#ifdef EFISDATADEBUG
                            if (iBufferLen!=74 && iBufferLen!=93 && iBufferLen!=225)
                                {
                                 g_Log.printf(MsgLog::EnEfis, MsgLog::EnWarning, "Invalid Efis data line length: ");
                                 g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "%d\n", iBufferLen);
                                }
#endif
                            if (iBufferLen==74 && szBuffer[0]=='!' && szBuffer[1]=='1')
                            {
                                // parse Skyview AHRS data
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=69;i++) calcCRC+=szBuffer[i];
                                calcCRC=calcCRC & 0xFF;

                                if (calcCRC==parseHexCRC(szBuffer, 70))
                                {
                                    suEfis.IAS         = parseFieldFloat(szBuffer, 23, 4, "XXXX",   -1.0f,   10.0f); // knots
                                    suEfis.Pitch       = parseFieldFloat(szBuffer, 11, 4, "XXXX",   -100.0f, 10.0f); // degrees
                                    suEfis.Roll        = parseFieldFloat(szBuffer, 15, 5, "XXXXX",  -180.0f, 10.0f); // degrees
                                    suEfis.Heading     = parseFieldInt  (szBuffer, 20, 3, "XXX",    -1,      1);
                                    suEfis.LateralG    = parseFieldFloat(szBuffer, 37, 3, "XXX",    -100.0f, 100.0f);
                                    suEfis.VerticalG   = parseFieldFloat(szBuffer, 40, 3, "XXX",    -100.0f, 10.0f);
                                    suEfis.PercentLift = parseFieldInt  (szBuffer, 43, 2, "XX",     -1,      1);     // 00 to 99, percentage of stall angle.
                                    suEfis.Palt        = parseFieldInt  (szBuffer, 27, 6, "XXXXXX", -10000,  1);     // feet
                                    suEfis.VSI         = parseFieldInt  (szBuffer, 45, 4, "XXXX",   -10000,  10);    // feet/min
                                    suEfis.TAS         = parseFieldFloat(szBuffer, 52, 4, "XXXX",   -1.0f,   10.0f); // kts
                                    suEfis.OAT         = parseFieldFloat(szBuffer, 49, 3, "XXX",    -100.0f, 1.0f);  // Celsius
                                    snprintf(suEfis.szTime, sizeof(suEfis.szTime),
                                             "%.2s:%.2s:%.2s.%.2s",
                                             szBuffer+3, szBuffer+5, szBuffer+7, szBuffer+9);
                                    uTimestamp = millis();
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "SKYVIEW ADAHRS: IAS %.2f, Pitch %.2f, Roll %.2f, LateralG %.2f, VerticalG %.2f, PercentLift %i, Palt %i, VSI %i, TAS %.2f, OAT %.2f, Heading %i ,Time %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll,
                                            suEfis.LateralG, suEfis.VerticalG, suEfis.PercentLift,
                                            suEfis.Palt, suEfis.VSI, suEfis.TAS, suEfis.OAT, suEfis.Heading, suEfis.szTime);
                                } // end if CRC OK

                                else
                                    g_Log.print(MsgLog::EnEfis, MsgLog::EnWarning, "SKYVIEW ADAHRS CRC Failed");

                            } // end if Msg Type 1

                            else if (iBufferLen==225 && szBuffer[0]=='!' && szBuffer[1]=='3')
                            {
                                // parse Skyview EMS data
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=220;i++) calcCRC+=szBuffer[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==parseHexCRC(szBuffer, 221))
                                {
                                    suEfis.FuelRemaining = parseFieldFloat(szBuffer, 44, 3, "XXX",  -1.0f, 10.0f); // gallons
                                    suEfis.FuelFlow      = parseFieldFloat(szBuffer, 29, 3, "XXX",  -1.0f, 10.0f); // gph
                                    suEfis.MAP           = parseFieldFloat(szBuffer, 26, 3, "XXX",  -1.0f, 10.0f); //inHg
                                    suEfis.RPM           = parseFieldInt  (szBuffer, 18, 4, "XXXX", -1,    1);
                                    suEfis.PercentPower  = parseFieldInt  (szBuffer,217, 3, "XXX",  -1,    1);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "SKYVIEW EMS: FuelRemaining %.2f, FuelFlow %.2f, MAP %.2f, RPM %i, PercentPower %i\n",
                                            suEfis.FuelRemaining, suEfis.FuelFlow, suEfis.MAP, suEfis.RPM, suEfis.PercentPower);
                                }
                                else
                                    g_Log.print(MsgLog::EnEfis, MsgLog::EnWarning, "SKYVIEW EMS CRC Failed");

                            } // end if Msg Type 3
                        } // end efisType ADVANCED

                        else if (enType == EnDynonD10) // Dynon D10, was 3
                        {
                            if (iBufferLen == DYNON_SERIAL_LEN)
                            {
                                // parse Dynon data
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=48;i++) calcCRC+=szBuffer[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==parseHexCRC(szBuffer, 49))
                                {
                                    // CRC passed
                                    suEfis.IAS         = parseFieldFloat(szBuffer, 20, 4, nullptr, 0.0f, 10.0f) * 1.94384f; // m/s to knots
                                    suEfis.Pitch       = parseFieldFloat(szBuffer,  8, 4, nullptr, 0.0f, 10.0f);
                                    suEfis.Roll        = parseFieldFloat(szBuffer, 12, 5, nullptr, 0.0f, 10.0f);
                                    suEfis.LateralG    = parseFieldFloat(szBuffer, 33, 3, nullptr, 0.0f, 100.0f);
                                    suEfis.VerticalG   = parseFieldFloat(szBuffer, 36, 3, nullptr, 0.0f, 10.0f);
                                    suEfis.PercentLift = parseFieldInt  (szBuffer, 39, 2, nullptr, 0,    1); // 00 to 99, percentage of stall angle
                                    // Status byte at position 46 (second char of 45..47 field)
                                    char szStatus[2] = { szBuffer[46], '\0' };
                                    long statusBitInt  = strtol(szStatus, nullptr, 16);
                                    if (bitRead(statusBitInt, 0))
                                    {
                                        // when bitmask bit 0 is 1, grab pressure altitude and VSI, otherwise use previous value (skip turn rate and density altitude)
                                        suEfis.Palt = (int)(parseFieldFloat(szBuffer, 24, 5, nullptr, 0.0f, 1.0f) * 3.28084f); // meters to feet
                                        suEfis.VSI  = (int)(parseFieldFloat(szBuffer, 29, 4, nullptr, 0.0f, 10.0f) * 60.0f);   // feet/sec to feet/min
                                    }
                                    uTimestamp = millis();
                                    snprintf(suEfis.szTime, sizeof(suEfis.szTime),
                                             "%.2s:%.2s:%.2s.%.2s",
                                             szBuffer+0, szBuffer+2, szBuffer+4, szBuffer+6);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "D10: IAS %.2f, Pitch %.2f, Roll %.2f, LateralG %.2f, VerticalG %.2f, PercentLift %i, Palt %i, VSI %i, Time %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll, suEfis.LateralG, suEfis.VerticalG, suEfis.PercentLift,suEfis.Palt,suEfis.VSI,suEfis.szTime);
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnDebug, "D10 CRC Failed");

                            } // end if length OK
                        } // end efisType DYNON D10

                        else if (enType == EnGarminG5) // G5, was 4
                        {
                            if (iBufferLen==59 && szBuffer[0]=='=' && szBuffer[1]=='1' && szBuffer[2]=='1')
                            {
                                // parse G5 data
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=54;i++) calcCRC+=szBuffer[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==parseHexCRC(szBuffer, 55))
                                {
                                    // CRC passed
                                    parseFieldFloatKeep(szBuffer, 23, 4, "____",   10.0f,  &suEfis.IAS);
                                    parseFieldFloatKeep(szBuffer, 11, 4, "____",   10.0f,  &suEfis.Pitch);
                                    parseFieldFloatKeep(szBuffer, 15, 5, "_____",  10.0f,  &suEfis.Roll);
                                    parseFieldIntKeep  (szBuffer, 20, 3, "___",    1,      &suEfis.Heading);
                                    parseFieldFloatKeep(szBuffer, 37, 3, "___",    100.0f, &suEfis.LateralG);
                                    parseFieldFloatKeep(szBuffer, 40, 3, "___",    10.0f,  &suEfis.VerticalG);
                                    parseFieldIntKeep  (szBuffer, 27, 6, "______", 1,      &suEfis.Palt);    // feet
                                    parseFieldIntKeep  (szBuffer, 45, 4, "____",   10,     &suEfis.VSI);     //10 fpm
                                    uTimestamp = millis();
                                    snprintf(suEfis.szTime, sizeof(suEfis.szTime),
                                             "%.2s:%.2s:%.2s.%.2s",
                                             szBuffer+3, szBuffer+5, szBuffer+7, szBuffer+9);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "G5 data: IAS %.2f, Pitch %.2f, Roll %.2f, Heading %i, LateralG %.2f, VerticalG %.2f, Palt %i, VSI %i, Time %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll, suEfis.Heading, suEfis.LateralG, suEfis.VerticalG,suEfis.Palt,suEfis.VSI,suEfis.szTime);
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "G5 CRC Failed");

                            }
                        } // efisType GARMIN G5

                        else if (enType == EnGarminG3X) // G3X, was 5
                        {
                            // parse G3X attitude data, 10hz
                            if (iBufferLen==59 && szBuffer[0]=='=' && szBuffer[1]=='1' && szBuffer[2]=='1')
                            {
                                // parse G3X data
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=54;i++) calcCRC+=szBuffer[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==parseHexCRC(szBuffer, 55))
                                {
                                    // CRC passed
                                    parseFieldFloatKeep(szBuffer, 23, 4, "____",   10.0f,  &suEfis.IAS);
                                    parseFieldFloatKeep(szBuffer, 11, 4, "____",   10.0f,  &suEfis.Pitch);
                                    parseFieldFloatKeep(szBuffer, 15, 5, "_____",  10.0f,  &suEfis.Roll);
                                    parseFieldIntKeep  (szBuffer, 20, 3, "___",    1,      &suEfis.Heading);
                                    parseFieldFloatKeep(szBuffer, 37, 3, "___",    100.0f, &suEfis.LateralG);
                                    parseFieldFloatKeep(szBuffer, 40, 3, "___",    10.0f,  &suEfis.VerticalG);
                                    parseFieldIntKeep  (szBuffer, 43, 2, "__",     1,      &suEfis.PercentLift);
                                    parseFieldIntKeep  (szBuffer, 27, 6, "______", 1,      &suEfis.Palt);    // feet
                                    parseFieldFloatKeep(szBuffer, 49, 3, "___",    1.0f,   &suEfis.OAT);     // celsius
                                    parseFieldIntKeep  (szBuffer, 45, 4, "____",   10,     &suEfis.VSI);     //10 fpm
                                    uTimestamp = millis();
                                    snprintf(suEfis.szTime, sizeof(suEfis.szTime),
                                             "%.2s:%.2s:%.2s.%.2s",
                                             szBuffer+3, szBuffer+5, szBuffer+7, szBuffer+9);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "G3X Attitude data: efisIAS %.2f, efisPitch %.2f, efisRoll %.2f, efisHeading %i, efisLateralG %.2f, efisVerticalG %.2f, efisPercentLift %i, efisPalt %i, efisVSI %i,efisTime %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll, suEfis.Heading, suEfis.LateralG, suEfis.VerticalG, suEfis.PercentLift, suEfis.Palt, suEfis.VSI, suEfis.szTime);
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnDebug, "G3X Attitude CRC Failed");
                            }

                            // parse G3X engine data, 5Hz
                            else if (iBufferLen==221 && szBuffer[0]=='=' && szBuffer[1]=='3' && szBuffer[2]=='1')
                            {
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=216;i++) calcCRC+=szBuffer[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==parseHexCRC(szBuffer, 217))
                                {
                                    parseFieldFloatKeep(szBuffer, 44, 3, "___",  10.0f, &suEfis.FuelRemaining);
                                    parseFieldFloatKeep(szBuffer, 29, 3, "___",  10.0f, &suEfis.FuelFlow);
                                    parseFieldFloatKeep(szBuffer, 26, 3, "___",  10.0f, &suEfis.MAP);
                                    parseFieldIntKeep  (szBuffer, 18, 4, "____", 1,     &suEfis.RPM);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "G3X EMS: efisFuelRemaining %.2f, efisFuelFlow %.2f, efisMAP %.2f, efisRPM %i\n",
                                            suEfis.FuelRemaining, suEfis.FuelFlow, suEfis.MAP, suEfis.RPM);
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "G3X EMS CRC Failed");
                            }

                        } // end efisType GARMIN G3X
                        iBufferLen = 0;  // reset buffer
                    } // end if 0x0A found
                } // 0x0A first
#ifdef EFISDATADEBUG
                else
                {
                    // show dropped characters
                    Serial.print("@");
                    Serial.print(InChar);
                }
#endif // efisdatadebug
                PrevInChar = InChar;
            } // end while reading text format Efis
        } // end default text type Efis

    } // end if Efis Data to read
} // end readEfisSerial()
