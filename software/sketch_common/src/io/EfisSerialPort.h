
#pragma once

#include <HardwareSerial.h>
#include <Stream.h>
#include <optional>

#include "src/Globals.h"
#include <efis/EfisParser.h>
#include <types/EfisFrame.h>
#include <util/SnapshotPublisher.h>

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

        // Per-sample timestamps from the VN-300 Common group (issue #637).
        // Stamped at IMU sample time, so each 400 Hz frame carries a
        // distinct value ~2.5 ms apart.
        uint64_t TimeStartupNs; // ns since VN-300 boot
        uint64_t TimeGpsNs;     // ns since GPS epoch; valid iff dateOk bit set
        uint8_t  TimeStatus;    // bit0=timeOk, bit1=dateOk, bit2=utcTimeValid

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

    // EFIS type and freshness timestamps stay public.  Single-word
    // reads/writes are atomic on Xtensa (these are unsigned long /
    // EnEfisType enum, both 32-bit aligned), so no mutex needed.
    EnEfisType       enType;
    unsigned long    uTimestamp;            // millis() at last successful decode
    unsigned long    lastReceivedEfisTime;

    // The decoded data structs are PRIVATE.  Readers call SnapshotEfis() /
    // SnapshotVn300() to get a coherent copy.
    //
    // Publishing is via SnapshotPublisher<T> — a lock-free seqcount.
    // Producer (EfisReadTask, single-writer) builds a staging struct on
    // stack and calls suEfis_pub_.publish() / suVN300_pub_.publish(),
    // which is wait-free.  Readers call SnapshotEfis(out) which is
    // implemented as suEfis_pub_.read(): typically returns in ~150-200 ns
    // (one acquire-load + memcpy + one acquire-load).  No mutex; readers
    // never block the producer.
    //
    // Per-frame coherence is guaranteed by the seqcount: every Snapshot
    // call returns either the OLD struct or the NEW struct — never a
    // mix where some fields are from frame N and others from frame N+1.

    // Methods

    // Attach the IDF UART stream that EfisReadTask owns + reset parser
    // state for the given EFIS type. Real-hardware init path. Replaces
    // the old Stream*-polymorphic Init pair — the byte source is always
    // the IDF UART driver now, no synth-EFIS abstraction left.
    void AttachUart(class IdfUartStream* pStream, EnEfisType enEfisType);

    // Pump bytes through the parser, applying any complete frame /
    // VN-300 data + updating uTimestamp / lastReceivedEfisTime. Pure
    // computation, no I/O. EfisRead.cpp's wake handler bulk-reads from
    // the IDF stream then calls this with the resulting buffer; that's
    // a ~100x reduction in IDF syscalls vs the previous per-byte loop.
    void FeedBytes(const uint8_t* buf, size_t n);

    // Atomic snapshot of the published EFIS data structs.  Callers
    // declare a local of the appropriate type, pass it in by reference,
    // and read every field from the local copy that follows.  These are
    // the ONLY supported readers of the data — direct member access is
    // gone.  Both methods are wait-free (~150-200 ns); safe to call
    // from any task on any core EXCEPT deadlined ones.
    //
    // The `Snapshot*` variants spin if the producer is preempted mid-
    // publish.  Use them for tolerant consumers (web handlers, log
    // writer, WebSocket broadcaster, anything not on a hard cadence).
    void SnapshotEfis(SuEfisData& out) const;
    void SnapshotVn300(SuVN300Data& out) const;

    // Bounded-retry variants for deadlined consumers (DisplaySerial at
    // 20 Hz, AHRS at IMU rate, future audio).  Return false if 8
    // retries didn't get a coherent read; caller must skip the frame
    // rather than act on potentially-torn data.
    //
    // Return semantics:
    //   true  → `out` is coherent, caller can use it
    //   false → `out` is untouched/torn, caller must use a fallback
    //           (typically: leave the field at its prior value)
    [[nodiscard]] bool TrySnapshotEfis(SuEfisData& out) const;
    [[nodiscard]] bool TrySnapshotVn300(SuVN300Data& out) const;

    // Apply any pending RequestTypeChange call. Idempotent. EfisRead
    // calls this once per wake before FeedBytes; the type change runs
    // on the same task as the bytes that follow it, so the parser
    // reset can't race a concurrent decode.
    void ApplyPendingTypeChange();

    // Returns true if config has EFIS read enabled. Callers can short-
    // circuit the read+feed work when disabled.
    bool IsReadingEnabled() const;

    // Deprecated: the legacy single-call Read() loop. Now a thin
    // wrapper that calls ApplyPendingTypeChange + bulk-reads from the
    // attached IDF stream + FeedBytes. EfisReadTask still calls this
    // for backward compat with the synth-test paths; perf-critical
    // callers should prefer the bulk-read + FeedBytes split directly.
    void Read();
    bool IsDataFresh(unsigned long maxAgeMs) const
        { return (millis() - uTimestamp) < maxAgeMs; }

    // VN-300 bring-up diagnostics — bytes fed / sync matches / header & CRC
    // fail counters from the parser. Cheap struct copy. See Vn300.h for
    // semantics and interpretation guide. Only useful when enType == EnVN300.
    const onspeed::efis::Vn300Diagnostics& Vn300Diag() const {
        return parser_.Vn300Diag();
    }

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

    // The IDF UART stream owned by EfisReadTask. Set once via
    // AttachUart() at boot. nullptr until then (Read() / FeedBytes()
    // are no-ops in that state).
    class IdfUartStream*  pStream_ = nullptr;

    // Lock-free seqcount publishers for the decoded data.  Producer
    // (applyFrame / applyVn300Data) builds a stack-local staging
    // struct and calls publish() — wait-free.  Readers call .read()
    // via Snapshot* methods — wait-free in our task layout (see the
    // DEADLINED CALLERS note above).
    //
    // Lazy publisher placement: kept here as direct members (not
    // pointers) because SnapshotPublisher's constexpr constructor
    // is safe in static-init phase — there's no FreeRTOS dependency
    // to worry about.  Both structs default to zero-initialized
    // payloads with version=0 (even, "no writer in flight"), so the
    // very first reader (before any publish) gets a clean zero
    // payload, matching the previous mutex-based default.
    onspeed::util::SnapshotPublisher<SuEfisData>   suEfis_pub_;
    onspeed::util::SnapshotPublisher<SuVN300Data>  suVN300_pub_;

    // Convert EfisType enum to onspeed core enum
    static onspeed::efis::EfisType toCoreType(EnEfisType t);

    // Build a SuEfisData staging copy from a normalised EfisFrame and
    // publish via suEfis_pub_ (lock-free).  Single-writer — only
    // EfisReadTask calls this.
    void applyFrame(const onspeed::EfisFrame& frame);

    // Build SuVN300Data + SuEfisData-mirror staging copies from
    // Vn300Data and publish both via their respective publishers.
    // Single-writer — only EfisReadTask calls this.  The suEfis
    // mirror (Pitch/Roll/Heading) is published in lock-step with the
    // suVN300 update so any reader sees attitude consistent with the
    // VN-300 frame it came from.
    void applyVn300Data(const onspeed::efis::Vn300Data& data);
};
