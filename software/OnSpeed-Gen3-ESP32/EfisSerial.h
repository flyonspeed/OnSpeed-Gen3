
#pragma once

//#include <SoftwareSerial.h>
#include <HardwareSerial.h>

#include "Globals.h"

class EfisSerialIO
{
public:
    EfisSerialIO();

    // Structures
    enum EnEfisType
    {
        EnNone          = 0,
        EnVN300         = 1,
        EnDynonSkyview  = 2,
        EnDynonD10      = 3,
        EnGarminG5      = 4,
        EnGarminG3X     = 5,
        EnMglBinary     = 6,
    };

    // Decoded EFIS data
    struct SuEfisData
    {
        float   DecelRate;
        float   IAS;
        float   Pitch;
        float   Roll;
        float   LateralG;
        float   VerticalG;
        int     PercentLift;
        int     Palt;
        int     VSI;
        float   TAS;
        float   OAT;
        float   FuelRemaining;
        float   FuelFlow;
        float   MAP;
        int     RPM;
        int     PercentPower;
        int     Heading;
        String  Time;
    };

    // Decoded VN-300 data
    struct SuVN300Data
    {
        float   AngularRateRoll;
        float   AngularRatePitch;
        float   AngularRateYaw;
        float   VelNedNorth;
        float   VelNedEast;
        float   VelNedDown;
        float   AccelFwd;
        float   AccelLat;
        float   AccelVert;
        float   Yaw;
        float   Pitch;
        float   Roll;
        float   LinAccFwd;
        float   LinAccLat;
        float   LinAccVert;
        float   YawSigma;
        float   RollSigma;
        float   PitchSigma;
        float   GnssVelNedNorth;
        float   GnssVelNedEast;
        float   GnssVelNedDown;
        byte    GPSFix;
        double  GnssLat;
        double  GnssLon;
        String  TimeUTC;
    };

    // Data
public:
    HardwareSerial    * pSerial;
//    bool                bEnabled;
    EnEfisType          enType;
    SuEfisData          suEfis;
    SuVN300Data         suVN300;

    // Two different input buffers are provided. Buffer[] is for
    // incoming binary data. BufferString is for incoming text
    // style data.
    byte                Buffer[127];    // Binary buffer
    byte                BufferIndex;
    byte                PrevInByte;

    String              BufferString;   // Text buffer
    char                PrevInChar;

    bool                efisPacketInProgress = false;
    unsigned long       lastReceivedEfisTime;

    int                 mglMsgLen;

    unsigned long       uTimestamp; // Millisecond timestamp of decoded data

#ifdef EFISDATADEBUG
    int                 MaxAvailable;
#endif

    // Methods
public:
    void Init(EnEfisType enEfisType, HardwareSerial * pEfisSerial);
    void Enable(bool bEnable);
    void Read();
};