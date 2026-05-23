
#include "src/Globals.h"
#include "src/io/EfisSerialPort.h"

#include <aero/WindTriangle.h>

#include <cmath>

// ---------------------------------------------------------------------------
// EfisSerialPort
// ---------------------------------------------------------------------------

EfisSerialPort::EfisSerialPort()
    : pSerial(nullptr)
    , enType(EnNone)
    , uTimestamp(0)
    , lastReceivedEfisTime(0)
    , parser_(onspeed::efis::EfisType::None)
{
    suEfis.DecelRate        = 0.00f;
    suEfis.IAS              = 0.00f;
    suEfis.Pitch            = 0.00f;
    suEfis.Roll             = 0.00f;
    suEfis.LateralG         = 0.00f;
    suEfis.VerticalG        = 0.00f;
    suEfis.PercentLift      = 0;
    suEfis.Palt             = 0;
    suEfis.VSI              = 0;
    suEfis.TAS              = 0.00f;
    suEfis.OAT              = 0.00f;
    // EMS engine fields zero-initialize to "no data received."  When an
    // EFIS is connected but sends no EMS frames (non-EMS-equipped
    // aircraft, or a D10/G5/MGL connection where no EMS frame exists),
    // applyFrame() never overwrites these defaults — the SD log reads
    // 0 for the duration.  Post-flight tools should treat the row's
    // efisRpm/efisMap/efisFuelFlow/efisFuelRemaining/efisPercentPower
    // columns as "absent" when all five read 0 with `efisAge` not set.
    // Changing this to NaN/empty-string output is tracked separately;
    // leaving 0 here preserves the pre-PR contract for downstream tools.
    suEfis.FuelRemaining    = 0.00f;
    suEfis.FuelFlow         = 0.00f;
    suEfis.MAP              = 0.00f;
    suEfis.RPM              = 0;
    suEfis.PercentPower     = 0;
    suEfis.Heading          = -1;
    suEfis.szTime[0]        = '\0';

    suVN300.AngularRateRoll  = 0.00f;
    suVN300.AngularRatePitch = 0.00f;
    suVN300.AngularRateYaw   = 0.00f;
    suVN300.VelNedNorth      = 0.00f;
    suVN300.VelNedEast       = 0.00f;
    suVN300.VelNedDown       = 0.00f;
    suVN300.AccelFwd         = 0.00f;
    suVN300.AccelLat         = 0.00f;
    suVN300.AccelVert        = 0.00f;
    suVN300.Yaw              = 0.00f;
    suVN300.Pitch            = 0.00f;
    suVN300.Roll             = 0.00f;
    suVN300.LinAccFwd        = 0.00f;
    suVN300.LinAccLat        = 0.00f;
    suVN300.LinAccVert       = 0.00f;
    suVN300.YawSigma         = 0.00f;
    suVN300.RollSigma        = 0.00f;
    suVN300.PitchSigma       = 0.00f;
    suVN300.GnssVelNedNorth  = 0.00f;
    suVN300.GnssVelNedEast   = 0.00f;
    suVN300.GnssVelNedDown   = 0.00f;
    suVN300.GPSFix           = 0;
    suVN300.GnssLat          = 0.00;
    suVN300.GnssLon          = 0.00;
    suVN300.EstAltMeters     = 0.00;
    suVN300.TimeStartupNs    = 0;
    suVN300.TimeGpsNs        = 0;
    suVN300.TimeStatus       = 0;
    suVN300.WindSpd        = std::nanf("");
    suVN300.WindDir       = std::nanf("");
    suVN300.WindVertical   = std::nanf("");

    uTimestamp = millis();
}

// ---------------------------------------------------------------------------

