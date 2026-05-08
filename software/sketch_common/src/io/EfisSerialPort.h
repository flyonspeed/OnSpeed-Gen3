
#pragma once

#include <HardwareSerial.h>
#include <optional>

#include "src/Globals.h"
#include <efis/EfisParser.h>
#include <types/EfisFrame.h>

// EfisSerialPort — thin UART adapter that reads the hardware serial port,
// feeds bytes to the appropriate onspeed_core protocol parser, and exposes
// the decoded data to the rest of the sketch.
//
// The class keeps backward-compatible public fields (suEfis, suVN300, enType,
// uTimestamp) so that existing callers in LogSensor, DataServer, AHRS, Config,
// and DisplaySerial do not need to change in this PR.
//
// Internally the protocol parsing is done by onspeed::efis::EfisParser, which
// is the dispatcher introduced in PR 2.2. The legacy EfisSerialIO class and its
// 805-line Read() method have been deleted; this class replaces them.
//
// Call site summary:
//   Init()   — called once from setup() with enEfisType and a HardwareSerial*.
//   Read()   — called from the EFIS task loop at ~115200 baud.
//
// Thread safety: same as the original — no internal mutex; callers that read
// suEfis/suVN300 should hold xSensorMutex if they need a consistent snapshot.

class EfisSerialPort
{
public:
    EfisSerialPort();

    // Enum mirrors original EfisSerialIO::EnEfisType for call-site compatibility.
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

    // Decoded EFIS data (backward-compatible with EfisSerialIO::SuEfisData).
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
        char    szTime[16];
    };

    // Decoded VN-300 data (backward-compatible with EfisSerialIO::SuVN300Data).
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
        double  EstAltMeters;   // INS-estimated altitude (Common.Position LLA)
        char    szTimeUTC[24];
    };

    // Public data (accessed directly by callers in original code).
    HardwareSerial*  pSerial;
    EnEfisType       enType;
    SuEfisData       suEfis;
    SuVN300Data      suVN300;
    unsigned long    uTimestamp;       // millis() at last successful decode
    unsigned long    lastReceivedEfisTime;

    // Methods
    void Init(EnEfisType enEfisType, HardwareSerial* pEfisSerial);
    void Read();
    bool IsDataFresh(unsigned long maxAgeMs) const
        { return (millis() - uTimestamp) < maxAgeMs; }

private:
    onspeed::efis::EfisParser  parser_;

    // Convert EfisType enum to onspeed core enum
    static onspeed::efis::EfisType toCoreType(EnEfisType t);

    // Populate suEfis from a normalised EfisFrame.
    void applyFrame(const onspeed::EfisFrame& frame);

    // Populate suVN300 from Vn300Data.
    void applyVn300Data(const onspeed::efis::Vn300Data& data);
};
