
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
    suEfis.Time             = "";
    suEfis.Time.reserve(16);

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
    suVN300.TimeUTC         = "";
    suVN300.TimeUTC.reserve(24);

    BufferIndex             = 0;

    mglMsgLen               = 0;

    BufferString.reserve(256);

    uTimestamp = millis();
}

// ----------------------------------------------------------------------------

void EfisSerialIO::Init(EnEfisType enEfisType, HardwareSerial * pEfisSerial)
{
    uint32_t hwSerialConfig = SerialConfig::SERIAL_8E1; // SERIAL_8N1

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
                    String vnFracSec = String(int(millis()/10));
                    vnFracSec = vnFracSec.substring(vnFracSec.length()-2);
                    suVN300.TimeUTC = String(vnHour)+":"+String(vnMin)+":"+String(vnSec)+"."+vnFracSec;

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
                            suVN300.GnssLat, suVN300.GnssLon, suVN300.GPSFix, suVN300.TimeUTC.c_str());
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

                            // sprintf(efisTime,"%i:%i:%i",byte(Buffer[32]),byte(Buffer[33]),byte(Buffer[34]));  // pull the time out of message.
                            suEfis.Time = String(Buffer[32])+":"+String(Buffer[33])+":"+String(Buffer[34]);  // get efis time in string.
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
                if  (BufferString.length()>230)
                {
                    g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "Efis data buffer overflow");
                    BufferString = ""; // prevent buffer overflow;
                }

                // Data line terminates with 0D0A, when buffer is empty look for 0A in the incoming stream and dump everything else
                if ((BufferString.length() > 0 || PrevInChar == char(0x0A)))
                {
                    BufferString += InChar;

                    if (InChar == char(0x0A))
                    {
                        // end of line
                        if (enType == EnDynonSkyview) // Advanced, was "2"
                        {
#ifdef EFISDATADEBUG
                            int lineLength=BufferString.length();
                            if (lineLength!=74 && lineLength!=93 && lineLength!=225)
                                {
                                 g_Log.printf(MsgLog::EnEfis, MsgLog::EnWarning, "Invalid Efis data line length: ");
                                 g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "%d\n", lineLength);
                                }
#endif
                            if (BufferString.length()==74 && BufferString[0]=='!' && BufferString[1]=='1')
                            {
                                // parse Skyview AHRS data
                                String parseString;
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=69;i++) calcCRC+=BufferString[i];
                                calcCRC=calcCRC & 0xFF;

                                if (calcCRC==(int)strtol(&BufferString.substring(70, 72)[0],NULL,16)) // convert from hex back into integer for camparison, issue with missing leading zeros when comparing hex formats
                                {
                                    //float efisIAS
                                    parseString = BufferString.substring(23, 27);
                                    if (parseString!="XXXX") suEfis.IAS = parseString.toFloat()/10;      else suEfis.IAS = -1; // knots
                                    //float efisPitch
                                    parseString = BufferString.substring(11, 15);
                                    if (parseString!="XXXX") suEfis.Pitch = parseString.toFloat()/10;    else suEfis.Pitch = -100; // degrees
                                    // float efisRoll
                                    parseString = BufferString.substring(15, 20);
                                    if (parseString!="XXXXX") suEfis.Roll = parseString.toFloat()/10;    else suEfis.Roll = -180; // degrees
                                    // float MagneticHeading
                                    parseString = BufferString.substring(20, 23);
                                    if (parseString!="XXX") suEfis.Heading = parseString.toInt();        else suEfis.Heading = -1;
                                    // float efisLateralG
                                    parseString = BufferString.substring(37, 40);
                                    if (parseString!="XXX") suEfis.LateralG = parseString.toFloat()/100; else suEfis.LateralG = -100;
                                    // float efisVerticalG
                                    parseString = BufferString.substring(40, 43);
                                    if (parseString!="XXX") suEfis.VerticalG = parseString.toFloat()/10; else suEfis.VerticalG = -100;
                                    // int efisPercentLift
                                    parseString = BufferString.substring(43, 45);
                                    if (parseString!="XX") suEfis.PercentLift = parseString.toInt();     else suEfis.PercentLift = -1; // 00 to 99, percentage of stall angle.
                                    // int efisPalt
                                    parseString = BufferString.substring(27, 33);
                                    if (parseString!="XXXXXX") suEfis.Palt = parseString.toInt();        else suEfis.Palt = -10000; // feet
                                    // int efisVSI
                                    parseString = BufferString.substring(45, 49);
                                    if (parseString!="XXXX") suEfis.VSI = parseString.toInt() * 10;      else suEfis.VSI = -10000; // feet/min
                                    //float efisTAS;
                                    parseString = BufferString.substring(52, 56);
                                    if (parseString!="XXXX") suEfis.TAS = parseString.toFloat()/10;      else suEfis.TAS = -1; // kts
                                    //float efisOAT;
                                    parseString = BufferString.substring(49, 52);
                                    if (parseString!="XXX") suEfis.OAT = parseString.toFloat();          else suEfis.OAT = -100; // Celsius
                                    // String efisTime
                                    suEfis.Time = BufferString.substring(3, 5)+":"+BufferString.substring(5, 7)+":"+BufferString.substring(7, 9)+"."+BufferString.substring(9, 11);
                                    uTimestamp = millis();
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "SKYVIEW ADAHRS: IAS %.2f, Pitch %.2f, Roll %.2f, LateralG %.2f, VerticalG %.2f, PercentLift %i, Palt %i, VSI %i, TAS %.2f, OAT %.2f, Heading %i ,Time %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll,
                                            suEfis.LateralG, suEfis.VerticalG, suEfis.PercentLift,
                                            suEfis.Palt, suEfis.VSI, suEfis.TAS, suEfis.OAT, suEfis.Heading, suEfis.Time.c_str());
                                } // end if CRC OK

                                else
                                    g_Log.print(MsgLog::EnEfis, MsgLog::EnWarning, "SKYVIEW ADAHRS CRC Failed");

                            } // end if Msg Type 1

                            else if (BufferString.length()==225 && BufferString[0]=='!' && BufferString[1]=='3')
                            {
                                // parse Skyview EMS data
                                String parseString;
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=220;i++) calcCRC+=BufferString[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==(int)strtol(&BufferString.substring(221, 223)[0],NULL,16)) // convert from hex back into integer for camparison, issue with missing leading zeros when comparing hex formats
                                {
                                    //float efisFuelRemaining=0.00;
                                    parseString = BufferString.substring(44, 47);
                                    if (parseString!="XXX") suEfis.FuelRemaining = parseString.toFloat()/10; else suEfis.FuelRemaining = -1; // gallons
                                    //float efisFuelFlow=0.00;
                                    parseString = BufferString.substring(29, 32);
                                    if (parseString!="XXX") suEfis.FuelFlow = parseString.toFloat()/10;      else suEfis.FuelFlow = -1; // gph
                                    //float efisMAP=0.00;
                                    parseString = BufferString.substring(26, 29);
                                    if (parseString!="XXX") suEfis.MAP = parseString.toFloat()/10;           else suEfis.MAP = -1; //inHg
                                    // int efisRPM=0;
                                    parseString = BufferString.substring(18, 22);
                                    if (parseString!="XXXX") suEfis.RPM = parseString.toInt();               else suEfis.RPM = -1;
                                    // int efisPercentPower=0;
                                    parseString = BufferString.substring(217, 220);
                                    if (parseString!="XXX") suEfis.PercentPower = parseString.toInt();       else suEfis.PercentPower = -1;
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
                            if (BufferString.length() == DYNON_SERIAL_LEN)
                            {
                                // parse Dynon data
                                String parseString;
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=48;i++) calcCRC+=BufferString[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==(int)strtol(&BufferString.substring(49, 51)[0],NULL,16)) // convert from hex back into integer for camparison, issue with missing leading zeros when comparing hex formats
                                {
                                    // CRC passed
                                    parseString        = BufferString.substring(20, 24);
                                    suEfis.IAS         = parseString.toFloat() / 10 * 1.94384; // m/s to knots
                                    parseString        = BufferString.substring(8, 12);
                                    suEfis.Pitch       = parseString.toFloat()/10;
                                    parseString        = BufferString.substring(12, 17);
                                    suEfis.Roll        = parseString.toFloat()/10;
                                    parseString        = BufferString.substring(33, 36);
                                    suEfis.LateralG    = parseString.toFloat()/100;
                                    parseString        = BufferString.substring(36, 39);
                                    suEfis.VerticalG   = parseString.toFloat()/10;
                                    parseString        = BufferString.substring(39, 41);
                                    suEfis.PercentLift = parseString.toInt(); // 00 to 99, percentage of stall angle
                                    parseString        = BufferString.substring(45,47);
                                    long statusBitInt  = strtol(&parseString[1], NULL, 16);
                                    if (bitRead(statusBitInt, 0))
                                    {
                                        // when bitmask bit 0 is 1, grab pressure altitude and VSI, otherwise use previous value (skip turn rate and density altitude)
                                        parseString = BufferString.substring(24, 29);
                                        suEfis.Palt = parseString.toInt()*3.28084; // meters to feet
                                        parseString = BufferString.substring(29, 33);
                                        suEfis.VSI  = int(parseString.toFloat()/10*60); // feet/sec to feet/min
                                    }
                                    uTimestamp = millis();
                                    suEfis.Time = BufferString.substring(0, 2)+":"+BufferString.substring(2, 4)+":"+BufferString.substring(4, 6)+"."+BufferString.substring(6, 8);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "D10: IAS %.2f, Pitch %.2f, Roll %.2f, LateralG %.2f, VerticalG %.2f, PercentLift %i, Palt %i, VSI %i, Time %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll, suEfis.LateralG, suEfis.VerticalG, suEfis.PercentLift,suEfis.Palt,suEfis.VSI,suEfis.Time.c_str());
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnDebug, "D10 CRC Failed");

                            } // end if length OK
                        } // end efisType DYNON D10

                        else if (enType == EnGarminG5) // G5, was 4
                        {
                            if (BufferString.length()==59 && BufferString[0]=='=' && BufferString[1]=='1' && BufferString[2]=='1')
                            {
                                // parse G5 data
                                String parseString;
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=54;i++) calcCRC+=BufferString[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==(int)strtol(&BufferString.substring(55, 57)[0],NULL,16)) // convert from hex back into integer for camparison, issue with missing leading zeros when comparing hex formats
                                {
                                    // CRC passed
                                    parseString = BufferString.substring(23, 27);
                                    if (parseString!="____")   suEfis.IAS       = parseString.toFloat()/10;
                                    parseString = BufferString.substring(11, 15);
                                    if (parseString!="____")   suEfis.Pitch     = parseString.toFloat()/10;
                                    parseString = BufferString.substring(15, 20);
                                    if (parseString!="_____")  suEfis.Roll      = parseString.toFloat()/10;
                                    parseString = BufferString.substring(20, 23);
                                    if (parseString!="___")    suEfis.Heading   = parseString.toInt();
                                    parseString = BufferString.substring(37, 40);
                                    if (parseString!="___")    suEfis.LateralG  = parseString.toFloat()/100;
                                    parseString = BufferString.substring(40, 43);
                                    if (parseString!="___")    suEfis.VerticalG = parseString.toFloat()/10;
                                    parseString = BufferString.substring(27, 33);
                                    if (parseString!="______") suEfis.Palt      = parseString.toInt(); // feet
                                    parseString = BufferString.substring(45, 49);
                                    if (parseString!="____")   suEfis.VSI       = parseString.toInt()*10; //10 fpm
                                    uTimestamp = millis();
                                    suEfis.Time = BufferString.substring(3, 5)+":"+BufferString.substring(5, 7)+":"+BufferString.substring(7, 9)+"."+BufferString.substring(9, 11);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "G5 data: IAS %.2f, Pitch %.2f, Roll %.2f, Heading %i, LateralG %.2f, VerticalG %.2f, Palt %i, VSI %i, Time %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll, suEfis.Heading, suEfis.LateralG, suEfis.VerticalG,suEfis.Palt,suEfis.VSI,suEfis.Time.c_str());
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "G5 CRC Failed");

                            }
                        } // efisType GARMIN G5

                        else if (enType == EnGarminG3X) // G3X, was 5
                        {
                            // parse G3X attitude data, 10hz
                            if (BufferString.length()==59 && BufferString[0]=='=' && BufferString[1]=='1' && BufferString[2]=='1')
                            {
                                // parse G3X data
                                String parseString;
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=54;i++) calcCRC+=BufferString[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==(int)strtol(&BufferString.substring(55, 57)[0],NULL,16)) // convert from hex back into integer for camparison, issue with missing leading zeros when comparing hex formats
                                {
                                    // CRC passed
                                    parseString = BufferString.substring(23, 27);
                                    if (parseString!="____")   suEfis.IAS        = parseString.toFloat()/10;
                                    parseString = BufferString.substring(11, 15);
                                    if (parseString!="____")   suEfis.Pitch      = parseString.toFloat()/10;
                                    parseString = BufferString.substring(15, 20);
                                    if (parseString!="_____")  suEfis.Roll       = parseString.toFloat()/10;
                                    parseString = BufferString.substring(20, 23);
                                    if (parseString!="___")    suEfis.Heading    = parseString.toInt();
                                    parseString = BufferString.substring(37, 40);
                                    if (parseString!="___")    suEfis.LateralG   = parseString.toFloat()/100;
                                    parseString = BufferString.substring(40, 43);
                                    if (parseString!="___")    suEfis.VerticalG  = parseString.toFloat()/10;
                                    parseString = BufferString.substring(43, 45);
                                    if (parseString!="__")     suEfis.PercentLift = parseString.toInt();
                                    parseString = BufferString.substring(27, 33);
                                    if (parseString!="______") suEfis.Palt       = parseString.toInt(); // feet
                                    parseString = BufferString.substring(49, 52);
                                    if (parseString!="___")    suEfis.OAT        = parseString.toInt();
                                    parseString = BufferString.substring(45, 49); // celsius
                                    if (parseString!="____")   suEfis.VSI       = parseString.toInt()*10; //10 fpm
                                    uTimestamp = millis();
                                    suEfis.Time = BufferString.substring(3, 5)+":"+BufferString.substring(5, 7)+":"+BufferString.substring(7, 9)+"."+BufferString.substring(9, 11);
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "G3X Attitude data: efisIAS %.2f, efisPitch %.2f, efisRoll %.2f, efisHeading %i, efisLateralG %.2f, efisVerticalG %.2f, efisPercentLift %i, efisPalt %i, efisVSI %i,efisTime %s\n",
                                            suEfis.IAS, suEfis.Pitch, suEfis.Roll, suEfis.Heading, suEfis.LateralG, suEfis.VerticalG, suEfis.PercentLift, suEfis.Palt, suEfis.VSI, suEfis.Time.c_str());
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnDebug, "G3X Attitude CRC Failed");
                            }

                            // parse G3X engine data, 5Hz
                            else if (BufferString.length()==221 && BufferString[0]=='=' && BufferString[1]=='3' && BufferString[2]=='1')
                            {
                                String parseString;
                                //calculate CRC
                                int calcCRC=0;
                                for (int i=0;i<=216;i++) calcCRC+=BufferString[i];
                                calcCRC=calcCRC & 0xFF;
                                if (calcCRC==(int)strtol(&BufferString.substring(217, 219)[0],NULL,16)) // convert from hex back into integer for camparison, issue with missing leading zeros when comparing hex formats
                                {
                                    //float efisFuelRemaining=0.00;
                                    parseString = BufferString.substring(44, 47);
                                    if (parseString!="___")  suEfis.FuelRemaining = parseString.toFloat()/10;
                                    parseString = BufferString.substring(29, 32);
                                    if (parseString!="___")  suEfis.FuelFlow      = parseString.toFloat()/10;
                                    parseString = BufferString.substring(26, 29);
                                    if (parseString!="___")  suEfis.MAP           = parseString.toFloat()/10;
                                    parseString = BufferString.substring(18, 22);
                                    if (parseString!="____") suEfis.RPM           = parseString.toInt();
                                    if (g_Log.Test(MsgLog::EnEfis, MsgLog::EnDebug))
                                        g_Log.printf(MsgLog::EnEfis, MsgLog::EnDebug, "G3X EMS: efisFuelRemaining %.2f, efisFuelFlow %.2f, efisMAP %.2f, efisRPM %i\n",
                                            suEfis.FuelRemaining, suEfis.FuelFlow, suEfis.MAP, suEfis.RPM);
                                }

                                else
                                    g_Log.println(MsgLog::EnEfis, MsgLog::EnWarning, "G3X EMS CRC Failed");
                            }

                        } // end efisType GARMIN G3X
                        BufferString = "";  // reset buffer
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
