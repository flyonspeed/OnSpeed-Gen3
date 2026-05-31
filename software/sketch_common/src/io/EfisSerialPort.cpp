
#include "src/Globals.h"
#include "src/io/EfisSerialPort.h"
#include "src/io/IdfUartStream.h"

#include <aero/WindTriangle.h>

#include <cmath>

// ---------------------------------------------------------------------------
// EfisSerialPort
// ---------------------------------------------------------------------------

EfisSerialPort::EfisSerialPort()
    : enType(EnNone)
    , uTimestamp(0)
    , lastReceivedEfisTime(0)
    , parser_(onspeed::efis::EfisType::None)
{
    // Initial-state publish: most fields default to zero (matching the
    // SnapshotPublisher's default-constructed payload), but a few have
    // non-zero "no data yet" defaults that consumers depend on:
    //   - suEfis.Heading = -1  (sentinel for "no heading; treat as N/A")
    //   - suVN300.Wind* = NaN  (no GPS fix → no wind solution)
    // Publishing the staging structs here puts those defaults into the
    // SnapshotPublishers so the first reader (which may run before
    // any frame has decoded) sees them.
    //
    // The constructor runs during static init, BEFORE the FreeRTOS
    // scheduler is up.  SnapshotPublisher's publish() is just three
    // atomic ops + a memcpy — no FreeRTOS calls, no mutex creation,
    // safe to call in any context.
    SuEfisData efInit{};
    efInit.Heading = -1;
    suEfis_pub_.publish(efInit);

    SuVN300Data vnInit{};
    vnInit.WindSpd      = std::nanf("");
    vnInit.WindDir      = std::nanf("");
    vnInit.WindVertical = std::nanf("");
    suVN300_pub_.publish(vnInit);

    uTimestamp = millis();
}

// ---------------------------------------------------------------------------
// Atomic snapshot accessors.  Wait-free reads via SnapshotPublisher's
// seqcount.  See the header for the deadlined-caller contract.
// ---------------------------------------------------------------------------

void EfisSerialPort::SnapshotEfis(SuEfisData& out) const
{
    out = suEfis_pub_.read();
}

void EfisSerialPort::SnapshotVn300(SuVN300Data& out) const
{
    out = suVN300_pub_.read();
}

bool EfisSerialPort::TrySnapshotEfis(SuEfisData& out) const
{
    return suEfis_pub_.tryRead(out);
}