onspeed::efis::EfisType EfisSerialPort::toCoreType(EnEfisType t)
{
    switch (t)
    {
        case EnVN300:        return onspeed::efis::EfisType::Vn300;
        case EnDynonSkyview: return onspeed::efis::EfisType::DynonSkyview;
        case EnDynonD10:     return onspeed::efis::EfisType::DynonD10;
        case EnGarminG5:     return onspeed::efis::EfisType::GarminG5;
        case EnGarminG3X:    return onspeed::efis::EfisType::GarminG3X;
        case EnMglBinary:    return onspeed::efis::EfisType::MglBinary;
        case EnNone:
        default:
            return onspeed::efis::EfisType::None;
    }
}

// ---------------------------------------------------------------------------

void EfisSerialPort::Init(EnEfisType enEfisType, HardwareSerial* pEfisSerial)
{
    uint32_t hwSerialConfig = SerialConfig::SERIAL_8N1;

    enType     = enEfisType;
    pSerial    = pEfisSerial;
    pHwSerial_ = pEfisSerial;

    // Consume any pending request so a deferred Init triggered by a
    // prior RequestTypeChange is satisfied by this explicit Init.
    // Without this, the boot path's setup() Init followed by the
    // first loopTask Read() would fire Init twice (boot path calls
    // RequestTypeChange via LoadConfig → ApplyPostParseSideEffects,
    // then setup() calls Init directly).  Harmless but wasteful.
    pendingType_ = kNoPendingType;

    parser_.ChangeType(toCoreType(enType));

    pEfisSerial->end();

    if (enType != EnNone)
    {
        // VN-300 runs at 921600 baud to fit the 138-byte frame at 400 Hz
        // (55.2 kB/s, 60% utilization). Other EFIS types stay at 115200
        // (the de-facto avionics serial standard for Dynon/Garmin/MGL).
        const uint32_t baud = (enType == EnVN300) ? 921600 : 115200;
        pEfisSerial->begin(baud, hwSerialConfig, kEfisRx, kEfisTx, false);
    }
}

void EfisSerialPort::InitWithStream(EnEfisType enEfisType, Stream* pStream)
{
    enType     = enEfisType;
    pSerial    = pStream;
    pHwSerial_ = nullptr;        // signal "no UART to reinit"
    pendingType_ = kNoPendingType;
    parser_.ChangeType(toCoreType(enType));
    // No UART begin/end — the supplied Stream is responsible for its own
    // byte source.  Used by perf-synth builds with a SyntheticStream.
}

// ---------------------------------------------------------------------------

void EfisSerialPort::Read()
{
    // Apply any pending type change here, on the same task as the rest of
    // Read(), so the UART teardown / parser-state reset can't race a
    // concurrent read on another task. Web-handler / console paths request
    // the change via RequestTypeChange(); loopTask picks it up between
    // iterations. The flag-not-sentinel check is the only condition for
    // running Init — comparing the pending value against enType would be
    // self-defeating because RequestTypeChange already wrote enType
    // synchronously (so the schema-rotation log-header path reads the new
    // value), so the two are always equal by the time we get here.
    //
    // Consume window: if a RequestTypeChange call on another task lands
    // between the local read of pendingType_ and the clear two lines
    // below, the second request's pending value is overwritten by the
    // clear and the parser-reset for it is silently dropped. In
    // practice, two distinct EFIS-type changes seconds apart from each
    // other are vanishingly unlikely (config saves are user-initiated,
    // not automated), so accept the window rather than reach for an
    // atomic exchange.
    const int pending = pendingType_;
    if (pending != kNoPendingType) {
        pendingType_ = kNoPendingType;
        if (pHwSerial_ != nullptr) {
            // Real UART — re-init via the HardwareSerial path so begin()/end()
            // tear down and reconfigure the UART for the new protocol.
            Init(static_cast<EnEfisType>(pending), pHwSerial_);
        } else {
            // Synth or stream-only build — there's nothing to UART-reinit;
            // just reset the parser state to match the new type so the
            // bytes feed through the right state machine. The synth
            // Stream itself doesn't care about EFIS type changes (a
            // perf-synth binary is configured for one fixed protocol at
            // compile time), so a runtime-config-driven type change is
            // effectively cosmetic here.
            enType = static_cast<EnEfisType>(pending);
            parser_.ChangeType(toCoreType(enType));
        }
    }

    if (!g_Config.bReadEfisData)
        return;

    static constexpr int kPacketSize = 512;
    int packetCount = 0;
    bool anyByteSeen = false;

    // Stack-allocated scratch frames — populated copy-free via
    // TryTakeFrame() / TryTakeVn300Data(). Avoids the prior
    // optional<EfisFrame>::TakeFrame() copy round-trip.
    onspeed::EfisFrame      frame;
    onspeed::efis::Vn300Data vnData;

    while (pSerial->available() && packetCount < kPacketSize)
    {
        const uint8_t b = static_cast<uint8_t>(pSerial->read());
        packetCount++;
        anyByteSeen = true;

        parser_.FeedByte(b);

        if (parser_.TryTakeFrame(frame))
        {
            applyFrame(frame);
            uTimestamp = millis();
        }

        if (enType == EnVN300 && parser_.TryTakeVn300Data(vnData))
        {
            applyVn300Data(vnData);
        }
    }

    // Update lastReceivedEfisTime ONCE per Read() rather than per byte —
    // the value is only used for the "EFIS link alive" timeout check at
    // ~Hz cadence; per-byte millis() calls add up at 400+ Hz packet rates.
    if (anyByteSeen)
        lastReceivedEfisTime = millis();
}

