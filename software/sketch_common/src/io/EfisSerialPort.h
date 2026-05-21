
#pragma once

#include <HardwareSerial.h>
#include <Stream.h>
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

        // Wind triangle, computed from GnssVelNed + ownship attitude + TAS.
        // NaN when no valid wind solution (low TAS, NaN inputs, no GPS fix).
        // WindDir is the "from" bearing in [0, 360), measured CW from north
        // in the same frame as Yaw.  The analysis workbook assumes true; the
        // VN-300 must be configured with WMM declination for that to hold.
        // OnSpeed does not correct.  WindVertical is positive for an updraft.
        float   WindSpd;        // knots
        float   WindDir;        // degrees, true if VN-300 has declination configured
        float   WindVertical;   // knots, positive = updraft
    };

    // Public data (accessed directly by callers in original code).
    // Typed as Stream* so the perf-synth build can substitute a
    // SyntheticStream that produces VN-300 bytes without going through a
    // real UART. Read() only uses Stream::available()/read(), so widening
    // here is invisible to production callers — the .ino still passes a
    // HardwareSerial* into Init(), where it's implicitly converted.
    Stream*          pSerial;
    EnEfisType       enType;
    SuEfisData       suEfis;
    SuVN300Data      suVN300;
    unsigned long    uTimestamp;       // millis() at last successful decode
    unsigned long    lastReceivedEfisTime;

    // Methods
    void Init(EnEfisType enEfisType, HardwareSerial* pEfisSerial);

    // Synth-build variant: skip the UART begin()/end() dance and just
    // wire pSerial to the supplied Stream. Used by the perf-synth env to
    // point the parser at a SyntheticVn300Stream / SyntheticSkyviewStream.
    // Same parser-state reset as Init().
    void InitWithStream(EnEfisType enEfisType, Stream* pStream);

    void Read();
    bool IsDataFresh(unsigned long maxAgeMs) const
        { return (millis() - uTimestamp) < maxAgeMs; }

    // Request a type change to be applied on the next Read() call. Safe to
    // call from any task: the cheap part (enType update, so other readers
    // of g_EfisSerial.enType see the new value immediately) runs inline,
    // and the expensive part (UART teardown + parser-state reset) is
    // deferred to Read() so it can't race a concurrent read on another
    // task. A pending request that matches the current enType is a no-op.
    void RequestTypeChange(EnEfisType enNewType) {
        // Update enType immediately so any caller that snapshots
        // g_EfisSerial.enType (LogSensor::Open's header-build path,
        // DataServer's VN-300 gating, etc.) sees the new value at once.
        // The actual parser_ + UART reset is deferred — until Read()
        // runs it, the driver still parses bytes as the OLD protocol.
        // That's acceptable: the driver will produce garbage frames for
        // ~one loopTask cycle (a few ms) until Read picks up the
        // pending request.
        enType = enNewType;
        pendingType_ = enNewType;
    }

private:
    onspeed::efis::EfisParser  parser_;

    // Cross-task pending-reinit slot. Sentinel value (-1 cast to enum) means
    // "no pending request". WebServer task writes; loopTask reads + clears.
    // Single-word aligned writes on ESP32 are atomic; no mutex needed.
    static constexpr int kNoPendingType = -1;
    volatile int pendingType_ = kNoPendingType;

    // Set by Init() to the HardwareSerial we own (for UART begin/end on
    // pending-reinit). InitWithStream() leaves this nullptr — synth builds
    // never call the UART begin path, and pending-reinit becomes a parser-
    // only reset there (the web UI's EFIS-type change has no UART work to
    // do when the byte source isn't a real UART).
    HardwareSerial*  pHwSerial_ = nullptr;

    // Convert EfisType enum to onspeed core enum
    static onspeed::efis::EfisType toCoreType(EnEfisType t);

    // Populate suEfis from a normalised EfisFrame.
    void applyFrame(const onspeed::EfisFrame& frame);

    // Populate suVN300 from Vn300Data.
    void applyVn300Data(const onspeed::efis::Vn300Data& data);
};