bool EfisSerialPort::TrySnapshotVn300(SuVN300Data& out) const
{
    return suVN300_pub_.tryRead(out);
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

void EfisSerialPort::AttachUart(IdfUartStream* pStream, EnEfisType enEfisType)
{
    // No mutex to create — SnapshotPublisher is constexpr-constructible
    // and the static-init defaults are already in place from the
    // EfisSerialPort constructor.

    enType       = enEfisType;
    pStream_     = pStream;
    pendingType_ = kNoPendingType;
    parser_.ChangeType(toCoreType(enType));
    // The IDF UART driver is installed by the caller (EfisReadTaskInit
    // in tasks/EfisRead.cpp); no UART begin/end here.
}

void EfisSerialPort::ApplyPendingTypeChange()
{
    // Same task-affinity contract as before: only EfisReadTask calls
    // this, so the parser reset can't race the byte pump.  Consume
    // window vs RequestTypeChange (cross-task write) is acceptable —
    // see RequestTypeChange comment.
    const int pending = pendingType_;
    if (pending != kNoPendingType) {
        pendingType_ = kNoPendingType;
        enType = static_cast<EnEfisType>(pending);
        parser_.ChangeType(toCoreType(enType));
        // We do NOT re-init the UART here — the baud rate change for
        // VN-300 vs other EFIS types is set at AttachUart time (boot).
        // Live EFIS-type changes via web UI now require reboot to
        // change baud.  Previously this code path called
        // HardwareSerial::end()/begin() to reconfigure; with the IDF
        // UART driver that's a more invasive sequence and not worth
        // supporting hot.  See issue tracker for explicit reboot UX.
    }
}

bool EfisSerialPort::IsReadingEnabled() const
{
    return g_Config.bReadEfisData;
}

void EfisSerialPort::FeedBytes(const uint8_t* buf, size_t n)
{
    if (n == 0) return;

    // Stack-allocated scratch frames — populated copy-free via
    // TryTakeFrame() / TryTakeVn300Data(). Avoids the prior
    // optional<EfisFrame>::TakeFrame() copy round-trip.
    onspeed::EfisFrame      frame;
    onspeed::efis::Vn300Data vnData;

    for (size_t i = 0; i < n; ++i)
    {
        parser_.FeedByte(buf[i]);

        // Coalesce the two "frame complete" callbacks for VN-300 into a
        // single mutex-protected publish.  Otherwise applyFrame would
        // give the mutex, applyVn300Data would re-take it, and a reader
        // running on Core 1 (AHRS, DisplaySerial) could observe partial
        // state — Yaw/Pitch/Roll from applyFrame but TimeStartupNs /
        // Lat / Lon from the PRIOR applyVn300Data.  The torn-read
        // detector caught this pattern empirically (see
        // tools/bench/check-atomic-publish.py).
        const bool gotFrame = parser_.TryTakeFrame(frame);
        const bool gotVn300 = (enType == EnVN300) && parser_.TryTakeVn300Data(vnData);

        if (gotFrame && gotVn300)
        {
            // VN-300 dual-hit: call applyVn300Data only.  It writes
            // BOTH suVN300_published_ (the full VN frame: Yaw/Pitch/Roll
            // + Lat/Lon + tNs + everything else) AND mirrors
            // Pitch/Roll/Heading into suEfis_published_, all under a
            // single mutex hold.  One atomic publish.
            //
            // The earlier "call BOTH applyFrame + applyVn300Data" shape
            // ran with two separate mutex takes.  A reader on Core 0
            // (LogSensor) running between them saw frame-N Pitch/Roll
            // (from applyFrame's mirror) combined with frame-(N-1)
            // Lat/Lon/tNs (still in suVN300 from the previous
            // applyVn300Data).  log_098 measured this at 1.6% of stim
            // rows (6118 / 385094) — small, but real, and visible to
            // anyone running the offline atomic-publish analyzer.
            //
            // For non-VN-300 frames this branch isn't taken (gotVn300
            // is false unless enType == EnVN300).
            applyVn300Data(vnData);
            uTimestamp = millis();
        }
        else if (gotFrame)
        {
            applyFrame(frame);
            uTimestamp = millis();
        }
        else if (gotVn300)
        {
            // Shouldn't happen — parser sets both flags together —
            // but be defensive.
            applyVn300Data(vnData);
            uTimestamp = millis();
        }
    }

    // Update once per drain (not per byte) — the field's only consumer
    // is the EFIS-link-alive timeout at ~Hz cadence.
    lastReceivedEfisTime = millis();
}

void EfisSerialPort::Read()
{
    ApplyPendingTypeChange();
    if (!IsReadingEnabled() || pStream_ == nullptr)
        return;

    // Bulk-drain whatever's in the IDF software RX buffer.  Single
    // uart_read_bytes(N=size) syscall instead of N call pairs of
    // available()+read().  At 400 Hz × 138 B = 55 kB/s this is the
    // difference between ~110K syscalls/sec and ~800 syscalls/sec.
    // Buffer sized at 2x the IDF SW buffer (2048 B) for safety — in
    // practice we'll never see >2KB queued at any UART_DATA event.
    uint8_t scratch[2048];
    const size_t got = pStream_->readBulk(scratch, sizeof(scratch));
    FeedBytes(scratch, got);
}

// ---------------------------------------------------------------------------

void EfisSerialPort::applyFrame(const onspeed::EfisFrame& frame)
{
    using onspeed::EfisSource;
    namespace F = onspeed::EfisField;

    // Each parser sets EfisFrame::fieldsPresent bits for the fields it
    // actually wrote this frame. Branching on the bitmask is honest
    // about intent ("did the parser write this field?") rather than
    // inferring it from float-NaN tests.
    const uint32_t fp = frame.fieldsPresent;

    // Read-modify-write on suEfis_pub_.  EfisFrame is a sparse delta —
    // only the bits set in fp get updated; the rest of suEfis retains
    // its prior value.  So we read the current published state into a
    // local, mutate the fields marked present, and re-publish the
    // whole struct atomically.
    //
    // Safe because EfisReadTask is the single writer.  A reader that
    // races between our read and publish sees the OLD struct (still
    // coherent) until our publish lands, then sees the NEW struct.
    // No interleaved-publisher window because no other task is
    // publishing.
    SuEfisData suEfis = suEfis_pub_.read();

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

    if (fp & F::Rpm)              suEfis.RPM           = static_cast<int>(frame.rpm);
    if (fp & F::MapInchHg)        suEfis.MAP           = frame.mapInchHg;
    if (fp & F::FuelFlowGph)      suEfis.FuelFlow      = frame.fuelFlowGph;
    if (fp & F::FuelRemainingGal) suEfis.FuelRemaining = frame.fuelRemainingGal;
    if (fp & F::PercentPower)     suEfis.PercentPower  = static_cast<int>(frame.percentPower);

    if (frame.timeOfDayHms[0] != '\0') {
        strncpy(suEfis.szTime, frame.timeOfDayHms, sizeof(suEfis.szTime) - 1);
        suEfis.szTime[sizeof(suEfis.szTime) - 1] = '\0';
    }

    // VN-300 frames are NEVER routed through applyFrame in this codebase.
    // FeedBytes() routes the dual-hit (gotFrame && gotVn300) case to
    // applyVn300Data() only, which publishes both suVN300_pub_ and the
    // suEfis mirror.  See FeedBytes() in this file for the dispatch logic.

    suEfis_pub_.publish(suEfis);
}

// ---------------------------------------------------------------------------

void EfisSerialPort::applyVn300Data(const onspeed::efis::Vn300Data& data)
{
    // Build a fully-populated staging struct in stack memory.  Readers
    // see either the entire OLD struct or the entire NEW struct — never
    // a torn mix.  This is the load-bearing fix for the per-frame
    // atomicity Lenny called out.  Wind-triangle (which involves a
    // possibly-expensive ComputeWind call + reads of g_AHRS.fTAS) is
    // done OUTSIDE the mutex on the staging copy; the mutex only
    // protects the brief memcpy of the finished struct into published.
    SuVN300Data staging;
    staging.AngularRateRoll  = data.angularRateRoll;
    staging.AngularRatePitch = data.angularRatePitch;
    staging.AngularRateYaw   = data.angularRateYaw;
    staging.VelNedNorth      = data.velNedNorth;
    staging.VelNedEast       = data.velNedEast;
    staging.VelNedDown       = data.velNedDown;
    staging.AccelFwd         = data.accelFwd;
    staging.AccelLat         = data.accelLat;
    staging.AccelVert        = data.accelVert;
    staging.Yaw              = data.yaw;
    staging.Pitch            = data.pitch;
    staging.Roll             = data.roll;
    staging.LinAccFwd        = data.linAccFwd;
    staging.LinAccLat        = data.linAccLat;
    staging.LinAccVert       = data.linAccVert;
    staging.YawSigma         = data.yawSigma;
    staging.RollSigma        = data.rollSigma;
    staging.PitchSigma       = data.pitchSigma;
    staging.GnssVelNedNorth  = data.gnssVelNedNorth;
    staging.GnssVelNedEast   = data.gnssVelNedEast;
    staging.GnssVelNedDown   = data.gnssVelNedDown;
    staging.GnssLat          = data.gnssLat;
    staging.GnssLon          = data.gnssLon;
    staging.EstAltMeters     = data.estAltMeters;
    staging.GPSFix           = data.gpsFix;
    staging.TimeStartupNs    = data.timeStartupNs;
    staging.TimeGpsNs        = data.timeGpsNs;
    staging.TimeStatus       = data.timeStatus;

    // Wind triangle.  Snapshot TAS without a mutex: aligned 32-bit float
    // DRAM loads are atomic on Xtensa LX7, so no torn-read for a scalar.
    // Gate on GPS fix; without it, GnssVelNed is noise.
    constexpr float kKtPerMps = 1.943844f;
    const float ownshipTasMps = g_AHRS.fTAS;
    if (data.gpsFix > 0) {
        auto wind = onspeed::aero::ComputeWind(
            data.gnssVelNedNorth, data.gnssVelNedEast, data.gnssVelNedDown,
            data.yaw, data.pitch, ownshipTasMps);
        if (wind) {
            staging.WindSpd      = wind->windSpeedMps    * kKtPerMps;
            staging.WindDir      = wind->windDirDeg;
            staging.WindVertical = wind->windVerticalMps * kKtPerMps;
        } else {
            staging.WindSpd      = std::nanf("");
            staging.WindDir      = std::nanf("");
            staging.WindVertical = std::nanf("");
        }
    } else {
        staging.WindSpd      = std::nanf("");
        staging.WindDir      = std::nanf("");
        staging.WindVertical = std::nanf("");
    }

    // Publish the full VN-300 frame via suVN300_pub_.  Wait-free.
    suVN300_pub_.publish(staging);

    // Mirror attitude into suEfis_pub_ via a read-modify-write.  This
    // is the second publish for the same logical "VN-300 frame N
    // arrived" event.  Each Snapshot* call returns a coherent struct
    // (the seqcount guarantees it), but a reader running between the
    // two publishes can see VN-300 frame N's data with suEfis
    // attitude still at frame N-1 (or vice versa) for the few
    // hundred nanoseconds between calls.
    //
    // Acceptable in our task layout: the only consumer that snapshots
    // BOTH structs in lockstep is LogSensor at log-rate (50-416 Hz),
    // which is much slower than EfisReadTask's publish rate, so the
    // two-publish gap is invisible at log granularity.  For consumers
    // that need cross-struct atomicity (none today), a future change
    // could combine the two payloads into one struct with one
    // publisher.
    SuEfisData efMirror = suEfis_pub_.read();
    efMirror.Pitch   = data.pitch;
    efMirror.Roll    = data.roll;
    efMirror.Heading = static_cast<int>(data.yaw);
    suEfis_pub_.publish(efMirror);
}