// ---------------------------------------------------------------------------

void EfisSerialPort::applyFrame(const onspeed::EfisFrame& frame)
{
    using onspeed::EfisSource;
    namespace F = onspeed::EfisField;

    // Each parser sets EfisFrame::fieldsPresent bits for the fields it
    // actually wrote this frame. Branching on the bitmask is honest
    // about intent ("did the parser write this field?") rather than
    // inferring it from float-NaN tests. On Xtensa LX7 the codegen is
    // comparable (isfinite emits as an integer mask on the float bit
    // pattern, not via the FPU); the structural clarity is the real
    // win. NaN sentinels remain in place as a fallback for any
    // consumer that still tests std::isfinite() directly.
    const uint32_t fp = frame.fieldsPresent;

    if (fp & F::Ias)        suEfis.IAS         = frame.iasKt;
    if (fp & F::Pitch)      suEfis.Pitch       = frame.pitchDeg;
    if (fp & F::Roll)       suEfis.Roll        = frame.rollDeg;
    if (fp & F::Heading)    suEfis.Heading     = static_cast<int>(frame.headingDeg);
    if (fp & F::LateralG)   suEfis.LateralG    = frame.lateralG;
    if (fp & F::VerticalG)  suEfis.VerticalG   = frame.verticalG;
    if (fp & F::AoaPercent) suEfis.PercentLift = static_cast<int>(frame.aoaPercent);
    if (fp & F::Palt)       suEfis.Palt        = static_cast<int>(frame.paltFt);
    if (fp & F::Vsi)        suEfis.VSI         = static_cast<int>(frame.vsiFpm);
    if (fp & F::Tas)        suEfis.TAS         = frame.tasKt;
    if (fp & F::OatCelsius) suEfis.OAT         = frame.oatCelsius;

    // Engine fields (Dynon SkyView EMS and Garmin G3X EMS; absent on
    // non-EMS frames and on protocols that don't carry engine data).
    if (fp & F::Rpm)              suEfis.RPM           = static_cast<int>(frame.rpm);
    if (fp & F::MapInchHg)        suEfis.MAP           = frame.mapInchHg;
    if (fp & F::FuelFlowGph)      suEfis.FuelFlow      = frame.fuelFlowGph;
    if (fp & F::FuelRemainingGal) suEfis.FuelRemaining = frame.fuelRemainingGal;
    if (fp & F::PercentPower)     suEfis.PercentPower  = static_cast<int>(frame.percentPower);

    // Copy time-of-day string ONLY when this frame actually carries one.
    if (frame.timeOfDayHms[0] != '\0')
        {
        strncpy(suEfis.szTime, frame.timeOfDayHms, sizeof(suEfis.szTime) - 1);
        suEfis.szTime[sizeof(suEfis.szTime) - 1] = '\0';
        }

    // VN-300: mirror valid attitude into suVN300 so other consumers
    // (display, log, HUD) can read a consistent attitude regardless of
    // which path populated it.
    if (frame.source == EfisSource::Vn300)
    {
        if (fp & F::Pitch)   suVN300.Pitch = frame.pitchDeg;
        if (fp & F::Roll)    suVN300.Roll  = frame.rollDeg;
        if (fp & F::Heading) suVN300.Yaw   = frame.headingDeg;
    }
}

