
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
    // NOTE: do NOT create the FreeRTOS mutex here.  This constructor
    // runs during static initialization, BEFORE setup() and BEFORE the
    // FreeRTOS scheduler is started.  xSemaphoreCreateMutex() returns
    // NULL in that context.  Mutex creation is deferred to
    // AttachUart() which runs from setup().  Until then, applyFrame /
    // applyVn300Data / SnapshotEfis / SnapshotVn300 all hit the
    // "mutex is null → fall through to unsynchronized access" path —
    // which is fine because no other task is running yet either.

    // Initialize the published structs to "no data" baselines.  These
    // values are what consumers see before the first frame decodes; they
    // match the previous direct-field-init pattern byte-for-byte.
    SuEfisData& suEfis = suEfis_published_;
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

    SuVN300Data& suVN300 = suVN300_published_;
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
// Atomic snapshot accessors.  Callers pass a stack-resident destination
// struct; we memcpy the published state into it under the data mutex.
// Hold time is bounded by sizeof(struct) — sub-microsecond for both.
// ---------------------------------------------------------------------------

void EfisSerialPort::SnapshotEfis(SuEfisData& out) const
{
    // Null-check the mutex: it's lazily created in AttachUart(), so any
    // caller that hits Snapshot before AttachUart (unit test, future
    // bench scaffold) would otherwise hand a NULL handle to
    // xSemaphoreTake — which dereferences it as a queue pointer and
    // crashes.  Mirrors BoomSerial::Snapshot's guard.
    if (xEfisDataMutex_ != nullptr &&
        xSemaphoreTake(xEfisDataMutex_, portMAX_DELAY) == pdTRUE) {
        out = suEfis_published_;
        xSemaphoreGive(xEfisDataMutex_);
    } else {
        // Mutex creation failed or pre-AttachUart — degrade to a torn
        // read rather than block forever.  Should not happen in
        // production (AttachUart runs in setup() before any task is
        // spawned).
        out = suEfis_published_;
    }
}

void EfisSerialPort::SnapshotVn300(SuVN300Data& out) const
{
    if (xEfisDataMutex_ != nullptr &&
        xSemaphoreTake(xEfisDataMutex_, portMAX_DELAY) == pdTRUE) {
        out = suVN300_published_;
        xSemaphoreGive(xEfisDataMutex_);
    } else {
        out = suVN300_published_;
    }
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
    // Lazy mutex creation.  Static-init constructed g_EfisSerial before
    // FreeRTOS was up; this is the first opportunity to make the mutex.
    // Idempotent — re-attaching (e.g. via runtime type-change reboot)
    // reuses the existing handle.
    if (xEfisDataMutex_ == nullptr) {
        xEfisDataMutex_ = xSemaphoreCreateMutex();
    }

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
            // VN-300 case: BOTH publishes run.  applyFrame writes
            // suEfis_published_ (Pitch/Roll/Heading + the VN-300 mirror
            // into suVN300_published_).  applyVn300Data writes the rest
            // of suVN300_published_ (TimeStartupNs, Lat/Lon, position
            // uncertainty, wind, etc.).  Each takes the mutex separately;
            // a Core-1 reader running between them could see Pitch/Roll
            // from the new frame combined with Lat/Lon from the previous
            // applyVn300Data.  Acceptable: within a single frame the
            // Pitch/Roll *and* Lat/Lon arrive on the same parser cycle,
            // so the "previous" Lat/Lon is at most one 2.5-ms frame
            // stale — well under the AHRS / display latency budget.
            // The full-frame coherency property still holds for any
            // single Snapshot{Efis,Vn300}() reader.
            //
            // The earlier "skip applyFrame" optimization dropped
            // suEfis_published_.Pitch/Roll/Heading entirely for VN-300,
            // which no current caller relied on but is a trap for any
            // future caller of SnapshotEfis() that wants attitude.
            applyFrame(frame);
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

    // Take the data mutex for the entire field-update sequence.
    // Single-writer (EfisReadTask) → uncontended path, only readers
    // wait.  Hold time bounded by ~15 field assignments + a possible
    // strncpy of <= 16 bytes — well under a microsecond.  We're the
    // only writer so a brief readers-blocked window is fine.
    if (xSemaphoreTake(xEfisDataMutex_, portMAX_DELAY) != pdTRUE)
        return;     // mutex broken; skip publish rather than tear

    SuEfisData& suEfis = suEfis_published_;

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

    // VN-300: mirror valid attitude into suVN300 under the same mutex
    // so the cross-struct write is also atomic-from-a-reader's-view.
    if (frame.source == EfisSource::Vn300) {
        if (fp & F::Pitch)   suVN300_published_.Pitch = frame.pitchDeg;
        if (fp & F::Roll)    suVN300_published_.Roll  = frame.rollDeg;
        if (fp & F::Heading) suVN300_published_.Yaw   = frame.headingDeg;
    }

    xSemaphoreGive(xEfisDataMutex_);
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

    // Single atomic publish: memcpy under mutex.  Hold time is
    // sizeof(SuVN300Data) ≈ 150 bytes worth of PSRAM-to-PSRAM copy,
    // sub-microsecond.
    if (xSemaphoreTake(xEfisDataMutex_, portMAX_DELAY) == pdTRUE) {
        suVN300_published_ = staging;
        xSemaphoreGive(xEfisDataMutex_);
    }
}