// ---------------------------------------------------------------------------

void EfisSerialPort::applyVn300Data(const onspeed::efis::Vn300Data& data)
{
    suVN300.AngularRateRoll  = data.angularRateRoll;
    suVN300.AngularRatePitch = data.angularRatePitch;
    suVN300.AngularRateYaw   = data.angularRateYaw;
    suVN300.VelNedNorth      = data.velNedNorth;
    suVN300.VelNedEast       = data.velNedEast;
    suVN300.VelNedDown       = data.velNedDown;
    suVN300.AccelFwd         = data.accelFwd;
    suVN300.AccelLat         = data.accelLat;
    suVN300.AccelVert        = data.accelVert;
    suVN300.Yaw              = data.yaw;
    suVN300.Pitch            = data.pitch;
    suVN300.Roll             = data.roll;
    suVN300.LinAccFwd        = data.linAccFwd;
    suVN300.LinAccLat        = data.linAccLat;
    suVN300.LinAccVert       = data.linAccVert;
    suVN300.YawSigma         = data.yawSigma;
    suVN300.RollSigma        = data.rollSigma;
    suVN300.PitchSigma       = data.pitchSigma;
    suVN300.GnssVelNedNorth  = data.gnssVelNedNorth;
    suVN300.GnssVelNedEast   = data.gnssVelNedEast;
    suVN300.GnssVelNedDown   = data.gnssVelNedDown;
    suVN300.GnssLat          = data.gnssLat;
    suVN300.GnssLon          = data.gnssLon;
    suVN300.EstAltMeters     = data.estAltMeters;
    suVN300.GPSFix           = data.gpsFix;
    suVN300.TimeStartupNs    = data.timeStartupNs;
    suVN300.TimeGpsNs        = data.timeGpsNs;
    suVN300.TimeStatus       = data.timeStatus;

    // Wind triangle.  Snapshot TAS without a mutex: aligned 32-bit float
    // DRAM loads are atomic on Xtensa LX7, so no torn-read is possible for
    // this single scalar.  Do NOT extend this reasoning to multi-word reads
    // (struct snapshots, double) — those are not atomic and would need the
    // xAhrsMutex.  Gate on GPS fix; without it, GnssVelNed is noise.
    constexpr float kKtPerMps = 1.943844f;
    const float ownshipTasMps = g_AHRS.fTAS;
    if (data.gpsFix > 0) {
        auto wind = onspeed::aero::ComputeWind(
            data.gnssVelNedNorth, data.gnssVelNedEast, data.gnssVelNedDown,
            data.yaw, data.pitch, ownshipTasMps);
        if (wind) {
            suVN300.WindSpd      = wind->windSpeedMps    * kKtPerMps;
            suVN300.WindDir     = wind->windDirDeg;
            suVN300.WindVertical = wind->windVerticalMps * kKtPerMps;
        } else {
            suVN300.WindSpd      = std::nanf("");
            suVN300.WindDir     = std::nanf("");
            suVN300.WindVertical = std::nanf("");
        }
    } else {
        suVN300.WindSpd      = std::nanf("");
        suVN300.WindDir     = std::nanf("");
        suVN300.WindVertical = std::nanf("");
    }
}
